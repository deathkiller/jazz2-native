#include "BaseSprite.h"
#include "RenderCommand.h"
#include "../tracy.h"

namespace nCine
{
	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: DrawableNode(parent, xx, yy), _texture(texture), _texRect(0, 0, 0, 0), _flippedX(false), _flippedY(false), _paletteOffset(0.0f)
	{
		_renderCommand.GetMaterial().SetBlendingEnabled(true);
	}

	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, Vector2f position)
		: BaseSprite(parent, texture, position.X, position.Y)
	{
	}

	void BaseSprite::setSize(float width, float height)
	{
		// Update anchor points when size changes
		if (_anchorPoint.X != 0.0f) {
			_anchorPoint.X = (_anchorPoint.X / _width) * width;
		}
		if (_anchorPoint.Y != 0.0f) {
			_anchorPoint.Y = (_anchorPoint.Y / _height) * height;
		}

		_width = width;
		_height = height;
		_dirtyBits.set(DirtyBitPositions::SizeBit);
		_dirtyBits.set(DirtyBitPositions::AabbBit);
	}

	/** @note Setting a texture that is already assigned is equivalent to @ref resetTexture() */
	void BaseSprite::setTexture(Texture* texture)
	{
		// Allow self-assignment to take into account the case where the texture stays the same but it loads new data
		textureHasChanged(texture);
		_texture = texture;
		_dirtyBits.set(DirtyBitPositions::TextureBit);
	}

	/** @note Use this method when the content of the currently assigned texture changes */
	void BaseSprite::resetTexture()
	{
		textureHasChanged(_texture);
		_dirtyBits.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::setTexRect(const Recti& rect)
	{
		_texRect = rect;
		setSize(static_cast<float>(rect.W), static_cast<float>(rect.H));

		if (_flippedX) {
			_texRect.X += _texRect.W;
			_texRect.W *= -1;
		}

		if (_flippedY) {
			_texRect.Y += _texRect.H;
			_texRect.H *= -1;
		}

		_dirtyBits.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::setFlippedX(bool flippedX)
	{
		if (_flippedX != flippedX) {
			_texRect.X += _texRect.W;
			_texRect.W *= -1;
			_flippedX = flippedX;

			_dirtyBits.set(DirtyBitPositions::TextureBit);
		}
	}

	void BaseSprite::setFlippedY(bool flippedY)
	{
		if (_flippedY != flippedY) {
			_texRect.Y += _texRect.H;
			_texRect.H *= -1;
			_flippedY = flippedY;

			_dirtyBits.set(DirtyBitPositions::TextureBit);
		}
	}

	BaseSprite::BaseSprite(const BaseSprite& other)
		: DrawableNode(other), _texture(other._texture), _texRect(other._texRect),
			_flippedX(other._flippedX), _flippedY(other._flippedY), _paletteOffset(other._paletteOffset)
	{
	}

	void BaseSprite::setPaletteOffset(float paletteOffset)
	{
		if (_paletteOffset != paletteOffset) {
			_paletteOffset = paletteOffset;
			// Uploaded together with the sprite size (see updateRenderCommand)
			_dirtyBits.set(DirtyBitPositions::SizeBit);
		}
	}

	void BaseSprite::shaderHasChanged()
	{
		_renderCommand.GetMaterial().ReserveUniformsDataMemory();
		RHI::UniformCache* textureUniform = _renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}

		_dirtyBits.set(DirtyBitPositions::TransformationBit);
		_dirtyBits.set(DirtyBitPositions::ColorBit);
		_dirtyBits.set(DirtyBitPositions::SizeBit);
		_dirtyBits.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::updateRenderCommand()
	{
		ZoneScopedC(0x81A861);

		// The block members, resolved once per shader change rather than by name on every dirty sprite -
		// an animated actor rewrites its texRect every animation frame, so this is a per-frame path.
		// A material without a shader program has no members to write (all-null placeholders), but the
		// transformation and texture binding below still have to happen.
		static const RenderCommand::InstanceUniforms NullInstanceUniforms{};
		const RenderCommand::InstanceUniforms* instanceUniforms = _renderCommand.GetInstanceUniforms();
		if (instanceUniforms == nullptr) {
			instanceUniforms = &NullInstanceUniforms;
		}

		if (_dirtyBits.test(DirtyBitPositions::TransformationUploadBit)) {
			_renderCommand.SetTransformation(_worldMatrix);
			//_dirtyBits.reset(DirtyBitPositions::TransformationUploadBit);
		}
		if (_dirtyBits.test(DirtyBitPositions::ColorUploadBit)) {
			if (instanceUniforms->Color != nullptr) {
				instanceUniforms->Color->SetFloatVector(absColor().Data());
			}
			//_dirtyBits.reset(DirtyBitPositions::ColorUploadBit);
		}
		if (_dirtyBits.test(DirtyBitPositions::SizeBit)) {
			if (instanceUniforms->SpriteSize != nullptr) {
				instanceUniforms->SpriteSize->SetFloatValue(_width, _height);
			}
			// Present only in palette shaders (sprite_vs/batched_sprites_vs); null elsewhere
			if (instanceUniforms->PaletteOffset != nullptr) {
				instanceUniforms->PaletteOffset->SetFloatValue(_paletteOffset);
			}
			_dirtyBits.reset(DirtyBitPositions::SizeBit);
		}

		if (_dirtyBits.test(DirtyBitPositions::TextureBit)) {
			if (_texture != nullptr) {
				_renderCommand.GetMaterial().SetTexture(*_texture);

				RHI::UniformCache* texRectUniform = instanceUniforms->TexRect;
				if (texRectUniform != nullptr) {
					const Vector2i texSize = _texture->GetSize();
					const float texScaleX = _texRect.W / float(texSize.X);
					const float texBiasX = _texRect.X / float(texSize.X);
					const float texScaleY = _texRect.H / float(texSize.Y);
					const float texBiasY = _texRect.Y / float(texSize.Y);

					texRectUniform->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				}
			} else {
				_renderCommand.GetMaterial().SetTexture(nullptr);
			}

			_dirtyBits.reset(DirtyBitPositions::TextureBit);
		}
	}
}
