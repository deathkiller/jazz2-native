#include "InGameConsole.h"
#include "FormattedTextBlock.h"
#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/I18n.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Input/IInputManager.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

namespace Jazz2::UI
{
	struct LogLine {
		MessageLevel Level;
		float TimeLeft;
		FormattedTextBlock Message;

		LogLine(MessageLevel level, String&& message, Font* font);
	};

	static SmallVector<LogLine, 0> _logHistory;
	static SmallVector<String, 0> _commandHistory;

	InGameConsole::InGameConsole(LevelHandler* levelHandler)
		: _levelHandler(levelHandler), _currentLine{}, _textCursor(0), _carretAnim(0.0f), _scrollPos(0), _isVisible(false),
			_keyboardVisible(false)
#if defined(DEATH_TARGET_ANDROID)
			, _recalcVisibleBoundsTimeLeft(30.0f)
#endif
	{
		PruneLogHistory();
		_historyIndex = _commandHistory.size();

#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
		_initialVisibleSize.X = _currentVisibleBounds.W;
		_initialVisibleSize.Y = _currentVisibleBounds.H;
#endif
	}

	InGameConsole::~InGameConsole()
	{
		// Make sure the on-screen keyboard is not left open if the console is destroyed while visible
		if (_keyboardVisible) {
			theApplication().HideScreenKeyboard();
			_keyboardVisible = false;
		}
	}

	void InGameConsole::OnInitialized()
	{
		auto& resolver = ContentResolver::Get();
		_smallFont = resolver.GetFont(FontType::Small);

		for (auto& line : _logHistory) {
			line.Message.SetFont(_smallFont);
		}
	}

	void InGameConsole::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		_carretAnim += timeMult;

#if defined(DEATH_TARGET_ANDROID)
		if (_isVisible && _keyboardVisible) {
			_recalcVisibleBoundsTimeLeft -= timeMult;
			if (_recalcVisibleBoundsTimeLeft <= 0.0f) {
				_recalcVisibleBoundsTimeLeft = 60.0f;
				_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
			}
		}
#endif

