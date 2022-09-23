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

		float _animation;
		bool _isDirty;
		SelectedZone _selectedZone;
		Vector2f _lastPos;
		int _lastPointerId;

		void DrawOutlinedSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size);
	};
}