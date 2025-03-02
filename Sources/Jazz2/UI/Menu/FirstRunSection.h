#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class FirstRunItemType {
		LegacyPreset,
		ReforgedPreset
	};

	struct FirstRunItem {
		FirstRunItemType Type;
		String DisplayName;
		String Description;
	};
#endif

	/** @brief First run menu section */
	class FirstRunSection : public ScrollableMenuSection<FirstRunItem>
	{
	public:
		FirstRunSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnBackPressed() override;
		void OnSelectionChanged(ListViewItem& item) override;
		void OnExecuteSelected() override;

	private:
		bool _committed;
	};
}