#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Gameplay enhancements menu section

		Lets the player toggle the individual "Reforged" gameplay enhancements, such as the Reforged gameplay, HUD,
		main menu, ledge climbing, and weapon wheel. Built declaratively on top of @ref WidgetSection, with an
		animated intro panel describing the section.
	*/
	class GameplayEnhancementsSection : public WidgetSection
	{
	public:
		~GameplayEnhancementsSection() override;

		Recti GetClipRectangle(const Recti& contentBounds) override;
		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	private:
		float _transition = 0.0f;
		bool _isDirty = false;
	};
}
