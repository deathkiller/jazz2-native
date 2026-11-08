#include "GameModeObjectActor.h"

#if defined(WITH_MULTIPLAYER)

#include "../../Multiplayer/Teams.h"

#include <cmath>

namespace Jazz2::Actors::Multiplayer
{
	GameModeObjectActor::GameModeObjectActor()
		: _progress(0.0f), _phase(0.0f), _visual(Visual::ControlPoint), _team(0xFF), _isLoaded(false), _isDirty(true)
	{
	}

	void GameModeObjectActor::Preload(const ActorActivationDetails& details)
	{
		PreloadMetadataAsync("Object/GameModeObject"_s);
	}

	Task<bool> GameModeObjectActor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_visual = (Visual)details.Params[0];
		_team = details.Params[1];

		SetState(ActorState::ApplyGravitation | ActorState::CanBeFrozen, false);
		SetState(ActorState::ForceDisableCollisions, true);

		async_await RequestMetadataAsync("Object/GameModeObject"_s);

		_isLoaded = true;
		ApplyVisual();

		async_return true;
	}

	void GameModeObjectActor::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_isDirty && _isLoaded) {
			ApplyVisual();
		}

		if (_visual == Visual::ControlPoint) {
			// A capture in progress pulses, a neutral point stays translucent
			_phase += timeMult * 0.1f;
			float alpha = (_team == 0xFF ? 0.4f : 1.0f);
			if (_progress > 0.0f) {
				alpha = 0.5f + 0.5f * std::abs(std::sin(_phase * (1.0f + _progress * 3.0f)));
			}
			_renderer.SetAlphaF(alpha);
		} else if (_visual == Visual::Flag) {
			_renderer.SetAlphaF(_team == 0xFF ? 0.8f : 1.0f);
		}
	}

	void GameModeObjectActor::SetVisual(Visual visual, std::uint8_t team, float progress, bool facingLeft)
	{
		_progress = progress;
		if (IsFacingLeft() != facingLeft) {
			SetFacingLeft(facingLeft);
		}
		if (_visual != visual || _team != team) {
			_visual = visual;
			_team = team;
			_isDirty = true;
		}
	}

	void GameModeObjectActor::ApplyVisual()
	{
		_isDirty = false;

		// See Object/GameModeObject.res
		std::int32_t state;
		std::uint8_t team = (_team < Jazz2::Multiplayer::MaxTeamCount ? _team : 0xFF);
		switch (_visual) {
			default:
			case Visual::ControlPoint: state = (team == 0xFF ? 4 : team); break;
			case Visual::Jail: state = 8 + (team == 0xFF ? 0 : team); break;
			// Only blue and red flag sprites exist, other teams reuse them by parity
			case Visual::Flag: state = 16 + (team == 0xFF ? 0 : team % 2); break;
		}
		SetAnimation((AnimState)state);
	}
}

#endif
