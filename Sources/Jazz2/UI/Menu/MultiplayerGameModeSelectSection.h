#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScrollableMenuSection.h"
#include "../../Multiplayer/MpGameMode.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	struct MultiplayerGameModeItem {
		Jazz2::Multiplayer::MpGameMode Mode;
		StringView DisplayName;
	};
#endif

	/**
		@brief Multiplayer game mode selection menu section
		
		Lists the available multiplayer game modes (such as Battle, Race, Treasure Hunt, and Cooperation) and lets the
		player choose one for the server being configured.
	*/
	class MultiplayerGameModeSelectSection : public ScrollableMenuSection<MultiplayerGameModeItem>
	{
	public:
		/** @brief Creates a new instance */
		MultiplayerGameModeSelectSection();

		void OnShow(IMenuContainer* root) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
	};
}

#endif