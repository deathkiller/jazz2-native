#include "DominationMode.h"

#if defined(WITH_MULTIPLAYER)

#include "../ServerInitialization.h"
#include "../../Actors/Player.h"
#include "../../UI/Font.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/FrameTimer.h"

#include <float.h>
#include <cstring>

namespace Jazz2::Multiplayer::GameModes
{
	DominationMode::DominationMode()
		: _teamPoints{}, _scoreInterval(0.0f)
	{
	}

	void DominationMode::OnRoundStarted(IGameModeContext& ctx)
	{
		std::memset(_teamPoints, 0, sizeof(_teamPoints));
		_points.clear();

		// Easy-only and hard-only events, and CTF bases with their own direction and the default names
		SmallVector<CapturePointInfo, 0> found;
		ctx.FindCapturePoints(found);
		SmallVector<Pair<Vector2f, std::uint16_t>, 0> bases;
		ctx.FindEventPositions(EventType::CtfBase, bases);
		for (const auto& base : bases) {
			auto& point = found.emplace_back();
			point.Pos = base.first();
			point.Direction = ((base.second() >> 8) != 0 ? -1 : 1);
		}
		nCine::sort(found.begin(), found.end(), [](const CapturePointInfo& a, const CapturePointInfo& b) {
			return (a.Pos.Y != b.Pos.Y ? a.Pos.Y < b.Pos.Y : a.Pos.X < b.Pos.X);
		});

		if (found.empty()) {
			// A level not made for Domination, so the points are spread over the spawn points
			SmallVector<Vector2f, 0> spawnPoints;
			ctx.FindSpawnPoints(spawnPoints);
			SmallVector<Vector2f, 0> picked;
			if (!spawnPoints.empty()) {
				picked.push_back(spawnPoints[0]);
				while (picked.size() < 3 && picked.size() < spawnPoints.size()) {
					// The spawn point farthest from all points taken so far
					Vector2f best = spawnPoints[0];
					float bestDistance = -1.0f;
					for (auto candidate : spawnPoints) {
						float nearest = FLT_MAX;
						for (auto taken : picked) {
							nearest = std::min(nearest, (candidate - taken).Length());
						}
						if (nearest > bestDistance) {
							bestDistance = nearest;
							best = candidate;
						}
					}
					if (bestDistance <= 0.0f) {
						break;
					}
					picked.push_back(best);
				}
			}
			for (auto pos : picked) {
				auto& point = found.emplace_back();
				point.Pos = pos;
				point.Direction = 1;
			}
		}

		auto& objects = ctx.GetGameModeObjects();
		objects.clear();
		for (const auto& info : found) {
			if (_points.size() >= MaxControlPoints) {
				break;
			}
			ControlPoint& point = _points.emplace_back();
			point.Pos = info.Pos;
			point.Team = GameModeObject::NeutralTeam;
			point.CapturingTeam = GameModeObject::NeutralTeam;
			point.CaptureFrames = 0.0f;
			point.ScoreFrames = 0.0f;
			// Empty unless a Text event names it, clients then show "Control Point N" in their own language
			point.Name = info.Name;

			GameModeObject& obj = AddObject(ctx, GameModeObjectType::ControlPoint, GameModeObject::NeutralTeam, info.Pos);
			obj.Direction = (std::int8_t)info.Direction;
			// Scripts compare the names, so it must not depend on the language of the server
			obj.Name = (!point.Name.empty() ? point.Name : Death::format("Control Point {}", (std::int32_t)_points.size()));
			point.ObjectId = obj.Id;
		}

		// 5 seconds for each started group of 4 points
		_scoreInterval = (float)(((_points.size() + 3) / 4) * 5) * FrameTimer::FramesPerSecond;

		ctx.SyncGameModeObjects();
		ctx.SyncTeamScores();
	}
	void DominationMode::OnUpdate(IGameModeContext& ctx, float timeMult)
	{
		std::uint8_t teamCount = ctx.GetTeamCount();
		bool objectsChanged = false, scoreChanged = false;
		auto& objects = ctx.GetGameModeObjects();

		for (std::size_t i = 0; i < _points.size(); i++) {
			auto& point = _points[i];

			// Who is touching the point right now
			bool teamTouching[MaxTeamCount] = {};
			for (auto* player : ctx.GetPlayers()) {
				std::uint8_t team = ctx.GetPlayerState(player).Team;
				if (team < teamCount && !ctx.IsSpectating(player) && player->GetHealth() > 0 &&
					(player->GetPos() - point.Pos).Length() <= TouchRadius) {
					teamTouching[team] = true;
				}
			}

			std::uint8_t prevCapturingTeam = point.CapturingTeam;
			if (point.CapturingTeam != GameModeObject::NeutralTeam) {
				// A defender touching the point blocks the capture
				if (point.Team < teamCount && teamTouching[point.Team]) {
					point.CapturingTeam = GameModeObject::NeutralTeam;
					point.CaptureFrames = 0.0f;
				} else {
					point.CaptureFrames += timeMult;
					if (point.CaptureFrames >= CaptureTimeSecs * FrameTimer::FramesPerSecond) {
						point.Team = point.CapturingTeam;
						point.CapturingTeam = GameModeObject::NeutralTeam;
						point.CaptureFrames = 0.0f;
						point.ScoreFrames = 0.0f;
						if (!point.Name.empty()) {
							ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::ControlPointCaptured, ServerMessageTeam{point.Team}, point.Name));
						} else {
							ctx.ShowAlertToAllPlayers(ServerMessage(ServerMessageType::ControlPointCapturedUnnamed, ServerMessageTeam{point.Team}, (std::int32_t)(i + 1)));
						}
						objectsChanged = true;
					}
				}
			} else {
				for (std::uint8_t team = 0; team < teamCount; team++) {
					if (team != point.Team && teamTouching[team]) {
						point.CapturingTeam = team;
						point.CaptureFrames = 0.0f;
						break;
					}
				}
			}
			if (prevCapturingTeam != point.CapturingTeam) {
				objectsChanged = true;
			}

