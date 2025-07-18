﻿#include "ActorBase.h"
#include "../ContentResolver.h"
#include "../ILevelHandler.h"
#include "../PreferencesCache.h"
#include "../Events/EventMap.h"
#include "../Tiles/TileMap.h"
#include "../Collisions/DynamicTreeBroadPhase.h"

#include "Explosion.h"
#include "Player.h"
#include "Solid/Pole.h"
#include "Solid/PushableBox.h"
#include "Weapons/ShieldFireShot.h"
#include "Weapons/FreezerShot.h"
#include "Weapons/ToasterShot.h"
#include "Weapons/Thunderbolt.h"

#if !defined(WITH_COROUTINES)
#	pragma message("WITH_COROUTINES is not defined, building without asynchronous loading support")
#endif

#include "../../nCine/tracy.h"
#include "../../nCine/Primitives/Matrix4x4.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Actors
{
	ActorBase::ActorBase()
		: _state(ActorState::None), _levelHandler(nullptr), _internalForceY(0.0f), _elasticity(0.0f), _friction(1.5f),
			_unstuckCooldown(0.0f), _frozenTimeLeft(0.0f), _maxHealth(1), _health(1), _spawnFrames(0.0f), _metadata(nullptr),
			_renderer(this), _currentAnimation(nullptr), _currentTransition(nullptr), _currentTransitionCancellable(false),
			_collisionProxyID(Collisions::NullNode)
	{
	}

	ActorBase::~ActorBase()
	{
	}

	bool ActorBase::IsFacingLeft() const
	{
		return GetState(ActorState::IsFacingLeft);
	}

	void ActorBase::SetFacingLeft(bool value)
	{
		if (IsFacingLeft() == value) {
			return;
		}

		SetState(ActorState::IsFacingLeft, value);
		_renderer.setFlippedX(value);
		
		// Recalculate hotspot
		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res != nullptr) {
			_renderer.Hotspot.X = static_cast<float>(IsFacingLeft() ? (res->Base->FrameDimensions.X - res->Base->Hotspot.X) : res->Base->Hotspot.X);
			_renderer.setAbsAnchorPoint(_renderer.Hotspot.X, _renderer.Hotspot.Y);
		}
	}

	void ActorBase::SetParent(SceneNode* parent)
	{
		_renderer.setParent(parent);
	}

	Task<bool> ActorBase::OnActivated(const ActorActivationDetails& details)
	{
		_state |= details.State | ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation;
		_levelHandler = details.LevelHandler;
		_pos = Vector2f((float)details.Pos.X, (float)details.Pos.Y);
		_originTile = Vector2i((std::int32_t)details.Pos.X / 32, (std::int32_t)details.Pos.Y / 32);
		_spawnFrames = _levelHandler->GetElapsedFrames();

		std::uint16_t layer = (std::uint16_t)details.Pos.Z;
		_renderer.setLayer(layer);

		bool success = async_await OnActivatedAsync(details);

		_renderer.setPosition(std::round(_pos.X), std::round(_pos.Y));

		OnUpdateHitbox();

		_state |= ActorState::Initialized;

		async_return success;
	}

	Task<bool> ActorBase::OnActivatedAsync(const ActorActivationDetails& details)
	{
		// This should be overridden and return true
		async_return false;
	}

	bool ActorBase::OnTileDeactivated()
	{
		return true;
	}

	void ActorBase::OnAttach(ActorBase* parent)
	{
		// Can be overridden
	}

	void ActorBase::OnDetach(ActorBase* parent)
	{
		// Can be overridden
	}

	void ActorBase::OnUpdate(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
		TryStandardMovement(timeMult, params);
		OnUpdateHitbox();
		UpdateFrozenState(timeMult);
	}

	void ActorBase::OnUpdateHitbox()
	{
		if (_metadata != nullptr) {
			UpdateHitbox(_metadata->BoundingBox.X, _metadata->BoundingBox.Y);
		}
	}

	bool ActorBase::OnDraw(RenderQueue& renderQueue)
	{
		// Override and return true to suppress default rendering
		return false;
	}

	void ActorBase::OnHealthChanged(ActorBase* collider)
	{
		// Can be overridden
	}

	bool ActorBase::OnPerish(ActorBase* collider)
	{
		if (GetState(ActorState::IsCreatedFromEventMap)) {
			auto events = _levelHandler->EventMap();
			if (events != nullptr) {
				events->Deactivate(_originTile.X, _originTile.Y);
				events->StoreTileEvent(_originTile.X, _originTile.Y, EventType::Empty);
			}
		}

		_state |= ActorState::IsDestroyed | ActorState::SkipPerPixelCollisions;
		return true;
	}

	void ActorBase::OnHitFloor(float timeMult)
	{
		// Called from inside the position update code when the object hits floor
		// and was falling earlier. Objects should override this if they need to
		// (e.g. the Player class playing a sound).
	}

	void ActorBase::OnHitCeiling(float timeMult)
	{
		// Called from inside the position update code when the object hits ceiling.
		// Objects should override this if they need to.
	}

	void ActorBase::OnHitWall(float timeMult)
	{
		// Called from inside the position update code when the object hits a wall.
		// Objects should override this if they need to.
	}

	bool ActorBase::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (GetState(ActorState::CanBeFrozen)) {
			HandleFrozenStateChange(other.get());
		}
		return false;
	}

	bool ActorBase::CanCauseDamage(ActorBase* collider)
	{
		return false;
	}

	void ActorBase::OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams)
	{
		// Can be overridden
	}

	void ActorBase::TryStandardMovement(float timeMult, TileCollisionParams& params)
	{
		ZoneScoped;

		if (_unstuckCooldown > 0.0f) {
			_unstuckCooldown -= timeMult;
		}

		float currentGravity;
		float currentElasticity = _elasticity;
		if ((_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation) {
			currentGravity = _levelHandler->GetGravity();
			if (_pos.Y >= _levelHandler->GetWaterLevel()) {
				currentGravity *= 0.5f;
				currentElasticity *= 0.7f;
			}
		} else {
			currentGravity = 0.0f;
		}

		float accelY = (_internalForceY + _externalForce.Y) * timeMult;

		_speed.X = std::clamp(_speed.X, -16.0f, 16.0f);
		_speed.Y = std::clamp(_speed.Y + accelY, -16.0f, 16.0f);

		float effectiveSpeedX, effectiveSpeedY;
		if (_frozenTimeLeft > 0.0f) {
			effectiveSpeedX = std::clamp(_externalForce.X * timeMult, -16.0f, 16.0f);
			effectiveSpeedY = ((_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation
				? (_speed.Y + 0.5f * accelY)
				: std::clamp(((currentGravity * 2.0f) + _internalForceY) * timeMult, -16.0f, 16.0f));
		} else {
			effectiveSpeedX = _speed.X + _externalForce.X * timeMult;
			effectiveSpeedY = _speed.Y + 0.5f * accelY;
		}
		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		if (std::abs(effectiveSpeedX) > 0.0f || std::abs(effectiveSpeedY) > 0.0f) {
			if (GetState(ActorState::CanJump | ActorState::ApplyGravitation)) {
				// All ground-bound movement is handled here. In the basic case, the actor
				// moves horizontally, but it can also logically move up or down if it is
				// moving across a slope. In here, angles between about 45 degrees down
				// to 45 degrees up are attempted with some intervals to attempt to keep
				// the actor attached to the slope in question.

				// Always try values a bit over the 45 degree incline; subpixel coordinates
				// may mean the actor actually needs to move a pixel up or down even though
				// the speed wouldn't warrant that large of a change.
				// Not doing this will cause hiccups with uphill slopes in particular.
				// Beach tileset also has some spots where two properly set up adjacent
				// tiles have a 2px jump, so adapt to that.
				bool success = false;
				float maxYDiff = std::max(3.0f, std::abs(effectiveSpeedX) + 2.5f);
				for (float yDiff = maxYDiff + effectiveSpeedY; yDiff >= -maxYDiff + effectiveSpeedY; yDiff -= CollisionCheckStep) {
					if (MoveInstantly(Vector2f(effectiveSpeedX, yDiff), MoveType::Relative, params)) {
						success = true;
						break;
					}
				}

				// Also try to move horizontally as far as possible
				float xDiff = std::abs(effectiveSpeedX);
				float maxXDiff = -xDiff;
				if (!success) {
					std::int32_t sign = (effectiveSpeedX > 0.0f ? 1 : -1);
					for (; xDiff >= maxXDiff; xDiff -= CollisionCheckStep) {
						if (MoveInstantly(Vector2f(xDiff * sign, 0.0f), MoveType::Relative, params)) {
							success = true;
							break;
						}
					}

					bool moved = false;
					if (!success && _unstuckCooldown <= 0.0f) {
						AABBf aabb = AABBInner;
						float t = aabb.B - 14.0f;
						if (aabb.T < t) {
							aabb.T = t;
						}
						TileCollisionParams params2 = { TileDestructType::None, true };
						if (!_levelHandler->IsPositionEmpty(this, aabb, params2)) {
							for (float yDiff = -2.0f; yDiff >= -12.0f; yDiff -= 2.0f) {
								if (MoveInstantly(Vector2f(0.0f, yDiff), MoveType::Relative, params)) {
									moved = true;
									_unstuckCooldown = 60.0f;
									break;
								}
							}

							if (!moved) {
								for (float yDiff = 2.0f; yDiff <= 14.0f; yDiff += 2.0f) {
									if (MoveInstantly(Vector2f(0.0f, yDiff), MoveType::Relative, params)) {
										moved = true;
										_unstuckCooldown = 60.0f;
										break;
									}
								}
							}
						}
					}

					if (!moved) {
						// If no angle worked in the previous step, the actor is facing a wall
						if (xDiff > CollisionCheckStep || (xDiff > 0.0f && currentElasticity > 0.0f)) {
							_speed.X = -(currentElasticity * _speed.X);
							_externalForce.X = 0.0f;
						}
						OnHitWall(timeMult);
					}
				}
			} else {
				// Airborne movement is handled here
				// First, attempt to move directly based on the current speed values
				if (!MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, params)) {
					// First, attempt to move horizontally as much as possible
					float maxDiff = std::abs(effectiveSpeedX);
					float xDiff = maxDiff;
					for (; xDiff > std::numeric_limits<float>::epsilon(); xDiff -= CollisionCheckStep) {
						if (MoveInstantly(Vector2f(std::copysign(xDiff, effectiveSpeedX), 0.0f), MoveType::Relative, params)) {
							break;
						}
					}

					// Then, try the same vertically
					bool hitCalled = false;
					maxDiff = std::abs(effectiveSpeedY);
					float yDiff = maxDiff;
					for (; yDiff > std::numeric_limits<float>::epsilon(); yDiff -= CollisionCheckStep) {
						float yDiffSigned = std::copysign(yDiff, effectiveSpeedY);
						if (MoveInstantly(Vector2f(0.0f, yDiffSigned), MoveType::Relative, params) ||
							// Add horizontal tolerance
							MoveInstantly(Vector2f(yDiff * 0.2f, yDiffSigned), MoveType::Relative, params) ||
							MoveInstantly(Vector2f(yDiff * -0.2f, yDiffSigned), MoveType::Relative, params)) {
							break;
						}
					}

					if (effectiveSpeedY < 0.0f) {
						if (-yDiff > effectiveSpeedY) {
							// Reset speed and also internal force, mainly because of player jump
							_speed.Y = 0.0f;
							if (_internalForceY < 0.0f) {
								_internalForceY = 0.0f;
							}
							OnHitCeiling(timeMult);
							hitCalled = true;
						}
					} else if (effectiveSpeedY > 0.0f && yDiff < effectiveSpeedY && currentGravity <= 0.0f) {
						// If there is no gravity and actor is touching floor, the callback wouldn't be called otherwise
						OnHitFloor(timeMult);
						hitCalled = true;
					}

					// If the actor didn't move all the way horizontally, it hit a wall (or was already touching it)
					if (xDiff < std::abs(effectiveSpeedX) * 0.3f) {
						if (xDiff > 0.0f && currentElasticity > 0.0f) {
							_speed.X = -(currentElasticity * _speed.X);
							_externalForce.X = 0.0f;
						}

						// Don't call OnHitWall() if OnHitFloor() or OnHitCeiling() was called this step
						if (!hitCalled) {
							OnHitWall(timeMult);
						}
					}
				}
			}
		}

		// Reduce all forces if they are present
		if (std::abs(_externalForce.X) > 0.0f) {
			if (_externalForce.X > 0.0f) {
				_externalForce.X = std::max(_externalForce.X - _friction * timeMult, 0.0f);
			} else {
				_externalForce.X = std::min(_externalForce.X + _friction * timeMult, 0.0f);
			}
		}

		// Handle gravity and collision with floor
		if (currentGravity > 0.0f) {
			if (_speed.Y >= 0.0f) {
				// Actor is going down
				AABBf aabb = AABBInner;
				aabb.B += CollisionCheckStep;
				if (_levelHandler->IsPositionEmpty(this, aabb, params)) {
					// There is still some space below - only apply gravity
					SetState(ActorState::CanJump, false);
					_speed.Y += currentGravity * timeMult;
				} else {
					// Actor is on the floor
					OnHitFloor(timeMult);
					if (currentElasticity != 0.0f) {
						SetState(ActorState::CanJump, false);
						_speed.Y = -(currentElasticity * effectiveSpeedY / timeMult);
					} else {
						SetState(ActorState::CanJump, true);
						_speed.Y = 0.0f;
					}
				}
			} else {
				// Actor is going up - only apply gravity
				SetState(ActorState::CanJump, false);
				_speed.Y += currentGravity * timeMult;
			}

			_externalForce.Y = std::min(_externalForce.Y + currentGravity * 0.33f * timeMult, 0.0f);
			_internalForceY = std::min(_internalForceY + currentGravity * 0.33f * timeMult, 0.0f);
		}
	}

	void ActorBase::UpdateHitbox(std::int32_t w, std::int32_t h)
	{
		if (_currentAnimation == nullptr) {
			return;
		}

		auto& base = _currentAnimation->Base;
		if (base->Coldspot != Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue)) {
			AABBInner = AABBf(
				_pos.X - base->Hotspot.X + base->Coldspot.X - (w / 2),
				_pos.Y - base->Hotspot.Y + base->Coldspot.Y - h,
				_pos.X - base->Hotspot.X + base->Coldspot.X + (w / 2),
				_pos.Y - base->Hotspot.Y + base->Coldspot.Y
			);
		} else {
			// Collision base set to the bottom of the sprite.
			// This is probably still not the correct way to do it, but at least it works for now.
			AABBInner = AABBf(
				_pos.X - (w / 2),
				_pos.Y - base->Hotspot.Y + base->FrameDimensions.Y - h,
				_pos.X + (w / 2),
				_pos.Y - base->Hotspot.Y + base->FrameDimensions.Y
			);
		}
	}

	void ActorBase::CreateParticleDebrisOnPerish(ActorBase* collider)
	{
		ParticleDebrisEffect effect;
		if (runtime_cast<Weapons::ToasterShot>(collider) || runtime_cast<Weapons::ShieldFireShot>(collider)) {
			effect = ParticleDebrisEffect::Fire;
		} else if (runtime_cast<Weapons::Thunderbolt>(collider)) {
			effect = ParticleDebrisEffect::Lightning;
		} else if (_pos.Y > _levelHandler->GetWaterLevel()) {
			effect = ParticleDebrisEffect::StandardInWater;
		} else if (_frozenTimeLeft > 0.0f) {
			effect = ParticleDebrisEffect::Frozen;
		} else {
			effect = ParticleDebrisEffect::Standard;
		}

		Vector2f speed;
		if (runtime_cast<Solid::Pole>(collider) || runtime_cast<Solid::PushableBox>(collider)) {
			speed = Vector2f(0.0f, -1.0f);
		} else if (runtime_cast<Weapons::Thunderbolt>(collider)) {
			speed = _pos - collider->_pos;
		} else if (collider != nullptr) {
			speed = collider->_speed;;
		}

		CreateParticleDebrisOnPerish(effect, speed);
	}

	void ActorBase::CreateParticleDebrisOnPerish(ParticleDebrisEffect effect, Vector2f speed)
	{
		_levelHandler->HandleCreateParticleDebrisOnPerish(this, effect, speed);

		auto tilemap = _levelHandler->TileMap();
		if (tilemap == nullptr) {
			return;
		}

		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		Texture* texture = res->Base->TextureDiffuse.get();
		if (texture == nullptr) {
			return;
		}

		float x = _pos.X - res->Base->Hotspot.X;
		float y = _pos.Y - res->Base->Hotspot.Y;

		if (effect == ParticleDebrisEffect::Fire) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->size();

			for (std::int32_t fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (std::int32_t fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.8f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(0.5f, 2.0f) / res->Base->FrameDimensions.X,
						 Random().FastFloat(0.0f, 0.2f));
					debris.Acceleration = Vector2(0.0f, 0.06f);

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.001f;

					debris.Time = 320.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = texture;
					debris.Flags = Tiles::TileMap::DebrisFlags::Bounce;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Lightning) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->size();

			for (std::int32_t fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (std::int32_t fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(10.0f, 50.0f);

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = texture;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Dissolve) {
			constexpr int DebrisSize = 2;

			Vector2i texSize = texture->size();

			float x = _pos.X - res->Base->Hotspot.X;
			float y = _pos.Y - res->Base->Hotspot.Y;

			for (std::int32_t fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (std::int32_t fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / res->Base->FrameDimensions.X,
							((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.2f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.01f;

					debris.Time = 280.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = texture;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::StandardInWater) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->size();

			for (std::int32_t fy = 0; fy < res->Base->FrameDimensions.Y; fy += DebrisSize + 1) {
				for (int fx = 0; fx < res->Base->FrameDimensions.X; fx += DebrisSize + 1) {
					float currentSize = DebrisSize * Random().FastFloat(0.2f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - fx : fx), y + fy);
					debris.Depth = _renderer.layer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - res->Base->FrameDimensions.X / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / res->Base->FrameDimensions.X,
						 ((fy - res->Base->FrameDimensions.Y / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / res->Base->FrameDimensions.Y);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(300.0f, 340.0f);;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = (((float)(_renderer.CurrentFrame % res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.X) + ((float)fx / float(texSize.X)));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = (((float)(_renderer.CurrentFrame / res->Base->FrameConfiguration.X) / res->Base->FrameConfiguration.Y) + ((float)fy / float(texSize.Y)));

					debris.DiffuseTexture = texture;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Frozen) {
			for (std::int32_t i = 0; i < 20; i++) {
				Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 10), Explosion::Type::IceShrapnel);
			}

			_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
			return;
		}

		float length = speed.Length();
		Vector2f force = (length > 0.0f ? (speed / length * 1.4f) : Vector2f::Zero);
		tilemap->CreateParticleDebris(res, Vector3f(_pos.X, _pos.Y, (float)_renderer.layer()), force, _renderer.CurrentFrame, IsFacingLeft());
	}

	void ActorBase::CreateSpriteDebris(AnimState state, std::int32_t count)
	{
		_levelHandler->HandleCreateSpriteDebris(this, state, count);

		auto* tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr && _metadata != nullptr) {
			auto* res = _metadata->FindAnimation(state);
			if (res != nullptr) {
				tilemap->CreateSpriteDebris(res, Vector3f(_pos.X, _pos.Y, (float)_renderer.layer()), count);
			}
		}
	}

	float ActorBase::GetIceShrapnelScale() const
	{
		return 1.0f;
	}

	std::shared_ptr<AudioBufferPlayer> ActorBase::PlaySfx(StringView identifier, float gain, float pitch)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			AudioBuffer* buffer;
			if (!it->second.Buffers.empty()) {
				std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
				buffer = &it->second.Buffers[idx]->Buffer;
			} else {
				buffer = nullptr;
			}

			return _levelHandler->PlaySfx(this, identifier, buffer, Vector3f(_pos.X, _pos.Y, 0.0f), false, gain, pitch);
		}

		return nullptr;
	}

	bool ActorBase::SetAnimation(AnimState state, bool skipAnimation)
	{
		if (_metadata == nullptr) {
			LOGE("No metadata loaded");
			return false;
		}

		if (_currentAnimation != nullptr && _currentAnimation->State == state) {
			return true;
		}

		auto* anim = _metadata->FindAnimation(state);
		if (anim == nullptr) {
			//LOGE("No animation found for state 0x{:.8x}", state);
			return false;
		}

		if (_currentTransition == nullptr || _currentTransitionCancellable) {
			if (_currentTransition != nullptr) {
				_currentTransition = nullptr;

				if (_currentTransitionCallback) {
					auto oldCallback = std::move(_currentTransitionCallback);
					_currentTransitionCallback = nullptr;
					oldCallback();
				}
			}

			_currentAnimation = anim;
			RefreshAnimation(skipAnimation);
		} else {
			// It will be set after active transition
			_currentAnimation = anim;
		}

		return true;
	}

	bool ActorBase::SetTransition(AnimState state, bool cancellable, Function<void()>&& callback)
	{
		auto* anim = _metadata->FindAnimation(state);
		if (anim == nullptr) {
			if (callback) {
				callback();
			}
			return false;
		}

		if (_currentTransitionCallback) {
			auto oldCallback = std::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		_currentTransition = anim;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = std::move(callback);		
		RefreshAnimation();

		return true;
	}

	void ActorBase::CancelTransition()
	{
		if (_currentTransition != nullptr && _currentTransitionCancellable) {
			if (_currentTransitionCallback) {
				auto oldCallback = std::move(_currentTransitionCallback);
				_currentTransitionCallback = nullptr;
				oldCallback();
			}

			_currentTransition = nullptr;
			RefreshAnimation();
		}
	}

	void ActorBase::ForceCancelTransition()
	{
		if (_currentTransition != nullptr) {
			_currentTransition = nullptr;
			_currentTransitionCancellable = true;
			_currentTransitionCallback = nullptr;
			RefreshAnimation();
		}
	}

	void ActorBase::OnAnimationStarted()
	{
		// Can be overriden
	}

	void ActorBase::OnAnimationFinished()
	{
		if (_currentTransition != nullptr) {
			_currentTransition = nullptr;

			RefreshAnimation();

			if (_currentTransitionCallback) {
				auto oldCallback = std::move(_currentTransitionCallback);
				_currentTransitionCallback = nullptr;
				oldCallback();
			}
		}
	}

	void ActorBase::OnPacketReceived(MemoryStream& packet)
	{
		LOGD("Packet wasn't handled by derived class");
	}

	void ActorBase::SendPacket(ArrayView<const std::uint8_t> data)
	{
		_levelHandler->SendPacket(this, data);
	}

	bool ActorBase::IsCollidingWith(ActorBase* other)
	{
		bool perPixel1 = (_state & ActorState::SkipPerPixelCollisions) != ActorState::SkipPerPixelCollisions;
		bool perPixel2 = (other->_state & ActorState::SkipPerPixelCollisions) != ActorState::SkipPerPixelCollisions;

		if ((perPixel1 && std::abs(_renderer.rotation()) > 0.1f) || (perPixel2 && std::abs(other->_renderer.rotation()) > 0.1f)) {
			if (!perPixel1 && std::abs(other->_renderer.rotation()) > 0.1f) {
				return other->IsCollidingWithAngled(AABBInner);
			} else if (!perPixel2 && std::abs(_renderer.rotation()) > 0.1f) {
				return IsCollidingWithAngled(other->AABBInner);
			}
			return IsCollidingWithAngled(other);
		}

		GraphicResource* res1 = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		GraphicResource* res2 = (other->_currentTransition != nullptr ? other->_currentTransition : other->_currentAnimation);
		if (res1 == nullptr || res2 == nullptr) {
			if (res1 != nullptr) {
				return IsCollidingWith(other->AABBInner);
			}
			if (res2 != nullptr) {
				return other->IsCollidingWith(AABBInner);
			}
			return false;
		}

		Vector2i& hotspot1 = res1->Base->Hotspot;
		Vector2i& hotspot2 = res2->Base->Hotspot;

		Vector2i& size1 = res1->Base->FrameDimensions;
		Vector2i& size2 = res2->Base->FrameDimensions;

		AABBf aabb1, aabb2;
		if (!perPixel1) {
			aabb1 = AABBInner;
		} else if (GetState(ActorState::IsFacingLeft)) {
			aabb1 = AABBf(_pos.X + hotspot1.X - size1.X, _pos.Y - hotspot1.Y, (float)size1.X, (float)size1.Y);
			aabb1.B += aabb1.T;
			aabb1.R += aabb1.L;
		} else {
			aabb1 = AABBf(_pos.X - hotspot1.X, _pos.Y - hotspot1.Y, (float)size1.X, (float)size1.Y);
			aabb1.B += aabb1.T;
			aabb1.R += aabb1.L;
		}
		if (!perPixel2) {
			aabb2 = other->AABBInner;
		} else if (other->GetState(ActorState::IsFacingLeft)) {
			aabb2 = AABBf(other->_pos.X + hotspot2.X - size2.X, other->_pos.Y - hotspot2.Y, (float)size2.X, (float)size2.Y);
			aabb2.B += aabb2.T;
			aabb2.R += aabb2.L;
		} else {
			aabb2 = AABBf(other->_pos.X - hotspot2.X, other->_pos.Y - hotspot2.Y, (float)size2.X, (float)size2.Y);
			aabb2.B += aabb2.T;
			aabb2.R += aabb2.L;
		}

		// Bounding Box intersection
		AABBf inter = AABBf::Intersect(aabb1, aabb2);
		if (inter.R <= 0 || inter.B <= 0) {
			return false;
		}

		if (!perPixel1 || !perPixel2) {
			if (perPixel1 == perPixel2) {
				return true;
			}

			//PixelData p;
			uint8_t* p;
			GraphicResource* res;
			bool isFacingLeftCurrent;
			std::int32_t x1, y1, x2, y2, xs, dx, dy, stride;
			if (perPixel1) {
				res = res1;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = GetState(ActorState::IsFacingLeft);

				x1 = (std::int32_t)std::max(inter.L, other->AABBInner.L);
				y1 = (std::int32_t)std::max(inter.T, other->AABBInner.T);
				x2 = (std::int32_t)std::min(inter.R, other->AABBInner.R);
				y2 = (std::int32_t)std::min(inter.B, other->AABBInner.B);

				xs = (std::int32_t)aabb1.L;

				std::int32_t frame1 = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
				dx = (frame1 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
				dy = (frame1 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (std::int32_t)aabb1.T;

			} else {
				res = res2;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = other->GetState(ActorState::IsFacingLeft);

				x1 = (std::int32_t)std::max(inter.L, AABBInner.L);
				y1 = (std::int32_t)std::max(inter.T, AABBInner.T);
				x2 = (std::int32_t)std::min(inter.R, AABBInner.R);
				y2 = (std::int32_t)std::min(inter.B, AABBInner.B);

				xs = (std::int32_t)aabb2.L;

				std::int32_t frame2 = std::min(other->_renderer.CurrentFrame, res->FrameCount - 1);
				dx = (frame2 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
				dy = (frame2 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (std::int32_t)aabb2.T;
			}

			stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

			// Per-pixel collision check
			for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
				for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
					std::int32_t i1 = i - xs;
					if (isFacingLeftCurrent) {
						i1 = res->Base->FrameDimensions.X - i1 - 1;
					}

					if (p[((j + dy) * stride) + i1 + dx] > AlphaThreshold) {
						return true;
					}
				}
			}
		} else {
			std::int32_t x1 = (std::int32_t)inter.L;
			std::int32_t y1 = (std::int32_t)inter.T;
			std::int32_t x2 = (std::int32_t)inter.R;
			std::int32_t y2 = (std::int32_t)inter.B;

			std::int32_t x1s = (std::int32_t)aabb1.L;
			std::int32_t x2s = (std::int32_t)aabb2.L;

			std::int32_t frame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
			std::int32_t dx1 = (frame1 % res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.X;
			std::int32_t dy1 = (frame1 / res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.Y - (std::int32_t)aabb1.T;
			std::int32_t stride1 = res1->Base->FrameConfiguration.X * res1->Base->FrameDimensions.X;

			std::int32_t frame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
			std::int32_t dx2 = (frame2 % res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.X;
			std::int32_t dy2 = (frame2 / res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.Y - (std::int32_t)aabb2.T;
			std::int32_t stride2 = res2->Base->FrameConfiguration.X * res2->Base->FrameDimensions.X;

			// Per-pixel collision check
			auto p1 = res1->Base->Mask.get();
			auto p2 = res2->Base->Mask.get();

			for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
				for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
					std::int32_t i1 = i - x1s;
					if (GetState(ActorState::IsFacingLeft)) {
						i1 = res1->Base->FrameDimensions.X - i1 - 1;
					}
					std::int32_t i2 = i - x2s;
					if (other->GetState(ActorState::IsFacingLeft)) {
						i2 = res2->Base->FrameDimensions.X - i2 - 1;
					}

					if (p1[((j + dy1) * stride1) + i1 + dx1] > AlphaThreshold && p2[((j + dy2) * stride2) + i2 + dx2] > AlphaThreshold) {
						return true;
					}
				}
			}
		}

		return false;
	}

	bool ActorBase::IsCollidingWith(const AABBf& aabb)
	{
		bool perPixel = (_state & ActorState::SkipPerPixelCollisions) != ActorState::SkipPerPixelCollisions;
		if (!perPixel) {
			return aabb.Overlaps(AABBInner);
		} else if (std::abs(_renderer.rotation()) > 0.1f) {
			return IsCollidingWithAngled(aabb);
		}

		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res == nullptr) {
			return false;
		}

		Vector2i& hotspot = res->Base->Hotspot;
		Vector2i& size = res->Base->FrameDimensions;

		AABBf aabbSelf;
		if (GetState(ActorState::IsFacingLeft)) {
			aabbSelf = AABBf(_pos.X + hotspot.X - size.X, _pos.Y - hotspot.Y, (float)size.X, (float)size.Y);
			aabbSelf.B += aabbSelf.T;
			aabbSelf.R += aabbSelf.L;
		} else {
			aabbSelf = AABBf(_pos.X - hotspot.X, _pos.Y - hotspot.Y, (float)size.X, (float)size.Y);
			aabbSelf.B += aabbSelf.T;
			aabbSelf.R += aabbSelf.L;
		}

		// Bounding Box intersection
		AABBf inter = AABBf::Intersect(aabb, aabbSelf);
		if (inter.R <= 0 || inter.B <= 0) {
			return false;
		}

		std::int32_t x1 = (std::int32_t)std::max(inter.L, aabb.L);
		std::int32_t y1 = (std::int32_t)std::max(inter.T, aabb.T);
		std::int32_t x2 = (std::int32_t)std::min(inter.R, aabb.R);
		std::int32_t y2 = (std::int32_t)std::min(inter.B, aabb.B);

		std::int32_t xs = (std::int32_t)aabbSelf.L;

		std::int32_t frame1 = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		std::int32_t dx = (frame1 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
		std::int32_t dy = (frame1 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (int)aabbSelf.T;
		std::int32_t stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

		// Per-pixel collision check
		auto p = res->Base->Mask.get();

		for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
			for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
				std::int32_t i1 = i - xs;
				if (GetState(ActorState::IsFacingLeft)) {
					i1 = res->Base->FrameDimensions.X - i1 - 1;
				}

				if (p[((j + dy) * stride) + i1 + dx] > AlphaThreshold) {
					return true;
				}
			}
		}

		return false;
	}

	bool ActorBase::IsCollidingWithAngled(ActorBase* other)
	{
		GraphicResource* res1 = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		GraphicResource* res2 = (other->_currentTransition != nullptr ? other->_currentTransition : other->_currentAnimation);
		if (res1 == nullptr || res2 == nullptr) {
			return false;
		}

		Matrix4x4f transform1 = Matrix4x4f::Translation((float)-res1->Base->Hotspot.X, (float)-res1->Base->Hotspot.Y, 0.0f);
		if (GetState(ActorState::IsFacingLeft)) {
			transform1 = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform1;
		}
		transform1 = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.rotation()) * transform1;

		Matrix4x4f transform2 = Matrix4x4f::Translation((float)-res2->Base->Hotspot.X, (float)-res2->Base->Hotspot.Y, 0.0f);
		if (other->GetState(ActorState::IsFacingLeft)) {
			transform2 = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform2;
		}
		transform2 = Matrix4x4f::Translation(other->_pos.X, other->_pos.Y, 0.0f) * Matrix4x4f::RotationZ(other->_renderer.rotation()) * transform2;

		std::int32_t width1 = res1->Base->FrameDimensions.X;
		std::int32_t height1 = res1->Base->FrameDimensions.Y;
		std::int32_t width2 = res2->Base->FrameDimensions.X;
		std::int32_t height2 = res2->Base->FrameDimensions.Y;

		// Bounding Box intersection
		AABBf aabb1, aabb2;
		{
			Vector3f tl = Vector3f::Zero * transform1;
			Vector3f tr = Vector3f((float)width1, 0.0f, 0.0f) * transform1;
			Vector3f bl = Vector3f(0.0f, (float)height1, 0.0f) * transform1;
			Vector3f br = Vector3f((float)width1, (float)height1, 0.0f) * transform1;

			float minX = std::min(std::min(tl.X, tr.X), std::min(bl.X, br.X));
			float minY = std::min(std::min(tl.Y, tr.Y), std::min(bl.Y, br.Y));
			float maxX = std::max(std::max(tl.X, tr.X), std::max(bl.X, br.X));
			float maxY = std::max(std::max(tl.Y, tr.Y), std::max(bl.Y, br.Y));

			aabb1 = AABBf(std::floor(minX), std::floor(minY), std::ceil(maxX), std::ceil(maxY));
		}
		{
			Vector3f tl = Vector3f::Zero * transform2;
			Vector3f tr = Vector3f((float)width2, 0.0f, 0.0f) * transform2;
			Vector3f bl = Vector3f(0.0f, (float)height2, 0.0f) * transform2;
			Vector3f br = Vector3f((float)width2, (float)height2, 0.0f) * transform2;

			float minX = std::min(std::min(tl.X, tr.X), std::min(bl.X, br.X));
			float minY = std::min(std::min(tl.Y, tr.Y), std::min(bl.Y, br.Y));
			float maxX = std::max(std::max(tl.X, tr.X), std::max(bl.X, br.X));
			float maxY = std::max(std::max(tl.Y, tr.Y), std::max(bl.Y, br.Y));

			aabb2 = AABBf(std::floor(minX), std::floor(minY), std::ceil(maxX), std::ceil(maxY));
		}

		if (!aabb1.Overlaps(aabb2)) {
			return false;
		}

		// Per-pixel collision check
		Matrix4x4f transformAToB = transform2.Inverse() * transform1;

		// TransformNormal with [1, 0] and [0, 1] vectors
		Vector3f stepX = Vector3f(transformAToB[0][0], transformAToB[0][1], 0.0f) * PerPixelCollisionStep;
		Vector3f stepY = Vector3f(transformAToB[1][0], transformAToB[1][1], 0.0f) * PerPixelCollisionStep;

		Vector3f yPosIn2 = Vector3f::Zero * transformAToB;

		std::int32_t frame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
		std::int32_t dx1 = (frame1 % res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.X;
		std::int32_t dy1 = (frame1 / res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.Y;
		std::int32_t stride1 = res1->Base->FrameConfiguration.X * res1->Base->FrameDimensions.X;

		std::int32_t frame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
		std::int32_t dx2 = (frame2 % res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.X;
		std::int32_t dy2 = (frame2 / res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.Y;
		std::int32_t stride2 = res2->Base->FrameConfiguration.X * res2->Base->FrameDimensions.X;

		auto p1 = res1->Base->Mask.get();
		auto p2 = res2->Base->Mask.get();

		for (std::int32_t y1 = 0; y1 < height1; y1 += PerPixelCollisionStep) {
			Vector3f posIn2 = yPosIn2;

			for (std::int32_t x1 = 0; x1 < width1; x1 += PerPixelCollisionStep) {
				std::int32_t x2 = (std::int32_t)std::round(posIn2.X);
				std::int32_t y2 = (std::int32_t)std::round(posIn2.Y);

				if (x2 >= 0 && x2 < width2 && y2 >= 0 && y2 < height2) {
					if (p1[((y1 + dy1) * stride1) + x1 + dx1] > AlphaThreshold && p2[((y2 + dy2) * stride2) + x2 + dx2] > AlphaThreshold) {
						return true;
					}
				}
				posIn2 += stepX;
			}
			yPosIn2 += stepY;
		}

		return false;
	}

	bool ActorBase::IsCollidingWithAngled(const AABBf& aabb)
	{
		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res == nullptr) {
			return false;
		}

		Matrix4x4f transform = Matrix4x4f::Translation((float)-res->Base->Hotspot.X, (float)-res->Base->Hotspot.Y, 0.0f);
		if (GetState(ActorState::IsFacingLeft)) {
			transform = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform;
		}
		transform = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.rotation()) * transform;

		std::int32_t width = res->Base->FrameDimensions.X;
		std::int32_t height = res->Base->FrameDimensions.Y;

		// Bounding Box intersection
		AABBf aabbSelf;
		{
			Vector3f tl = Vector3f::Zero * transform;
			Vector3f tr = Vector3f((float)width, 0.0f, 0.0f) * transform;
			Vector3f bl = Vector3f(0.0f, (float)height, 0.0f) * transform;
			Vector3f br = Vector3f((float)width, (float)height, 0.0f) * transform;

			float minX = std::min(std::min(tl.X, tr.X), std::min(bl.X, br.X));
			float minY = std::min(std::min(tl.Y, tr.Y), std::min(bl.Y, br.Y));
			float maxX = std::max(std::max(tl.X, tr.X), std::max(bl.X, br.X));
			float maxY = std::max(std::max(tl.Y, tr.Y), std::max(bl.Y, br.Y));

			aabbSelf = AABBf(std::floor(minX), std::floor(minY), std::ceil(maxX), std::ceil(maxY));
		}

		if (!aabb.Overlaps(aabbSelf)) {
			return false;
		}

		// TransformNormal with [1, 0] and [0, 1] vectors
		Vector3f stepX = Vector3f(transform[0][0], transform[0][1], 0.0f) * PerPixelCollisionStep;
		Vector3f stepY = Vector3f(transform[1][0], transform[1][1], 0.0f) * PerPixelCollisionStep;

		Vector3f yPosInAABB = Vector3f::Zero * transform;

		std::int32_t frame = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		std::int32_t dx = (frame % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
		std::int32_t dy = (frame / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y;
		std::int32_t stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

		auto p = res->Base->Mask.get();

		for (std::int32_t y1 = 0; y1 < height; y1 += PerPixelCollisionStep) {
			Vector3f posInAABB = yPosInAABB;

			for (std::int32_t x1 = 0; x1 < width; x1 += PerPixelCollisionStep) {
				std::int32_t x2 = (std::int32_t)std::round(posInAABB.X);
				std::int32_t y2 = (std::int32_t)std::round(posInAABB.Y);

				if (p[((y1 + dy) * stride) + x1 + dx] > AlphaThreshold &&
					x2 >= aabb.L && x2 < aabb.R && y2 >= aabb.T && y2 < aabb.B) {
					return true;
				}

				posInAABB += stepX;
			}
			yPosInAABB += stepY;
		}

		return false;
	}

	void ActorBase::UpdateAABB()
	{
		if ((_state & (ActorState::CollideWithOtherActors | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject)) == ActorState::None) {
			// Collisions are deactivated
			return;
		}

		if ((_state & ActorState::SkipPerPixelCollisions) != ActorState::SkipPerPixelCollisions) {
			GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
			if (res == nullptr) {
				return;
			}

			Vector2i hotspot = res->Base->Hotspot;
			Vector2i size = res->Base->FrameDimensions;

			if (std::abs(_renderer.rotation()) > 0.1f) {
				Matrix4x4f transform = Matrix4x4f::Translation((float)-hotspot.X, (float)-hotspot.Y, 0.0f);
				if (GetState(ActorState::IsFacingLeft)) {
					transform = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform;
				}
				transform = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.rotation()) * transform;

				Vector3f tl = Vector3f::Zero * transform;
				Vector3f tr = Vector3f((float)size.X, 0.0f, 0.0f) * transform;
				Vector3f bl = Vector3f(0.0f, (float)size.Y, 0.0f) * transform;
				Vector3f br = Vector3f((float)size.X, (float)size.Y, 0.0f) * transform;

				float minX = std::min(std::min(tl.X, tr.X), std::min(bl.X, br.X));
				float minY = std::min(std::min(tl.Y, tr.Y), std::min(bl.Y, br.Y));
				float maxX = std::max(std::max(tl.X, tr.X), std::max(bl.X, br.X));
				float maxY = std::max(std::max(tl.Y, tr.Y), std::max(bl.Y, br.Y));

				AABB.L = minX;
				AABB.T = minY;
				AABB.R = maxX;
				AABB.B = maxY;
			} else {
				AABB.L = (IsFacingLeft() ? (_pos.X + hotspot.X - size.X) : (_pos.X - hotspot.X));
				AABB.T = _pos.Y - hotspot.Y;
				AABB.R = AABB.L + size.X;
				AABB.B = AABB.T + size.Y;
			}
		} else {
			OnUpdateHitbox(); 
			AABB = AABBInner;
		}
	}

	void ActorBase::RefreshAnimation(bool skipAnimation)
	{
		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res == nullptr) {
			return;
		}

		_renderer.FrameConfiguration = res->Base->FrameConfiguration;
		_renderer.FrameDimensions = res->Base->FrameDimensions;
		if (res->AnimDuration < 0.0f) {
			if (res->FrameCount > 1) {
				_renderer.FirstFrame = res->FrameOffset + nCine::Random().Next(0, res->FrameCount);
			} else {
				_renderer.FirstFrame = res->FrameOffset;
			}

			_renderer.LoopMode = AnimationLoopMode::FixedSingle;
		} else {
			_renderer.FirstFrame = res->FrameOffset;
			_renderer.LoopMode = res->LoopMode;
		}

		_renderer.FrameCount = res->FrameCount;
		_renderer.AnimDuration = res->AnimDuration;
		_renderer.AnimTime = (skipAnimation && res->AnimDuration >= 0.0f && _renderer.LoopMode != AnimationLoopMode::FixedSingle ? _renderer.AnimDuration : 0.0f);

		_renderer.Hotspot.X = static_cast<float>(IsFacingLeft() ? (res->Base->FrameDimensions.X - res->Base->Hotspot.X) : res->Base->Hotspot.X);
		_renderer.Hotspot.Y = static_cast<float>(res->Base->Hotspot.Y);

		_renderer.setTexture(res->Base->TextureDiffuse.get());
		_renderer.UpdateVisibleFrames();

		OnAnimationStarted();

		if ((_state & ActorState::ForceDisableCollisions) != ActorState::ForceDisableCollisions) {
			_state |= ActorState::IsDirty;
		}
	}

	void ActorBase::PreloadMetadataAsync(StringView path)
	{
		ContentResolver::Get().PreloadMetadataAsync(path);
	}

	void ActorBase::RequestMetadata(StringView path)
	{
		_metadata = ContentResolver::Get().RequestMetadata(path);
	}
	
#if !defined(WITH_COROUTINES)
	void ActorBase::RequestMetadataAsync(StringView path)
	{
		_metadata = ContentResolver::Get().RequestMetadata(path);
	}
#endif

	void ActorBase::UpdateFrozenState(float timeMult)
	{
		if (_frozenTimeLeft > 0.0f) {
			_frozenTimeLeft -= timeMult;
			if (_frozenTimeLeft > 0.0f) {
				// Cannot be directly in `ActorBase::HandleFrozenStateChange()` due to bug in `BaseSprite::updateRenderCommand()`,
				// it would be called before `BaseSprite::updateRenderCommand()` but after `SceneNode::transform()`
				_renderer.Initialize(ActorRendererType::FrozenMask);
			} else {
				_renderer.AnimPaused = false;
				_renderer.Initialize(ActorRendererType::Default);

				float scale = GetIceShrapnelScale();
				for (std::int32_t i = 0; i < 10; i++) {
					Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 10), Explosion::Type::IceShrapnel, scale);
				}

				Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.layer() + 90), Explosion::Type::SmokeWhite, scale);

				_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
			}
		}
	}

	void ActorBase::HandleFrozenStateChange(ActorBase* shot)
	{
		if (auto* freezerShot = runtime_cast<Weapons::FreezerShot>(shot)) {
			if (runtime_cast<ActorBase>(freezerShot->GetOwner()) != this) {
				_frozenTimeLeft = freezerShot->FrozenDuration();
				_renderer.AnimPaused = true;
				freezerShot->DecreaseHealth(INT32_MAX);
			}
		} else if (runtime_cast<Weapons::ToasterShot>(shot) || runtime_cast<Weapons::ShieldFireShot>(shot) ||
				   runtime_cast<Weapons::Thunderbolt>(shot)) {
			_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);
		}
	}

	bool ActorBase::IsInvulnerable()
	{
		return GetState(ActorState::IsInvulnerable);
	}

	std::int32_t ActorBase::GetHealth()
	{
		return _health;
	}

	void ActorBase::SetHealth(std::int32_t value)
	{
		_health = value;
	}

	std::int32_t ActorBase::GetMaxHealth()
	{
		return _maxHealth;
	}

	void ActorBase::DecreaseHealth(std::int32_t amount, ActorBase* collider)
	{
		if (amount == 0) {
			return;
		}

		if (amount > _health) {
			_health = 0;
		} else {
			_health -= amount;
		}

		if (_health <= 0) {
			OnPerish(collider);
		} else {
			OnHealthChanged(collider);
		}
	}

	bool ActorBase::MoveInstantly(Vector2f pos, MoveType type, TileCollisionParams& params)
	{
		Vector2f newPos;
		AABBf aabb;
		if ((type & MoveType::Relative) == MoveType::Relative) {
			if (pos == Vector2f::Zero) {
				return true;
			}
			newPos = _pos + pos;
			aabb = AABBInner + pos;
		} else {
			newPos = pos;
			aabb = AABBInner + (pos - _pos);
		}

		bool free = ((type & MoveType::Force) == MoveType::Force || _levelHandler->IsPositionEmpty(this, aabb, params));
		if (free) {
			AABBInner = aabb;
			_pos = newPos;
			if ((_state & ActorState::ForceDisableCollisions) != ActorState::ForceDisableCollisions) {
				_state |= ActorState::IsDirty;
			}
		}
		return free;
	}

	void ActorBase::AddExternalForce(float x, float y)
	{
		_externalForce.X += x;
		_externalForce.Y += y;
	}

	ActorBase::ActorRenderer::ActorRenderer(ActorBase* owner)
		: BaseSprite(nullptr, nullptr, 0.0f, 0.0f), AnimPaused(false), LoopMode(AnimationLoopMode::Loop), FirstFrame(0),
			FrameCount(0), AnimDuration(0.0f), AnimTime(0.0f), CurrentFrame(0), _owner(owner),
			_rendererType((ActorRendererType)-1), _rendererTransition(0.0f)
	{
		_type = ObjectType::Sprite;
		renderCommand_.setType(RenderCommand::Type::Sprite);
		Initialize(ActorRendererType::Default);
	}

	void ActorBase::ActorRenderer::Initialize(ActorRendererType type)
	{
		if (_rendererType == type) {
			return;
		}

		_rendererType = type;

		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless()) {
			return;
		}

		bool shaderChanged;
		switch (type) {
			case ActorRendererType::Outline: shaderChanged = renderCommand_.material().setShader(resolver.GetShader(PrecompiledShader::Outline)); break;
			case ActorRendererType::WhiteMask: shaderChanged = renderCommand_.material().setShader(resolver.GetShader(PrecompiledShader::WhiteMask)); break;
			case ActorRendererType::PartialWhiteMask: shaderChanged = renderCommand_.material().setShader(resolver.GetShader(PrecompiledShader::PartialWhiteMask)); break;
			case ActorRendererType::FrozenMask: shaderChanged = renderCommand_.material().setShader(resolver.GetShader(PrecompiledShader::FrozenMask)); break;
			default: shaderChanged = renderCommand_.material().setShaderProgramType(Material::ShaderProgramType::Sprite); break;
		}
		if (shaderChanged) {
			shaderHasChanged();
			renderCommand_.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			if (type == ActorRendererType::Outline || type == ActorRendererType::FrozenMask) {
				_rendererTransition = 0.0f;
				if (texture_ != nullptr) {
					Vector2i texSize = texture_->size();
					setColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
				}
			} else {
				setColor(Colorf::White);
			}
		}
	}

	void ActorBase::ActorRenderer::OnUpdate(float timeMult)
	{
		_owner->OnUpdate(timeMult);

		Vector2f pos = _owner->_pos;
		if (!PreferencesCache::UnalignedViewport || (_owner->_state & ActorState::IsDirty) != ActorState::IsDirty) {
			pos.X = std::floor(pos.X);
			pos.Y = std::floor(pos.Y);
		}
		setPosition(pos.X, pos.Y);

		if (IsAnimationRunning()) {
			switch (LoopMode) {
				case AnimationLoopMode::Loop:
					AnimTime += timeMult * FrameTimer::SecondsPerFrame;
					if (AnimTime > AnimDuration) {
						std::int32_t n = (std::int32_t)(AnimTime / AnimDuration);
						AnimTime -= AnimDuration * n;
						_owner->OnAnimationFinished();
					}
					break;
				case AnimationLoopMode::Once:
					float newAnimTime = AnimTime + timeMult * FrameTimer::SecondsPerFrame;
					if (AnimTime > AnimDuration) {
						_owner->OnAnimationFinished();
					}
					AnimTime = newAnimTime;
					break;
			}

			UpdateVisibleFrames();
		}

		switch (_rendererType) {
			case ActorRendererType::Outline:
				if (_rendererTransition < 0.8f) {
					_rendererTransition = std::min(_rendererTransition + timeMult * 0.06f, 0.8f);
					if (texture_ != nullptr) {
						Vector2i texSize = texture_->size();
						setColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
					}
				}
				break;
			case ActorRendererType::FrozenMask:
				if (_rendererTransition < 1.0f) {
					_rendererTransition = std::min(_rendererTransition + timeMult * 0.14f, 1.0f);
					if (texture_ != nullptr) {
						Vector2i texSize = texture_->size();
						setColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
					}
				}
				break;
		}

		BaseSprite::OnUpdate(timeMult);
	}

	bool ActorBase::ActorRenderer::OnDraw(RenderQueue& renderQueue)
	{
		if (_owner->OnDraw(renderQueue)) {
			return true;
		}

		return BaseSprite::OnDraw(renderQueue);
	}

	void ActorBase::ActorRenderer::textureHasChanged(Texture* newTexture)
	{
		if (_rendererType == ActorRendererType::Outline || _rendererType == ActorRendererType::FrozenMask) {
			if (newTexture != nullptr) {
				Vector2i texSize = newTexture->size();
				setColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
			}
		}
	}

	bool ActorBase::ActorRenderer::IsAnimationRunning()
	{
		if (FrameCount <= 0) {
			return false;
		}

		switch (LoopMode) {
			case AnimationLoopMode::FixedSingle:
				return false;
			case AnimationLoopMode::Loop:
				return !AnimPaused;
			case AnimationLoopMode::Once:
				return !AnimPaused && AnimTime < AnimDuration;
			default:
				return false;
		}
	}

	ActorRendererType ActorBase::ActorRenderer::GetRendererType() const
	{
		return _rendererType;
	}

	void ActorBase::ActorRenderer::UpdateVisibleFrames()
	{
		// Calculate visible frames
		CurrentFrame = 0;
		if (FrameCount > 0 && AnimDuration > 0.0f) {
			// Calculate currently visible frame
			float frameTemp = (FrameCount * AnimTime) / AnimDuration;
			CurrentFrame = (std::int32_t)frameTemp;

			// Normalize current frame when exceeding anim duration
			if (LoopMode == AnimationLoopMode::Once || LoopMode == AnimationLoopMode::FixedSingle) {
				CurrentFrame = std::clamp(CurrentFrame, 0, FrameCount - 1);
			} else {
				CurrentFrame = NormalizeFrame(CurrentFrame, 0, FrameCount);
			}
		}
		CurrentFrame = FirstFrame + std::clamp(CurrentFrame, 0, FrameCount - 1);

		// Set current animation frame rectangle
		std::int32_t col = CurrentFrame % FrameConfiguration.X;
		std::int32_t row = CurrentFrame / FrameConfiguration.X;
		setTexRect(Recti(FrameDimensions.X * col, FrameDimensions.Y * row, FrameDimensions.X, FrameDimensions.Y));
		setAbsAnchorPoint(Hotspot.X, Hotspot.Y);
	}

	int ActorBase::ActorRenderer::NormalizeFrame(std::int32_t frame, std::int32_t min, std::int32_t max)
	{
		if (frame >= min && frame < max) {
			return frame;
		} else if (frame < min) {
			return max + ((frame - min) % max);
		} else {
			return min + (frame % (max - min));
		}
	}
}