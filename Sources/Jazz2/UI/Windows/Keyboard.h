#pragma once

#if defined(WITH_UI_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Widgets.h"

namespace Jazz2::UI::Windows
{
	class Surface;

	/**
		@brief Keyboard on the screen, for a gamepad or a touch screen where the system has none

		It types into the box that has the focus as a keyboard would, its keys don't take the focus. It stays at the
		bottom of the screen until the text is confirmed, or the box loses the focus.
	*/
	class OnScreenKeyboard : public Panel
	{
		DEATH_RUNTIME_OBJECT(Panel);

	public:
		OnScreenKeyboard();
		~OnScreenKeyboard() override;

		/** @brief Shows the keyboard, unless it's shown already */
		static void Show(Surface& surface);
		/** @brief Returns `true` if the keyboard is shown */
		static bool IsShown(const Surface& surface);

		void Update(float timeMult) override;
		bool KeepsFocus() const override;

	private:
		static OnScreenKeyboard* _instance;

		bool _shift;
		SmallVector<Button*, 0> _letterKeys;

		void Type(StringView text);
		void Press(Keys key);
		void UpdateLetters();
	};
}

#endif
