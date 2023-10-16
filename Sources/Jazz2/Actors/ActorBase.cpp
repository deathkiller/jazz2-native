#include "ActorBase.h"
#include "../ILevelHandler.h"
#include "../Events/EventMap.h"
#include "../Tiles/TileMap.h"
#include "../Collisions/DynamicTreeBroadPhase.h"

#include "Explosion.h"
#include "Player.h"
#include "Weapons/FreezerShot.h"
#include "Weapons/ToasterShot.h"

#if !defined(WITH_COROUTINES)
#	pragma message("WITH_COROUTINES is not defined, building without asynchronous loading support")
#endif

#include "../../nCine/Primitives/Matrix4x4.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Base/FrameTimer.h"

using namespace nCine;

namespace Jazz2::Actors
{
	ActorBase::ActorBase()
		:
		_state(ActorState::None),
		_levelHandler(nullptr),
		_internalForceY(0.0f),
		_elasticity(0.0f),
		_friction(1.5f),
		_unstuckCooldown(0.0f),
		_frozenTimeLeft(0.0f),
		_maxHealth(1),
		_health(1),
		_spawnFrames(0.0f),
		_metadata(nullptr),
		_renderer(this),
		_currentAnimation(nullptr),
		_currentTransition(nullptr),
		_currentAnimationState(AnimState::Uninitialized),
		_currentTransitionState(AnimState::Idle),
		_currentTransitionCancellable(false),
		CollisionProxyID(Collisions::NullNode)
	{
	}

	ActorBase::~ActorBase()
	{
	}

