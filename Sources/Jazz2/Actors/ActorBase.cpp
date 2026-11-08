#include "ActorBase.h"
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
#include "../../nCine/Graphics/RHI/Rhi.h"

using namespace Jazz2::Tiles;
using namespace nCine;

namespace Jazz2::Actors
{
	namespace
	{
		/** @brief Returns the bit pattern of a float without passing it through a floating-point instruction */
		inline std::uint32_t FloatBits(float value)
		{
			// Not std::memcpy(), the Nintendo 64 compiler expands it into byte loads and stores
			union { float f; std::uint32_t i; } u;
			u.f = value;
			return u.i;
		}

		/**
		 * @brief Whether both coordinates are finite and within the range of @ref floorFast()
		 *
		 * Tested on the bit pattern, on the Nintendo 64 any FPU instruction on a NaN traps.
		 */
		inline bool IsUsablePosition(const Vector2f& pos)
		{
			// 2^22, infinities and NaNs compare above it too
			constexpr std::uint32_t MaxCoordinateBits = 0x4A800000u;
			return ((FloatBits(pos.X) & 0x7FFFFFFFu) < MaxCoordinateBits && (FloatBits(pos.Y) & 0x7FFFFFFFu) < MaxCoordinateBits);
		}

		/** @brief Whether a render type leaves the alpha channel of the sprite color to the actor's own fade */
		constexpr bool KeepsActorFadeInAlpha(ActorRendererType type)
		{
			// Outline and FrozenMask use the alpha for their own transition
			return (type != ActorRendererType::Outline && type != ActorRendererType::FrozenMask);
		}
	}

	ActorBase::ActorBase()
		: _levelHandler(nullptr), _metadata(nullptr), _currentAnimation(nullptr), _currentTransition(nullptr),
			_internalForceY(0.0f), _elasticity(0.0f), _friction(1.5f), _unstuckCooldown(0.0f), _frozenTimeLeft(0.0f), _maxHealth(1), _health(1),
			_spawnFrames(0.0f), _spawnPos(0, 0, 0), _spawnEventParams{}, _spawnEventType(EventType::Empty), _currentTransitionCancellable(false),
			_renderer(this), _collisionProxyID(Collisions::NullNode), _state(ActorState::None)
	{
	}

	ActorBase::~ActorBase()
	{
	}

#if defined(WITH_ANGELSCRIPT)
	void ActorBase::SetUpdateOverride(std::unique_ptr<IActorUpdateOverride> value)
	{
		_updateOverride = Death::move(value);
	}
#endif

	void ActorBase::EmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
#if defined(WITH_ANGELSCRIPT)
		if DEATH_UNLIKELY(_updateOverride != nullptr) {
			if (!_updateOverride->OwnsLights()) {
				OnEmitLights(lights);
			}
			_updateOverride->OnEmitLights(this, lights);
			return;
		}
#endif
		OnEmitLights(lights);
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
		_renderer.SetFlippedX(value);
		
