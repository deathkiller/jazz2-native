#pragma once

#include "IMenuContainer.h"
#include "../Canvas.h"
#include "../ControlScheme.h"
#include "../../../Main.h"

#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Input/IInputManager.h"

namespace Jazz2::UI::Menu
{
	/** @brief Base class of a menu section */
	class MenuSection
	{
	public:
		MenuSection() : _root(nullptr) {}
		virtual ~MenuSection() {}

		/** @brief Allows to override clip rectangle of the middle layer */
		virtual Recti GetClipRectangle(const Recti& contentBounds) {
			return {};
		}

		/** @brief Called when the section is shown */
		virtual void OnShow(IMenuContainer* root) {
			_root = root;
		}

		/** @brief Called when the section is hidden */
		virtual void OnHide() {
			_root = nullptr;
		}

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
		virtual void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) = 0;

		/** @brief Allows to override navigation behavior */
		virtual NavigationFlags GetNavigationFlags() const {
			return NavigationFlags::AllowAll;
		}

	protected:
		static constexpr float TouchKineticDivider = 0.004f;
		static constexpr float TouchKineticFriction = 7000.0f;
		static constexpr float TouchKineticDamping = 0.2f;

#ifndef DOXYGEN_GENERATING_OUTPUT
		IMenuContainer* _root;
#endif
	};
}