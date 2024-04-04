#pragma once

#include "ScrollableMenuSection.h"

namespace Jazz2::UI::Menu
{
	enum class ControlsOptionsItemType {
		RemapControls,
		TouchControls,
		ToggleRunAction,
		EnableAltGamepad,
#if defined(DEATH_TARGET_ANDROID)
		UseNativeBackButton,
#endif
		InputDiagnostics
	};

	struct ControlsOptionsItem {
		ControlsOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
	};

	class ControlsOptionsSection : public ScrollableMenuSection<ControlsOptionsItem>
	{
	public:
		ControlsOptionsSection();
		~ControlsOptionsSection();

		void OnDraw(Canvas* canvas) override;

	private:
		bool _isDirty;

		void OnHandleInput() override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
	};
}