#pragma once

#include "../../../nCine/Input/IInputManager.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::UI::Menu
{
	/**
		@brief Helper class that manages text editing state for text input fields
		
		Holds the current text, cursor position, and caret blink animation for an editable field, and translates
		keyboard and text input events into edits. Reused by menu sections that need text entry.
	*/
	class TextInputBuffer
	{
	public:
		/** @brief Creates a new instance with the specified maximum text length */
		explicit TextInputBuffer(std::uint32_t maxLength = UINT32_MAX);

		/** @brief Returns `true` if the buffer is currently accepting input */
		bool IsActive() const;
		/** @brief Activates the buffer, optionally seeding it with the specified text */
		void Activate(StringView initialText = {});
		/** @brief Deactivates the buffer */
		void Deactivate();

		/** @brief Returns the current text */
		StringView GetText() const;
		/** @brief Returns the current cursor position */
		std::size_t GetCursor() const;
		/** @brief Returns the current caret blink animation phase */
		float GetCaretAnim() const;

		/** @brief Advances the caret blink animation */
		void Update(float timeMult);
		/** @brief Handles editing keys (Backspace, Delete, arrow keys, Home, End). @return `true` if the event was consumed. */
		bool OnKeyPressed(const nCine::KeyboardEvent& event);
		/** @brief Inserts text at the current cursor position. @return `true` if the text was inserted. */
		bool OnTextInput(const nCine::TextInputEvent& event);

	private:
		String _text;
		std::size_t _cursor;
		float _caretAnim;
		std::uint32_t _maxLength;
		bool _active;
	};
}
