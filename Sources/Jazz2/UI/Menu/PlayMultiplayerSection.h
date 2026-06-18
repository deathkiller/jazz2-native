#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "WidgetSection.h"

namespace Jazz2::UI::Menu
{
	/**
		@brief Play multiplayer menu section

		Multiplayer entry screen listing the options to connect to a server or to create a public or private server.
		Built declaratively on top of @ref WidgetSection.
	*/
	class PlayMultiplayerSection : public WidgetSection
	{
	public:
		void OnShow(IMenuContainer* root) override;
	};
}

#endif
