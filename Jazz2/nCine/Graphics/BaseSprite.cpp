#include "BaseSprite.h"
#include "RenderCommand.h"

namespace nCine {

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: DrawableNode(parent, xx, yy), texture_(texture), texRect_(0, 0, 0, 0),
		flippedX_(false), flippedY_(false), spriteBlock_(nullptr)
	{
		renderCommand_.material().setBlendingEnabled(true);
	}

	BaseSprite::BaseSprite(SceneNode* parent, Texture* texture, const Vector2f& position)
		: BaseSprite(parent, texture, position.X, position.Y)
	{
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void BaseSprite::setSize(float width, float height)
	{
		// Update anchor points when size changes
		if (anchorPoint_.X != 0.0f)
			anchorPoint_.X = (anchorPoint_.X / width_) * width;
		if (anchorPoint_.Y != 0.0f)
			anchorPoint_.Y = (anchorPoint_.Y / height_) * height;

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

	///////////////////////////////////////////////////////////
	// PROTECTED FUNCTIONS
	///////////////////////////////////////////////////////////

	BaseSprite::BaseSprite(const BaseSprite& other)
		: DrawableNode(other), texture_(other.texture_), texRect_(other.texRect_),
		flippedX_(other.flippedX_), flippedY_(other.flippedY_), spriteBlock_(nullptr)
	{
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void BaseSprite::updateRenderCommand()
	{
		if (dirtyBits_.test(DirtyBitPositions::TransformationBit)) {
			renderCommand_.setTransformation(worldMatrix_);
			dirtyBits_.reset(DirtyBitPositions::TransformationBit);
		}
		if (dirtyBits_.test(DirtyBitPositions::ColorBit)) {
			spriteBlock_->uniform("color")->setFloatVector(Colorf(absColor()).Data());
			dirtyBits_.reset(DirtyBitPositions::ColorBit);
		}
		if (dirtyBits_.test(DirtyBitPositions::SizeBit)) {
			spriteBlock_->uniform("spriteSize")->setFloatValue(width_, height_);
			dirtyBits_.reset(DirtyBitPositions::SizeBit);
		}

		if (dirtyBits_.test(DirtyBitPositions::TextureBit)) {
			if (texture_) {
				renderCommand_.material().setTexture(*texture_);

				const Vector2i texSize = texture_->size();
				const float texScaleX = texRect_.W / float(texSize.X);
				const float texBiasX = (-0.25f + texRect_.X) / float(texSize.X);
				const float texScaleY = texRect_.H / float(texSize.Y);
				// TODO: revise this fix/workaround
				const float texBiasY = (0.25f + texRect_.Y) / float(texSize.Y);

				spriteBlock_->uniform("texRect")->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
			} else
				renderCommand_.material().setTexture(nullptr);

			dirtyBits_.reset(DirtyBitPositions::TextureBit);
		}
	}

}
