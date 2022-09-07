#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class EpisodeSelectSection : public MenuSection
	{
	public:
		EpisodeSelectSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class ItemFlags {
			None = 0x00,

			IsAvailable = 0x01,
			IsCompleted = 0x02,
			CanContinue = 0x04
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(ItemFlags);

		struct ItemData {
			String Name;
			String DisplayName;
			String FirstLevel;
			String PreviousEpisode;
			ItemFlags Flags;
			uint16_t Position;
			float TouchY;
		};

		SmallVector<ItemData> _items;
		int _selectedIndex;
		float _animation;

		void ExecuteSelected();
		void AddEpisode(const StringView& episodeFile);
	};
}