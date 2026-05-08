#include "TextInputBuffer.h"

#include <Utf8.h>
#include <Containers/StringConcatenable.h>

using namespace nCine;
using namespace Death;
using namespace Death::Containers;

namespace Jazz2::UI::Menu
{
	TextInputBuffer::TextInputBuffer(std::uint32_t maxLength)
		: _cursor(0), _caretAnim(0.0f), _maxLength(maxLength), _active(false)
	{
	}

	bool TextInputBuffer::IsActive() const
	{
		return _active;
	}

	void TextInputBuffer::Activate(StringView initialText)
	{
		_text = initialText;
		_cursor = _text.size();
		_caretAnim = 0.0f;
		_active = true;
	}

	void TextInputBuffer::Deactivate()
	{
		_active = false;
	}

	StringView TextInputBuffer::GetText() const
	{
		return _text;
	}

	std::size_t TextInputBuffer::GetCursor() const
	{
		return _cursor;
	}

	float TextInputBuffer::GetCaretAnim() const
	{
		return _caretAnim;
	}

	void TextInputBuffer::Update(float timeMult)
	{
		if (_active) {
			_caretAnim += timeMult;
		}
	}

	bool TextInputBuffer::OnKeyPressed(const KeyboardEvent& event)
	{
		if (!_active) {
			return false;
		}

		switch (event.sym) {
			case Keys::Backspace: {
				if (_cursor > 0) {
					auto [_, prevPos] = Utf8::PrevChar(_text, _cursor);
					_text = _text.prefix(prevPos) + _text.exceptPrefix(_cursor);
					_cursor = prevPos;
					_caretAnim = 0.0f;
				}
				return true;
			}
			case Keys::Delete: {
				if (_cursor < _text.size()) {
					auto [_, nextPos] = Utf8::NextChar(_text, _cursor);
					_text = _text.prefix(_cursor) + _text.exceptPrefix(nextPos);
					_caretAnim = 0.0f;
				}
				return true;
			}
			case Keys::Left: {
				if (_cursor > 0) {
					auto [c, prevPos] = Utf8::PrevChar(_text, _cursor);
					_cursor = prevPos;
					_caretAnim = 0.0f;
				}
				return true;
			}
			case Keys::Right: {
				if (_cursor < _text.size()) {
					auto [c, nextPos] = Utf8::NextChar(_text, _cursor);
					_cursor = nextPos;
					_caretAnim = 0.0f;
				}
				return true;
			}
			case Keys::Home: {
				_cursor = 0;
				_caretAnim = 0.0f;
				return true;
			}
			case Keys::End: {
				_cursor = _text.size();
				_caretAnim = 0.0f;
				return true;
			}
			default: return false;
		}
	}

	bool TextInputBuffer::OnTextInput(const TextInputEvent& event)
	{
		if (!_active) {
			return false;
		}

		if (_text.size() + event.length <= _maxLength) {
			_text = _text.prefix(_cursor) + StringView(event.text, event.length) + _text.exceptPrefix(_cursor);
			_cursor += event.length;
			_caretAnim = 0.0f;
			return true;
		}
		return false;
	}
}
