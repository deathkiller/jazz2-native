#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Graphics options menu section

		Lists the graphics and video settings, such as rescale mode, resolution, antialiasing, and lighting quality.
		Built declaratively on top of @ref WidgetSection.
	*/
	class GraphicsOptionsSection : public WidgetSection
	{
	public:
		~GraphicsOptionsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnDraw(Canvas* canvas) override;

	private:
		bool _isDirty = false;
		// Backing storage for the formatted (non-catalog) values, so those choice rows can hand out a stable view
		String _resolutionValue;
		String _lightingResolutionValue;
	};
}
