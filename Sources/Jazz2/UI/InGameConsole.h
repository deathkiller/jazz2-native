#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../ILevelHandler.h"

#include "../../nCine/Input/InputEvents.h"

namespace Jazz2::UI
{
	class InGameConsole : public Canvas
	{
	public:
		InGameConsole(LevelHandler* levelHandler);
		~InGameConsole();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		void OnAttached();
		void OnKeyPressed(const KeyboardEvent& event);
		void OnTextInput(const TextInputEvent& event);

		void WriteLine(TraceLevel level, String line);

	private:
		struct LogLine {
			TraceLevel Level;
			String Message;

			LogLine(TraceLevel level, String&& message);
		};

		static constexpr std::uint16_t MainLayer = 100;
		static constexpr std::uint16_t ShadowLayer = 80;
		static constexpr std::uint16_t FontLayer = 200;
		static constexpr std::uint16_t FontShadowLayer = 120;

		static constexpr std::int32_t MaxLineLength = 256;
		
		LevelHandler* _levelHandler;
		Font* _smallFont;
		char _currentLine[MaxLineLength];
		std::size_t _textCursor;
		float _carretAnim;
		SmallVector<LogLine, 0> _log;

		void ProcessCurrentLine();
	};
}