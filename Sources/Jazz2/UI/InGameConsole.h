#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../LevelHandler.h"

#include "../../nCine/Input/InputEvents.h"

namespace Jazz2::UI
{
	/**
		@brief Message importance level
		
		Classifies a line written to the @ref InGameConsole by severity and purpose, ranging from echoed
		input and debug output through informational, chat and confirmation messages up to warnings,
		errors and fatal conditions. It influences how each message is presented in the log.
	*/
	enum class MessageLevel
	{
		Unknown,		/**< Unspecified */
		Echo,			/**< Echo of the input */
		Debug,			/**< Debug */
		Info,			/**< Info */
		Chat,			/**< Chat */
		Confirm,		/**< Confirmation */
		Important,		/**< Important */
		Warning,		/**< Warning */
		Error,			/**< Error */
		Assert,			/**< Assert */
		Fatal			/**< Fatal */
	};

	/**
		@brief In-game console
		
		Developer/cheat console overlaid on the level. It shows a scrollable log of messages classified by
		@ref MessageLevel and accepts typed commands with a navigable input history.
	*/
	class InGameConsole : public Canvas
	{
	public:
		/** @brief Creates a new instance */
		InGameConsole(LevelHandler* levelHandler);
		~InGameConsole();

		/** @brief Called when the console is fully initialized */
		void OnInitialized();
		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		/** @brief Called when a key is pressed */
		void OnKeyPressed(const KeyboardEvent& event);
		/** @brief Called when text is entered */
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
		std::int32_t _scrollPos;
		bool _isVisible;

		void ProcessCurrentLine();
		void PruneLogHistory();
		void GetPreviousCommandFromHistory();
		void GetNextCommandFromHistory();
		void ScrollUp(std::int32_t amount);
		void ScrollDown(std::int32_t amount);
	};
}