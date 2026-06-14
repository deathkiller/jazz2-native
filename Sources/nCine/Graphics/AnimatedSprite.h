#pragma once

#include "Sprite.h"
#include "RectAnimation.h"

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Textured sprite that cycles through one or more frame animations
		
		Extends @ref Sprite with a set of @ref RectAnimation instances. The active animation advances its
		texture rectangle over time during @ref OnUpdate, allowing several named animations to be stored and
		switched between by index.
	*/
	class AnimatedSprite : public Sprite
	{
	public:
		/** @brief Creates a sprite with no parent and no texture, positioned at the origin */
		AnimatedSprite();
		/** @brief Creates a sprite as a child of @p parent, positioned at the relative origin */
		AnimatedSprite(SceneNode* parent, Texture* texture);
		/** @brief Creates a sprite with a texture but no parent, positioned at the origin */
		explicit AnimatedSprite(Texture* texture);
		/** @brief Creates a sprite as a child of @p parent at the relative position (@p xx, @p yy) */
		AnimatedSprite(SceneNode* parent, Texture* texture, float xx, float yy);
		/** @brief Creates a sprite as a child of @p parent at the relative position @p position */
		AnimatedSprite(SceneNode* parent, Texture* texture, Vector2f position);
		/** @brief Creates a sprite with a texture but no parent at the position (@p xx, @p yy) */
		AnimatedSprite(Texture* texture, float xx, float yy);
		/** @brief Creates a sprite with a texture but no parent at the position @p position */
		AnimatedSprite(Texture* texture, Vector2f position);

		AnimatedSprite& operator=(const AnimatedSprite&) = delete;
		AnimatedSprite(AnimatedSprite&&) = default;
		AnimatedSprite& operator=(AnimatedSprite&&) = default;

		/** @brief Returns a copy of this object */
		inline AnimatedSprite clone() const {
			return AnimatedSprite(*this);
		}

		/** @brief Returns whether the current animation is paused */
		bool isPaused() const;
		/** @brief Sets the pause state of the current animation */
		void setPaused(bool isPaused);

		void OnUpdate(float timeMult) override;

		/** @brief Adds a new animation */
		void addAnimation(const RectAnimation& anim);
		/** @brief Adds a new animation with move semantics */
		void addAnimation(RectAnimation&& anim);
		/** @brief Removes all animations */
		void clearAnimations();

		/** @brief Returns the number of animations */
		inline std::uint32_t numAnimations() {
			return std::uint32_t(anims_.size());
		}
		/** @brief Returns the array of all animations */
		inline SmallVectorImpl<RectAnimation>& animations() {
			return anims_;
		}
		/** @brief Returns the array of all animations (read-only) */
		inline const SmallVectorImpl<RectAnimation>& animations() const {
			return anims_;
		}

		/** @brief Returns the index of the current animation */
		std::uint32_t animationIndex() const {
			return currentAnimIndex_;
		}
		/** @brief Sets the current animation by index and resets its frame number */
		void setAnimationIndex(std::uint32_t animIndex);

		/** @brief Returns the current animation, or `nullptr` if there is none */
		RectAnimation* currentAnimation();
		/** @brief Returns the current animation (read-only), or `nullptr` if there is none */
		const RectAnimation* currentAnimation() const;

		/** @brief Returns the frame number of the current animation */
		std::uint32_t frame() const;
		/** @brief Sets the current animation to the specified frame number */
		void setFrame(std::uint32_t frameNum);

		inline static ObjectType sType() {
			return ObjectType::AnimatedSprite;
		}

	protected:
		/** @brief Protected copy constructor used to clone objects */
		AnimatedSprite(const AnimatedSprite& other);

	private:
		SmallVector<RectAnimation, 0> anims_;
		std::uint32_t currentAnimIndex_;
	};

}
