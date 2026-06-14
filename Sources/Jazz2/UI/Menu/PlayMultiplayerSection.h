#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class PlayMultiplayerItemType {
		ConnectToServer,
		CreatePublicServer,
		CreatePrivateServer
	};

	struct PlayMultiplayerItem {
		PlayMultiplayerItemType Type;
		StringView DisplayName;
	};
#endif

	/**
		@brief Play multiplayer menu section
		
		Multiplayer entry screen listing the options to connect to a server or to create a public or private server.
	*/
	class PlayMultiplayerSection : public ScrollableMenuSection<PlayMultiplayerItem>
	{
	public:
		/** @brief Creates a new instance */
		PlayMultiplayerSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}

#endif