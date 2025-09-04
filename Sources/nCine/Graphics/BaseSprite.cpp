#include "BaseSprite.h"
#include "RenderCommand.h"
#include "../tracy.h"

namespace nCine
{
	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: DrawableNode(parent, xx, yy), texture_(texture), texRect_(0, 0, 0, 0), flippedX_(false), flippedY_(false), instanceBlock_(nullptr)
	{
		renderCommand_.GetMaterial().SetBlendingEnabled(true);
	}

	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, Vector2f position)
		: BaseSprite(parent, texture, position.X, position.Y)
	{
	}

	void BaseSprite::setSize(float width, float height)
	{
		// Update anchor points when size changes
		if (anchorPoint_.X != 0.0f) {
			anchorPoint_.X = (anchorPoint_.X / width_) * width;
		}
		if (anchorPoint_.Y != 0.0f) {
			anchorPoint_.Y = (anchorPoint_.Y / height_) * height;
		}

		width_ = width;
		height_ = height;
		dirtyBits_.set(DirtyBitPositions::SizeBit);
		dirtyBits_.set(DirtyBitPositions::AabbBit);
	}

	/*! \note If you set a texture that is already assigned, this method would be equivalent to `resetTexture()` */
	void BaseSprite::setTexture(Texture* texture)
	{
		// Allow self-assignment to take into account the case where the texture stays the same but it loads new data
		textureHasChanged(texture);
		texture_ = texture;
		dirtyBits_.set(DirtyBitPositions::TextureBit);
	}

	/*! \note Use this method when the content of the currently assigned texture changes */
	void BaseSprite::resetTexture()
	{
		textureHasChanged(texture_);
		dirtyBits_.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::setTexRect(const Recti& rect)
	{
		texRect_ = rect;
		setSize(static_cast<float>(rect.W), static_cast<float>(rect.H));

		if (flippedX_) {
			texRect_.X += texRect_.W;
			texRect_.W *= -1;
		}

		if (flippedY_) {
			texRect_.Y += texRect_.H;
			texRect_.H *= -1;
		}

		dirtyBits_.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::setFlippedX(bool flippedX)
	{
		if (flippedX_ != flippedX) {
			texRect_.X += texRect_.W;
			texRect_.W *= -1;
			flippedX_ = flippedX;

			dirtyBits_.set(DirtyBitPositions::TextureBit);
		}
	}

	void BaseSprite::setFlippedY(bool flippedY)
	{
		if (flippedY_ != flippedY) {
			texRect_.Y += texRect_.H;
			texRect_.H *= -1;
			flippedY_ = flippedY;

			dirtyBits_.set(DirtyBitPositions::TextureBit);
		}
	}

	BaseSprite::BaseSprite(const BaseSprite& other)
		: DrawableNode(other), texture_(other.texture_), texRect_(other.texRect_),
			flippedX_(other.flippedX_), flippedY_(other.flippedY_), instanceBlock_(nullptr)
	{
	}

	void BaseSprite::shaderHasChanged()
	{
		renderCommand_.GetMaterial().ReserveUniformsDataMemory();
		instanceBlock_ = renderCommand_.GetMaterial().UniformBlock(Material::InstanceBlockName);
		GLUniformCache* textureUniform = renderCommand_.GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}

		dirtyBits_.set(DirtyBitPositions::TransformationBit);
		dirtyBits_.set(DirtyBitPositions::ColorBit);
		dirtyBits_.set(DirtyBitPositions::SizeBit);
		dirtyBits_.set(DirtyBitPositions::TextureBit);
	}

	void BaseSprite::updateRenderCommand()
	{
		ZoneScopedC(0x81A861);

		if (dirtyBits_.test(DirtyBitPositions::TransformationUploadBit)) {
			renderCommand_.SetTransformation(worldMatrix_);
			//dirtyBits_.reset(DirtyBitPositions::TransformationUploadBit);
		}
		if (dirtyBits_.test(DirtyBitPositions::ColorUploadBit)) {
			GLUniformCache* colorUniform = instanceBlock_->GetUniform(Material::ColorUniformName);
			if (colorUniform != nullptr) {
				colorUniform->SetFloatVector(absColor().Data());
			}
			//dirtyBits_.reset(DirtyBitPositions::ColorUploadBit);
		}
		if (dirtyBits_.test(DirtyBitPositions::SizeBit)) {
			GLUniformCache* spriteSizeUniform = instanceBlock_->GetUniform(Material::SpriteSizeUniformName);
			if (spriteSizeUniform != nullptr) {
				spriteSizeUniform->SetFloatValue(width_, height_);
			}
			dirtyBits_.reset(DirtyBitPositions::SizeBit);
		}

		if (dirtyBits_.test(DirtyBitPositions::TextureBit)) {
			if (texture_ != nullptr) {
				renderCommand_.GetMaterial().SetTexture(*texture_);

				GLUniformCache* texRectUniform = instanceBlock_->GetUniform(Material::TexRectUniformName);
				if (texRectUniform != nullptr) {
					const Vector2i texSize = texture_->GetSize();
					const float texScaleX = texRect_.W / float(texSize.X);
					const float texBiasX = texRect_.X / float(texSize.X);
					const float texScaleY = texRect_.H / float(texSize.Y);
					const float texBiasY = texRect_.Y / float(texSize.Y);

					texRectUniform->SetFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				}
			} else {
				renderCommand_.GetMaterial().SetTexture(nullptr);
			}

			dirtyBits_.reset(DirtyBitPositions::TextureBit);
		}
	}
}
