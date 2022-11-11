#include "TNT.h"
#include "../../ILevelHandler.h"
#include "../../LevelInitialization.h"
#include "../../Tiles/TileMap.h"
#include "../Player.h"
#include "../Explosion.h"
#include "../Enemies/EnemyBase.h"
#include "../Solid/GenericContainer.h"
#include "../Solid/PowerUpMorphMonitor.h"
#include "../Solid/PowerUpShieldMonitor.h"
#include "../Solid/PowerUpWeaponMonitor.h"
#include "../Solid/TriggerCrate.h"
#include "../Environment/BirdCage.h"
#include "../Solid/Pole.h"

namespace Jazz2::Actors::Weapons
{
	TNT::TNT()
		:
		_timeLeft(0.0f),
		_lightIntensity(0.0f),
		_isExploded(false)
	{
	}

	Task<bool> TNT::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_timeLeft = 200.0f;

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation, false);

		async_await RequestMetadataAsync("Weapon/TNT"_s);

		AnimState state = AnimState::Idle;
		SetAnimation(AnimState::Idle);

		async_return true;
	}

	Player* TNT::GetOwner()
	{
		return dynamic_cast<Player*>(_owner.get());
	}

	void TNT::OnFire(const std::shared_ptr<ActorBase>& owner)
	{
		_owner = owner;
	}

	void TNT::OnUpdate(float timeMult)
	{
		ActorBase::OnUpdate(timeMult);

		if (_timeLeft > 0.0f) {
			_timeLeft -= timeMult;

			if (_timeLeft > 40.0f) {
				_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 50.0f, [this](ActorBase* actor) {
					if (!actor->IsInvulnerable()) {
						if (dynamic_cast<Enemies::EnemyBase*>(actor) != nullptr ||
							dynamic_cast<Solid::GenericContainer*>(actor) != nullptr ||
							dynamic_cast<Solid::PowerUpMorphMonitor*>(actor) != nullptr ||
							dynamic_cast<Solid::PowerUpShieldMonitor*>(actor) != nullptr ||
							dynamic_cast<Solid::PowerUpWeaponMonitor*>(actor) != nullptr ||
							dynamic_cast<Solid::TriggerCrate*>(actor) != nullptr ||
							dynamic_cast<Solid::Pole*>(actor) != nullptr ||
							dynamic_cast<Environment::BirdCage*>(actor) != nullptr) {
							_timeLeft = 40.0f;
							return false;
						}
					}
					return true;
				});
			}
		} else if (!_isExploded) {
			_isExploded = true;
			_lightIntensity = 0.8f;

			// TODO: Sound + Animation
			SetTransition(AnimState::TransitionActivate, false, [this]() {
				DecreaseHealth(INT32_MAX);
			});

			PlaySfx("Explosion"_s);

			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 50.0f, [this](ActorBase* actor) {
				actor->OnHandleCollision(shared_from_this());
				return true;
			});

			auto tiles = _levelHandler->TileMap();
			if (tiles != nullptr) {
				AABBf aabb = AABBf(_pos.X - 34, _pos.Y - 34, _pos.X + 34, _pos.Y + 34);
				TileCollisionParams params = { TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::TNT, 8 };
				tiles->IsTileEmpty(aabb, params);
				if (params.TilesDestroyed > 0) {
					if (auto player = dynamic_cast<Player*>(_owner.get())) {
						player->AddScore(params.TilesDestroyed * 50);
					}
				}
			}
		} else {
			_lightIntensity -= timeMult * 0.02f;
			_renderer.setScale(_renderer.scale() + timeMult * 0.02f);
		}
	}

	void TNT::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		if (_lightIntensity > 0.0f) {
			auto& light = lights.emplace_back();
			light.Pos = _pos;
			light.Intensity = _lightIntensity;
			light.Brightness = _lightIntensity * 0.6f;
			light.RadiusNear = 5.0f;
			light.RadiusFar = 120.0f;
		}
	}

	bool TNT::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto tnt = dynamic_cast<TNT*>(other.get())) {
			if (_timeLeft > 40.0f) {
				_timeLeft = 40.0f;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}
}