	bool ActorBase::IsFacingLeft()
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
		GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
		if (res != nullptr) {
			_renderer.Hotspot.X = -((res->Base->FrameDimensions.X / 2) - (IsFacingLeft() ? (res->Base->FrameDimensions.X - res->Base->Hotspot.X) : res->Base->Hotspot.X));
			_renderer.Hotspot.Y = -((res->Base->FrameDimensions.Y / 2) - res->Base->Hotspot.Y);
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
		_originTile = Vector2i((int)details.Pos.X / 32, (int)details.Pos.Y / 32);
		_spawnFrames = _levelHandler->ElapsedFrames();

		uint16_t layer = (uint16_t)details.Pos.Z;
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

	void ActorBase::OnTriggeredEvent(EventType eventType, uint8_t* eventParams)
	{
		// Can be overridden
	}

	void ActorBase::TryStandardMovement(float timeMult, TileCollisionParams& params)
	{
		if (_unstuckCooldown > 0.0f) {
			_unstuckCooldown -= timeMult;
		}

		float currentGravity;
		float currentElasticity = _elasticity;
		if ((_state & ActorState::ApplyGravitation) == ActorState::ApplyGravitation) {
			currentGravity = _levelHandler->Gravity;
			if (_pos.Y >= _levelHandler->WaterLevel()) {
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
					int sign = (effectiveSpeedX > 0.0f ? 1 : -1);
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
						}
						OnHitWall(timeMult);
					}
				}

				// Run all floor-related hooks, such as the player's check for hurting positions
				OnHitFloor(timeMult);
			} else {
				// Airborne movement is handled here
				// First, attempt to move directly based on the current speed values
				if (!MoveInstantly(Vector2f(effectiveSpeedX, effectiveSpeedY), MoveType::Relative, params)) {
					// First, attempt to move horizontally as much as possible
					float maxDiff = std::abs(effectiveSpeedX);
					int sign = (effectiveSpeedX > 0.0f ? 1 : -1);
					float xDiff = maxDiff;
					for (; xDiff > std::numeric_limits<float>::epsilon(); xDiff -= CollisionCheckStep) {
						if (MoveInstantly(Vector2f(xDiff * sign, 0.0f), MoveType::Relative, params)) {
							break;
						}
					}

					// Then, try the same vertically
					maxDiff = std::abs(effectiveSpeedY);
					sign = (effectiveSpeedY > 0.0f ? 1 : -1);
					float yDiff = maxDiff;
					for (; yDiff > std::numeric_limits<float>::epsilon(); yDiff -= CollisionCheckStep) {
						float yDiffSigned = (yDiff * sign);
						if (MoveInstantly(Vector2f(0.0f, yDiffSigned), MoveType::Relative, params) ||
							// Add horizontal tolerance
							MoveInstantly(Vector2f(yDiff * 0.2f, yDiffSigned), MoveType::Relative, params) ||
							MoveInstantly(Vector2f(yDiff * -0.2f, yDiffSigned), MoveType::Relative, params)) {
							break;
						}
					}

					// Place us to the ground only if no horizontal movement was
					// involved (this prevents speeds resetting if the actor
					// collides with a wall from the side while in the air)
					if (yDiff < std::abs(effectiveSpeedY)) {
						if (effectiveSpeedY > 0.0f) {
							_speed.Y = -(currentElasticity * effectiveSpeedY / timeMult);

							OnHitFloor(timeMult);

							if (_speed.Y > -CollisionCheckStep) {
								_speed.Y = 0.0f;
								SetState(ActorState::CanJump, true);
							}
						} else {
							_speed.Y = 0.0f;
							OnHitCeiling(timeMult);
						}
					}

					// If the actor didn't move all the way horizontally, it hit a wall (or was already touching it)
					if (xDiff < std::abs(effectiveSpeedX) * 0.3f) {
						if (xDiff > 0.0f && currentElasticity > 0.0f) {
							_speed.X = -(currentElasticity * _speed.X);
						}

						// Don't call OnHitWall() if OnHitFloor() or OnHitCeiling() was called this step
						if (yDiff >= std::abs(effectiveSpeedY)) {
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

		// Set the actor as airborne if there seems to be enough space below it
		if (currentGravity > 0.0f) {
			AABBf aabb = AABBInner;
			aabb.B += CollisionCheckStep;
			if (_levelHandler->IsPositionEmpty(this, aabb, params)) {
				_speed.Y += currentGravity * timeMult;
				SetState(ActorState::CanJump, false);
			} else if (std::abs(effectiveSpeedY) <= std::numeric_limits<float>::epsilon()) {
				SetState(ActorState::CanJump, true);
			}

			_externalForce.Y = std::min(_externalForce.Y + currentGravity * 0.33f * timeMult, 0.0f);
			_internalForceY = std::min(_internalForceY + currentGravity * 0.33f * timeMult, 0.0f);
		}
	}

	void ActorBase::UpdateHitbox(int w, int h)
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

	void ActorBase::CreateParticleDebris()
	{
		auto tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr) {
			tilemap->CreateParticleDebris(_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation,
				Vector3f(_pos.X, _pos.Y, (float)_renderer.layer()), Vector2f::Zero, _renderer.CurrentFrame, IsFacingLeft());
		}
	}

	void ActorBase::CreateSpriteDebris(const StringView& identifier, int count)
	{
		auto tilemap = _levelHandler->TileMap();
		if (tilemap != nullptr && _metadata != nullptr) {
			auto it = _metadata->Graphics.find(String::nullTerminatedView(identifier));
			if (it != _metadata->Graphics.end()) {
				tilemap->CreateSpriteDebris(&it->second, Vector3f(_pos.X, _pos.Y, (float)_renderer.layer()), count);
			}
		}
	}

	std::shared_ptr<AudioBufferPlayer> ActorBase::PlaySfx(const StringView& identifier, float gain, float pitch)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			int idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int)it->second.Buffers.size()) : 0);
			return _levelHandler->PlaySfx(it->second.Buffers[idx].get(), Vector3f(_pos.X, _pos.Y, 0.0f), false, gain, pitch);
		} else {
			return nullptr;
		}
	}

	void ActorBase::SetAnimation(const StringView& identifier)
	{
		if (_metadata == nullptr) {
			LOGE("No metadata loaded");
			return;
		}

		auto it = _metadata->Graphics.find(String::nullTerminatedView(identifier));
		if (it == _metadata->Graphics.end()) {
			LOGE("No animation found for \"%s\"", identifier.data());
			return;
		}

		_currentAnimation = &it->second;
		_currentAnimationState = AnimState::Idle;

		RefreshAnimation();
	}

	bool ActorBase::SetAnimation(AnimState state)
	{
		if (_metadata == nullptr) {
			LOGE("No metadata loaded");
			return false;
		}

		if (_currentTransitionState != AnimState::Idle && !_currentTransitionCancellable) {
			return false;
		}

		if (_currentAnimation != nullptr && _currentAnimation->HasState(state)) {
			_currentAnimationState = state;
			return false;
		}

		AnimationCandidate candidates[AnimationCandidatesCount];
		int count = FindAnimationCandidates(state, candidates);
		if (count == 0) {
			//LOGE("No animation found for state 0x%08x", state);
			return false;
		}

		if (_currentTransitionState != AnimState::Idle) {
			_currentTransitionState = AnimState::Idle;

			if (_currentTransitionCallback != nullptr) {
				auto oldCallback = std::move(_currentTransitionCallback);
				_currentTransitionCallback = nullptr;
				oldCallback();
			}
		}

		int index = (count > 1 ? nCine::Random().Next(0, count) : 0);
		_currentAnimation = candidates[index].Resource;
		_currentAnimationState = state;

		RefreshAnimation();

		return true;
	}

	bool ActorBase::SetTransition(const StringView& identifier, bool cancellable, const std::function<void()>& callback)
	{
		if (_metadata == nullptr) {
			return false;
		}

		auto it = _metadata->Graphics.find(String::nullTerminatedView(identifier));
		if (it == _metadata->Graphics.end()) {
			if (callback != nullptr) {
				callback();
			}
			return false;
		}

		if (_currentTransitionCallback != nullptr) {
			auto oldCallback = std::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		_currentTransition = &it->second;
		_currentTransitionState = AnimState::TransitionByName;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = callback;

		RefreshAnimation();

		return true;
	}

	bool ActorBase::SetTransition(const StringView& identifier, bool cancellable, std::function<void()>&& callback)
	{
		if (_metadata == nullptr) {
			return false;
		}

		auto it = _metadata->Graphics.find(String::nullTerminatedView(identifier));
		if (it == _metadata->Graphics.end()) {
			if (callback != nullptr) {
				callback();
			}
			return false;
		}

		if (_currentTransitionCallback != nullptr) {
			auto oldCallback = std::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		_currentTransition = &it->second;
		_currentTransitionState = AnimState::TransitionByName;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = std::move(callback);

		RefreshAnimation();

		return true;
	}

	bool ActorBase::SetTransition(AnimState state, bool cancellable, const std::function<void()>& callback)
	{
		AnimationCandidate candidates[AnimationCandidatesCount];
		int count = FindAnimationCandidates(state, candidates);
		if (count == 0) {
			if (callback != nullptr) {
				callback();
			}
			return false;
		}

		if (_currentTransitionCallback != nullptr) {
			auto oldCallback = std::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		int index = (count > 1 ? nCine::Random().Next(0, count) : 0);
		_currentTransition = candidates[index].Resource;
		_currentTransitionState = state;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = callback;

		RefreshAnimation();

		return true;
	}

	bool ActorBase::SetTransition(AnimState state, bool cancellable, std::function<void()>&& callback)
	{
		AnimationCandidate candidates[AnimationCandidatesCount];
		int count = FindAnimationCandidates(state, candidates);
		if (count == 0) {
			if (callback != nullptr) {
				callback();
			}
			return false;
		}

		if (_currentTransitionCallback != nullptr) {
			auto oldCallback = std::move(_currentTransitionCallback);
			_currentTransitionCallback = nullptr;
			oldCallback();
		}

		int index = (count > 1 ? nCine::Random().Next(0, count) : 0);
		_currentTransition = candidates[index].Resource;
		_currentTransitionState = state;
		_currentTransitionCancellable = cancellable;
		_currentTransitionCallback = std::move(callback);
		
		RefreshAnimation();

		return true;
	}

	void ActorBase::CancelTransition()
	{
		if (_currentTransitionState != AnimState::Idle && _currentTransitionCancellable) {
			if (_currentTransitionCallback != nullptr) {
				auto oldCallback = std::move(_currentTransitionCallback);
				_currentTransitionCallback = nullptr;
				oldCallback();
			}

			_currentTransitionState = AnimState::Idle;

			RefreshAnimation();
		}
	}

	void ActorBase::ForceCancelTransition()
	{
		if (_currentTransitionState == AnimState::Idle) {
			return;
		}

		_currentTransitionCancellable = true;
		_currentTransitionCallback = nullptr;
		_currentTransitionState = AnimState::Idle;

		RefreshAnimation();
	}

	void ActorBase::OnAnimationStarted()
	{
		// Can be overriden
	}

	void ActorBase::OnAnimationFinished()
	{
		if (_currentTransitionState != AnimState::Idle) {
			_currentTransitionState = AnimState::Idle;

			RefreshAnimation();

			if (_currentTransitionCallback != nullptr) {
				auto oldCallback = std::move(_currentTransitionCallback);
				_currentTransitionCallback = nullptr;
				oldCallback();
			}
		}
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

		GraphicResource* res1 = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
		GraphicResource* res2 = (other->_currentTransitionState != AnimState::Idle ? other->_currentTransition : other->_currentAnimation);
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
			int x1, y1, x2, y2, xs, dx, dy, stride;
			if (perPixel1) {
				res = res1;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = GetState(ActorState::IsFacingLeft);

				x1 = (int)std::max(inter.L, other->AABBInner.L);
				y1 = (int)std::max(inter.T, other->AABBInner.T);
				x2 = (int)std::min(inter.R, other->AABBInner.R);
				y2 = (int)std::min(inter.B, other->AABBInner.B);

				xs = (int)aabb1.L;

				int frame1 = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
				dx = (frame1 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
				dy = (frame1 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (int)aabb1.T;

			} else {
				res = res2;
				p = res->Base->Mask.get();

				isFacingLeftCurrent = other->GetState(ActorState::IsFacingLeft);

				x1 = (int)std::max(inter.L, AABBInner.L);
				y1 = (int)std::max(inter.T, AABBInner.T);
				x2 = (int)std::min(inter.R, AABBInner.R);
				y2 = (int)std::min(inter.B, AABBInner.B);

				xs = (int)aabb2.L;

				int frame2 = std::min(other->_renderer.CurrentFrame, res->FrameCount - 1);
				dx = (frame2 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
				dy = (frame2 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (int)aabb2.T;
			}

			stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

			// Per-pixel collision check
			for (int i = x1; i < x2; i += PerPixelCollisionStep) {
				for (int j = y1; j < y2; j += PerPixelCollisionStep) {
					int i1 = i - xs;
					if (isFacingLeftCurrent) {
						i1 = res->Base->FrameDimensions.X - i1 - 1;
					}

					if (p[((j + dy) * stride) + i1 + dx] > AlphaThreshold) {
						return true;
					}
				}
			}
		} else {
			int x1 = (int)inter.L;
			int y1 = (int)inter.T;
			int x2 = (int)inter.R;
			int y2 = (int)inter.B;

			int x1s = (int)aabb1.L;
			int x2s = (int)aabb2.L;

			int frame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
			int dx1 = (frame1 % res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.X;
			int dy1 = (frame1 / res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.Y - (int)aabb1.T;
			int stride1 = res1->Base->FrameConfiguration.X * res1->Base->FrameDimensions.X;

			int frame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
			int dx2 = (frame2 % res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.X;
			int dy2 = (frame2 / res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.Y - (int)aabb2.T;
			int stride2 = res2->Base->FrameConfiguration.X * res2->Base->FrameDimensions.X;

			// Per-pixel collision check
			auto p1 = res1->Base->Mask.get();
			auto p2 = res2->Base->Mask.get();

			for (int i = x1; i < x2; i += PerPixelCollisionStep) {
				for (int j = y1; j < y2; j += PerPixelCollisionStep) {
					int i1 = i - x1s;
					if (GetState(ActorState::IsFacingLeft)) {
						i1 = res1->Base->FrameDimensions.X - i1 - 1;
					}
					int i2 = i - x2s;
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
			AABBf inter2 = AABBf::Intersect(aabb, AABBInner);
			return (inter2.R > 0 && inter2.B > 0);
		} else if (std::abs(_renderer.rotation()) > 0.1f) {
			return IsCollidingWithAngled(aabb);
		}

		GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
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

		int x1 = (int)std::max(inter.L, aabb.L);
		int y1 = (int)std::max(inter.T, aabb.T);
		int x2 = (int)std::min(inter.R, aabb.R);
		int y2 = (int)std::min(inter.B, aabb.B);

		int xs = (int)aabbSelf.L;

		int frame1 = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		int dx = (frame1 % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
		int dy = (frame1 / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y - (int)aabbSelf.T;
		int stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

		// Per-pixel collision check
		auto p = res->Base->Mask.get();

		for (int i = x1; i < x2; i += PerPixelCollisionStep) {
			for (int j = y1; j < y2; j += PerPixelCollisionStep) {
				int i1 = i - xs;
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
		GraphicResource* res1 = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
		GraphicResource* res2 = (other->_currentTransitionState != AnimState::Idle ? other->_currentTransition : other->_currentAnimation);
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

		int width1 = res1->Base->FrameDimensions.X;
		int height1 = res1->Base->FrameDimensions.Y;
		int width2 = res2->Base->FrameDimensions.X;
		int height2 = res2->Base->FrameDimensions.Y;

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

		int frame1 = std::min(_renderer.CurrentFrame, res1->FrameCount - 1);
		int dx1 = (frame1 % res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.X;
		int dy1 = (frame1 / res1->Base->FrameConfiguration.X) * res1->Base->FrameDimensions.Y;
		int stride1 = res1->Base->FrameConfiguration.X * res1->Base->FrameDimensions.X;

		int frame2 = std::min(other->_renderer.CurrentFrame, res2->FrameCount - 1);
		int dx2 = (frame2 % res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.X;
		int dy2 = (frame2 / res2->Base->FrameConfiguration.X) * res2->Base->FrameDimensions.Y;
		int stride2 = res2->Base->FrameConfiguration.X * res2->Base->FrameDimensions.X;

		auto p1 = res1->Base->Mask.get();
		auto p2 = res2->Base->Mask.get();

		for (int y1 = 0; y1 < height1; y1 += PerPixelCollisionStep) {
			Vector3f posIn2 = yPosIn2;

			for (int x1 = 0; x1 < width1; x1 += PerPixelCollisionStep) {
				int x2 = (int)std::round(posIn2.X);
				int y2 = (int)std::round(posIn2.Y);

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
		GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
		if (res == nullptr) {
			return false;
		}

		Matrix4x4f transform = Matrix4x4f::Translation((float)-res->Base->Hotspot.X, (float)-res->Base->Hotspot.Y, 0.0f);
		if (GetState(ActorState::IsFacingLeft)) {
			transform = Matrix4x4f::Scaling(-1.0f, 1.0f, 1.0f) * transform;
		}
		transform = Matrix4x4f::Translation(_pos.X, _pos.Y, 0.0f) * Matrix4x4f::RotationZ(_renderer.rotation()) * transform;

		int width = res->Base->FrameDimensions.X;
		int height = res->Base->FrameDimensions.Y;

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

		int frame = std::min(_renderer.CurrentFrame, res->FrameCount - 1);
		int dx = (frame % res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.X;
		int dy = (frame / res->Base->FrameConfiguration.X) * res->Base->FrameDimensions.Y;
		int stride = res->Base->FrameConfiguration.X * res->Base->FrameDimensions.X;

		auto p = res->Base->Mask.get();

		for (int y1 = 0; y1 < height; y1 += PerPixelCollisionStep) {
			Vector3f posInAABB = yPosInAABB;

			for (int x1 = 0; x1 < width; x1 += PerPixelCollisionStep) {
				int x2 = (int)std::round(posInAABB.X);
				int y2 = (int)std::round(posInAABB.Y);

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
			GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
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

	void ActorBase::RefreshAnimation()
	{
		GraphicResource* res = (_currentTransitionState != AnimState::Idle ? _currentTransition : _currentAnimation);
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
		_renderer.AnimTime = 0.0f;

		_renderer.Hotspot.X = -((res->Base->FrameDimensions.X / 2) - (IsFacingLeft() ? (res->Base->FrameDimensions.X - res->Base->Hotspot.X) : res->Base->Hotspot.X));
		_renderer.Hotspot.Y = -((res->Base->FrameDimensions.Y / 2) - res->Base->Hotspot.Y);

		_renderer.setTexture(res->Base->TextureDiffuse.get());
		_renderer.UpdateVisibleFrames();

		OnAnimationStarted();

		if ((_state & ActorState::ForceDisableCollisions) != ActorState::ForceDisableCollisions) {
			_state |= ActorState::IsDirty;
		}
	}

	int ActorBase::FindAnimationCandidates(AnimState state, AnimationCandidate candidates[AnimationCandidatesCount])
	{
		if (_metadata == nullptr) {
			LOGE("No metadata loaded");
			return 0;
		}

		int i = 0;
		auto it = _metadata->Graphics.begin();
		while (it != _metadata->Graphics.end()) {
			if (i >= AnimationCandidatesCount) {
				break;
			}

			if (it->second.HasState(state)) {
				candidates[i].Identifier = &it->first;
				candidates[i].Resource = &it->second;
				i++;
			}
			++it;
		}

		return i;
	}

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

				for (int i = 0; i < 10; i++) {
					Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 10), Explosion::Type::IceShrapnel);
				}

				Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer() + 90), Explosion::Type::SmokeWhite);

				_levelHandler->PlayCommonSfx("IceBreak"_s, Vector3f(_pos.X, _pos.Y, 0.0f));
			}
		}
	}

	void ActorBase::HandleFrozenStateChange(ActorBase* shot)
	{
		if (auto freezerShot = dynamic_cast<Actors::Weapons::FreezerShot*>(shot)) {
			if (dynamic_cast<ActorBase*>(freezerShot->GetOwner()) != this) {
				_frozenTimeLeft = freezerShot->FrozenDuration();
				_renderer.AnimPaused = true;
				freezerShot->DecreaseHealth(INT32_MAX);
			}
		} else if(auto toasterShot = dynamic_cast<Actors::Weapons::ToasterShot*>(shot)) {
			_frozenTimeLeft = std::min(1.0f, _frozenTimeLeft);
		}
	}

	bool ActorBase::IsInvulnerable()
	{
		return GetState(ActorState::IsInvulnerable);
	}

	int ActorBase::GetHealth()
	{
		return _health;
	}

	void ActorBase::SetHealth(int value)
	{
		_health = value;
	}

	int ActorBase::GetMaxHealth()
	{
		return _maxHealth;
	}

	void ActorBase::DecreaseHealth(int amount, ActorBase* collider)
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

	bool ActorBase::MoveInstantly(const Vector2f& pos, MoveType type, TileCollisionParams& params)
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
			_renderer.setPosition(std::round(newPos.X), std::round(newPos.Y));

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

	void ActorBase::ActorRenderer::Initialize(ActorRendererType type)
	{
		if (_rendererType == type) {
			return;
		}

		_rendererType = type;

		bool shaderChanged;
		switch (type) {
			case ActorRendererType::Outline: shaderChanged = renderCommand_.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::Outline)); break;
			case ActorRendererType::WhiteMask: shaderChanged = renderCommand_.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::WhiteMask)); break;
			case ActorRendererType::PartialWhiteMask: shaderChanged = renderCommand_.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::PartialWhiteMask)); break;
			case ActorRendererType::FrozenMask: shaderChanged = renderCommand_.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::FrozenMask)); break;
			default: shaderChanged = renderCommand_.material().setShaderProgramType(Material::ShaderProgramType::SPRITE); break;
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

		if (IsAnimationRunning()) {
			switch (LoopMode) {
				case AnimationLoopMode::Loop:
					AnimTime += timeMult * FrameTimer::SecondsPerFrame;
					if (AnimTime > AnimDuration) {
						int n = (int)(AnimTime / AnimDuration);
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

	void ActorBase::ActorRenderer::UpdateVisibleFrames()
	{
		// Calculate visible frames
		CurrentFrame = 0;
		if (FrameCount > 0 && AnimDuration > 0.0f) {
			// Calculate currently visible frame
			float frameTemp = (FrameCount * AnimTime) / AnimDuration;
			CurrentFrame = (int)frameTemp;

			// Normalize current frame when exceeding anim duration
			if (LoopMode == AnimationLoopMode::Once || LoopMode == AnimationLoopMode::FixedSingle) {
				CurrentFrame = std::clamp(CurrentFrame, 0, FrameCount - 1);
			} else {
				CurrentFrame = NormalizeFrame(CurrentFrame, 0, FrameCount);
			}
		}
		CurrentFrame = FirstFrame + std::clamp(CurrentFrame, 0, FrameCount - 1);

		// Set current animation frame rectangle
		int col = CurrentFrame % FrameConfiguration.X;
		int row = CurrentFrame / FrameConfiguration.X;
		setTexRect(Recti(FrameDimensions.X * col, FrameDimensions.Y * row, FrameDimensions.X, FrameDimensions.Y));
		setAbsAnchorPoint((float)Hotspot.X, (float)Hotspot.Y);
	}

	int ActorBase::ActorRenderer::NormalizeFrame(int frame, int min, int max)
	{
		if (frame >= min && frame < max) return frame;

		if (frame < min) {
			return max + ((frame - min) % max);
		} else {
			return min + (frame % (max - min));
		}
	}
}