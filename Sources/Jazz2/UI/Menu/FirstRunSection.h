#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class FirstRunItemType {
		LegacyPreset,
		ReforgedPreset
	};

	struct FirstRunItem {
		FirstRunItemType Type;
		String DisplayName;
		String Description;
	};

	class FirstRunSection : public ScrollableMenuSection<FirstRunItem>
	{
	public:
		FirstRunSection();

		Recti GetClipRectangle(const Vector2i& viewSize) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	private:
		bool _committed;

		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected) override;
		void OnBackPressed() override;
		void OnSelectionChanged(ListViewItem& item) override;
		void OnExecuteSelected() override;
	};
}