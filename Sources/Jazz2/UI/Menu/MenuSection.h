﻿#pragma once

#include "IMenuContainer.h"
#include "../Canvas.h"
#include "../../Input/ControlScheme.h"
#include "../../../Main.h"

#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Input/IInputManager.h"

using namespace Jazz2::Input;

namespace Jazz2::UI::Menu
{
	/** @brief Base class of a menu section */
	class MenuSection
	{
	public:
		MenuSection();
		virtual ~MenuSection();

		/** @brief Allows to override clip rectangle of the middle layer */
		virtual Recti GetClipRectangle(const Recti& contentBounds);

		/** @brief Called when the section is shown */
		virtual void OnShow(IMenuContainer* root);
		/** @brief Called when the section is hidden */
		virtual void OnHide();

		/** @brief Called when the section should be updated */
		virtual void OnUpdate(float timeMult) = 0;
		/** @brief Called when the section should be drawn --- the bottom background layer */
		virtual void OnDraw(Canvas* canvas) = 0;
		/** @brief Called when the section should be drawn --- the middle clipped layer */
		virtual void OnDrawClipped(Canvas* canvas) {}
		/** @brief Called when the section should be drawn --- the top overlay layer */
		virtual void OnDrawOverlay(Canvas* canvas) {}
		/** @brief Called when a key is pressed */
		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) {}
		/** @brief Called when a key is released */
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) {}
		/** @brief Called when a text input is detected */
		virtual void OnTextInput(const nCine::TextInputEvent& event) {}
		/** @brief Called when a touch event is triggered */
		virtual void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) = 0;

		/** @brief Allows to override navigation behavior */
		virtual NavigationFlags GetNavigationFlags() const;

	protected:
		/** @brief Kinetic divider for smooth touch controls */
		static constexpr float TouchKineticDivider = 0.004f;
		/** @brief Kinetic friction for smooth touch controls */
		static constexpr float TouchKineticFriction = 7000.0f;
		/** @brief Kinetic damping for smooth touch controls */
		static constexpr float TouchKineticDamping = 0.2f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		IMenuContainer* _root;
#endif
	};
}