#include "SteamNote.h"
#include "../../ILevelHandler.h"

namespace Jazz2::Actors::Environment
{
	SteamNote::SteamNote()
		:
		_cooldown(0.0f)
	{
	}

	Task<bool> SteamNote::OnActivatedAsync(const ActorActivationDetails& details)
	{
		
		_pos.X -= 10.0f;

		SetState(ActorState::SkipPerPixelCollisions, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Object/SteamNote"_s);

		SetAnimation(AnimState::Default);

		PlaySfx("Appear"_s, 0.4f);

		// It's incorrectly positioned one tile up in "share2.j2l", so move it to correct position
		OnUpdateHitbox();
		bool moved = false;
		int i = 10;
		while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 4.0f), MoveType::Relative)) {
			moved = true;
		}
		while (i-- > 0 && MoveInstantly(Vector2f(0.0f, 1.0f), MoveType::Relative)) {
			moved = true;
		}
		if (moved) {
			MoveInstantly(Vector2f(0.0f, 18.0f), MoveType::Relative | MoveType::Force);
		}

		async_return true;
	}

	void SteamNote::OnUpdate(float timeMult)
	{
		if (_cooldown > 0.0f) {
			_cooldown -= timeMult;
			if (_cooldown <= 0.0f) {
				_renderer.AnimTime = 0.0f;
				_renderer.AnimPaused = false;
				_renderer.setDrawEnabled(true);

				PlaySfx("Appear"_s, 0.4f);
			}
		}
	}

	void SteamNote::OnUpdateHitbox()
	{
		UpdateHitbox(6, 6);
	}

	void SteamNote::OnAnimationFinished()
	{
		_renderer.AnimPaused = true;
		_renderer.setDrawEnabled(false);
		_cooldown = 80.0f;
	}
}