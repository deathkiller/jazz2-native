#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Language selection menu section

		Lists the available interface languages found in the content and cache directories and lets the player choose
		one. Built declaratively on top of @ref WidgetSection; the list scrolls when there are more languages than fit.
	*/
	class LanguageSelectSection : public WidgetSection
	{
	public:
		void OnShow(IMenuContainer* root) override;
	};
}
