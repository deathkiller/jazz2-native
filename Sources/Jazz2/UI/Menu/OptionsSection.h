#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class OptionsItemType {
		Gameplay,
		Graphics,
		Sounds,
		Controls,
		UserProfile
	};

	struct OptionsItem {
		OptionsItemType Type;
		StringView DisplayName;
	};
#endif

	/** @brief Options menu section */
	class OptionsSection : public ScrollableMenuSection<OptionsItem>
	{
	public:
		OptionsSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}