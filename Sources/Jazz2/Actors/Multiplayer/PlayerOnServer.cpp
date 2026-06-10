#include "PlayerOnServer.h"

#if defined(WITH_MULTIPLAYER)

#include "../Weapons/ShotBase.h"
#include "../Weapons/FreezerShot.h"
#include "../../Multiplayer/MpLevelHandler.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::Actors::Multiplayer
{
	PlayerOnServer::PlayerOnServer()
		: _lastAttackerTimeout(0.0f), _canTakeDamage(true), _justWarped(false), _bumpCooldown(0.0f), _bumpInitialized(false)
	{
	}

	void PlayerOnServer::OnUpdate(float timeMult)
	{
		MpPlayer::OnUpdate(timeMult);

		// Enable player-vs-player collision dispatch once. Activation sets actor state asynchronously and
		// never touches CollideWithOtherActors, so it is safe to enable it here; the base warp logic may
		// still toggle it during transitions, which is intentional.
		if (!_bumpInitialized) {
			_bumpInitialized = true;
			if (_playerType != PlayerType::Spectate) {
				SetState(ActorState::CollideWithOtherActors, true);
			}
		}

		if (_bumpCooldown > 0.0f) {
			_bumpCooldown -= timeMult;
		}

		if (_lastAttackerTimeout > 0.0f) {
			_lastAttackerTimeout -= timeMult;
			if (_lastAttackerTimeout <= 0.0f) {
				_lastAttacker = nullptr;
			}
		}
	}

	bool PlayerOnServer::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		// Players physically bump each other (team-independent). Attacking players deal damage instead
		// (handled below), so they don't bump. Separation runs every overlapping frame so players can't
		// pass through each other; only the network resync is throttled (see _bumpCooldown).
		if (auto* anotherPlayer = runtime_cast<PlayerOnServer>(other.get())) {
			if (_health > 0 && anotherPlayer->_health > 0 && !IsAttacking() && !anotherPlayer->IsAttacking()) {
				ApplyPlayerBump(*anotherPlayer);
			}
		}

		// TODO: Check player special move here
		if (auto* weaponOwner = MpLevelHandler::GetWeaponOwner(other.get())) {
			auto* otherPlayerOnServer = static_cast<PlayerOnServer*>(weaponOwner);
			if (_health > 0 && GetPeerDescriptor()->Team != otherPlayerOnServer->GetPeerDescriptor()->Team) {
				bool otherIsPlayer = false;
				if (auto* anotherPlayer = runtime_cast<PlayerOnServer>(other.get())) {
					bool isAttacking = IsAttacking();
					if (!isAttacking && !anotherPlayer->IsAttacking()) {
						return true;
					}
					if (isAttacking) {
						return false;
					}
					otherIsPlayer = true;
				}

				_lastAttacker = otherPlayerOnServer->shared_from_this();
				_lastAttackerTimeout = 300.0f;

				// Decrease remaining shield time by 5 secs
				if (_activeShieldTime > (5.0f * FrameTimer::FramesPerSecond)) {
					_activeShieldTime -= (5.0f * FrameTimer::FramesPerSecond);
				} else if (auto* freezerShot = runtime_cast<Weapons::FreezerShot>(other.get())) {
					Freeze(3.0f * FrameTimer::FramesPerSecond);
				} else {
					TakeDamage(1, 4.0f * (_pos.X > other->GetPos().X ? 1.0f : -1.0f));
				}
				if (!otherIsPlayer) {
					other->DecreaseHealth(INT32_MAX);
				}
				return true;
			}
		}

		return Player::OnHandleCollision(std::move(other));
	}

	void PlayerOnServer::ApplyPlayerBump(PlayerOnServer& other)
	{
		// Compute the overlap of the two collision boxes and resolve along the axis of least penetration
		// (minimum translation vector), the standard way to keep two AABBs from interpenetrating
		const AABBf& a = AABBInner;
		const AABBf& b = other.AABBInner;
		float overlapX = std::min(a.R, b.R) - std::max(a.L, b.L);
		float overlapY = std::min(a.B, b.B) - std::max(a.T, b.T);
		if (overlapX <= 0.0f || overlapY <= 0.0f) {
			// Boxes don't actually overlap (e.g. per-pixel collision reported the hit) - nothing to separate
			return;
		}

		Vector2f normal;
		float penetration;
		if (overlapX < overlapY) {
			normal = Vector2f(_pos.X < other._pos.X ? -1.0f : 1.0f, 0.0f);
			penetration = overlapX;
		} else {
			normal = Vector2f(0.0f, _pos.Y < other._pos.Y ? -1.0f : 1.0f);
			penetration = overlapY;
		}

		// Positional separation so the players can't pass through each other. MoveInstantly checks the
		// tilemap, so a player is never pushed into a wall (it just stays put if blocked). Capped per frame
		// to avoid a jarring jump when the overlap is large.
		float push = std::min(penetration * 0.5f, MaxSeparationPerFrame);
		MoveInstantly(Vector2f(normal.X * push, normal.Y * push), MoveType::Relative);
		other.MoveInstantly(Vector2f(-normal.X * push, -normal.Y * push), MoveType::Relative);

		// Equal-mass elastic impulse along the same axis, applied only while the two are approaching so it
		// can't compound across frames. Restitution and the minimum are gameplay feel knobs.
		Vector2f relativeSpeed = _speed - other._speed;
		float approachSpeed = relativeSpeed.X * normal.X + relativeSpeed.Y * normal.Y;
		if (approachSpeed < 0.0f) {
			float impulse = std::max(-(1.0f + BumpRestitution) * approachSpeed * 0.5f, BumpMinSeparationSpeed);
			_speed += normal * impulse;
			other._speed += normal * (-impulse);
		}

		// Server is authoritative: resync the post-bump position/velocity to the owning clients so they
		// don't overwrite it with their own (stale) state. Throttled so sustained contact doesn't spam.
		if (_bumpCooldown <= 0.0f) {
			_bumpCooldown = BumpSyncIntervalFrames;
			other._bumpCooldown = BumpSyncIntervalFrames;

			auto* mpHandler = static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler);
			mpHandler->HandlePlayerBumped(this);
			mpHandler->HandlePlayerBumped(&other);
		}
	}

	bool PlayerOnServer::CanCauseDamage(ActorBase* collider)
	{
		if (_canTakeDamage && _health > 0) {
			if (auto* weaponOwner = MpLevelHandler::GetWeaponOwner(collider)) {
				return (static_cast<PlayerOnServer*>(weaponOwner)->GetPeerDescriptor()->Team != GetPeerDescriptor()->Team);
			}
		}

		return false;
	}

	bool PlayerOnServer::TakeDamage(std::int32_t amount, float pushForce, bool ignoreInvulnerable)
	{
		if (!_canTakeDamage || !MpPlayer::TakeDamage(amount, pushForce, ignoreInvulnerable)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerTakeDamage(this, amount, pushForce);

		return true;
	}

	bool PlayerOnServer::AddLives(std::int32_t count)
	{
		// TODO: OneUpCollectible are currently blocked in multiplayer
		return false;
	}

	bool PlayerOnServer::MorphTo(PlayerType type)
	{
		if (!MpPlayer::MorphTo(type)) {
			return false;
		}

		static_cast<Jazz2::Multiplayer::MpLevelHandler*>(_levelHandler)->HandlePlayerMorphTo(this, type);
	
		return true;
	}

	bool PlayerOnServer::IsAttacking() const
	{
		if (_currentSpecialMove == SpecialMoveType::Buttstomp && _currentTransition != nullptr && _sugarRushLeft <= 0.0f) {
			return false;
		}

		return (_currentSpecialMove != SpecialMoveType::None || _sugarRushLeft > 0.0f);
	}
}

#endif