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
		MenuSection() : _root(nullptr) { }
		virtual ~MenuSection() { }

		virtual Recti GetClipRectangle(const Recti& contentBounds)
		{
			return {};
		}

		virtual void OnShow(IMenuContainer* root)
		{
			_root = root;
		}

		virtual void OnHide()
		{
			_root = nullptr;
		}

		virtual void OnUpdate(float timeMult) = 0;
		virtual void OnDraw(Canvas* canvas) = 0;
		virtual void OnDrawClipped(Canvas* canvas) {}
		virtual void OnDrawOverlay(Canvas* canvas) {}
		virtual void OnKeyPressed(const nCine::KeyboardEvent& event) {}
		virtual void OnKeyReleased(const nCine::KeyboardEvent& event) {}
		virtual void OnTextInput(const nCine::TextInputEvent& event) {}
		virtual void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) = 0;

		virtual NavigationFlags GetNavigationFlags() const {
			return NavigationFlags::AllowAll;
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		IMenuContainer* _root;
#endif
	};
}