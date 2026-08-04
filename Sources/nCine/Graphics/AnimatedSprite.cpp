#include "AnimatedSprite.h"
#include "../../Main.h"

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
		: Sprite(parent, texture, xx, yy), _anims(4), _currentAnimIndex(0)
	{
		_type = ObjectType::AnimatedSprite;
	}

	AnimatedSprite::AnimatedSprite(SceneNode* parent, Texture* texture, Vector2f position)
		: AnimatedSprite(parent, texture, position.X, position.Y)
	{
	}

	AnimatedSprite::AnimatedSprite(Texture* texture, float xx, float yy)
		: AnimatedSprite(nullptr, texture, xx, yy)
	{
	}

	AnimatedSprite::AnimatedSprite(Texture* texture, Vector2f position)
		: AnimatedSprite(nullptr, texture, position.X, position.Y)
	{
	}

	bool AnimatedSprite::isPaused() const
	{
		bool isPaused = true;
		if (!_anims.empty()) {
			isPaused = _anims[_currentAnimIndex].isPaused();
		}
		return isPaused;
	}

	void AnimatedSprite::setPaused(bool isPaused)
	{
		if (!_anims.empty()) {
			_anims[_currentAnimIndex].setPaused(isPaused);
		}
	}

	void AnimatedSprite::OnUpdate(float timeMult)
	{
		if (!_anims.empty()) {
			const unsigned int previousFrame = _anims[_currentAnimIndex].frame();
			_anims[_currentAnimIndex].updateFrame(timeMult);

			// Updating sprite texture rectangle only on change
			if (previousFrame != _anims[_currentAnimIndex].frame()) {
				setTexRect(_anims[_currentAnimIndex].rect());
			}
		}

		Sprite::OnUpdate(timeMult);
	}

	void AnimatedSprite::addAnimation(const RectAnimation& anim)
	{
		_anims.push_back(anim);
		_currentAnimIndex = (unsigned int)_anims.size() - 1;
		setTexRect(_anims[_currentAnimIndex].rect());
	}

	void AnimatedSprite::addAnimation(RectAnimation&& anim)
	{
		_anims.push_back(std::move(anim));
		_currentAnimIndex = (unsigned int)_anims.size() - 1;
		setTexRect(_anims[_currentAnimIndex].rect());
	}

	void AnimatedSprite::clearAnimations()
	{
		_anims.clear();
	}

	void AnimatedSprite::setAnimationIndex(std::uint32_t animIndex)
	{
		if (!_anims.empty()) {
			DEATH_ASSERT(animIndex < _anims.size());
			_currentAnimIndex = animIndex;
			setTexRect(_anims[_currentAnimIndex].rect());
		}
	}

	RectAnimation* AnimatedSprite::currentAnimation()
	{
		RectAnimation* currentAnim = nullptr;
		if (!_anims.empty()) {
			currentAnim = &_anims[_currentAnimIndex];
		}
		return currentAnim;
	}

	const RectAnimation* AnimatedSprite::currentAnimation() const
	{
		const RectAnimation* currentAnim = nullptr;
		if (!_anims.empty()) {
			currentAnim = &_anims[_currentAnimIndex];
		}
		return currentAnim;
	}

	std::uint32_t AnimatedSprite::frame() const
	{
		unsigned int frame = 0;
		if (!_anims.empty()) {
			frame = _anims[_currentAnimIndex].frame();
		}
		return frame;
	}

	void AnimatedSprite::setFrame(std::uint32_t frameNum)
	{
		if (!_anims.empty()) {
			_anims[_currentAnimIndex].setFrame(frameNum);
			setTexRect(_anims[_currentAnimIndex].rect());
		}
	}

	AnimatedSprite::AnimatedSprite(const AnimatedSprite& other)
		: Sprite(other), _anims(other._anims), _currentAnimIndex(other._currentAnimIndex)
	{
		_type = ObjectType::AnimatedSprite;
		setFrame(other.frame());
	}
}
