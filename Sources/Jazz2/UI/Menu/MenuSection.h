#pragma once

#include "IMenuContainer.h"
#include "../Canvas.h"
#include "../../../Common.h"

#include "../../../nCine/Primitives/Vector2.h"
#include "../../../nCine/Input/IInputManager.h"

namespace Jazz2::UI::Menu
{
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
		virtual void OnDrawClipped(Canvas* canvas) { }
		virtual void OnDrawOverlay(Canvas* canvas) { }
		virtual void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) = 0;

		virtual bool IsGamepadNavigationEnabled() const {
			return true;
		}

	protected:
		IMenuContainer* _root;
	};
}