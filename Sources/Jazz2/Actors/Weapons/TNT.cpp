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
		: _proximityLeft(0.0f), _timeLeft(0.0f), _lightIntensity(0.0f), _isExploded(false)
	{
	}

	Task<bool> TNT::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_timeLeft = FuseTicks * TicksToFrames;
		_preexplosionTime = (std::int32_t)_timeLeft / 16;

		SetState(ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation, false);


		async_await RequestMetadataAsync("Weapon/TNT"_s);

		SetAnimation(AnimState::Idle);

		auto* tiles = _levelHandler->TileMap();
		if (tiles != nullptr) {
			AABBf aabb = AABBf(_pos.X - 34.0f, _pos.Y - 34.0f, _pos.X + 34.0f, _pos.Y + 34.0f);
			TileCollisionParams params = { TileDestructType::Special | TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::TNT, 8 };
			if (tiles->CanBeDestroyed(aabb, params)) {
				_timeLeft = 40.0f;
			}
		}

		async_return true;
	}

	bool TNT::OnHandleCollision(ActorBase* other)
	{
		if (auto* tnt = runtime_cast<TNT>(other)) {
			if (tnt->_isExploded && _timeLeft > PreBlastTicks * TicksToFrames) {
				_timeLeft = PreBlastTicks * TicksToFrames;
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	Player* TNT::GetOwner()
	{
		return runtime_cast<Player>(_owner.get());
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

			if (_timeLeft > PreBlastTicks * TicksToFrames) {
				// Something near looks every few ticks and sets the TNT off
				_proximityLeft -= timeMult;
				if (_proximityLeft <= 0.0f) {
					_proximityLeft += ProximityTicks * TicksToFrames;
					_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, 64.0f, [this](ActorBase* actor) {
						if (actor->CanCauseDamage(this)) {
							_timeLeft = PreBlastTicks * TicksToFrames;
							return false;
						}
						return true;
					});
				}
			} else {
				std::int32_t fraction = (std::int32_t)_timeLeft / 16;
				if (_preexplosionTime != fraction) {
					_preexplosionTime = fraction;

					_renderer.SetScale(5.0f);
					if (_noise == nullptr) {
						_noise = PlaySfx(Random().NextBool() ? "Bell1"_s : "Bell2"_s);
					} else if (!_noise->IsPlaying()) {
						_noise->Play();
					}
				}
				_renderer.SetScale(_renderer.GetScale() - timeMult * 0.36f);
			}
		} else if (!_isExploded) {
			_isExploded = true;
			_lightIntensity = 0.8f;

			SetTransition(AnimState::TransitionActivate, false, [this]() {
				DecreaseHealth(INT32_MAX);
			});

			_renderer.SetScale(1.0f);
			PlaySfx("Explosion"_s);

			_levelHandler->FindCollisionActorsByRadius(_pos.X, _pos.Y, BlastRadius, [this](ActorBase* actor) {
				auto* enemy = runtime_cast<Enemies::EnemyBase>(actor);
				if (enemy == nullptr) {
					actor->OnHandleCollision(this);
				} else if (!enemy->GetState(ActorState::IsInvulnerable)) {
					// The nearer the blast, the harder it hits
					Vector2f delta = _pos - enemy->GetPos();
					float distanceSqr = delta.X * delta.X + delta.Y * delta.Y;
					if (distanceSqr < BlastRadius * BlastRadius) {
						std::int32_t damage = 1 + std::int32_t((BlastRadius * BlastRadius - distanceSqr) * float(MaxBlastDamage - 1) / (BlastRadius * BlastRadius));
						// Some bosses lose one point to a blast however close it is, as in the original
						if (enemy->TakesSingleBlastDamage()) {
							damage = 1;
						}
						enemy->DecreaseHealth(damage, this);
					}
				}
				return true;
			});

			auto* tiles = _levelHandler->TileMap();
			if (tiles != nullptr) {
				AABBf aabb = AABBf(_pos.X - 72.0f, _pos.Y - 72.0f, _pos.X + 72.0f, _pos.Y + 72.0f);
				TileCollisionParams params = { TileDestructType::Special | TileDestructType::Weapon | TileDestructType::IgnoreSolidTiles, false, WeaponType::TNT, 8 };
				tiles->IsTileEmpty(aabb, params);
				if (params.TilesDestroyed > 0) {
					if (auto* player = runtime_cast<Player>(_owner.get())) {
						player->AddScore(params.TilesDestroyed * 50);
					}
				}
			}
		} else {
			_lightIntensity -= timeMult * 0.02f;
			_renderer.SetScale(_renderer.GetScale() + timeMult * 0.01f);
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
}