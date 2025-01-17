#pragma once

#include "ScrollableMenuSection.h"
#include "../ControlScheme.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Base/BitArray.h"

namespace Jazz2::UI::Menu
{
	struct RemapControlsItem {
		PlayerActions Type;
		String DisplayName;
	};

	class RemapControlsSection : public ScrollableMenuSection<RemapControlsItem>
	{
	public:
		static constexpr std::int32_t MaxTargetCount = 6;

		RemapControlsSection(std::int32_t playerIndex);
		~RemapControlsSection() override;

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		std::int32_t _selectedColumn;
		std::int32_t _playerIndex;
		float _timeout;
		float _hintAnimation;
		JoyMappedState _joyStatesLast[ControlScheme::MaxConnectedGamepads];
		bool _isDirty;
		bool _waitForInput;
		bool _waitForInputPrev;

		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnHandleInput() override;
		void OnExecuteSelected() override;
		void OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos) override;
		void OnBackPressed() override;

		void RefreshPreviousState();
		bool HasCollision(PlayerActions action, MappingTarget target, PlayerActions& collidingAction, std::int32_t& collidingAssignment);
	};
}