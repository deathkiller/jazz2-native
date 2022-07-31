#include "Bomb.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../Explosion.h"

#include "../../../nCine/Base/Random.h"

namespace Jazz2::Actors::Environment
{
	Bomb::Bomb()
		:
		_timeLeft(Random().NextFloat(40.0f, 90.0f))
	{
	}

	void Bomb::Preload(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		switch (theme) {
			case 0: PreloadMetadataAsync("Object/Bomb"_s); break;
			case 1: PreloadMetadataAsync("Enemy/LizardFloat"_s); break;
			case 2: PreloadMetadataAsync("Enemy/LizardFloatXmas"_s); break;
		}
	}

	Task<bool> Bomb::OnActivatedAsync(const ActorActivationDetails& details)
	{
		uint8_t theme = details.Params[0];
		SetFacingLeft(details.Params[2] != 0);

		_health = INT32_MAX;
		_elasticity = 0.3f;

		switch (theme) {
			case 0: co_await RequestMetadataAsync("Object/Bomb"_s); break;
			case 1: co_await RequestMetadataAsync("Enemy/LizardFloat"_s); break;
			case 2: co_await RequestMetadataAsync("Enemy/LizardFloatXmas"_s); break;
		}

		SetAnimation("Bomb"_s);

		co_return true;
	}

	void Bomb::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_timeLeft > 0.0f) {
			_timeLeft -= timeMult;
		} else {
			DecreaseHealth(INT32_MAX);
		}
	}

	void Bomb::OnUpdateHitbox()
	{
		UpdateHitbox(6, 6);
	}

	bool Bomb::OnPerish(ActorBase* collider)
	{
		_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 40, [this](ActorBase* actor) {
			if (auto player = dynamic_cast<Player*>(actor)) {
				bool pushLeft = (_pos.X > player->GetPos().X);
				player->TakeDamage(1, pushLeft ? -8.0f : 8.0f);
			}
			return true;
		});

		// Explosion.Large is the same as Explosion.Bomb
		Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()), Explosion::Type::Large);

		_levelHandler->PlayCommonSfx("Bomb"_s, Vector3f(_pos.X, _pos.Y, 0.0f));

		return ActorBase::OnPerish(collider);
	}
}