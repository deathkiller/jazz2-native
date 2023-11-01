#pragma once

#include "MenuSection.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::UI::Menu
{
	class SimpleMessageSection : public MenuSection
	{
	public:
		SimpleMessageSection(const StringView& message);
		SimpleMessageSection(String&& message);

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		String _message;
	};
}