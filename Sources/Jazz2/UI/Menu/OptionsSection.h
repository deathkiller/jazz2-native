#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class OptionsItemType {
		Gameplay,
		Graphics,
		Sounds,
		Controls
	};

	struct OptionsItem {
		OptionsItemType Type;
		StringView DisplayName;
	};

	class OptionsSection : public ScrollableMenuSection<OptionsItem>
	{
	public:
		OptionsSection();

		void OnDraw(Canvas* canvas) override;

	private:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}