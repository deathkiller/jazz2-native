#pragma once

#include "MenuSection.h"

namespace Jazz2::UI::Menu
{
	class TouchControlsOptionsSection : public MenuSection
	{
	public:
		TouchControlsOptionsSection();
		~TouchControlsOptionsSection();

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class SelectedZone {
			None,
			Left,
			Right
		};

		SelectedZone _selectedZone;
		Vector2f _lastPos;
		std::int32_t _lastPointerId;
		bool _isDirty;

		void DrawOutlinedSolid(float x, float y, std::uint16_t z, Alignment align, const Vector2f& size);
	};
}