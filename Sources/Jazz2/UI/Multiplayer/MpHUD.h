#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../HUD.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/** @brief Player MpHUD */
	class MpHUD : public HUD
	{
#if defined(WITH_ANGELSCRIPT)
		friend struct Scripting::Legacy::jjCANVAS;
#endif

	public:
		MpHUD(Jazz2::Multiplayer::MpLevelHandler* levelHandler);

	private:
		void DrawScore(const Rectf& view, Actors::Player* player) override;
	};
}

#endif