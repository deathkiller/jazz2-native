#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../HUD.h"
#include "../../Multiplayer/MpLevelHandler.h"

namespace Jazz2::UI::Multiplayer
{
	/** @brief Player HUD for multiplayer */
	class MpHUD : public HUD
	{
	public:
		MpHUD(Jazz2::Multiplayer::MpLevelHandler* levelHandler);

	protected:
		void OnDrawScore(const Rectf& view, Actors::Player* player) override;

	private:
		Font* _mediumFont;
	};
}

#endif