#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Controls options menu section

		Lists the input and control-related settings, such as control remapping, touch controls, and gamepad
		options, with entries that lead into the more specific control screens. Built declaratively on top of
		@ref WidgetSection.
	*/
	class ControlsOptionsSection : public WidgetSection
	{
	public:
		~ControlsOptionsSection() override;

		void OnShow(IMenuContainer* root) override;

	private:
		bool _isDirty = false;
	};
}