		for (std::int32_t i = _logHistory.size() - 1; i >= 0; i--) {
			auto& line = _logHistory[i];
			if (line.TimeLeft <= 0.0f) {
				break;
			}
			line.TimeLeft -= timeMult;
		}
	}

	bool InGameConsole::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _levelHandler->GetViewSize();
		Vector2f currentLinePos = Vector2f(46.0f, ViewSize.Y - 60.0f);
		float width = ViewSize.X - currentLinePos.X - 24.0f;

		if (_isVisible) {
			DrawSolid(Vector2f(0.0f, 0.0f), 80, ViewSize.As<float>(), Colorf(0.0f, 0.0f, 0.0f, std::min(AnimTime * 5.0f, 0.6f)));

			Colorf color = (_currentLine[0] == '/' ? Colorf(0.38f, 0.48f, 0.69f, 0.5f) : Colorf(0.62f, 0.44f, 0.34f, 0.5f));

			// Current line
			std::int32_t charOffset = 0, charOffsetShadow = 0;
			_smallFont->DrawString(this, ">"_s, charOffsetShadow, currentLinePos.X - 16.0f + 1.0f, currentLinePos.Y + 2.0f, FontShadowLayer + 100,
				Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
			_smallFont->DrawString(this, ">"_s, charOffset, currentLinePos.X - 16.0f, currentLinePos.Y, FontLayer + 100,
				Alignment::Left, color, 0.8f, 0.0f, 0.0f, 0.0f);

			StringView currentLine = _currentLine;
			_smallFont->DrawString(this, currentLine, charOffsetShadow, currentLinePos.X + 1.0f, currentLinePos.Y + 2.0f, FontShadowLayer + 100,
				Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
			_smallFont->DrawString(this, currentLine, charOffset, currentLinePos.X, currentLinePos.Y, FontLayer + 100,
				Alignment::Left, color, 0.8f, 0.0f, 0.0f, 0.0f);

			// Carret
			Vector2f textToCursorSize = _smallFont->MeasureString(StringView{_currentLine, _textCursor}, 0.8f);
			DrawSolid(Vector2f(currentLinePos.X + textToCursorSize.X + 1.0f, currentLinePos.Y - 7.0f), FontLayer + 120, Vector2f(1.0f, 12.0f),
				Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
		}

		// History
		if (_scrollPos > (std::int32_t)_logHistory.size() - 1) {
			_scrollPos = 0;
		}

		Vector2f historyLinePos = currentLinePos;
		historyLinePos.Y -= 4.0f;
		for (std::int32_t i = _logHistory.size() - 1 - _scrollPos; i >= 0; i--) {
			if (historyLinePos.Y < 50.0f) {
				break;
			}

			auto& line = _logHistory[i];
			if (line.Level == MessageLevel::Debug && !_isVisible) {
				continue;
			}
			float alpha = (_isVisible ? 1.0f : std::min(line.TimeLeft * 0.06f, 1.0f));
			if (alpha <= 0.0f) {
				break;
			}

			Colorf color;
			switch (line.Level) {
				default: color = Font::DefaultColor; color.A *= alpha; break;
				case MessageLevel::Echo: color = Font::DefaultColor; color.A *= alpha; break;
				case MessageLevel::Debug: color = Colorf(0.36f, 0.44f, 0.35f, 0.5f * sqrtf(alpha)); break;
				case MessageLevel::Error: color = Colorf(0.6f, 0.41f, 0.40f, 0.5f * sqrtf(alpha)); break;
				case MessageLevel::Fatal: color = Colorf(0.48f, 0.38f, 0.34f, 0.5f * sqrtf(alpha)); break;
			}

			std::int32_t charOffset = 0;
			line.Message.SetDefaultColor(color);

			Vector2f textSize = line.Message.MeasureSize(Vector2f(width, 400.0f));
			historyLinePos.Y -= textSize.Y;

			if (line.Level == MessageLevel::Echo) {
				std::int32_t charOffset = 0, charOffsetShadow = 0;
				_smallFont->DrawString(this, "›"_s, charOffsetShadow, historyLinePos.X - 13.0f, historyLinePos.Y + 2.0f, FontShadowLayer + 100,
							Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f * sqrtf(alpha)), 0.8f, 0.0f, 0.0f, 0.0f);
				_smallFont->DrawString(this, "›"_s, charOffset, historyLinePos.X - 13.0f, historyLinePos.Y, FontLayer + 100,
					Alignment::Left, color, 0.8f, 0.0f, 0.0f, 0.0f);
			}

			line.Message.Draw(this, Rectf(historyLinePos.X, historyLinePos.Y, width, 400.0f), FontLayer + 100, charOffset);
		}

		// On-screen keyboard support for touch devices: a tappable hint in the top-left corner toggles the software
		// keyboard, and while it is shown the current input is mirrored near the top of the screen so it is not hidden
		// behind the keyboard (mirrors how text input is handled in the main menu)
		if (_isVisible && theApplication().CanShowScreenKeyboard()) {
			bool keyboardCoversInput = _keyboardVisible;
#if defined(DEATH_TARGET_ANDROID)
			// On Android the keyboard state can't be queried directly, so detect it from the shrunk visible bounds
			keyboardCoversInput = (_keyboardVisible &&
				(_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y) &&
				(_currentVisibleBounds.Y * ViewSize.Y / _initialVisibleSize.Y < 32.0f));
#endif

			if (keyboardCoversInput) {
				// Dark overlay over the console with the input line redrawn near the top, above the keyboard
				DrawSolid(Vector2f(0.0f, 0.0f), KeyboardLayer, ViewSize.As<float>(), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

				Colorf color = (_currentLine[0] == '/' ? Colorf(0.38f, 0.48f, 0.69f, 0.5f) : Colorf(0.62f, 0.44f, 0.34f, 0.5f));
				float topLineY = (ViewSize.Y >= 300 ? 34.0f : 22.0f);

				std::int32_t charOffset = 0, charOffsetShadow = 0;
				_smallFont->DrawString(this, ">"_s, charOffsetShadow, 120.0f - 16.0f + 1.0f, topLineY + 2.0f, KeyboardLayer + 10,
					Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 1.0f, 0.0f, 0.0f, 0.0f);
				_smallFont->DrawString(this, ">"_s, charOffset, 120.0f - 16.0f, topLineY, KeyboardLayer + 12,
					Alignment::Left, color, 1.0f, 0.0f, 0.0f, 0.0f);

				StringView currentLine = _currentLine;
				_smallFont->DrawString(this, currentLine, charOffsetShadow, 120.0f + 1.0f, topLineY + 2.0f, KeyboardLayer + 10,
					Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 1.0f, 0.0f, 0.0f, 0.0f);
				_smallFont->DrawString(this, currentLine, charOffset, 120.0f, topLineY, KeyboardLayer + 12,
					Alignment::Left, color, 1.0f, 0.0f, 0.0f, 0.0f);

				Vector2f textToCursorSize = _smallFont->MeasureString(StringView{_currentLine, _textCursor}, 1.0f);
				DrawSolid(Vector2f(120.0f + textToCursorSize.X + 1.0f, topLineY - 8.0f), KeyboardLayer + 14, Vector2f(1.0f, 14.0f),
					Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
			}

			// TRANSLATORS: Tappable hint in the top-left corner of the in-game console to toggle the on-screen keyboard
			StringView keyboardLabel = _("Keyboard");
			Vector2f labelSize = _smallFont->MeasureString(keyboardLabel, 0.8f);
			DrawSolid(Vector2f(10.0f, 11.0f), KeyboardLayer + 20, Vector2f(labelSize.X + 12.0f, 18.0f),
				Colorf(0.0f, 0.0f, 0.0f, _keyboardVisible ? 0.5f : 0.3f));
			std::int32_t hintCharOffset = 0;
			_smallFont->DrawString(this, keyboardLabel, hintCharOffset, 16.0f, 20.0f, KeyboardLayer + 22,
				Alignment::Left, _keyboardVisible ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Colorf(0.45f, 0.45f, 0.45f, 0.5f), 0.8f, 0.0f, 0.0f, 0.0f);
		}

		return true;
	}

	void InGameConsole::OnKeyPressed(const KeyboardEvent& event)
	{
		switch (event.sym) {
			case Keys::Return:
			case Keys::NumPadEnter: {
				ProcessCurrentLine();
				break;
			}
			case Keys::Tab: {
				// If the console is bound to Tab key, allow to toggle it off
				if (ControlScheme::ContainsTarget(PlayerAction::Console, ControlScheme::CreateTarget(Keys::Tab))) {
					Hide();
				}
				break;
			}
			case Keys::Backspace: {
				if (_textCursor > 0) {
					std::size_t lengthAfter = std::strlen(&_currentLine[_textCursor]);
					std::size_t startPos;
					if ((event.mod & KeyMod::Ctrl) != KeyMod::None) {
						auto found = StringView{_currentLine, _textCursor}.findLastOr(' ', _currentLine);
						startPos = found.begin() - _currentLine;
					} else {
						auto [_, prevPos] = Utf8::PrevChar(_currentLine, _textCursor);
						startPos = prevPos;
					}
					std::memmove(&_currentLine[startPos], &_currentLine[_textCursor], lengthAfter + 1);
					_textCursor = startPos;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Delete: {
				if (_currentLine[_textCursor] != '\0') {
					std::size_t lengthAfter = std::strlen(&_currentLine[_textCursor]);
					std::size_t endPos;
					if ((event.mod & KeyMod::Ctrl) != KeyMod::None) {
						auto found = StringView{&_currentLine[_textCursor]}.findOr(' ', &_currentLine[_textCursor + lengthAfter]);
						endPos = found.end() - _currentLine;
					} else {
						auto [_, nextPos] = Utf8::NextChar(_currentLine, _textCursor);
						endPos = nextPos;
					}
					std::memmove(&_currentLine[_textCursor], &_currentLine[endPos], lengthAfter + 1);
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Left: {
				if (_textCursor > 0) {
					auto [c, prevPos] = Utf8::PrevChar(_currentLine, _textCursor);
					_textCursor = prevPos;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Right: {
				if (_currentLine[_textCursor] != '\0') {
					auto [c, nextPos] = Utf8::NextChar(_currentLine, _textCursor);
					_textCursor = nextPos;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::Up: {
				if (event.mod & KeyMod::Ctrl) {
					ScrollUp(1);
				} else {
					GetPreviousCommandFromHistory();
				}
				break;
			}
			case Keys::Down: {
				if (event.mod & KeyMod::Ctrl) {
					ScrollDown(1);
				} else {
					GetNextCommandFromHistory();
				}
				break;
			}
			case Keys::PageUp: {
				ScrollUp(3);
				break;
			}
			case Keys::PageDown: {
				ScrollDown(3);
				break;
			}
			case Keys::K: { // Clear line
				if ((event.mod & KeyMod::Ctrl) != KeyMod::None && (event.mod & (KeyMod::Alt | KeyMod::Shift)) == KeyMod::None) {
					_currentLine[0] = '\0';
					_textCursor = 0;
					_carretAnim = 0.0f;
				}
				break;
			}
			case Keys::L: { // Clear history
				if ((event.mod & KeyMod::Ctrl) != KeyMod::None && (event.mod & (KeyMod::Alt | KeyMod::Shift)) == KeyMod::None) {
					_logHistory.clear();
				}
				break;
			}
			case Keys::V: { // Paste
				if ((event.mod & KeyMod::Ctrl) != KeyMod::None && (event.mod & (KeyMod::Alt | KeyMod::Shift)) == KeyMod::None) {
					auto text = theApplication().GetInputManager().getClipboardText();
					if (!text.empty()) {
						auto trimmedText = text.trimmedPrefix();
						if (auto firstNewLine = trimmedText.find('\n')) {
							trimmedText = trimmedText.prefix(firstNewLine.begin());
						}
						trimmedText = trimmedText.trimmedSuffix();

						std::size_t lengthTotal = std::strlen(_currentLine);
						if (lengthTotal + trimmedText.size() < MaxLineLength) {
							std::size_t lengthAfter = std::strlen(&_currentLine[_textCursor]);
							if (lengthAfter > 0) {
								std::memmove(&_currentLine[_textCursor + trimmedText.size()], &_currentLine[_textCursor], lengthAfter);
							}
							_currentLine[_textCursor + trimmedText.size() + lengthAfter] = '\0';
							std::memcpy(&_currentLine[_textCursor], trimmedText.data(), trimmedText.size());
							_textCursor += trimmedText.size();
							_carretAnim = 0.0f;
						}
					}
				}
				break;
			}
		}
	}

	void InGameConsole::OnTextInput(const TextInputEvent& event)
	{
		if (_textCursor + event.length < MaxLineLength) {
			std::size_t lengthAfter = std::strlen(&_currentLine[_textCursor]);
			if (lengthAfter > 0) {
				std::memmove(&_currentLine[_textCursor + event.length], &_currentLine[_textCursor], lengthAfter);
			}
			_currentLine[_textCursor + event.length + lengthAfter] = '\0';
			std::memcpy(&_currentLine[_textCursor], event.text, event.length);
			_textCursor += event.length;
			_carretAnim = 0.0f;
		}
	}

	void InGameConsole::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
	{
		if (!_isVisible || !theApplication().CanShowScreenKeyboard()) {
			return;
		}

		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				// The top-left corner toggles the on-screen keyboard (matching the hint drawn in OnDraw)
				if (x < 0.2f && y < 80.0f) {
					ToggleScreenKeyboard();
				}
			}
		}
	}

	void InGameConsole::Clear()
	{
		_logHistory.clear();
	}

	bool InGameConsole::IsVisible() const
	{
		return _isVisible;
	}

	void InGameConsole::Show()
	{
		// Don't allow to show the console if it was hidden in the last frame (mainly to fix toggling with Tab key)
		if (AnimTime <= AnimTimeMultiplier) {
			return;
		}

		_isVisible = true;

		_currentLine[0] = '\0';
		_textCursor = 0;
		_carretAnim = 0.0f;

		AnimTime = 0.0f;

#if defined(DEATH_TARGET_ANDROID)
		// On touch-only devices the on-screen keyboard is the only way to type, so show it right away. On desktop
		// the keyboard stays hidden until the user taps the hint, so it never pops up over a physical keyboard.
		if (!_keyboardVisible && theApplication().CanShowScreenKeyboard()) {
			theApplication().ShowScreenKeyboard();
			_keyboardVisible = true;
			RecalcLayoutForScreenKeyboard();
		}
#endif
	}

	void InGameConsole::Hide()
	{
		_isVisible = false;

		if (_keyboardVisible) {
			theApplication().HideScreenKeyboard();
			_keyboardVisible = false;
		}

		AnimTime = 0.0f;
	}

	void InGameConsole::ToggleScreenKeyboard()
	{
		if (!theApplication().CanShowScreenKeyboard()) {
			return;
		}

		if (_keyboardVisible) {
			theApplication().HideScreenKeyboard();
			_keyboardVisible = false;
		} else {
			theApplication().ShowScreenKeyboard();
			_keyboardVisible = true;
		}

		RecalcLayoutForScreenKeyboard();
	}

	void InGameConsole::RecalcLayoutForScreenKeyboard()
	{
#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();

		if (_recalcVisibleBoundsTimeLeft > 30.0f) {
			_recalcVisibleBoundsTimeLeft = 30.0f;
		}
#endif
	}

	void InGameConsole::WriteLine(MessageLevel level, String line)
	{
#if defined(DEATH_TRACE)
		switch (level) {
			//case MessageLevel::Info: is skipped, because these messages are usually written to the log separately
			case MessageLevel::Echo: __DEATH_TRACE(TraceLevel::Info, {}, "> │ {}", Font::StripFormatting(line)); break;
			case MessageLevel::Chat:
			case MessageLevel::Confirm: __DEATH_TRACE(TraceLevel::Info, {}, "< │ {}", Font::StripFormatting(line)); break;
			case MessageLevel::Warning: __DEATH_TRACE(TraceLevel::Warning, {}, "< │ {}", Font::StripFormatting(line)); break;
			case MessageLevel::Error: __DEATH_TRACE(TraceLevel::Error, {}, "< │ {}", Font::StripFormatting(line)); break;
			case MessageLevel::Assert: __DEATH_TRACE(TraceLevel::Assert, {}, "< │ {}", Font::StripFormatting(line)); break;
			case MessageLevel::Fatal: __DEATH_TRACE(TraceLevel::Fatal, {}, "< │ {}", Font::StripFormatting(line)); break;
		}
#endif
		_logHistory.emplace_back(level, std::move(line), _smallFont);
		_scrollPos = 0;
	}

	void InGameConsole::ProcessCurrentLine()
	{
		if (_currentLine[0] == '\0') {
			// Empty line
			return;
		}

		StringView line = _currentLine;
		if (_commandHistory.empty() || _commandHistory.back() != line) {
			_commandHistory.emplace_back(line);
		}
		_historyIndex = _commandHistory.size();

		if (line == "/clear"_s || line == "/cls"_s) {
			Clear();
		} else {
			if (!_levelHandler->OnConsoleCommand(line)) {
				WriteLine(MessageLevel::Echo, line);
				WriteLine(MessageLevel::Error, _("Unknown command"));
			}
		}

		_currentLine[0] = '\0';
		_textCursor = 0;
		_carretAnim = 0.0f;
	}

	void InGameConsole::PruneLogHistory()
	{
		constexpr std::int32_t MaxHistoryLines = 64;

		if (_logHistory.size() > MaxHistoryLines) {
			_logHistory.erase(&_logHistory[0], &_logHistory[_logHistory.size() - MaxHistoryLines]);
		}
	}

	void InGameConsole::GetPreviousCommandFromHistory()
	{
		if (_historyIndex > 0) {
			_historyIndex--;
			std::memcpy(_currentLine, _commandHistory[_historyIndex].data(), _commandHistory[_historyIndex].size() + 1);
			_textCursor = _commandHistory[_historyIndex].size();
			_carretAnim = 0.0f;
		}
	}

	void InGameConsole::GetNextCommandFromHistory()
	{
		if (_historyIndex < _commandHistory.size()) {
			_historyIndex++;
			if (_historyIndex == _commandHistory.size()) {
				_currentLine[0] = '\0';
				_textCursor = 0;
			} else {
				std::memcpy(_currentLine, _commandHistory[_historyIndex].data(), _commandHistory[_historyIndex].size() + 1);
				_textCursor = _commandHistory[_historyIndex].size();
			}
			_carretAnim = 0.0f;
		}
	}

	void InGameConsole::ScrollUp(std::int32_t amount)
	{
		_scrollPos += amount;

		if (_scrollPos > (std::int32_t)_logHistory.size() - 1) {
			_scrollPos = (std::int32_t)_logHistory.size() - 1;
		}
	}

	void InGameConsole::ScrollDown(std::int32_t amount)
	{
		_scrollPos -= amount;

		if (_scrollPos < 0) {
			_scrollPos = 0;
		}
	}

	LogLine::LogLine(MessageLevel level, String&& message, Font* font)
		: Level(level), TimeLeft(5.0f * FrameTimer::FramesPerSecond)
	{
		Message.SetText(std::move(message));
		Message.SetScale(0.8f);
		Message.SetFont(font);
		Message.SetMultiline(true);
		Message.SetWrapping(true);
	}
}