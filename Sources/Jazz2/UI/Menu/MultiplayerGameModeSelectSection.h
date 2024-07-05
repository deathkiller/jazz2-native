#pragma once

#if defined(WITH_MULTIPLAYER)

#include "ScrollableMenuSection.h"
#include "../../Multiplayer/MultiplayerGameMode.h"

namespace Jazz2::UI::Menu
{
	struct MultiplayerGameModeItem {
		Multiplayer::MultiplayerGameMode Mode;
		StringView DisplayName;
	};

	class MultiplayerGameModeSelectSection : public ScrollableMenuSection<MultiplayerGameModeItem>
	{
	public:
		MultiplayerGameModeSelectSection();

		void OnDraw(Canvas* canvas) override;

	private:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
	};
}

#endif