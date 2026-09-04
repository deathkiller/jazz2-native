#pragma once

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Sounds options menu section

		Lets the player adjust the audio settings, namely the master, sound effects, and music volumes. Built
		declaratively on top of @ref WidgetSection using spread-out @ref Slider rows.
	*/
	class SoundsOptionsSection : public WidgetSection
	{
	public:
		~SoundsOptionsSection() override;

		void OnShow(IMenuContainer* root) override;

	private:
		bool _isDirty = false;
#if defined(WITH_PSPAUDIO) || defined(WITH_AHIAUDIO)
		// Backing storage for the formatted sample rate, so the choice row can hand out a stable view
		String _sampleRateValue;
		// Whether the music has to be reopened at the new rate when the section is left
		bool _sampleRateChanged = false;
#endif
	};
}
