#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Options menu section

		Top-level options screen listing the settings categories (gameplay, graphics, sounds, controls, and user
		profile). Built declaratively on top of @ref WidgetSection.
	*/
	class OptionsSection : public WidgetSection
	{
	public:
		void OnShow(IMenuContainer* root) override;
		void OnLayoutChanged(IMenuContainer* root) override;

	private:
		// The rows, whose height follows the view size (see GetItemHeight())
		SmallVector<ListItem*, 5> _items;

		static float GetItemHeight(IMenuContainer* root);
	};
}