		// The anchor follows the flip
		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res != nullptr) {
			_renderer.UpdateAnchor();
		}
	}

	void ActorBase::SetParent(SceneNode* parent)
	{
		_renderer.SetParent(parent);
	}

	Task<bool> ActorBase::OnActivated(const ActorActivationDetails& details)
	{
		_state |= details.State | ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors | ActorState::ApplyGravitation;
		_levelHandler = details.LevelHandler;
		_pos = _frameStartPos = Vector2f((float)details.Pos.X, (float)details.Pos.Y);
		_originTile = Vector2i((std::int32_t)details.Pos.X / 32, (std::int32_t)details.Pos.Y / 32);
		if ((details.State & (ActorState::IsCreatedFromEventMap | ActorState::IsFromGenerator)) != ActorState::None) {
			// An off-grid event's object isn't in its own tile
			if (auto* events = _levelHandler->EventMap(); events != nullptr && events->GetOffGridEventCount() > 0) {
				_originTile = events->GetOriginTile(Vector2i(details.Pos.X, details.Pos.Y), details.Type);
			}
		}
		_spawnFrames = _levelHandler->GetElapsedFrames();
		_spawnPos = details.Pos;

		// Remembered to respawn the object from a snapshot, only EventSpawner sets the type (with full-size params)
		_spawnEventType = details.Type;
		if (details.Type != EventType::Empty && details.Params != nullptr) {
			std::memcpy(_spawnEventParams, details.Params, sizeof(_spawnEventParams));
		}

		std::uint16_t layer = (std::uint16_t)details.Pos.Z;
		_renderer.SetLayer(layer);

		bool success = async_await OnActivatedAsync(details);

		// An offset applied while activating is placement, not a travelled path
		ResetPathTracking();

		_renderer.SetPosition(roundFast(_pos.X), roundFast(_pos.Y));

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

	namespace
	{
		// Instance flags that are part of the live state, the rest come from spawning or are internal
		constexpr ActorState SerializableStateMask = ActorState::IsInvulnerable | ActorState::CanJump | ActorState::CanBeFrozen |
			ActorState::IsFacingLeft | ActorState::CollideWithTileset | ActorState::CollideWithOtherActors |
			ActorState::CollideWithSolidObjects | ActorState::ApplyGravitation | ActorState::IsSolidObject |
			ActorState::SkipPerPixelCollisions | ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjectsBelow |
			ActorState::ExcludeSimilar;
	}

	bool ActorBase::IsSerializable() const
	{
		static_assert(SpawnParamsSize == Events::EventSpawner::SpawnParamsSize, "Size of spawn parameters mismatch");

		return (_spawnEventType != EventType::Empty);
	}

	void ActorBase::OnSerializeState(Stream& dest)
	{
		dest.WriteValueAsLE<float>(_pos.X);
		dest.WriteValueAsLE<float>(_pos.Y);
		dest.WriteValueAsLE<float>(_speed.X);
		dest.WriteValueAsLE<float>(_speed.Y);
		dest.WriteValueAsLE<float>(_externalForce.X);
		dest.WriteValueAsLE<float>(_externalForce.Y);
		dest.WriteValueAsLE<float>(_internalForceY);
		dest.WriteValueAsLE<float>(_elasticity);
		dest.WriteValueAsLE<float>(_friction);
		dest.WriteValueAsLE<float>(_frozenTimeLeft);
		dest.WriteVariableInt32(_health);
		dest.WriteVariableInt32(_maxHealth);
		dest.WriteVariableUint32((std::uint32_t)(_state & SerializableStateMask));

		// Transitions can't be restored without their callback
		dest.WriteVariableUint32(_currentAnimation != nullptr ? (std::uint32_t)_currentAnimation->State : UINT32_MAX);
		dest.WriteValueAsLE<float>(_currentTransition == nullptr ? _renderer.AnimTime : 0.0f);

		std::uint8_t rendererFlags = 0;
		if (_renderer.IsDrawEnabled()) rendererFlags |= 0x01;
		if (_renderer.AnimPaused) rendererFlags |= 0x02;
		dest.WriteValue<std::uint8_t>(rendererFlags);
		dest.WriteValueAsLE<std::uint16_t>(_renderer.GetLayer());
		dest.WriteValueAsLE<float>(_renderer.GetRotation());
		dest.WriteValueAsLE<float>(_renderer.GetScale().X);
		dest.WriteValueAsLE<float>(_renderer.GetScale().Y);
		dest.WriteValueAsLE<float>(_renderer.GetAlpha());
	}

	void ActorBase::OnDeserializeState(Stream& src)
	{
		_pos.X = src.ReadValueAsLE<float>();
		_pos.Y = src.ReadValueAsLE<float>();
		_speed.X = src.ReadValueAsLE<float>();
		_speed.Y = src.ReadValueAsLE<float>();
		_externalForce.X = src.ReadValueAsLE<float>();
		_externalForce.Y = src.ReadValueAsLE<float>();
		_internalForceY = src.ReadValueAsLE<float>();
		_elasticity = src.ReadValueAsLE<float>();
		_friction = src.ReadValueAsLE<float>();
		_frozenTimeLeft = src.ReadValueAsLE<float>();
		_health = src.ReadVariableInt32();
		_maxHealth = src.ReadVariableInt32();
		ActorState state = (ActorState)src.ReadVariableUint32();
		_state = (_state & ~SerializableStateMask) | (state & SerializableStateMask);

		std::uint32_t animState = src.ReadVariableUint32();
		float animTime = src.ReadValueAsLE<float>();
		std::uint8_t rendererFlags = src.ReadValue<std::uint8_t>();
		std::uint16_t layer = src.ReadValueAsLE<std::uint16_t>();
		float rotation = src.ReadValueAsLE<float>();
		float scaleX = src.ReadValueAsLE<float>();
		float scaleY = src.ReadValueAsLE<float>();
		float alpha = src.ReadValueAsLE<float>();

		// A transition from the activation has a callback that doesn't fit the restored state
		ForceCancelTransition();
		if (animState != UINT32_MAX) {
			if (_currentAnimation == nullptr || (std::uint32_t)_currentAnimation->State != animState) {
				_currentAnimation = nullptr;
				SetAnimation((AnimState)animState);
			}
			if (_currentAnimation != nullptr && _renderer.AnimDuration >= 0.0f) {
				_renderer.AnimTime = std::clamp(animTime, 0.0f, _renderer.AnimDuration);
			}
		}

		_renderer.SetDrawEnabled((rendererFlags & 0x01) != 0);
		_renderer.AnimPaused = ((rendererFlags & 0x02) != 0 || _frozenTimeLeft > 0.0f);
		_renderer.SetLayer(layer);
		_renderer.SetRotation(rotation);
		_renderer.SetScale(scaleX, scaleY);
		_renderer.SetAlphaF(alpha);
		_renderer.SetFlippedX(IsFacingLeft());
		if (_currentAnimation != nullptr) {
			_renderer.UpdateAnchor();
		}

		ResetPathTracking();
		_renderer.SetPosition(roundFast(_pos.X), roundFast(_pos.Y));
		OnUpdateHitbox();

		if ((_state & ActorState::ForceDisableCollisions) != ActorState::ForceDisableCollisions) {
			_state |= ActorState::IsDirty;
		}
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
			if DEATH_UNLIKELY(_metadata->BoundingBox == Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue)) {
				// Metadata without a box (first sheet unreadable) falls back to the current sprite
				if (_currentAnimation != nullptr && _currentAnimation->Base != nullptr) {
					Vector2i boundingBox = GenericGraphicResource::GetDefaultBoundingBox(_currentAnimation->Base->FrameDimensions);
					UpdateHitbox(boundingBox.X, boundingBox.Y);
				}
				return;
			}
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
		// Can be overridden
	}

	void ActorBase::OnHitCeiling(float timeMult)
	{
		// Can be overridden
	}

	void ActorBase::OnHitWall(float timeMult)
	{
		// Can be overridden
	}

	bool ActorBase::OnHandleCollision(ActorBase* other)
	{
		if (GetState(ActorState::CanBeFrozen)) {
			HandleFrozenStateChange(other);
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

		_speed.X = std::clamp(_speed.X, -_horizontalSpeedLimit, _horizontalSpeedLimit);
		_speed.Y = std::clamp(_speed.Y + accelY, -_verticalSpeedLimit, _verticalSpeedLimit);

		float effectiveSpeedX, effectiveSpeedY;
		if (_frozenTimeLeft > 0.0f) {
			effectiveSpeedX = std::clamp(GetFrozenMomentumX() + _externalForce.X * timeMult, -16.0f, 16.0f);
			effectiveSpeedY = ((_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation
				? (_speed.Y + 0.5f * accelY)
				: std::clamp(((currentGravity * 2.0f) + _internalForceY) * timeMult, -16.0f, 16.0f));
		} else {
			effectiveSpeedX = _speed.X + _externalForce.X * timeMult;
			effectiveSpeedY = _speed.Y + 0.5f * accelY;
		}
		// Half a step of gravity (velocity Verlet) keeps jump height frame-rate independent, only in flight
		// and before the rise cap, otherwise a capped rise is slowed twice
		if (currentGravity > 0.0f && (_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation &&
			(_speed.Y < 0.0f || (_state & ActorState::CanJump) != ActorState::CanJump)) {
			// No Euler bias here, it breaks the fitted rises (see Docs/MovementAccuracyReference.dox)
			effectiveSpeedY += 0.5f * GetGravityModifier(currentGravity, /*isRising:*/_speed.Y < 0.0f) * timeMult;
		}

		// Only the applied rise is capped, `_speed.Y` keeps its momentum, as the original's springs and jumps
		if (_maxRiseSpeed > 0.0f && effectiveSpeedY < -_maxRiseSpeed) {
			effectiveSpeedY = -_maxRiseSpeed;
		}
		// Same on the horizontal axis, as the original's dash
		if (_maxAppliedSpeedX > 0.0f) {
			effectiveSpeedX = std::clamp(effectiveSpeedX, -_maxAppliedSpeedX, _maxAppliedSpeedX);
		}

		effectiveSpeedX *= timeMult;
		effectiveSpeedY *= timeMult;

		Vector2f posBefore = _pos;
		bool movementAttempted = (std::abs(effectiveSpeedX) > 0.0f || std::abs(effectiveSpeedY) > 0.0f);
		if (movementAttempted) {
			// Long moves are split into sub-steps (per 60 Hz frame and at most `MaxMovementStep` long), so actors
			// can't pass through thin geometry or get stuck in walls at low frame rates
			float distance = std::max(std::abs(effectiveSpeedX), std::abs(effectiveSpeedY));
			std::int32_t substepCount = 1;
			if (distance > MaxMovementStep) {
				substepCount = std::max<std::int32_t>((std::int32_t)timeMult, 1);
				if ((float)substepCount < timeMult) {
					substepCount++;
				}
				if (distance > substepCount * MaxMovementStep) {
					substepCount = (std::int32_t)(distance / MaxMovementStep) + 1;
				}
				if (substepCount > MaxMovementSubsteps) {
					substepCount = MaxMovementSubsteps;
				}
			}
			float stepX = effectiveSpeedX / substepCount;
			float stepY = effectiveSpeedY / substepCount;

			MovementResult result = {};
			for (std::int32_t i = 0; i < substepCount; i++) {
				MovementResult stepResult = {};
				bool stepMoved = TryMoveSubstep(stepX, stepY, currentGravity, currentElasticity, params, stepResult);

				result.HitWall |= stepResult.HitWall;
				result.HitCeiling |= stepResult.HitCeiling;
				result.HitFloor |= stepResult.HitFloor;

				// Don't keep pushing into whatever was hit for the rest of the frame
				if (stepResult.HitWall) {
					stepX = 0.0f;
				}
				if (stepResult.HitCeiling || stepResult.HitFloor) {
					stepY = 0.0f;
				}
				if (!stepMoved || (stepX == 0.0f && stepY == 0.0f)) {
					break;
				}
			}

			// Callbacks fire once per frame
			bool hitCalled = false;
			if (result.HitCeiling) {
				OnHitCeiling(timeMult);
				hitCalled = true;
			} else if (result.HitFloor) {
				OnHitFloor(timeMult);
				hitCalled = true;
			}
			if (result.HitWall && !hitCalled) {
				OnHitWall(timeMult);
			}
		}

		// An actor that didn't move may be inside solid geometry, check it unless something else places it
		if (_pos == posBefore && (movementAttempted || (_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation)) {
			TryUnstuck();
			if (_speed.Y > 0.0f && (_state & ActorState::CollideWithTilesetLegacy) == ActorState::CollideWithTilesetLegacy) {
				// A player held by the head in a ceiling creeps up into it, measured in JJ2+
				AABBf head = AABBInner;
				head.T = head.B - 23.0f;
				head.B = head.T + 5.0f;
				TileCollisionParams headParams = { TileDestructType::None, false };
				if (auto* tiles = _levelHandler->TileMap(); tiles != nullptr && !tiles->IsTileEmpty(head, headParams)) {
					MoveInstantly(Vector2f(0.0f, -_speed.Y * timeMult / 16.0f), MoveType::Relative | MoveType::Force);
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

		// Handle gravity and collision with floor, the state is re-checked because a hit callback may turn gravity off
		if (currentGravity > 0.0f && (_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation) {
			if (_speed.Y >= 0.0f) {
				// Actor is going down
				AABBf aabb = AABBInner;
				aabb.B += CollisionCheckStep;
				if ((_state & ActorState::CollideWithTilesetReduced) == ActorState::CollideWithTilesetReduced && aabb.B - aabb.T >= 20.0f) {
					// Only the bottom of a reduced hitbox finds the floor, for a player just its feet (measured in JJ2+)
					aabb.T = std::max(aabb.T, aabb.B - ((_state & ActorState::CollideWithTilesetLegacy) == ActorState::CollideWithTilesetLegacy ? 2.0f : std::max(14.0f, (aabb.B - aabb.T) - 10.0f)));
				}
				if (_levelHandler->IsPositionEmpty(this, aabb, params)) {
					// There is still some space below - only apply gravity
					SetState(ActorState::CanJump, false);
					_speed.Y += GetGravityModifier(currentGravity, /*isRising:*/false) * timeMult;
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
				_speed.Y += GetGravityModifier(currentGravity, /*isRising:*/true) * timeMult;
			}

			_externalForce.Y = std::min(_externalForce.Y + currentGravity * 0.33f * timeMult, 0.0f);
			_internalForceY = std::min(_internalForceY + currentGravity * 0.33f * timeMult, 0.0f);
		}
	}

	bool ActorBase::IsSupportedAfterMove(float stepX, float stepY)
	{
		// Tells a downhill slope from a gap, with own params so the probe can't destroy tiles
		AABBf aabb = AABBInner + Vector2f(stepX, stepY);
		aabb.T = aabb.B;
		aabb.B += CollisionCheckStep;
		TileCollisionParams probeParams = { TileDestructType::None, true };
		return !_levelHandler->IsPositionEmpty(this, aabb, probeParams);
	}

	bool ActorBase::TryMoveSubstep(float stepX, float stepY, float currentGravity, float currentElasticity, TileCollisionParams& params, MovementResult& result)
	{
		if (GetState(ActorState::CanJump | ActorState::ApplyGravitation)) {
			// Ground-bound movement tries angles from a bit over 45 degrees down to 45 degrees up to stick to slopes.
			// A downward candidate needs ground under it, otherwise gaps are fallen into instead of jumped over.
			bool success = false;
			float maxYDiff = std::max(3.0f, std::abs(stepX) + 2.5f);
			for (float yDiff = maxYDiff + stepY; yDiff >= -maxYDiff + stepY; yDiff -= CollisionCheckStep) {
				if (yDiff > stepY && !IsSupportedAfterMove(stepX, yDiff)) {
					continue;
				}
				if (MoveInstantly(Vector2f(stepX, yDiff), MoveType::Relative, params)) {
					success = true;
					break;
				}
			}

			if (success) {
				return true;
			}

			// Also try to move horizontally as far as possible
			float xDiff = std::abs(stepX);
			if (xDiff > CollisionCheckStep) {
				float sign = (stepX > 0.0f ? 1.0f : -1.0f);
				for (; xDiff > CollisionCheckStep; xDiff -= CollisionCheckStep) {
					if (MoveInstantly(Vector2f(xDiff * sign, 0.0f), MoveType::Relative, params)) {
						// Slid up against a wall, still counts as hitting it
						success = true;
						break;
					}
				}
			}

			// Facing a wall, its horizontal speed is spent (or reflected) as in the airborne branch
			if (xDiff > CollisionCheckStep || (xDiff > 0.0f && currentElasticity > 0.0f)) {
				_speed.X = -(currentElasticity * _speed.X);
				_externalForce.X = 0.0f;
			}
			result.HitWall = true;
			return success;
		}

		// Airborne movement is handled here
		// First, attempt to move directly based on the current speed values
		if (MoveInstantly(Vector2f(stepX, stepY), MoveType::Relative, params)) {
			return true;
		}

		// First, attempt to move horizontally as much as possible
		float xDiff = std::abs(stepX);
		// Mostly-horizontal airborne movement climbs gentle upslopes instead of stopping at their foot
		bool movedHorizontally = MoveInstantly(Vector2f(stepX, 0.0f), MoveType::Relative, params);
		// `>=` so a jump into a 45 degree slope keeps its momentum, as in the original
		if (!movedHorizontally && std::abs(stepX) >= std::abs(stepY)) {
			float maxClimb = std::abs(stepX) + 2.5f;
			// Landing on a ledge edge reaches `_landingTolerance`, only when really descending
			// so walls don't get easier to mount
			if (stepY > CollisionCheckStep && _landingTolerance > 0.0f) {
				maxClimb = std::max(maxClimb, _landingTolerance);
			}
			for (float yUp = CollisionCheckStep; yUp <= maxClimb; yUp += CollisionCheckStep) {
				if (MoveInstantly(Vector2f(stepX, -yUp), MoveType::Relative, params)) {
					movedHorizontally = true;
					break;
				}
			}
		}
		if (!movedHorizontally) {
			for (; xDiff > std::numeric_limits<float>::epsilon(); xDiff -= CollisionCheckStep) {
				if (MoveInstantly(Vector2f(std::copysign(xDiff, stepX), 0.0f), MoveType::Relative, params)) {
					break;
				}
			}
		}

		// Then, try the same vertically
		float yDiff = std::abs(stepY);
		for (; yDiff > std::numeric_limits<float>::epsilon(); yDiff -= CollisionCheckStep) {
			float yDiffSigned = std::copysign(yDiff, stepY);
			if (MoveInstantly(Vector2f(0.0f, yDiffSigned), MoveType::Relative, params) ||
				// Add horizontal tolerance
				MoveInstantly(Vector2f(yDiff * 0.2f, yDiffSigned), MoveType::Relative, params) ||
				MoveInstantly(Vector2f(yDiff * -0.2f, yDiffSigned), MoveType::Relative, params)) {
				break;
			}
		}

		if (stepY < 0.0f) {
			if (-yDiff > stepY) {
				// Reset speed and also internal force, mainly because of player jump
				_speed.Y = 0.0f;
				if (_internalForceY < 0.0f) {
					_internalForceY = 0.0f;
				}
				result.HitCeiling = true;
			}
		} else if (stepY > 0.0f && yDiff < stepY && currentGravity <= 0.0f) {
			// If there is no gravity and actor is touching floor, the callback wouldn't be called otherwise
			result.HitFloor = true;
		}

		// If the actor didn't move all the way horizontally, it hit a wall (or was already touching it)
		if (xDiff < std::abs(stepX) * 0.3f) {
			if (xDiff > 0.0f && currentElasticity > 0.0f) {
				_speed.X = -(currentElasticity * _speed.X);
				_externalForce.X = 0.0f;
			}
			result.HitWall = true;
		}

		return (xDiff > std::numeric_limits<float>::epsilon() || yDiff > std::numeric_limits<float>::epsilon());
	}

	bool ActorBase::TryUnstuck()
	{
		if (_unstuckCooldown > 0.0f || !GetState(ActorState::CollideWithTileset)) {
			return false;
		}

		// Only the lower part of the hitbox decides, the upper part may overlap a low ceiling
		AABBf aabb = AABBInner;
		float t = aabb.B - 14.0f;
		if (aabb.T < t) {
			aabb.T = t;
		}
		TileCollisionParams paramsProbe = { TileDestructType::None, true };
		if (_levelHandler->IsPositionEmpty(this, aabb, paramsProbe)) {
			// Throttled, nearly every actor at rest isn't stuck
			_unstuckCooldown = UnstuckRetryNotStuck;
			return false;
		}

		// Probe outwards and take the first free spot, up first, then sideways, down and diagonals
		static const Vector2f Directions[] = {
			{ 0.0f, -1.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f },
			{ -0.7f, -0.7f }, { 0.7f, -0.7f }, { -0.7f, 0.7f }, { 0.7f, 0.7f }
		};

		// Non-destructive, the search must not carve tiles or award score
		TileCollisionParams paramsEscape = { TileDestructType::None, true };

		// A player is only lifted out of tiles, one in a wall stays there (measured in JJ2+)
		std::size_t directionCount = sizeof(Directions) / sizeof(Directions[0]);
		if ((_state & ActorState::CollideWithTilesetLegacy) == ActorState::CollideWithTilesetLegacy) {
			auto* tiles = _levelHandler->TileMap();
			if (tiles != nullptr && !tiles->IsTileEmpty(aabb, paramsProbe)) {
				directionCount = 1;
			}
		}
		for (float distance = 2.0f; distance <= MaxUnstuckDistance; distance += 2.0f) {
			for (std::size_t i = 0; i < directionCount; i++) {
				const Vector2f& direction = Directions[i];
				if (MoveInstantly(direction * distance, MoveType::Relative, paramsEscape)) {
					// The escape is a relocation, not a travelled path
					ResetPathTracking();
					_unstuckCooldown = UnstuckCooldown;
					return true;
				}
			}
		}

		// Nowhere to go, retry sooner than after an escape, the trap may move away
		_unstuckCooldown = UnstuckRetryTrapped;
		return false;
	}

	void ActorBase::UpdateHitbox(std::int32_t w, std::int32_t h)
	{
#if defined(WITH_ANGELSCRIPT)
		// A script-driven object can size its own hitbox
		if DEATH_UNLIKELY(_updateOverride != nullptr && _updateOverride->OnUpdateHitbox(this)) {
			return;
		}
#endif

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
			// Collision base at the bottom of the sprite's pixels, without the frame border
			AABBInner = AABBf(
				_pos.X - (w / 2),
				_pos.Y - base->Hotspot.Y + base->FrameDimensions.Y - GenericGraphicResource::FrameBorder - h,
				_pos.X + (w / 2),
				_pos.Y - base->Hotspot.Y + base->FrameDimensions.Y - GenericGraphicResource::FrameBorder
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
#if defined(WITH_ANGELSCRIPT)
		if DEATH_UNLIKELY(_updateOverride != nullptr && _updateOverride->OwnsDebris()) {
			return;
		}
#endif
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
		// Indexed debris is recolored through the palette offset (-1 = baked)
		std::int32_t paletteDebrisOffset = (((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed) ? (std::int32_t)res->PaletteOffset : -1);

		float x = _pos.X - res->Base->Hotspot.X;
		float y = _pos.Y - res->Base->Hotspot.Y;

		if (effect == ParticleDebrisEffect::Fire) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->GetSize();
			// Walk only the frame's own area, offset to where the trimmed frame is drawn
			const Recti debrisRect = res->Base->GetFrameRect(_renderer.CurrentFrame);
			const Vector2i frameOffset = res->Base->GetFrameOffset(_renderer.CurrentFrame);

			// Coarser step for big sprites, consoles can't afford thousands of particles
			const std::int32_t step = Tiles::TileMap::GetParticleDebrisStep(DebrisSize, debrisRect.W, debrisRect.H);
			const float particleSize = (float)(step - 1);

			for (std::int32_t fy = 0; fy < debrisRect.H; fy += step) {
				for (std::int32_t fx = 0; fx < debrisRect.W; fx += step) {
					float currentSize = particleSize * Random().FastFloat(0.8f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - frameOffset.X - fx : frameOffset.X + fx), y + frameOffset.Y + fy);
					debris.Depth = _renderer.GetLayer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - debrisRect.W / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(0.5f, 2.0f) / debrisRect.W,
						 Random().FastFloat(0.0f, 0.2f));
					debris.Acceleration = Vector2(0.0f, 0.06f);

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.001f;

					debris.Time = 320.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = ((float(debrisRect.X) + (float)fx) / float(texSize.X));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = ((float(debrisRect.Y) + (float)fy) / float(texSize.Y));

					debris.DiffuseTexture = texture;
					debris.PaletteOffset = paletteDebrisOffset;
					debris.Flags = Tiles::TileMap::DebrisFlags::Bounce;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Lightning) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->GetSize();
			// Walk only the frame's own area, offset to where the trimmed frame is drawn
			const Recti debrisRect = res->Base->GetFrameRect(_renderer.CurrentFrame);
			const Vector2i frameOffset = res->Base->GetFrameOffset(_renderer.CurrentFrame);

			// Coarser step for big sprites, consoles can't afford thousands of particles
			const std::int32_t step = Tiles::TileMap::GetParticleDebrisStep(DebrisSize, debrisRect.W, debrisRect.H);
			const float particleSize = (float)(step - 1);

			for (std::int32_t fy = 0; fy < debrisRect.H; fy += step) {
				for (std::int32_t fx = 0; fx < debrisRect.W; fx += step) {
					float currentSize = particleSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - frameOffset.X - fx : frameOffset.X + fx), y + frameOffset.Y + fy);
					debris.Depth = _renderer.GetLayer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - debrisRect.W / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / debrisRect.W,
						 ((fy - debrisRect.H / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 4.0f) / debrisRect.H);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(10.0f, 50.0f);

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = ((float(debrisRect.X) + (float)fx) / float(texSize.X));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = ((float(debrisRect.Y) + (float)fy) / float(texSize.Y));

					debris.DiffuseTexture = texture;
					debris.PaletteOffset = paletteDebrisOffset;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Dissolve) {
			constexpr int DebrisSize = 2;

			Vector2i texSize = texture->GetSize();
			// Walk only the frame's own area, offset to where the trimmed frame is drawn
			const Recti debrisRect = res->Base->GetFrameRect(_renderer.CurrentFrame);
			const Vector2i frameOffset = res->Base->GetFrameOffset(_renderer.CurrentFrame);

			float x = _pos.X - res->Base->Hotspot.X;
			float y = _pos.Y - res->Base->Hotspot.Y;

			// Coarser step for big sprites, consoles can't afford thousands of particles
			const std::int32_t step = Tiles::TileMap::GetParticleDebrisStep(DebrisSize, debrisRect.W, debrisRect.H);
			const float particleSize = (float)(step - 1);

			for (std::int32_t fy = 0; fy < debrisRect.H; fy += step) {
				for (std::int32_t fx = 0; fx < debrisRect.W; fx += step) {
					float currentSize = particleSize * Random().FastFloat(0.4f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris = { };
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - frameOffset.X - fx : frameOffset.X + fx), y + frameOffset.Y + fy);
					debris.Depth = _renderer.GetLayer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - debrisRect.W / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / debrisRect.W,
							((fy - debrisRect.H / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(2.0f, 5.0f) / debrisRect.H);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.2f;
					debris.ScaleSpeed = -0.004f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.01f;

					debris.Time = 280.0f;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = ((float(debrisRect.X) + (float)fx) / float(texSize.X));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = ((float(debrisRect.Y) + (float)fy) / float(texSize.Y));

					debris.DiffuseTexture = texture;
					debris.PaletteOffset = paletteDebrisOffset;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::StandardInWater) {
			constexpr std::int32_t DebrisSize = 3;

			Vector2i texSize = texture->GetSize();
			// Walk only the frame's own area, offset to where the trimmed frame is drawn
			const Recti debrisRect = res->Base->GetFrameRect(_renderer.CurrentFrame);
			const Vector2i frameOffset = res->Base->GetFrameOffset(_renderer.CurrentFrame);

			// Coarser step for big sprites, consoles can't afford thousands of particles
			const std::int32_t step = Tiles::TileMap::GetParticleDebrisStep(DebrisSize, debrisRect.W, debrisRect.H);
			const float particleSize = (float)(step - 1);

			for (std::int32_t fy = 0; fy < debrisRect.H; fy += step) {
				for (std::int32_t fx = 0; fx < debrisRect.W; fx += step) {
					float currentSize = particleSize * Random().FastFloat(0.2f, 1.1f);

					Tiles::TileMap::DestructibleDebris debris{};
					debris.Pos = Vector2f(x + (IsFacingLeft() ? res->Base->FrameDimensions.X - frameOffset.X - fx : frameOffset.X + fx), y + frameOffset.Y + fy);
					debris.Depth = _renderer.GetLayer();
					debris.Size = Vector2f(currentSize, currentSize);
					debris.Speed = Vector2f(((fx - debrisRect.W / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / debrisRect.W,
						 ((fy - debrisRect.H / 2) + Random().FastFloat(-2.0f, 2.0f)) * (IsFacingLeft() ? -1.0f : 1.0f) * Random().FastFloat(1.0f, 3.0f) / debrisRect.H);
					debris.Acceleration = Vector2f::Zero;

					debris.Scale = 1.0f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = -0.004f;

					debris.Time = Random().FastFloat(300.0f, 340.0f);;

					debris.TexScaleX = (currentSize / float(texSize.X));
					debris.TexBiasX = ((float(debrisRect.X) + (float)fx) / float(texSize.X));
					debris.TexScaleY = (currentSize / float(texSize.Y));
					debris.TexBiasY = ((float(debrisRect.Y) + (float)fy) / float(texSize.Y));

					debris.DiffuseTexture = texture;
					debris.PaletteOffset = paletteDebrisOffset;
					debris.Flags = Tiles::TileMap::DebrisFlags::Disappear;

					tilemap->CreateDebris(debris);
				}
			}
			return;
		}

		if (effect == ParticleDebrisEffect::Frozen) {
			for (std::int32_t i = 0; i < 20; i++) {
				Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 10), Explosion::Type::IceShrapnel,
					1.0f, Vector2f(GetFrozenMomentumX(), _speed.Y));
			}

			_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
			return;
		}

		float length = speed.Length();
		Vector2f force = (length > 0.0f ? (speed / length * 1.4f) : Vector2f::Zero);
		tilemap->CreateParticleDebris(res, Vector3f(_pos.X, _pos.Y, (float)_renderer.GetLayer()), force, _renderer.CurrentFrame, IsFacingLeft());
	}

	void ActorBase::CreateSpriteDebris(AnimState state, std::int32_t count, float elasticity)
	{
#if defined(WITH_ANGELSCRIPT)
		if DEATH_UNLIKELY(_updateOverride != nullptr && _updateOverride->OwnsDebris()) {
			return;
		}
#endif
		_levelHandler->HandleCreateSpriteDebris(this, state, count, elasticity);

		auto* tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr && _metadata != nullptr) {
			auto* res = _metadata->FindAnimation(state);
			if (res != nullptr) {
				tilemap->CreateSpriteDebris(res, Vector3f(_pos.X, _pos.Y, (float)_renderer.GetLayer()), count, elasticity);
			}
		}
	}

	float ActorBase::GetIceShrapnelScale() const
	{
		return 1.0f;
	}

	std::shared_ptr<AudioBufferPlayer> ActorBase::PlaySfx(StringView identifier, float gain, float pitch, SfxFlags flags)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			AudioBuffer* buffer;
			// The samples may not be loaded yet
			if (ContentResolver::Get().ResolveSound(it->second)) {
				std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
				buffer = &it->second.Buffers[idx]->Buffer;
			} else {
				buffer = nullptr;
			}

			return _levelHandler->PlaySfx(this, identifier, buffer, Vector3f(_pos.X, _pos.Y, 0.0f), false, gain, pitch, flags);
		}

		// Level weapon sounds are available to any actor
		if (auto* sound = ContentResolver::Get().FindWeaponSound(identifier)) {
			AudioBuffer* buffer = (!sound->Buffers.empty() ? &sound->Buffers[0]->Buffer : nullptr);
			return _levelHandler->PlaySfx(this, identifier, buffer, Vector3f(_pos.X, _pos.Y, 0.0f), false, gain, pitch, flags);
		}

		return nullptr;
	}

	void ActorBase::SetAnimationFrame(std::int32_t frame)
	{
		_renderer.AnimPaused = true;
		if (_renderer.FrameCount <= 0) {
			return;
		}
		frame = std::clamp<std::int32_t>(frame, 0, _renderer.FrameCount - 1);
		if (_renderer.AnimDuration <= 0.0f) {
			_renderer.AnimDuration = float(_renderer.FrameCount);
		}
		_renderer.AnimTime = (float(frame) + 0.5f) * _renderer.AnimDuration / float(_renderer.FrameCount);
		_renderer.UpdateVisibleFrames();
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
					auto oldCallback = Death::move(_currentTransitionCallback);
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
			auto oldCallback = Death::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		_currentTransition = anim;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = Death::move(callback);		
		RefreshAnimation();

		return true;
	}

	void ActorBase::CancelTransition()
	{
		if (_currentTransition != nullptr && _currentTransitionCancellable) {
			if (_currentTransitionCallback) {
				auto oldCallback = Death::move(_currentTransitionCallback);
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
				auto oldCallback = Death::move(_currentTransitionCallback);
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

		if ((perPixel1 && std::abs(_renderer.GetRotation()) > 0.1f) || (perPixel2 && std::abs(other->_renderer.GetRotation()) > 0.1f)) {
			if (!perPixel1 && std::abs(other->_renderer.GetRotation()) > 0.1f) {
				return other->IsCollidingWithAngled(AABBInner);
			} else if (!perPixel2 && std::abs(_renderer.GetRotation()) > 0.1f) {
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

		// The box must match the drawn frame (not the cell), the per-pixel test samples the mask by it
		const std::int32_t maskFrame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
		const std::int32_t maskFrame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
		const bool facingLeft1 = GetState(ActorState::IsFacingLeft);
		const bool facingLeft2 = other->GetState(ActorState::IsFacingLeft);
		const Recti frameRect1 = res1->Base->GetFrameRect(maskFrame1);
		const Recti frameRect2 = res2->Base->GetFrameRect(maskFrame2);
		const Vector2i frameAnchor1 = res1->Base->GetFrameAnchor(maskFrame1, facingLeft1);
		const Vector2i frameAnchor2 = res2->Base->GetFrameAnchor(maskFrame2, facingLeft2);

		AABBf aabb1, aabb2;
		if (!perPixel1) {
			aabb1 = AABBInner;
		} else {
			aabb1 = AABBf(_pos.X - frameAnchor1.X, _pos.Y - frameAnchor1.Y,
				(float)frameRect1.W, (float)frameRect1.H);
			aabb1.B += aabb1.T;
			aabb1.R += aabb1.L;
		}
		if (!perPixel2) {
			aabb2 = other->AABBInner;
		} else {
			aabb2 = AABBf(other->_pos.X - frameAnchor2.X, other->_pos.Y - frameAnchor2.Y,
				(float)frameRect2.W, (float)frameRect2.H);
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
			std::int32_t x1, y1, x2, y2, xs, dx, dy, stride, frameWidth;
			if (perPixel1) {
				res = res1;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = facingLeft1;

				x1 = (std::int32_t)std::max(inter.L, other->AABBInner.L);
				y1 = (std::int32_t)std::max(inter.T, other->AABBInner.T);
				x2 = (std::int32_t)std::min(inter.R, other->AABBInner.R);
				y2 = (std::int32_t)std::min(inter.B, other->AABBInner.B);

				xs = (std::int32_t)aabb1.L;

				dx = frameRect1.X;
				dy = frameRect1.Y - (std::int32_t)aabb1.T;
				frameWidth = frameRect1.W;

			} else {
				res = res2;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = facingLeft2;

				x1 = (std::int32_t)std::max(inter.L, AABBInner.L);
				y1 = (std::int32_t)std::max(inter.T, AABBInner.T);
				x2 = (std::int32_t)std::min(inter.R, AABBInner.R);
				y2 = (std::int32_t)std::min(inter.B, AABBInner.B);

				xs = (std::int32_t)aabb2.L;

				dx = frameRect2.X;
				dy = frameRect2.Y - (std::int32_t)aabb2.T;
				frameWidth = frameRect2.W;
			}

			stride = res->Base->GetMaskStride();

			// Without a mask, fall back to the inner hitboxes
			if (p == nullptr) {
				return AABBInner.Overlaps(other->AABBInner);
			}

			// Per-pixel collision check
			for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
				for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
					std::int32_t i1 = i - xs;
					if (isFacingLeftCurrent) {
						// Mirrored within the frame's own width
						i1 = frameWidth - i1 - 1;
					}

					if (IsMaskPixelSolid(p, ((j + dy) * stride) + i1 + dx)) {
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

			std::int32_t dx1 = frameRect1.X;
			std::int32_t dy1 = frameRect1.Y - (std::int32_t)aabb1.T;
			std::int32_t stride1 = res1->Base->GetMaskStride();

			std::int32_t dx2 = frameRect2.X;
			std::int32_t dy2 = frameRect2.Y - (std::int32_t)aabb2.T;
			std::int32_t stride2 = res2->Base->GetMaskStride();

			// Per-pixel collision check
			auto p1 = res1->Base->Mask.get();
			auto p2 = res2->Base->Mask.get();
			if (p1 == nullptr || p2 == nullptr) {
				return AABBInner.Overlaps(other->AABBInner);
			}

			for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
				for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
					std::int32_t i1 = i - x1s;
					if (GetState(ActorState::IsFacingLeft)) {
						i1 = frameRect1.W - i1 - 1;
					}
					std::int32_t i2 = i - x2s;
					if (other->GetState(ActorState::IsFacingLeft)) {
						i2 = frameRect2.W - i2 - 1;
					}

					if (IsMaskPixelSolid(p1, ((j + dy1) * stride1) + i1 + dx1) && IsMaskPixelSolid(p2, ((j + dy2) * stride2) + i2 + dx2)) {
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
		} else if (std::abs(_renderer.GetRotation()) > 0.1f) {
			return IsCollidingWithAngled(aabb);
		}

		GraphicResource* res = (_currentTransition != nullptr ? _currentTransition : _currentAnimation);
		if (res == nullptr) {
			return false;
		}

		// Sized and placed like the drawn frame, as above
		const std::int32_t maskFrame = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		const bool facingLeft = GetState(ActorState::IsFacingLeft);
		const Recti frameRectSelf = res->Base->GetFrameRect(maskFrame);
		const Vector2i frameAnchorSelf = res->Base->GetFrameAnchor(maskFrame, facingLeft);

		AABBf aabbSelf = AABBf(_pos.X - frameAnchorSelf.X, _pos.Y - frameAnchorSelf.Y,
			(float)frameRectSelf.W, (float)frameRectSelf.H);
		aabbSelf.B += aabbSelf.T;
		aabbSelf.R += aabbSelf.L;

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

		std::int32_t dx = frameRectSelf.X;
		std::int32_t dy = frameRectSelf.Y - (std::int32_t)aabbSelf.T;
		std::int32_t stride = res->Base->GetMaskStride();

		// Per-pixel collision check
		auto p = res->Base->Mask.get();
		if (p == nullptr) {
			return aabb.Overlaps(AABBInner);
		}

		for (std::int32_t i = x1; i < x2; i += PerPixelCollisionStep) {
			for (std::int32_t j = y1; j < y2; j += PerPixelCollisionStep) {
				std::int32_t i1 = i - xs;
				if (GetState(ActorState::IsFacingLeft)) {
					i1 = frameRectSelf.W - i1 - 1;
				}

				if (IsMaskPixelSolid(p, ((j + dy) * stride) + i1 + dx)) {
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

		// The transform maps the frame's own area (not the cell), the anchor is unflipped since Scaling mirrors it
		const std::int32_t maskFrame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
		const std::int32_t maskFrame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
		const Recti frameRect1 = res1->Base->GetFrameRect(maskFrame1);
		const Recti frameRect2 = res2->Base->GetFrameRect(maskFrame2);
		const Vector2i frameAnchor1 = res1->Base->GetFrameAnchor(maskFrame1);
		const Vector2i frameAnchor2 = res2->Base->GetFrameAnchor(maskFrame2);

		Matrix4x4f transform1 = Matrix4x4f::Translation((float)-frameAnchor1.X, (float)-frameAnchor1.Y, 0.0f);
		if (GetState(ActorState::IsFacingLeft)) {
			transform1 = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform1;
		}
		transform1 = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.GetRotation()) * transform1;

		Matrix4x4f transform2 = Matrix4x4f::Translation((float)-frameAnchor2.X, (float)-frameAnchor2.Y, 0.0f);
		if (other->GetState(ActorState::IsFacingLeft)) {
			transform2 = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform2;
		}
		transform2 = Matrix4x4f::Translation(other->_pos.X, other->_pos.Y, 0.0f) * Matrix4x4f::RotationZ(other->_renderer.GetRotation()) * transform2;

		std::int32_t width1 = frameRect1.W;
		std::int32_t height1 = frameRect1.H;
		std::int32_t width2 = frameRect2.W;
		std::int32_t height2 = frameRect2.H;

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

			aabb1 = AABBf(floorFast(minX), floorFast(minY), ceilFast(maxX), ceilFast(maxY));
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

			aabb2 = AABBf(floorFast(minX), floorFast(minY), ceilFast(maxX), ceilFast(maxY));
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

		std::int32_t dx1 = frameRect1.X;
		std::int32_t dy1 = frameRect1.Y;
		std::int32_t stride1 = res1->Base->GetMaskStride();

		std::int32_t dx2 = frameRect2.X;
		std::int32_t dy2 = frameRect2.Y;
		std::int32_t stride2 = res2->Base->GetMaskStride();

		auto p1 = res1->Base->Mask.get();
		auto p2 = res2->Base->Mask.get();
		if (p1 == nullptr || p2 == nullptr) {
			return AABBInner.Overlaps(other->AABBInner);
		}

		for (std::int32_t y1 = 0; y1 < height1; y1 += PerPixelCollisionStep) {
			Vector3f posIn2 = yPosIn2;

			for (std::int32_t x1 = 0; x1 < width1; x1 += PerPixelCollisionStep) {
				std::int32_t x2 = (std::int32_t)roundFast(posIn2.X);
				std::int32_t y2 = (std::int32_t)roundFast(posIn2.Y);

				if (x2 >= 0 && x2 < width2 && y2 >= 0 && y2 < height2) {
					if (IsMaskPixelSolid(p1, ((y1 + dy1) * stride1) + x1 + dx1) && IsMaskPixelSolid(p2, ((y2 + dy2) * stride2) + x2 + dx2)) {
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

		// The transform maps the frame's own area, as above
		const std::int32_t maskFrame = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		const Recti frameRectSelf = res->Base->GetFrameRect(maskFrame);
		const Vector2i frameAnchorSelf = res->Base->GetFrameAnchor(maskFrame);

		Matrix4x4f transform = Matrix4x4f::Translation((float)-frameAnchorSelf.X, (float)-frameAnchorSelf.Y, 0.0f);
		if (GetState(ActorState::IsFacingLeft)) {
			transform = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform;
		}
		transform = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.GetRotation()) * transform;

		std::int32_t width = frameRectSelf.W;
		std::int32_t height = frameRectSelf.H;

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

			aabbSelf = AABBf(floorFast(minX), floorFast(minY), ceilFast(maxX), ceilFast(maxY));
		}

		if (!aabb.Overlaps(aabbSelf)) {
			return false;
		}

		// TransformNormal with [1, 0] and [0, 1] vectors
		Vector3f stepX = Vector3f(transform[0][0], transform[0][1], 0.0f) * PerPixelCollisionStep;
		Vector3f stepY = Vector3f(transform[1][0], transform[1][1], 0.0f) * PerPixelCollisionStep;

		Vector3f yPosInAABB = Vector3f::Zero * transform;

		std::int32_t frame = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		std::int32_t dx = frameRectSelf.X;
		std::int32_t dy = frameRectSelf.Y;
		std::int32_t stride = res->Base->GetMaskStride();

		auto p = res->Base->Mask.get();
		if (p == nullptr) {
			return aabb.Overlaps(AABBInner);
		}

		for (std::int32_t y1 = 0; y1 < height; y1 += PerPixelCollisionStep) {
			Vector3f posInAABB = yPosInAABB;

			for (std::int32_t x1 = 0; x1 < width; x1 += PerPixelCollisionStep) {
				std::int32_t x2 = (std::int32_t)roundFast(posInAABB.X);
				std::int32_t y2 = (std::int32_t)roundFast(posInAABB.Y);

				if (IsMaskPixelSolid(p, ((y1 + dy) * stride) + x1 + dx) &&
					x2 >= aabb.L && x2 < aabb.R && y2 >= aabb.T && y2 < aabb.B) {
					return true;
				}

				posInAABB += stepX;
			}
			yPosInAABB += stepY;
		}

		return false;
	}

	bool ActorBase::HasCrossedOver(const ActorBase* other) const
	{
		Vector2f delta = _pos - _frameStartPos;

		// A move within one collision step can't skip anything, the overlap test catches it
		if (std::max(std::abs(delta.X), std::abs(delta.Y)) <= MaxMovementStep) {
			return false;
		}

		// The center's path against the other hitbox grown by this one's half extents (Minkowski sum), box only
		Vector2f extents = AABBInner.GetExtents();
		AABBf expanded = AABBf(other->AABBInner.L - extents.X, other->AABBInner.T - extents.Y,
			other->AABBInner.R + extents.X, other->AABBInner.B + extents.Y);

		// Slab test clipped to [0, 1], an axis that didn't move is checked directly to avoid dividing by zero
		Vector2f end = AABBInner.GetCenter();
		Vector2f start = end - delta;
		float tMin = 0.0f;
		float tMax = 1.0f;

		if (delta.X == 0.0f) {
			if (start.X < expanded.L || start.X > expanded.R) {
				return false;
			}
		} else {
			float t1 = (expanded.L - start.X) / delta.X;
			float t2 = (expanded.R - start.X) / delta.X;
			tMin = std::max(tMin, std::min(t1, t2));
			tMax = std::min(tMax, std::max(t1, t2));
		}

		if (delta.Y == 0.0f) {
			if (start.Y < expanded.T || start.Y > expanded.B) {
				return false;
			}
		} else {
			float t1 = (expanded.T - start.Y) / delta.Y;
			float t2 = (expanded.B - start.Y) / delta.Y;
			tMin = std::max(tMin, std::min(t1, t2));
			tMax = std::min(tMax, std::max(t1, t2));
		}

		return (tMin <= tMax);
	}

	void ActorBase::UpdateRendererPosition()
	{
		// An invalid position traps in floorFast() on the Nintendo 64, restore the actor and log it
		if DEATH_UNLIKELY(!IsUsablePosition(_pos)) {
			RestoreFromInvalidPosition();
		}

		Vector2f pos = _pos;
		if (!PreferencesCache::UnalignedViewport || (_state & ActorState::IsDirty) != ActorState::IsDirty) {
			pos.X = floorFast(pos.X);
			pos.Y = floorFast(pos.Y);
		}

		// Called again for every actor at the end of the frame, don't re-dirty the node for the same value
		if (_renderer.GetPosition() == pos) {
			return;
		}
		_renderer.SetPosition(pos.X, pos.Y);
	}

	void ActorBase::RestoreFromInvalidPosition()
	{
		// Raw bits are logged, formatting a NaN would trap too
		Vector2f fallback = (IsUsablePosition(_frameStartPos) ? _frameStartPos : _renderer.GetPosition());
		LOGE("Actor \"{}\" moved to an invalid position (bits {}, {} with speed bits {}, {}), restored to [{}, {}]",
			(_metadata != nullptr ? StringView(_metadata->Path) : "<no metadata>"_s), FloatBits(_pos.X), FloatBits(_pos.Y),
			FloatBits(_speed.X), FloatBits(_speed.Y), fallback.X, fallback.Y);
		_pos = fallback;
		_speed = Vector2f::Zero;
		_externalForce = Vector2f::Zero;
		// The hitbox was derived from the invalid position
		UpdateAABB();
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

			if (std::abs(_renderer.GetRotation()) > 0.1f) {
				Matrix4x4f transform = Matrix4x4f::Translation((float)-hotspot.X, (float)-hotspot.Y, 0.0f);
				if (GetState(ActorState::IsFacingLeft)) {
					transform = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform;
				}
				transform = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.GetRotation()) * transform;

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
		_renderer.FrameSource = res->Base;
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

		_renderer.Hotspot.X = static_cast<float>(res->Base->Hotspot.X);
		_renderer.Hotspot.Y = static_cast<float>(res->Base->Hotspot.Y);

		_renderer.SetTexture(res->Base->TextureDiffuse.get());
		// Indexed graphics must be drawn through the palette shader (recolored at draw time); plain ones keep the
		// default sprite path. The animation's palette offset selects the palette region (0 for most, gem rows for
		// gems); a recolor override set via SetPalette still takes precedence.
		_renderer.SetIndexed((res->Base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed, res->PaletteOffset);
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

	void ActorBase::RequestMetadata(StringView path, bool forceIndexed)
	{
		_metadata = ContentResolver::Get().RequestMetadata(path, forceIndexed);
	}

#if !defined(WITH_COROUTINES)
	void ActorBase::RequestMetadataAsync(StringView path, bool forceIndexed)
	{
		_metadata = ContentResolver::Get().RequestMetadata(path, forceIndexed);
	}
#endif

	bool ActorBase::IsFrozen() const
	{
		return (_frozenTimeLeft > 0.0f);
	}

	float ActorBase::GetFrozenMomentumX() const
	{
		return (GetState(ActorState::KeepsMomentumWhenFrozen) ? _speed.X : 0.0f);
	}

	void ActorBase::ThawOnNextUpdate()
	{
		if (_frozenTimeLeft > 0.0f) {
			_frozenTimeLeft = 0.0f;
			_thawPending = true;
		}
	}

	bool ActorBase::UpdateFrozenState(float timeMult)
	{
		constexpr float FrozenSpeedKeptPerTick = 31.0f / 32.0f;

		if (_frozenTimeLeft <= 0.0f) {
			if (!_thawPending) {
				return false;
			}
		} else {
			_frozenTimeLeft -= timeMult;
		}
		if (_frozenTimeLeft > 0.0f) {
			// Not in HandleFrozenStateChange(), it would run between SceneNode::Transform() and BaseSprite::UpdateRenderCommand()
			_renderer.Initialize(ActorRendererType::FrozenMask);
			if (GetState(ActorState::KeepsMomentumWhenFrozen)) {
				_speed.X *= std::pow(FrozenSpeedKeptPerTick, timeMult * Player::LegacyFrameRateScale);
			}
			return false;
		}

		_frozenTimeLeft = 0.0f;
		_thawPending = false;
		_renderer.AnimPaused = false;
		_renderer.Initialize(ActorRendererType::Default);

		// The ice flies apart as fast as the object slid in it
		float scale = GetIceShrapnelScale();
		for (std::int32_t i = 0; i < 10; i++) {
			Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 10), Explosion::Type::IceShrapnel,
				scale, Vector2f(GetFrozenMomentumX(), _speed.Y));
		}

		Explosion::Create(_levelHandler, Vector3i((std::int32_t)_pos.X, (std::int32_t)_pos.Y, _renderer.GetLayer() + 90), Explosion::Type::SmokeWhite, scale);

		_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
		return true;
	}

	void ActorBase::HandleFrozenStateChange(ActorBase* shot)
	{
		auto* shotBase = runtime_cast<Weapons::ShotBase>(shot);
		if (shotBase == nullptr) {
			return;
		}
		float frozenDuration = shotBase->GetFrozenDuration();
		if (frozenDuration > 0.0f) {
			ActorBase* owner = runtime_cast<ActorBase>(shotBase->GetOwner());
			if (owner != this && _levelHandler->CanFreezeActor(this, owner)) {
				_frozenTimeLeft = frozenDuration;
				_renderer.AnimPaused = true;
				shotBase->DecreaseHealth(INT32_MAX);
			}
		} else if (shotBase->ThawsFrozen()) {
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
#if defined(WITH_ANGELSCRIPT)
			if DEATH_UNLIKELY(_updateOverride != nullptr && _updateOverride->OnPerish(this, collider)) {
				return;
			}
#endif
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
			if ((type & MoveType::Relative) != MoveType::Relative) {
				// An absolute move is a relocation, not a travelled path
				ResetPathTracking();
			}
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
		: BaseSprite(nullptr, nullptr, 0.0f, 0.0f), FrameSource(nullptr), LoopMode(AnimationLoopMode::Loop), FirstFrame(0),
			FrameCount(0), AnimDuration(0.0f), AnimTime(0.0f), CurrentFrame(0), AnimPaused(false), _baseIndexed(false),
			_rendererType((ActorRendererType)-1), _owner(owner), _rendererTransition(0.0f), _paletteOffset(-1), _basePaletteOffset(0)
	{
		_type = ObjectType::Sprite;
		_renderCommand.SetType(RenderCommand::Type::Sprite);
		Initialize(ActorRendererType::Default);
	}

	void ActorBase::ActorRenderer::Initialize(ActorRendererType type)
	{
		if (_rendererType == type) {
			return;
		}

		const ActorRendererType previousType = _rendererType;
		_rendererType = type;

		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless()) {
			return;
		}

		// Indexed sprites need the palette-aware shader variant, otherwise they'd draw raw indices as near-black
		bool hasPalette = (_paletteOffset >= 0 || _baseIndexed);
		bool shaderChanged;
		switch (type) {
			case ActorRendererType::Outline: shaderChanged = _renderCommand.GetMaterial().SetShader(resolver.GetShader(hasPalette ? PrecompiledShader::OutlinePalette : PrecompiledShader::Outline)); break;
			case ActorRendererType::WhiteMask: shaderChanged = _renderCommand.GetMaterial().SetShader(resolver.GetShader(hasPalette ? PrecompiledShader::WhiteMaskPalette : PrecompiledShader::WhiteMask)); break;
			case ActorRendererType::PartialWhiteMask: shaderChanged = _renderCommand.GetMaterial().SetShader(resolver.GetShader(hasPalette ? PrecompiledShader::PartialWhiteMaskPalette : PrecompiledShader::PartialWhiteMask)); break;
			case ActorRendererType::FrozenMask: shaderChanged = _renderCommand.GetMaterial().SetShader(resolver.GetShader(hasPalette ? PrecompiledShader::FrozenMaskPalette : PrecompiledShader::FrozenMask)); break;
			default:
				// Without a palette, the built-in Sprite program is used
				shaderChanged = (hasPalette
					? _renderCommand.GetMaterial().SetShader(resolver.GetShader(PrecompiledShader::PaletteRemap))
					: _renderCommand.GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
				break;
		}
		if (shaderChanged) {
			ShaderHasChanged();
			_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);

			if (type == ActorRendererType::Outline || type == ActorRendererType::FrozenMask) {
				_rendererTransition = 0.0f;
				if (_texture != nullptr) {
					Vector2i texSize = _texture->GetSize();
					SetColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
				}
			} else {
				// Keep the actor's fade in alpha unless switching from Outline or FrozenMask
				const bool alphaIsActorFade = (KeepsActorFadeInAlpha(previousType) && KeepsActorFadeInAlpha(type));
				SetColor(Colorf(1.0f, 1.0f, 1.0f, alphaIsActorFade ? GetColor().A : 1.0f));
			}
		}

		// Bind the palette texture to unit 1 and upload the offset, after ShaderHasChanged() so the binding sticks
		if (hasPalette) {
			RHI::UniformCache* paletteUniform = _renderCommand.GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1); // GL_TEXTURE1
			}
			Texture* paletteTexture = resolver.GetPaletteTexture();
			if (paletteTexture != nullptr) {
				_renderCommand.GetMaterial().SetTexture(1, *paletteTexture);
			}
			// A recolor override wins over the sprite's own palette region
			SetPaletteOffset((float)(_paletteOffset >= 0 ? _paletteOffset : _basePaletteOffset));
		} else {
			SetPaletteOffset(0.0f);
		}
	}

	void ActorBase::ActorRenderer::SetPalette(std::int32_t paletteOffset)
	{
		if (_paletteOffset == paletteOffset) {
			return;
		}

		_paletteOffset = paletteOffset;
		ReinitializeCurrentType();
	}

	void ActorBase::ActorRenderer::SetIndexed(bool indexed, std::int32_t basePaletteOffset)
	{
		if (_baseIndexed == indexed && _basePaletteOffset == basePaletteOffset) {
			return;
		}

		_baseIndexed = indexed;
		_basePaletteOffset = basePaletteOffset;
		ReinitializeCurrentType();
	}

	void ActorBase::ActorRenderer::ReinitializeCurrentType()
	{
		// Re-run the setup to switch the palette-aware shader variant and rebind the palette
		if (_rendererType != (ActorRendererType)-1 && !ContentResolver::Get().IsHeadless()) {
			ActorRendererType current = _rendererType;
			_rendererType = (ActorRendererType)-1;
			Initialize(current);
		}
	}

	void ActorBase::ActorRenderer::OnUpdate(float timeMult)
	{
		// Start of the path for HasCrossedOver()
		_owner->_frameStartPos = _owner->_pos;

#if defined(WITH_ANGELSCRIPT)
		// A script-driven object updates through the script
		if DEATH_UNLIKELY(_owner->_updateOverride != nullptr) {
			_owner->_updateOverride->OnUpdate(_owner, timeMult);
		} else {
			_owner->OnUpdate(timeMult);
		}
#else
		_owner->OnUpdate(timeMult);
#endif

		_owner->UpdateRendererPosition();

#if defined(WITH_ANGELSCRIPT)
		// A level script can make the graphics true color while they're drawn
		if DEATH_UNLIKELY(FrameSource != nullptr && _baseIndexed != ((FrameSource->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed)) {
			SetIndexed(!_baseIndexed, _basePaletteOffset);
		}
#endif

		if (IsAnimationRunning()) {
			switch (LoopMode) {
				case AnimationLoopMode::Loop:
					AnimTime += timeMult * FrameTimer::SecondsPerFrame;
					if (AnimTime > AnimDuration) {
						if DEATH_LIKELY(AnimDuration > 0.0f) {
							std::int32_t n = (std::int32_t)(AnimTime / AnimDuration);
							AnimTime -= AnimDuration * n;
						} else {
							// A looping animation of no duration, avoid dividing by zero
							AnimTime = 0.0f;
						}
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
					if (_texture != nullptr) {
						Vector2i texSize = _texture->GetSize();
						SetColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
					}
				}
				break;
			case ActorRendererType::FrozenMask:
				if (_rendererTransition < 1.0f) {
					_rendererTransition = std::min(_rendererTransition + timeMult * 0.14f, 1.0f);
					if (_texture != nullptr) {
						Vector2i texSize = _texture->GetSize();
						SetColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
					}
				}
				break;
		}

		BaseSprite::OnUpdate(timeMult);
	}

	bool ActorBase::ActorRenderer::OnDraw(RenderQueue& renderQueue)
	{
#if defined(WITH_ANGELSCRIPT)
		// A script-driven object is drawn only when the script asks
		if DEATH_UNLIKELY(_owner->_updateOverride != nullptr && !_owner->_updateOverride->IsDrawn()) {
			return true;
		}
#endif

		if (_owner->OnDraw(renderQueue)) {
			return true;
		}

		return BaseSprite::OnDraw(renderQueue);
	}

	void ActorBase::ActorRenderer::TextureHasChanged(Texture* newTexture)
	{
		if (_rendererType == ActorRendererType::Outline || _rendererType == ActorRendererType::FrozenMask) {
			if (newTexture != nullptr) {
				Vector2i texSize = newTexture->GetSize();
				SetColor(Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, _rendererTransition));
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
				CurrentFrame = std::clamp<std::int32_t>(CurrentFrame, 0, FrameCount - 1);
			} else {
				CurrentFrame = NormalizeFrame(CurrentFrame, 0, FrameCount);
			}
		}
		CurrentFrame = FirstFrame + std::clamp<std::int32_t>(CurrentFrame, 0, FrameCount - 1);

		// Set current animation frame rectangle, a packed sheet gives every frame its own area and hotspot
		SetTexRect(FrameSource != nullptr ? FrameSource->GetFrameRect(CurrentFrame)
			: Recti(FrameDimensions.X * (CurrentFrame % FrameConfiguration.X),
				FrameDimensions.Y * (CurrentFrame / FrameConfiguration.X), FrameDimensions.X, FrameDimensions.Y));
		UpdateAnchor();
	}

	void ActorBase::ActorRenderer::UpdateAnchor()
	{
		// Mirror the hotspot within the frame's own area, which is the whole cell only on a regular grid
		if (FrameSource != nullptr) {
			const Vector2i anchor = FrameSource->GetFrameAnchor(CurrentFrame, IsFlippedX(), IsFlippedY());
			SetAbsAnchorPoint((float)anchor.X, (float)anchor.Y);
		} else {
			SetAbsAnchorPoint(IsFlippedX() ? (float)FrameDimensions.X - Hotspot.X : Hotspot.X,
				IsFlippedY() ? (float)FrameDimensions.Y - Hotspot.Y : Hotspot.Y);
		}
	}

	std::int32_t ActorBase::ActorRenderer::NormalizeFrame(std::int32_t frame, std::int32_t min, std::int32_t max)
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