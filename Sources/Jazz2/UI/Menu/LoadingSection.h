#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class LoadingSection : public MenuSection
	{
	public:
		LoadingSection(StringView message);
		LoadingSection(String&& message);

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	private:
		String _message;
	};
}