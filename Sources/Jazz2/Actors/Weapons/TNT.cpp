#include "TNT.h"
#include "../../ILevelHandler.h"
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

#include "../../../nCine/Base/Random.h"

using namespace Jazz2::Tiles;

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
		_preexplosionTime = (int)_timeLeft / 16;

		SetState(ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);


		async_await RequestMetadataAsync("Weapon/TNT"_s);

		SetAnimation(AnimState::Idle);

		auto tiles = _levelHandler->TileMap();
		if (tiles != nullptr) {
			AABBf aabb = AABBf(_pos.X - 34.0f, _pos.Y - 34.0f, _pos.X + 34.0f, _pos.Y + 34.0f);
			TileCollisionParams params = { TileDestructType::Special | TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::TNT, 8 };
			if (tiles->CanBeDestroyed(aabb, params)) {
				_timeLeft = 40.0f;
			}
		}

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

			if (_timeLeft > 35.0f) {
				_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 64.0f, [this](ActorBase* actor) {
					if (actor->GetState(ActorState::TriggersTNT)) {
						_timeLeft = 35.0f;
						return false;
					}
					return true;
				});
			} else if(_timeLeft < 30.0f) {
				int fraction = (int)_timeLeft / 16;
				if (_preexplosionTime != fraction) {
					_preexplosionTime = fraction;

					_renderer.setScale(5.0f);
					if (_noise == nullptr) {
						_noise = PlaySfx(Random().NextBool() ? "Bell1"_s : "Bell2"_s);
					} else if (!_noise->isPlaying()) {
						_noise->play();
					}
				}
				_renderer.setScale(_renderer.scale() - timeMult * 0.36f);
			}
		} else if (!_isExploded) {
			_isExploded = true;
			_lightIntensity = 0.8f;

			SetState(ActorState::TriggersTNT, true);
			SetTransition(AnimState::TransitionActivate, false, [this]() {
				DecreaseHealth(INT32_MAX);
			});

			_renderer.setScale(1.0f);
			PlaySfx("Explosion"_s);

			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 50.0f, [this](ActorBase* actor) {
				actor->OnHandleCollision(shared_from_this());
				return true;
			});

			auto tiles = _levelHandler->TileMap();
			if (tiles != nullptr) {
				AABBf aabb = AABBf(_pos.X - 34.0f, _pos.Y - 34.0f, _pos.X + 34.0f, _pos.Y + 34.0f);
				TileCollisionParams params = { TileDestructType::Special | TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::TNT, 8 };
				tiles->IsTileEmpty(aabb, params);
				if (params.TilesDestroyed > 0) {
					if (auto player = dynamic_cast<Player*>(_owner.get())) {
						player->AddScore(params.TilesDestroyed * 50);
					}
				}
			}
		} else {
			_lightIntensity -= timeMult * 0.02f;
			_renderer.setScale(_renderer.scale() + timeMult * 0.01f);
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
			if (tnt->_isExploded && _timeLeft > 35.0f) {
				_timeLeft = 35.0f;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}
}