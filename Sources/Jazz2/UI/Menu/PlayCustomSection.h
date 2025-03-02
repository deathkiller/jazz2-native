#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif

	class PlayCustomSection : public ScrollableMenuSection<PlayCustomItem>
	{
	public:
		PlayCustomSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}

#endif