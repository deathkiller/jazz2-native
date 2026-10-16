#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "UiWidgets.h"

namespace Jazz2::Editor
{
	class UiSurface;

	/**
		@brief Keyboard on the screen, for a gamepad or a touch screen where the system has none

		It types into the box that has the focus as a keyboard would, its keys don't take the focus. It stays at the
		bottom of the screen until the text is confirmed, or the box loses the focus.
	*/
	class UiOnScreenKeyboard : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		UiOnScreenKeyboard();
		~UiOnScreenKeyboard() override;

		/** @brief Shows the keyboard, unless it's shown already */
		static void Show(UiSurface& surface);
		/** @brief Returns `true` if the keyboard is shown */
		static bool IsShown(const UiSurface& surface);

		void Update(float timeMult) override;
		bool KeepsFocus() const override;

	private:
		static UiOnScreenKeyboard* _instance;

		bool _shift;
		SmallVector<UiButton*, 0> _letterKeys;

		void Type(StringView text);
		void Press(Keys key);
		void UpdateLetters();
	};
}

#endif
