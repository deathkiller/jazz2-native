#include "InGameConsole.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Base/Random.h"

#include <Utf8.h>
#include <Containers/StringConcatenable.h>

namespace Jazz2::UI
{
	InGameConsole::InGameConsole(LevelHandler* levelHandler)
		: _levelHandler(levelHandler), _currentLine{}, _textCursor(0), _carretAnim(0.0f)
	{
		auto& resolver = ContentResolver::Get();

		_smallFont = resolver.GetFont(FontType::Small);
	}

	InGameConsole::~InGameConsole()
	{
	}

	void InGameConsole::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		_carretAnim += timeMult;
	}

	bool InGameConsole::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _levelHandler->GetViewSize();
		Vector2f currentLinePos = Vector2f(46.0f, ViewSize.Y - 60.0f);

		DrawSolid(Vector2f(0.0f, 0.0f), 20, ViewSize.As<float>(), Colorf(0.0f, 0.0f, 0.0f, std::min(AnimTime * 5.0f, 0.6f)));

		// History
		Vector2f historyLinePos = currentLinePos;
		historyLinePos.Y -= 20.0f;
		for (std::int32_t i = _log.size() - 1; i >= 0; i--) {
			if (historyLinePos.Y < 30.0f) {
				break;
			}

			auto& line = _log[i];
			Colorf color;
			switch (line.Level) {
				default: color = Font::DefaultColor; break;

				case TraceLevel::Unknown: {
					color = Font::DefaultColor;

					std::int32_t charOffset = 0, charOffsetShadow = 0;
					_smallFont->DrawString(this, "·"_s, charOffsetShadow, historyLinePos.X - 15.0f, historyLinePos.Y + 2.0f, FontShadowLayer + 200,
								Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
					_smallFont->DrawString(this, "·"_s, charOffset, historyLinePos.X - 15.0f, historyLinePos.Y, FontLayer + 200,
						Alignment::Left, color, 0.8f, 0.0f, 0.0f, 0.0f);

					break;
				}

				case TraceLevel::Fatal: color = Colorf(0.48f, 0.38f, 0.34f, 0.5f); break;
				case TraceLevel::Error: color = Colorf(0.6f, 0.41f, 0.40f, 0.5f); break;
			}

			std::int32_t charOffset = 0, charOffsetShadow = 0;
			_smallFont->DrawString(this, line.Message, charOffsetShadow, historyLinePos.X, historyLinePos.Y + 2.0f, FontShadowLayer + 200,
						Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
			_smallFont->DrawString(this, line.Message, charOffset, historyLinePos.X, historyLinePos.Y, FontLayer + 200,
				Alignment::Left, color, 0.8f, 0.0f, 0.0f, 0.0f);

			historyLinePos.Y -= 16.0f;
		}

		// Current line
		std::int32_t charOffset = 0, charOffsetShadow = 0;
		_smallFont->DrawString(this, ">"_s, charOffsetShadow, currentLinePos.X - 16.0f + 1.0f, currentLinePos.Y + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
		_smallFont->DrawString(this, ">"_s, charOffset, currentLinePos.X - 16.0f, currentLinePos.Y, FontLayer + 200,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 0.0f, 0.0f);
		
		StringView currentLine = _currentLine;
		_smallFont->DrawString(this, currentLine, charOffsetShadow, currentLinePos.X + 1.0f, currentLinePos.Y + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f, 0.0f, 0.0f, 0.0f);
		_smallFont->DrawString(this, currentLine, charOffset, currentLinePos.X, currentLinePos.Y, FontLayer + 200,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 0.0f, 0.0f);

		Vector2f textToCursorSize = _smallFont->MeasureString(StringView{_currentLine, _textCursor}, 0.8f);
		DrawSolid(Vector2f(currentLinePos.X + textToCursorSize.X + 1.0f, currentLinePos.Y - 7.0f), FontLayer + 220, Vector2f(1.0f, 12.0f),
			Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);

		return true;
	}

	void InGameConsole::OnAttached()
	{
		_currentLine[0] = '\0';
		_textCursor = 0;
		_carretAnim = 0.0f;

		AnimTime = 0.0f;
	}

	void InGameConsole::OnKeyPressed(const KeyboardEvent& event)
	{
		switch (event.sym) {
			case Keys::Escape: {
				setParent(nullptr);
				break;
			}
			case Keys::Return: {
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

	void InGameConsole::WriteLine(TraceLevel level, String line)
	{
		_log.emplace_back(level, std::move(line));
	}

	void InGameConsole::ProcessCurrentLine()
	{
		if (_currentLine[0] == '\0') {
			// Empty line
			return;
		}

		StringView line = _currentLine;
		if (line == "clear"_s || line == "cls"_s) {
			_log.clear();
		} else {
			WriteLine(TraceLevel::Unknown, line);
			if (line == "help"_s) {
				WriteLine(TraceLevel::Info, _("For more information, visit the official website:") + " \f[w:80]\f[c:#707070]https://deat.tk/jazz2/help\f[/c]\f[/w]"_s);
			} else if (line == "jjk"_s || line == "jjkill"_s) {
				_levelHandler->CheatKill();
			} else if (line == "jjgod"_s) {
				_levelHandler->CheatGod();
			} else if (line == "jjnext"_s) {
				_levelHandler->CheatNext();
			} else if (line == "jjguns"_s || line == "jjammo"_s) {
				_levelHandler->CheatGuns();
			} else if (line == "jjrush"_s) {
				_levelHandler->CheatRush();
			} else if (line == "jjgems"_s) {
				_levelHandler->CheatGems();
			} else if (line == "jjbird"_s) {
				_levelHandler->CheatBird();
			} else if (line == "jjpower"_s) {
				_levelHandler->CheatPower();
			} else if (line == "jjcoins"_s) {
				_levelHandler->CheatCoins();
			} else if (line == "jjmorph"_s) {
				_levelHandler->CheatMorph();
			} else if (line == "jjshield"_s) {
				_levelHandler->CheatShield();
			} else {
				WriteLine(TraceLevel::Error, "Unknown command"_s);
			}
		}

		_currentLine[0] = '\0';
		_textCursor = 0;
		_carretAnim = 0.0f;
	}

	InGameConsole::LogLine::LogLine(TraceLevel level, String&& message)
		: Level(level), Message(std::move(message))
	{
	}
}