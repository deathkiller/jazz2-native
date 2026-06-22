#include "GameModeFactory.h"

#if defined(WITH_MULTIPLAYER)

#include "CooperationMode.h"
#include "BattleMode.h"
#include "TeamBattleMode.h"
#include "RaceMode.h"
#include "TeamRaceMode.h"
#include "TreasureHuntMode.h"
#include "TeamTreasureHuntMode.h"
#include "CaptureTheFlagMode.h"

namespace Jazz2::Multiplayer
{
	std::unique_ptr<IGameMode> CreateGameMode(MpGameMode mode)
	{
		switch (mode) {
			case MpGameMode::Cooperation: return std::make_unique<CooperationMode>();
			case MpGameMode::Battle: return std::make_unique<BattleMode>();
			case MpGameMode::TeamBattle: return std::make_unique<TeamBattleMode>();
			case MpGameMode::Race: return std::make_unique<RaceMode>();
			case MpGameMode::TeamRace: return std::make_unique<TeamRaceMode>();
			case MpGameMode::TreasureHunt: return std::make_unique<TreasureHuntMode>();
			case MpGameMode::TeamTreasureHunt: return std::make_unique<TeamTreasureHuntMode>();
			case MpGameMode::CaptureTheFlag: return std::make_unique<CaptureTheFlagMode>();

			// MpGameMode::Unknown has no rules object
			default: return nullptr;
		}
	}
}

#endif
