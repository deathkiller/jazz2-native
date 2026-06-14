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

	/**
		@brief Options menu section
		
		Top-level options screen listing the settings categories (gameplay, graphics, sounds, controls, and user
		profile).
	*/
	class OptionsSection : public ScrollableMenuSection<OptionsItem>
	{
	public:
		/** @brief Creates a new instance */
		OptionsSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}