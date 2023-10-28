#include "Checkpoint.h"
#include "../../ILevelHandler.h"
#include "../../Events/EventMap.h"
#include "../Player.h"

namespace Jazz2::Actors::Environment
{
	Checkpoint::Checkpoint()
		:
		_activated(false)
	{
	}

	void Checkpoint::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0:
			default:
				PreloadMetadataAsync("Object/Checkpoint"_s);
				break;
			case 1: // Xmas
				PreloadMetadataAsync("Object/CheckpointXmas"_s);
				break;
		}
	}

	Task<bool> Checkpoint::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::CanBeFrozen, false);

		_theme = details.Params[0];
		_activated = (details.Params[1] != 0);

		switch (_theme) {
			case 0:
			default:
				async_await RequestMetadataAsync("Object/Checkpoint"_s);
				break;
			case 1: // Xmas
				async_await RequestMetadataAsync("Object/CheckpointXmas"_s);
				break;
		}

		SetAnimation((AnimState)(_activated ? 1 : 0));

		if (GetState(ActorState::ApplyGravitation)) {
			OnUpdateHitbox();

			// Apply instant gravitation
			int i = 10;
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative)) {
				// Nothing to do...
			}
			while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative)) {
				// Nothing to do...
			}
		}

		async_return true;
	}

	void Checkpoint::OnUpdateHitbox()
	{
		UpdateHitbox(20, 20);
	}

	bool Checkpoint::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_activated) {
			return true;
		}

		if (auto player = dynamic_cast<Player*>(other.get())) {
			_activated = true;

			SetAnimation((AnimState)1);
			SetTransition(AnimState::TransitionActivate, false);

			PlaySfx("TransitionActivate"_s);

			// Deactivate event in map
			uint8_t playerParams[16] = { _theme, 1 };
			_levelHandler->EventMap()->StoreTileEvent(_originTile.X, _originTile.Y, EventType::Checkpoint, ActorState::None, playerParams);

			_levelHandler->SetCheckpoint(Vector2f(_pos.X, _pos.Y));
			return true;
		}

		return false;
	}
}