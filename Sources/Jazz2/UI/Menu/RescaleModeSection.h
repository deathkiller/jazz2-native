#pragma once

#include "ScrollableMenuSection.h"
#include "../../PreferencesCache.h"

namespace Jazz2::UI::Menu
{
	struct RescaleModeItem {
		RescaleMode Mode;
		StringView DisplayName;
	};

	class RescaleModeSection : public ScrollableMenuSection<RescaleModeItem>
	{
	public:
		RescaleModeSection();

		void OnDraw(Canvas* canvas) override;

	private:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
	};
}