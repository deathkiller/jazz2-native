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
#include "DeathCaptureTheFlagMode.h"
#include "DominationMode.h"
#include "FlagRunMode.h"
#include "HeadHuntersMode.h"
#include "JailbreakMode.h"
#include "LastRabbitStandingMode.h"
#include "PestilenceMode.h"
#include "RoastTagMode.h"
#include "TeamLastRabbitStandingMode.h"

namespace Jazz2::Multiplayer::GameModes
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
			case MpGameMode::RoastTag: return std::make_unique<RoastTagMode>();
			case MpGameMode::LastRabbitStanding: return std::make_unique<LastRabbitStandingMode>();
			case MpGameMode::ExtendedLastRabbitStanding: return std::make_unique<ExtendedLastRabbitStandingMode>();
			case MpGameMode::Pestilence: return std::make_unique<PestilenceMode>();
			case MpGameMode::TeamLastRabbitStanding: return std::make_unique<TeamLastRabbitStandingMode>();
			case MpGameMode::Jailbreak: return std::make_unique<JailbreakMode>();
			case MpGameMode::DeathCaptureTheFlag: return std::make_unique<DeathCaptureTheFlagMode>();
			case MpGameMode::FlagRun: return std::make_unique<FlagRunMode>();
			case MpGameMode::Domination: return std::make_unique<DominationMode>();
			case MpGameMode::HeadHunters: return std::make_unique<HeadHuntersMode>();

			// MpGameMode::Unknown has no rules object
			default: return nullptr;
		}
	}
}

#endif
