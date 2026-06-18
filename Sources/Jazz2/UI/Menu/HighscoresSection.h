#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class HighscoreFlags {
		None = 0,

		IsDefault = 0x01,
		IsReforged = 0x02,
		CheatsUsed = 0x04
	};

	DEATH_ENUM_FLAGS(HighscoreFlags);

	struct HighscoreItem {
		String PlayerName;
		std::uint64_t PlayerId;
		HighscoreFlags Flags;
		PlayerType Type;
		GameDifficulty Difficulty;
		std::int32_t Lives;
		std::int32_t Score;
		std::int32_t Gems[4];
		std::int64_t CreatedDate;
		std::uint64_t ElapsedMilliseconds;
	};

	struct HighscoreSeries {
		SmallVector<HighscoreItem, 0> Items;
	};
#endif

	/**
		@brief Highscores menu section

		Displays the saved highscore tables per series and lets the player enter a name when a completed run qualifies
		for a new entry. Built on top of @ref WidgetSection with each entry drawn through a @ref CanvasWidget; the series
		switching, inline name entry and serialization are driven by the section.
	*/
	class HighscoresSection : public WidgetSection
	{
	public:
		/** @brief Creates a new instance */
		HighscoresSection();
		/**
		 * @brief Creates a new instance and adds a new highscore from a completed run
		 *
		 * @param seriesIndex          Index of the highscore series the new item belongs to
		 * @param difficulty           Difficulty the run was played on
		 * @param isReforged           Whether the run used the Reforged ruleset
		 * @param cheatsUsed           Whether any cheats were used during the run
		 * @param elapsedMilliseconds  Total time spent playing the run
		 * @param itemToAdd            Player state carried over from the completed run
		 */
		HighscoresSection(std::int32_t seriesIndex, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, std::uint64_t elapsedMilliseconds, const PlayerCarryOver& itemToAdd);

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;
		NavigationFlags GetNavigationFlags() const override;

		/** @brief Returns the highscore series index for the specified episode, or `-1` if it has none */
		static std::int32_t TryGetSeriesIndex(StringView episodeName, bool playerDied);

	protected:
		void OnBackPressed() override;

	private:
		enum class SeriesName {
			BaseGame,
			SharewareDemo,
			TheSecretFiles,

			Count
		};

		static constexpr std::int32_t MaxItems = 10;
		static constexpr std::int32_t MaxPlayerNameLength = 24;
		static constexpr StringView FileName = "Highscores.list"_s;
		static constexpr std::uint16_t FileVersion = 1;

		HighscoreSeries _series[(std::int32_t)SeriesName::Count];
		std::int32_t _selectedSeries;
		std::int32_t _notValidPos;
		std::int32_t _notValidSeries;
		std::int32_t _pendingFocus;
		std::size_t _textCursor;
		float _carretAnim;
		float _animation;
		bool _waitForInput;
		ScrollView* _list;
#if defined(DEATH_TARGET_ANDROID)
		Vector2i _initialVisibleSize;
		Recti _currentVisibleBounds;
		float _recalcVisibleBoundsTimeLeft;
#endif

		std::int32_t GetSelectedRow() const;
		void RebuildList();
		void SwitchSeries(std::int32_t direction);
		void FinishNameEntry();
		void DrawScoreRow(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool isSelected, std::int32_t row);
		void FillDefaultsIfEmpty();
		void DeserializeFromFile();
		void SerializeToFile();
		void AddItemAndFocus(HighscoreItem&& item);
		void RecalcLayoutForScreenKeyboard();
	};
}
