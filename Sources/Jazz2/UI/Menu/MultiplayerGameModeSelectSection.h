#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScrollableMenuSection.h"
#include "../../Multiplayer/MpGameMode.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	struct MultiplayerGameModeItem {
		Multiplayer::MpGameMode Mode;
		StringView DisplayName;
	};
#endif

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