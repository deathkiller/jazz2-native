#pragma once

#include "../../../nCine/Input/IInputManager.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::UI::Menu
{
	/** @brief Helper class that manages text editing state for text input fields */
	class TextInputBuffer
	{
	public:
		explicit TextInputBuffer(std::uint32_t maxLength = UINT32_MAX);

		bool IsActive() const;
		void Activate(StringView initialText = {});
		void Deactivate();

		StringView GetText() const;
		std::size_t GetCursor() const;
		float GetCaretAnim() const;

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
