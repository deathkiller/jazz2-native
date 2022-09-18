#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class AboutSection : public MenuSection
	{
	public:
		AboutSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		char _additionalInfo[512];
	};
}