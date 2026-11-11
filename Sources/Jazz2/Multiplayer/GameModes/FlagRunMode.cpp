#include "FlagRunMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/FrameTimer.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace Jazz2::Multiplayer::GameModes
{
	FlagRunMode::FlagRunMode()
		: _carrierIndex(UINT32_MAX), _timeLeft(0.0f), _extraPoints(0), _teamPoints{}
	{
	}

	void FlagRunMode::OnRoundStarted(IGameModeContext& ctx)
	{
		std::memset(_teamPoints, 0, sizeof(_teamPoints));
		_carrierIndex = UINT32_MAX;
		_timeLeft = 0.0f;
		_extraPoints = 0;

		_capturePoints.clear();
		SmallVector<CapturePointInfo, 0> points;
		ctx.FindCapturePoints(points);
		for (const auto& point : points) {
			_capturePoints.push_back(point.Pos);
		}
		_captures.clear();
		if (_capturePoints.empty()) {
			// A level not made for Flag Run, the flag waits at a spawn point
			ctx.FindSpawnPoints(_capturePoints);
		}

		auto& objects = ctx.GetGameModeObjects();
		objects.clear();
		AddTeamBases(ctx, GameModeObjectType::Base, _basePos);
		RebuildFlags(ctx, Vector2f::Zero, GameModeObject::NeutralTeam);
		ctx.SyncTeamScores();
	}

	void FlagRunMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		if (_capturePoints.empty()) {
			return;
		}

		const auto& config = ctx.GetServerConfiguration();
		if (_carrierIndex == UINT32_MAX) {
			// The flag can be captured at any capture point, of the players touching one at once, the one with fewer captures gets it
			Actors::Player* taker = nullptr;
			Vector2f takerPoint;
			for (auto* player : ctx.GetPlayers()) {
				if (ctx.IsSpectating(player) || player->GetHealth() <= 0) {
					continue;
				}
				const Vector2f* touched = nullptr;
				for (const auto& point : _capturePoints) {
					if ((player->GetPos() - point).Length() <= TouchRadius) {
						touched = &point;
						break;
					}
				}
				if (touched == nullptr) {
					continue;
				}
				// No flag for a while after (re)spawning
				if (ctx.GetElapsedFrames() - ctx.GetPlayerState(player).SpawnedFrames < config.FlagPickupCooldownSecs * FrameTimer::FramesPerSecond) {
					continue;
				}
				if (taker == nullptr || GetCaptures(player) < GetCaptures(taker)) {
					taker = player;
					takerPoint = *touched;
				}
			}
			if (taker != nullptr) {
				PickUp(ctx, taker, CaptureTimeSecs, 0, takerPoint);
				ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::FlagRunTookFlag, ctx.GetPlayerName(taker)));
			}
			return;
		}
		auto* carrier = ctx.FindPlayerByIndex(_carrierIndex);
		if (carrier == nullptr || ctx.IsSpectating(carrier) || carrier->GetHealth() <= 0) {
			ReturnFlag(ctx);
			return;
		}

		auto& state = ctx.GetPlayerState(carrier);
		float prevSecs = std::ceil(_timeLeft / FrameTimer::FramesPerSecond);
		_timeLeft -= timeMult;
		if (_timeLeft <= 0.0f) {
			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::FlagRunOutOfTime, ctx.GetPlayerName(carrier)));
			ReturnFlag(ctx);
			ctx.KillPlayer(carrier);
			return;
		}

		float secsLeft = std::ceil(_timeLeft / FrameTimer::FramesPerSecond);
		if (secsLeft != prevSecs) {
			// The carrier sees the countdown in the HUD
			state.ModeCarried = (std::uint32_t)secsLeft;
			ctx.SyncPlayerModeState(carrier);
		}

		// Passing, an injured carrier gives the flag to a nearby teammate with full health
		if (config.AllowPassing && carrier->GetHealth() < carrier->GetMaxHealth()) {
			for (auto* player : ctx.GetPlayers()) {
				if (player != carrier && !ctx.IsSpectating(player) && ctx.GetPlayerState(player).Team == state.Team &&
					player->GetHealth() > 0 && player->GetHealth() >= player->GetMaxHealth() && (player->GetPos() - carrier->GetPos()).Length() <= TouchRadius) {
					float timeLeftSecs = _timeLeft / FrameTimer::FramesPerSecond;
					PickUp(ctx, player, timeLeftSecs, 1, player->GetPos());
					ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::FlagRunPassedFlag, ctx.GetPlayerName(carrier), ctx.GetPlayerName(player)));
					return;
				}
			}
		}
		std::uint8_t teamCount = ctx.GetTeamCount();
		for (std::uint8_t team = 0; team < teamCount; team++) {
			if (team == state.Team || (carrier->GetPos() - _basePos[team]).Length() > TouchRadius) {
				continue;
			}

			std::uint32_t points = (secsLeft > 30.0f ? 3 : (secsLeft > 15.0f ? 2 : 1)) + _extraPoints;
			if (state.Team < MaxTeamCount) {
				_teamPoints[state.Team] += points;
			}
			AddPoints(ctx, carrier, (std::int32_t)points);
			AddCapture(carrier);

			ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::FlagRunScored, ctx.GetPlayerName(carrier), points));
			ReturnFlag(ctx);
			ctx.SyncTeamScores();
			ctx.CheckGameEnds();
			break;
		}
	}

	void FlagRunMode::OnPlayerKilled(IGameModeContext& ctx, Actors::Player* victim, Actors::Player* killer)
	{
		std::uint32_t victimIndex = (std::uint32_t)victim->GetPlayerIndex();
		bool killerIsEnemy = (killer != nullptr && killer != victim && ctx.GetPlayerState(killer).Team != ctx.GetPlayerState(victim).Team);

		if (victimIndex == _carrierIndex) {
			// With less than 16 seconds left
			if (killerIsEnemy && _timeLeft < StealWindowSecs * FrameTimer::FramesPerSecond) {
				PickUp(ctx, killer, StealTimeSecs, 1, killer->GetPos());
				ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::FlagRunStoleFlag, ctx.GetPlayerName(killer)));
			} else {
				ReturnFlag(ctx);
			}
		} else if (killerIsEnemy && (std::uint32_t)killer->GetPlayerIndex() == _carrierIndex) {
			_extraPoints = std::min(_extraPoints + 1, MaxExtraPoints);
		}
	}

	bool FlagRunMode::TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score)
	{
		score = (team < MaxTeamCount ? _teamPoints[team] : 0);
		return true;
	}

	void FlagRunMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// The team scores are in the header like in Capture The Flag, the points of the player in the scoreboard
		auto& state = ctx.GetPlayerState(player);
		// A transparent white arrow points to the nearest capture point while nobody carries the flag
		const GameModeObject* nearest = nullptr;
		float nearestDistance = FLT_MAX;
		for (const auto& obj : ctx.GetGameModeObjects()) {
			if (obj.Type != GameModeObjectType::Flag) {
				continue;
			}
			if (obj.CarrierPlayerIndex != UINT32_MAX) {
				nearest = nullptr;
				break;
			}
			float distance = (obj.Pos - player->GetPos()).SqrLength();
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearest = &obj;
			}
		}
		if (nearest != nullptr) {
			hud.DrawTargetArrow(view, player, nearest->Pos, Colorf(1.0f, 1.0f, 1.0f, 0.5f));
		}

		if (HasFlag(ctx, player, PlayerModeFlags::CarriesFlag)) {
			// TRANSLATORS: Status shown in the HUD in Flag Run to the flag carrier, {} is number of seconds left
			DrawStatusLine(hud, view, _f("Take the flag to an enemy base! {}", state.ModeCarried),
				state.ModeCarried <= 15 ? Colorf(0.68f, 0.36f, 0.36f) : UI::Font::DefaultColor);
		}
	}

	StringView FlagRunMode::GetScoreboardLabel() const
	{
		return _("Points");
	}

	StringView FlagRunMode::GetRoleLabel(std::uint8_t modeFlags) const
	{
		if (modeFlags & (std::uint8_t)PlayerModeFlags::CarriesFlag) {
			// TRANSLATORS: Label shown above the flag carrier in Flag Run, keep it short
			return _("Flag");
		}
		return {};
	}

	void FlagRunMode::PickUp(IGameModeContext& ctx, Actors::Player* player, float timeSecs, std::uint32_t extraPoints, Vector2f pos)
	{
		ClearFlag(ctx, _carrierIndex, PlayerModeFlags::CarriesFlag);

		_carrierIndex = (std::uint32_t)player->GetPlayerIndex();
		_timeLeft = timeSecs * FrameTimer::FramesPerSecond;
		_extraPoints = extraPoints;

		auto& state = ctx.GetPlayerState(player);
		state.ModeCarried = (std::uint32_t)timeSecs;
		SetFlag(ctx, player, PlayerModeFlags::CarriesFlag, true);
		ctx.SyncPlayerModeState(player);
		RebuildFlags(ctx, pos, state.Team);
	}

	void FlagRunMode::ReturnFlag(IGameModeContext& ctx)
	{
		ClearFlag(ctx, _carrierIndex, PlayerModeFlags::CarriesFlag);
		_carrierIndex = UINT32_MAX;
		_timeLeft = 0.0f;
		_extraPoints = 0;
		RebuildFlags(ctx, Vector2f::Zero, GameModeObject::NeutralTeam);
	}

	void FlagRunMode::RebuildFlags(IGameModeContext& ctx, Vector2f carrierPos, std::uint8_t carrierTeam)
	{
		// One flag at every capture point while nobody carries it, otherwise just the carried one
		auto& objects = ctx.GetGameModeObjects();
		objects.erase(std::remove_if(objects.begin(), objects.end(), [](const GameModeObject& obj) {
			return (obj.Type == GameModeObjectType::Flag);
		}), objects.end());

		if (_carrierIndex != UINT32_MAX) {
			AddObject(ctx, GameModeObjectType::Flag, carrierTeam, carrierPos).CarrierPlayerIndex = _carrierIndex;
		} else {
			for (const auto& point : _capturePoints) {
				AddObject(ctx, GameModeObjectType::Flag, GameModeObject::NeutralTeam, point);
			}
		}
		ctx.SyncGameModeObjects();
	}

	std::uint32_t FlagRunMode::GetCaptures(Actors::Player* player) const
	{
		std::uint32_t index = (std::uint32_t)player->GetPlayerIndex();
		for (const auto& entry : _captures) {
			if (entry.first() == index) {
				return entry.second();
			}
		}
		return 0;
	}

	void FlagRunMode::AddCapture(Actors::Player* player)
	{
		std::uint32_t index = (std::uint32_t)player->GetPlayerIndex();
		for (auto& entry : _captures) {
			if (entry.first() == index) {
				entry.second()++;
				return;
			}
		}
		_captures.push_back(pair(index, (std::uint32_t)1));
	}
}

#endif
