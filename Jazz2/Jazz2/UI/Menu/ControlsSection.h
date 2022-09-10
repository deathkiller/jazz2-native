#pragma once

#include "MenuSection.h"

#include "../../../nCine/Input/InputEvents.h"

namespace Jazz2::UI::Menu
{
	class ControlsSection : public MenuSection
	{
	public:
		static constexpr int PossibleButtons = 3;

		ControlsSection();
		~ControlsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		int _selectedIndex;
		int _selectedColumn;
		int _currentPlayerIndex;
		float _animation;
		bool _isDirty;
		bool _waitForInput;
		float _delay;

		void Commit();
		void RefreshCollisions();
		bool HasCollision(KeySym key);
		bool HasCollision(int gamepadIndex, ButtonName gamepadButton);
		static StringView KeyToName(KeySym key);
	};
}