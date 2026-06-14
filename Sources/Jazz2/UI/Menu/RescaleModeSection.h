#pragma once

#include "ScrollableMenuSection.h"
#include "../../PreferencesCache.h"

namespace Jazz2::UI::Menu
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	struct RescaleModeItem {
		RescaleMode Mode;
		StringView DisplayName;
	};
#endif

	/**
		@brief Rescale mode selection menu section
		
		Lists the available screen rescaling and upscaling filter modes (such as pixel-perfect, the upscaling filters,
		and the CRT effects) and lets the player choose one.
	*/
	class RescaleModeSection : public ScrollableMenuSection<RescaleModeItem>
	{
	public:
		/** @brief Creates a new instance */
		RescaleModeSection();

		void OnDraw(Canvas* canvas) override;

	protected:
		void OnExecuteSelected() override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
	};
}