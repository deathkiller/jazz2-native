#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief First run menu section

		Shown on the first launch to let the player choose between the Legacy and Reforged gameplay presets. Built
		declaratively on top of @ref WidgetSection, with the choices rendered through @ref CanvasWidget rows.
	*/
	class FirstRunSection : public WidgetSection
	{
	public:
		Recti GetClipRectangle(const Recti& contentBounds) override;
		void OnShow(IMenuContainer* root) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnBackPressed() override;
	};
}
