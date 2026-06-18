#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Rescale mode selection menu section

		Lists the available screen rescaling and upscaling filter modes (such as pixel-perfect, the upscaling filters,
		and the CRT effects) and lets the player choose one. Built declaratively on top of @ref WidgetSection.
	*/
	class RescaleModeSection : public WidgetSection
	{
	public:
		void OnShow(IMenuContainer* root) override;
	};
}
