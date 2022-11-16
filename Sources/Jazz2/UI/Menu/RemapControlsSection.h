#pragma once

#include "MenuSection.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Base/BitArray.h"

namespace Jazz2::UI::Menu
{
	class RemapControlsSection : public MenuSection
	{
	public:
		static constexpr int PossibleButtons = 3;

		RemapControlsSection();
		~RemapControlsSection() override;

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
		BitArray _prevKeyPressed;
		BitArray _prevJoyPressed;

		void Commit();
		void RefreshPreviousState();
		void RefreshCollisions();
		bool HasCollision(KeySym key);
		bool HasCollision(int gamepadIndex, ButtonName gamepadButton);
		static StringView KeyToName(KeySym key);
	};
}