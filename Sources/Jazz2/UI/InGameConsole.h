#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../ILevelHandler.h"

#include "../../nCine/Input/InputEvents.h"

namespace Jazz2::UI
{
	/** @brief Message importance level */
	enum class MessageLevel
	{
		Unknown,		/**< Unspecified */
		Echo,			/**< Echo of the input */
		Debug,			/**< Debug */
		Info,			/**< Info */
		Important,		/**< Important */
		Warning,		/**< Warning */
		Error,			/**< Error */
		Assert,			/**< Assert */
		Fatal			/**< Fatal */
	};

	/** @brief In-game console */
	class InGameConsole : public Canvas
	{
	public:
		InGameConsole(LevelHandler* levelHandler);
		~InGameConsole();

		void OnInitialized();
		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnKeyPressed(const KeyboardEvent& event);
		void OnTextInput(const TextInputEvent& event);

		/** @brief Clears the console and its history */
		static void Clear();
		/** @brief Returns `true` if the console is visible */
		bool IsVisible() const;
		/** @brief Shows the console */
		void Show();
		/** @brief Hides the console */
		void Hide();
		/** @brief Writes a line to the console history */
		void WriteLine(MessageLevel level, String line);

	private:
		static constexpr std::uint16_t MainLayer = 100;
		static constexpr std::uint16_t ShadowLayer = 80;
		static constexpr std::uint16_t FontLayer = 200;
		static constexpr std::uint16_t FontShadowLayer = 120;

		static constexpr std::int32_t MaxLineLength = 128;
		
		LevelHandler* _levelHandler;
		Font* _smallFont;
		char _currentLine[MaxLineLength];
		std::size_t _textCursor;
		float _carretAnim;
		std::int32_t _historyIndex;
		bool _isVisible;

		void ProcessCurrentLine();
		void PruneLogHistory();
		void GetPreviousCommandFromHistory();
		void GetNextCommandFromHistory();
	};
}