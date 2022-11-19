#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class SimpleMessageSection : public MenuSection
	{
	public:
		enum class Message {
			Unknown,
			CannotLoadLevel
		};

		SimpleMessageSection(Message message);

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		Message _message;
	};
}