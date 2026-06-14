#pragma once

#include "MenuSection.h"

#include "../../../nCine/Input/InputEvents.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Input diagnostics menu section
		
		Shows live diagnostics for the connected input devices, displaying the current button and axis states of each
		mapped gamepad.
	*/
	class InputDiagnosticsSection : public MenuSection
	{
	public:
		/** @brief Creates a new instance */
		InputDiagnosticsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

		NavigationFlags GetNavigationFlags() const override {
			return NavigationFlags::AllowKeyboard;
		}

	private:
		std::int32_t _itemCount;
		std::int32_t _selectedIndex;
		float _animation;

		void OnHandleInput();
		void PrintAxisValue(StringView name, float value, float x, float y);

		static void AppendPressedButton(MutableStringView output, std::size_t& offset, StringView name);
	};
}