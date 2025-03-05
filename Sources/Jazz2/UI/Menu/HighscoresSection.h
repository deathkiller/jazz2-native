#pragma once

#include "ScrollableMenuSection.h"

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

	/** @brief Highscores menu section */
	class HighscoresSection : public ScrollableMenuSection<HighscoreItem*>
	{
	public:
		HighscoresSection();
		HighscoresSection(std::int32_t seriesIndex, GameDifficulty difficulty, bool isReforged, bool cheatsUsed, std::uint64_t elapsedMilliseconds, const PlayerCarryOver& itemToAdd);

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		NavigationFlags GetNavigationFlags() const override;

		static std::int32_t TryGetSeriesIndex(StringView episodeName, bool playerDied);

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnHandleInput() override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;
		void OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos) override;
		void OnExecuteSelected() override;
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
		std::size_t _textCursor;
		float _carretAnim;
		bool _waitForInput;
#if defined(DEATH_TARGET_ANDROID)
		Vector2i _initialVisibleSize;
		Recti _currentVisibleBounds;
		float _recalcVisibleBoundsTimeLeft;

		void RecalcLayoutForSoftInput();
#endif

		void FillDefaultsIfEmpty();
		void DeserializeFromFile();
		void SerializeToFile();
		void AddItemAndFocus(HighscoreItem&& item);
		void RefreshList();
	};
}