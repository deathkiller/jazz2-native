﻿#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class ControlsOptionsItemType {
		RemapControls,
		TouchControls,
		ToggleRunAction,
		GamepadButtonLabels,
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		EnableGamepadRumble,
#endif
#if defined(WITH_SDL)
		EnablePlayStationExtendedSupport,
#endif
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
		UseNativeBackButton,
#endif
		InputDiagnostics,
		ResetToDefault
	};

	struct ControlsOptionsItem {
		ControlsOptionsItemType Type;
		String DisplayName;
		bool HasBooleanValue;
		std::int32_t PlayerIndex;
	};
#endif

	class ControlsOptionsSection : public ScrollableMenuSection<ControlsOptionsItem>
	{
	public:
		ControlsOptionsSection();
		~ControlsOptionsSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;

	private:
		bool _isDirty;
	};
}