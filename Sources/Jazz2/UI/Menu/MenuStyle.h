#pragma once

#include "Tweening.h"

#include "../../../nCine/Primitives/Colorf.h"

#include <cstdint>

namespace Jazz2::UI::Menu
{
	/**
		@brief Style of an animated transition between two menu sections

		Selects how the outgoing and incoming sections are composited while a transition plays. All styles are
		implemented purely as a per-layer draw transform (offset/scale/tint) applied by the container, so they
		require no render targets and add no draw calls.
	*/
	enum class TransitionStyle {
		None,				/**< No animation, the switch is instant */
		Fade,				/**< Cross-fade by alpha */
		SlideHorizontal,	/**< Slide left when going forward, right when going back */
		SlideVertical,		/**< Slide down when going forward, up when going back */
		Scale				/**< Scale combined with a cross-fade */
	};

	/**
		@brief Parameters of a section transition

		Describes the @ref TransitionStyle, its duration in `timeMult` units, and the easing curve used to shape
		the progress. A @ref MenuSection can return its own to override @ref MenuStyle::DefaultTransition.
	*/
	struct TransitionInfo {
		TransitionStyle Style = TransitionStyle::SlideHorizontal;
		float Duration = 16.0f;
		Easing::Fn Ease = Easing::OutCubic;

		/** @brief Returns an instant (no animation) transition */
		static TransitionInfo Instant() {
			return { TransitionStyle::None, 0.0f, Easing::Linear };
		}
	};

	/**
		@brief Centralized look-and-feel of the menu

		Single source of truth for the menu's layout metrics, colors, text styling, and default transition. Editing
		this restyles the whole menu without touching individual sections. Shared by both menu containers and all
		sections through the painter helpers on the container.
	*/
	struct MenuStyle
	{
		/** @{ @name Layout metrics */

		/** @brief Height reserved for a single list item */
		std::int32_t ItemHeight = 40;
		/** @brief Distance of the top divider line from the content top */
		std::int32_t TopLine = 31;
		/** @brief Distance of the bottom divider line from the content bottom */
		std::int32_t BottomLine = 42;
		/** @brief Width of the dimmed background frame */
		float FrameWidth = 680.0f;

		/** @} */
		/** @{ @name Colors */

		/** @brief Color of a section title */
		nCine::Colorf TitleColor = nCine::Colorf(0.46f, 0.46f, 0.46f, 0.5f);
		/** @brief Color of a dimmed/disabled item */
		nCine::Colorf DisabledColor = nCine::Colorf(0.51f, 0.51f, 0.51f, 0.35f);
		/** @brief Tint of the dimmed background frame */
		nCine::Colorf FrameColor = nCine::Colorf::Black;

		/** @} */
		/** @{ @name Text styling */

		/** @brief Per-character wave angle offset of styled (selected/title) text */
		float TextAngleOffset = 0.7f;
		/** @brief Per-character horizontal wave variance of styled text */
		float TextVarianceX = 1.1f;
		/** @brief Per-character vertical wave variance of styled text */
		float TextVarianceY = 1.1f;
		/** @brief Wave speed of styled text */
		float TextWaveSpeed = 0.4f;
		/** @brief Character spacing of styled text */
		float TextCharSpacing = 0.9f;

		/** @brief Scale of an unselected list item */
		float ItemScale = 0.9f;
		/** @brief Base scale of a selected list item (before the elastic pop) */
		float SelectedItemBaseScale = 0.5f;
		/** @brief Additional scale added by the elastic pop of a selected item */
		float SelectedItemPopScale = 0.6f;

		/** @} */
		/** @{ @name Animation */

		/** @brief Speed of the per-item selection animation (per `timeMult`) */
		float SelectionAnimationSpeed = 0.016f;
		/** @brief Default transition used when a section doesn't override it */
		TransitionInfo DefaultTransition = { TransitionStyle::SlideHorizontal, 16.0f, Easing::OutCubic };

		/** @} */
	};
}
