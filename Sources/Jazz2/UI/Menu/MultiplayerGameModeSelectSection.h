#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Multiplayer game mode selection menu section

		Lists the available multiplayer game modes (such as Battle, Race, Treasure Hunt, and Cooperation) and lets the
		player choose one for the server being configured. Built declaratively on top of @ref WidgetSection.
	*/
	class MultiplayerGameModeSelectSection : public WidgetSection
	{
	public:
		void OnShow(IMenuContainer* root) override;
	};
}

#endif
