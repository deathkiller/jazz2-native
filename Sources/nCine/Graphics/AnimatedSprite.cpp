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

	bool AnimatedSprite::IsPaused() const
	{
		bool isPaused = true;
		if (!_anims.empty()) {
			isPaused = _anims[_currentAnimIndex].IsPaused();
		}
		return isPaused;
	}

	void AnimatedSprite::SetPaused(bool isPaused)
	{
		if (!_anims.empty()) {
			_anims[_currentAnimIndex].SetPaused(isPaused);
		}
	}

	void AnimatedSprite::OnUpdate(float timeMult)
	{
		if (!_anims.empty()) {
			const unsigned int previousFrame = _anims[_currentAnimIndex].GetFrame();
			_anims[_currentAnimIndex].UpdateFrame(timeMult);

			// Updating sprite texture rectangle only on change
			if (previousFrame != _anims[_currentAnimIndex].GetFrame()) {
				SetTexRect(_anims[_currentAnimIndex].GetRect());
			}
		}

		Sprite::OnUpdate(timeMult);
	}

	void AnimatedSprite::AddAnimation(const RectAnimation& anim)
	{
		_anims.push_back(anim);
		_currentAnimIndex = (unsigned int)_anims.size() - 1;
		SetTexRect(_anims[_currentAnimIndex].GetRect());
	}

	void AnimatedSprite::AddAnimation(RectAnimation&& anim)
	{
		_anims.push_back(Death::move(anim));
		_currentAnimIndex = (unsigned int)_anims.size() - 1;
		SetTexRect(_anims[_currentAnimIndex].GetRect());
	}

	void AnimatedSprite::ClearAnimations()
	{
		_anims.clear();
	}

	void AnimatedSprite::SetAnimationIndex(std::uint32_t animIndex)
	{
		if (!_anims.empty()) {
			DEATH_ASSERT(animIndex < _anims.size());
			_currentAnimIndex = animIndex;
			SetTexRect(_anims[_currentAnimIndex].GetRect());
		}
	}

	RectAnimation* AnimatedSprite::GetCurrentAnimation()
	{
		RectAnimation* currentAnim = nullptr;
		if (!_anims.empty()) {
			currentAnim = &_anims[_currentAnimIndex];
		}
		return currentAnim;
	}

	const RectAnimation* AnimatedSprite::GetCurrentAnimation() const
	{
		const RectAnimation* currentAnim = nullptr;
		if (!_anims.empty()) {
			currentAnim = &_anims[_currentAnimIndex];
		}
		return currentAnim;
	}

	std::uint32_t AnimatedSprite::GetFrame() const
	{
		unsigned int frame = 0;
		if (!_anims.empty()) {
			frame = _anims[_currentAnimIndex].GetFrame();
		}
		return frame;
	}

	void AnimatedSprite::SetFrame(std::uint32_t frameNum)
	{
		if (!_anims.empty()) {
			_anims[_currentAnimIndex].SetFrame(frameNum);
			SetTexRect(_anims[_currentAnimIndex].GetRect());
		}
	}

	AnimatedSprite::AnimatedSprite(const AnimatedSprite& other)
		: Sprite(other), _anims(other._anims), _currentAnimIndex(other._currentAnimIndex)
	{
		_type = ObjectType::AnimatedSprite;
		SetFrame(other.GetFrame());
	}
}
