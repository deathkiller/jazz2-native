#include "InGameConsole.h"
#include "FormattedTextBlock.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Base/Random.h"

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
		: _levelHandler(levelHandler), _currentLine{}, _textCursor(0), _carretAnim(0.0f), _scrollPos(0), _isVisible(false)
	{
		PruneLogHistory();
		_historyIndex = _commandHistory.size();
	}

	InGameConsole::~InGameConsole()
	{
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
		_isVisible = true;

		_currentLine[0] = '\0';
		_textCursor = 0;
		_carretAnim = 0.0f;

		AnimTime = 0.0f;
	}

	void InGameConsole::Hide()
	{
		_isVisible = false;
	}

	void InGameConsole::WriteLine(MessageLevel level, String line)
	{
#if defined(DEATH_TRACE)
		// TODO: Sanitize (\n,\r,\t) and strip formatting (\f) from player name
		switch (level) {
			//default: DEATH_TRACE(TraceLevel::Info, {}, "[<] %s", line.data()); break;
			case MessageLevel::Echo: DEATH_TRACE(TraceLevel::Info, {}, "[>] %s", line.data()); break;
			case MessageLevel::Warning: DEATH_TRACE(TraceLevel::Warning, {}, "[<] %s", line.data()); break;
			case MessageLevel::Error: DEATH_TRACE(TraceLevel::Error, {}, "[<] %s", line.data()); break;
			case MessageLevel::Assert: DEATH_TRACE(TraceLevel::Assert, {}, "[<] %s", line.data()); break;
			case MessageLevel::Fatal: DEATH_TRACE(TraceLevel::Fatal, {}, "[<] %s", line.data()); break;
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