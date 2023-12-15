#pragma once

#if defined(WITH_MULTIPLAYER)

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class PlayCustomItemType {
		PlayCustomLevels,
		PlayStoryInCoop,
		CreateCustomServer,
		ConnectToServer
	};

	struct PlayCustomItem {
		PlayCustomItemType Type;
		StringView DisplayName;
	};

	class PlayCustomSection : public ScrollableMenuSection<PlayCustomItem>
	{
	public:
		PlayCustomSection();

		void OnDraw(Canvas* canvas) override;

	private:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}

#endif