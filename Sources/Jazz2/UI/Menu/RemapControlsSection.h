#pragma once

#include "WidgetSection.h"
#include "../../Input/ControlScheme.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Base/BitArray.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Remap controls menu section

		Lists the player actions and lets the player rebind the individual control mappings for a given player by
		assigning new keyboard, gamepad, or touch inputs. Built on top of @ref WidgetSection with each action row drawn
		through a @ref CanvasWidget; the bespoke 2D (row/column) navigation and input capture are driven by the section.
	*/
	class RemapControlsSection : public WidgetSection
	{
	public:
		/** @brief Maximum number of mapping targets */
		static constexpr std::int32_t MaxTargetCount = 6;

		/** @brief Creates a new instance for the specified player */
		RemapControlsSection(std::int32_t playerIndex);
		~RemapControlsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		NavigationFlags GetNavigationFlags() const override;

	protected:
		void OnBackPressed() override;

	private:
		std::int32_t _selectedColumn;
		std::int32_t _playerIndex;
		float _timeout;
		float _hintAnimation;
		float _animation;
		JoyMappedState _joyStatesLast[ControlScheme::MaxConnectedGamepads];
		bool _isDirty;
		bool _waitForInput;
		bool _waitForInputPrev;
		ScrollView* _list;
		SmallVector<PlayerAction> _actions;

		/** @brief Returns the index of the currently selected action row */
		std::int32_t GetSelectedRow() const;
		/** @brief Begins waiting for an input to assign to the selected row/column */
		void StartCapture();
		/** @brief Clamps the selected column to the targets available for the given row */
		void ClampColumn(std::int32_t row);
		/** @brief Draws a single action row */
		void DrawRow(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, PlayerAction type, StringView name);
		/** @brief Handles a tap on the given row at the given pixel position */
		void HandleTap(std::int32_t row, Vector2i touchPos);
		/** @brief Refreshes the cached gamepad states */
		void RefreshPreviousState();
		/** @brief Returns `true` if the specified mapping target is already assigned to another action */
		bool HasCollision(PlayerAction action, MappingTarget target, PlayerAction& collidingAction, std::int32_t& collidingAssignment);
	};
}
