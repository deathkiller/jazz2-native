#pragma once

#include "ScrollableMenuSection.h"
#include "../../Input/ControlScheme.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Base/BitArray.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	struct RemapControlsItem {
		PlayerAction Type;
		String DisplayName;
	};
#endif

	class RemapControlsSection : public ScrollableMenuSection<RemapControlsItem>
	{
	public:
		/** @{ @name Constants */

		/** @brief Maximum number of mapping targets */
		static constexpr std::int32_t MaxTargetCount = 6;

		/** @} */

		/** @brief Creates a new instance for the specified player */
		RemapControlsSection(std::int32_t playerIndex);
		~RemapControlsSection() override;

		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;

	protected:
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnHandleInput() override;
		void OnExecuteSelected() override;
		void OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos) override;
		void OnBackPressed() override;

	protected:
		/** @brief Index of the currently selected mapping target column */
		std::int32_t _selectedColumn;
		/** @brief Index of the player whose controls are being remapped */
		std::int32_t _playerIndex;
		/** @brief Remaining time to wait for input before the assignment is cancelled */
		float _timeout;
		/** @brief Animation progress of the input hint */
		float _hintAnimation;
		/** @brief Last sampled gamepad states, used to detect newly pressed buttons */
		JoyMappedState _joyStatesLast[ControlScheme::MaxConnectedGamepads];
		/** @brief Whether the mapping has unsaved changes */
		bool _isDirty;
		/** @brief Whether the section is currently waiting for input to assign */
		bool _waitForInput;
		/** @brief Value of @ref _waitForInput from the previous frame */
		bool _waitForInputPrev;

		/** @brief Refreshes the cached gamepad states */
		void RefreshPreviousState();
		/** @brief Returns `true` if the specified mapping target is already assigned to another action */
		bool HasCollision(PlayerAction action, MappingTarget target, PlayerAction& collidingAction, std::int32_t& collidingAssignment);
	};
}