			// Every point keeps its own scoring timer from when it was captured
			if (point.Team < teamCount) {
				point.ScoreFrames += timeMult;
				if (point.ScoreFrames >= _scoreInterval) {
					point.ScoreFrames -= _scoreInterval;
					_teamPoints[point.Team]++;
					scoreChanged = true;
				}
			}

			for (auto& obj : objects) {
				if (obj.Id == point.ObjectId) {
					obj.Team = point.Team;
					obj.CapturingTeam = point.CapturingTeam;
					std::uint8_t progress = (std::uint8_t)std::min(point.CaptureFrames * 255.0f / (CaptureTimeSecs * FrameTimer::FramesPerSecond), 255.0f);
					// Clients get the capture progress in coarse steps
					if ((progress >> 5) != (obj.Progress >> 5)) {
						objectsChanged = true;
					}
					obj.Progress = progress;
					break;
				}
			}
		}

		if (objectsChanged) {
			ctx.SyncGameModeObjects();
		}
		if (scoreChanged) {
			ctx.SyncTeamScores();
			ctx.CheckGameEnds();
		}
	}

	bool DominationMode::TryGetTeamScore(IGameModeContext& ctx, std::uint8_t team, std::uint32_t& score)
	{
		score = (team < MaxTeamCount ? _teamPoints[team] : 0);
		return true;
	}

	std::uint32_t DominationMode::GetStateScore(const MpPlayerState& state) const
	{
		return state.Kills;
	}

	void DominationMode::OnDrawHUD(IGameModeContext& ctx, IGameModeHUD& hud, Actors::Player* player, const Rectf& view)
	{
		// The control points in their order under the team scores, "o" in the color of the team that holds it, "x" while neutral
		std::int32_t pointCount = 0;
		for (const auto& obj : ctx.GetGameModeObjects()) {
			if (obj.Type == GameModeObjectType::ControlPoint) {
				pointCount++;
			}
		}
		if (pointCount > 0) {
			constexpr float Spacing = 10.0f;
			float x = view.X + view.W * 0.5f - Spacing * (pointCount - 1) * 0.5f;
			float y = view.Y + 26.0f;
			// TRANSLATORS: Label of the control points in the HUD in Domination, short for "control points"
			hud.DrawHudText(GameModeFontType::Small, _("CPs:"), x - 8.0f, y, 1.0f, UI::Alignment::TopRight, UI::Font::DefaultColor, 0.7f, 0.88f);
			for (const auto& obj : ctx.GetGameModeObjects()) {
				if (obj.Type != GameModeObjectType::ControlPoint) {
					continue;
				}
				bool held = (obj.Team != GameModeObject::NeutralTeam);
				hud.DrawHudText(GameModeFontType::Small, held ? "o"_s : "x"_s, x, y, 1.0f, UI::Alignment::Top,
					held ? GetTeamColor(obj.Team) : Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.7f, 0.88f);
				x += Spacing;
			}
		}

		// The arrow points to the nearest point the team doesn't hold, or to the nearest one if it holds all,
		// transparent white for a neutral one (the objects are synchronized, so this works on clients too)
		std::uint8_t team = ctx.GetPlayerState(player).Team;
		Vector2f playerPos = player->GetPos();
		const GameModeObject* nearest = nullptr;
		const GameModeObject* nearestAny = nullptr;
		float nearestDistance = FLT_MAX, nearestAnyDistance = FLT_MAX;
		for (const auto& obj : ctx.GetGameModeObjects()) {
			if (obj.Type != GameModeObjectType::ControlPoint) {
				continue;
			}
			float distance = (obj.Pos - playerPos).Length();
			if (distance < nearestAnyDistance) {
				nearestAnyDistance = distance;
				nearestAny = &obj;
			}
			if (obj.Team != team && distance < nearestDistance) {
				nearestDistance = distance;
				nearest = &obj;
			}
		}
		if (nearest == nullptr) {
			nearest = nearestAny;
		}
		if (nearest != nullptr) {
			Colorf color = (nearest->Team == GameModeObject::NeutralTeam ? Colorf(1.0f, 1.0f, 1.0f, 0.5f) : GetTeamColor(nearest->Team));
			hud.DrawTargetArrow(view, player, nearest->Pos, color);
		}
	}
}

#endif
