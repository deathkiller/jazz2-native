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
		struct ItemData {
			Episode Description;
			float TouchY;
		};

		SmallVector<ItemData> _items;
		int _selectedIndex;
		float _animation;

		void ExecuteSelected();
		void AddEpisode(const StringView& episodeFile);
	};
}