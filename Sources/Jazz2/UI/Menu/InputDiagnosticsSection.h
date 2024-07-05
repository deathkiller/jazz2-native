#pragma once

#include "MenuSection.h"

#include "../../../nCine/Input/InputEvents.h"

namespace Jazz2::UI::Menu
{
	class InputDiagnosticsSection : public MenuSection
	{
	public:
		InputDiagnosticsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

		bool IsGamepadNavigationEnabled() const override
		{
			return false;
		}

	private:
		std::int32_t _itemCount;
		std::int32_t _selectedIndex;
		float _animation;

		void OnHandleInput();
		void PrintAxisValue(const char* name, float value, float x, float y);
	};
}