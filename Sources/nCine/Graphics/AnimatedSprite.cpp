#include "AnimatedSprite.h"
#include "../../Common.h"

namespace nCine
{
	AnimatedSprite::AnimatedSprite()
		: AnimatedSprite(nullptr, nullptr, 0.0f, 0.0f)
	{
	}

	AnimatedSprite::AnimatedSprite(SceneNode* parent, Texture* texture)
		: AnimatedSprite(parent, texture, 0.0f, 0.0f)
	{
	}

	AnimatedSprite::AnimatedSprite(Texture* texture)
		: AnimatedSprite(nullptr, texture, 0.0f, 0.0f)
	{
	}

	AnimatedSprite::AnimatedSprite(SceneNode* parent, Texture* texture, float xx, float yy)
		: Sprite(parent, texture, xx, yy), anims_(4), currentAnimIndex_(0)
	{
		_type = ObjectType::AnimatedSprite;
	}

	AnimatedSprite::AnimatedSprite(SceneNode* parent, Texture* texture, const Vector2f& position)
		: AnimatedSprite(parent, texture, position.X, position.Y)
	{
	}

	AnimatedSprite::AnimatedSprite(Texture* texture, float xx, float yy)
		: AnimatedSprite(nullptr, texture, xx, yy)
	{
	}

	AnimatedSprite::AnimatedSprite(Texture* texture, const Vector2f& position)
		: AnimatedSprite(nullptr, texture, position.X, position.Y)
	{
	}

	bool AnimatedSprite::isPaused() const
	{
		bool isPaused = true;
		if (!anims_.empty()) {
			isPaused = anims_[currentAnimIndex_].isPaused();
		}
		return isPaused;
	}

	void AnimatedSprite::setPaused(bool isPaused)
	{
		if (!anims_.empty()) {
			anims_[currentAnimIndex_].setPaused(isPaused);
		}
	}

	void AnimatedSprite::OnUpdate(float timeMult)
	{
		if (!anims_.empty()) {
			const unsigned int previousFrame = anims_[currentAnimIndex_].frame();
			anims_[currentAnimIndex_].updateFrame(timeMult);

			// Updating sprite texture rectangle only on change
			if (previousFrame != anims_[currentAnimIndex_].frame()) {
				setTexRect(anims_[currentAnimIndex_].rect());
			}
		}

		Sprite::OnUpdate(timeMult);
	}

	void AnimatedSprite::addAnimation(const RectAnimation& anim)
	{
		anims_.push_back(anim);
		currentAnimIndex_ = (unsigned int)anims_.size() - 1;
		setTexRect(anims_[currentAnimIndex_].rect());
	}

	void AnimatedSprite::addAnimation(RectAnimation&& anim)
	{
		anims_.push_back(std::move(anim));
		currentAnimIndex_ = (unsigned int)anims_.size() - 1;
		setTexRect(anims_[currentAnimIndex_].rect());
	}

	void AnimatedSprite::clearAnimations()
	{
		anims_.clear();
	}

	void AnimatedSprite::setAnimationIndex(unsigned int animIndex)
	{
		if (!anims_.empty()) {
			ASSERT(animIndex < anims_.size());
			currentAnimIndex_ = animIndex;
			setTexRect(anims_[currentAnimIndex_].rect());
		}
	}

	RectAnimation* AnimatedSprite::currentAnimation()
	{
		RectAnimation* currentAnim = nullptr;
		if (!anims_.empty()) {
			currentAnim = &anims_[currentAnimIndex_];
		}
		return currentAnim;
	}

	const RectAnimation* AnimatedSprite::currentAnimation() const
	{
		const RectAnimation* currentAnim = nullptr;
		if (!anims_.empty()) {
			currentAnim = &anims_[currentAnimIndex_];
		}
		return currentAnim;
	}

	unsigned int AnimatedSprite::frame() const
	{
		unsigned int frame = 0;
		if (!anims_.empty()) {
			frame = anims_[currentAnimIndex_].frame();
		}
		return frame;
	}

	void AnimatedSprite::setFrame(unsigned int frameNum)
	{
		if (!anims_.empty()) {
			anims_[currentAnimIndex_].setFrame(frameNum);
			setTexRect(anims_[currentAnimIndex_].rect());
		}
	}

	AnimatedSprite::AnimatedSprite(const AnimatedSprite& other)
		: Sprite(other), anims_(other.anims_), currentAnimIndex_(other.currentAnimIndex_)
	{
		_type = ObjectType::AnimatedSprite;
		setFrame(other.frame());
	}
}
