#pragma once

#include "MenuStyle.h"

#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Primitives/Rect.h"

#include <memory>

namespace Jazz2::UI
{
	class Canvas;
}

namespace Jazz2::UI::Menu
{
	class MenuSection;

	/**
		@brief Drives an animated transition between two menu sections

		Owns the lifecycle of the section animating out and computes the per-layer draw transform (offset, scale,
		tint) applied to the outgoing and incoming sections while the transition plays. The effect is realized purely
		through @ref Canvas::LayerOffset / @ref Canvas::LayerScale / @ref Canvas::LayerColor, so no render targets or
		extra draw calls are needed. A forward (push) transition keeps the outgoing section owned by the container and
		only hides it on completion; a backward (pop) transition takes ownership of the popped section so it can finish
		animating before being destroyed. Both menu containers own one of these and share all of the logic here.
	*/
	class MenuTransition
	{
	public:
		MenuTransition();

		/** @brief Returns `true` if a transition is currently playing */
		bool IsActive() const {
			return (_state != State::Idle);
		}

		/** @brief Returns the section animating out, or `nullptr` if no transition is active */
		MenuSection* GetOutgoing() const;

		/** @brief Starts a forward (push) transition; the outgoing section stays owned by the container */
		void BeginForward(MenuSection* outgoing, const TransitionInfo& info);
		/** @brief Starts a backward (pop) transition; takes ownership of the outgoing section until it finishes */
		void BeginBackward(std::unique_ptr<MenuSection> outgoing, const TransitionInfo& info);
		/** @brief Finishes any active transition immediately */
		void Skip();
		/** @brief Advances the transition; returns `true` if it finished during this call */
		bool Update(float timeMult);

		/** @brief Applies the transition's draw transform for the given role (incoming/outgoing) onto a canvas */
		void ApplyTransform(Canvas* canvas, bool incoming, nCine::Vector2i viewSize) const;
		/** @brief Resets a canvas draw transform back to identity */
		static void ResetTransform(Canvas* canvas);

		/**
		 * @brief Combines two section clip rectangles into one that covers both while a transition plays
		 *
		 * Empty rectangles (a section with no clipped layer) are ignored; if both are empty the full view is used.
		 * This keeps the clipped layer properly clipped during a transition instead of showing unclipped content.
		 */
		static nCine::Recti CombineClip(const nCine::Recti& a, const nCine::Recti& b, nCine::Vector2i viewSize);

	private:
		enum class State {
			Idle,
			Forward,
			Backward
		};

		void Finalize();

		State _state;
		MenuSection* _outgoingBorrowed;
		std::unique_ptr<MenuSection> _outgoingOwned;
		float _progress;
		TransitionInfo _info;
	};
}
