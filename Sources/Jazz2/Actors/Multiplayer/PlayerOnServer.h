#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayer.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Player on the server in online session
		
		Server-side base for a player simulated on the host, adding server-authoritative behavior on top of @ref
		MpPlayer. It handles damage, lives and morphing as the authority, and player-vs-player collisions (keeping
		overlapping players apart and applying bump impulses).
	*/
	class PlayerOnServer : public MpPlayer
	{
		DEATH_RUNTIME_OBJECT(MpPlayer);

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		/** @brief Creates a new instance */
		PlayerOnServer();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;
		bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false) override;
		bool AddLives(std::int32_t count) override;
		bool MorphTo(PlayerType type) override;
		bool SetShield(ShieldType shieldType, float timeLeft) override;
		bool IncreaseShieldTime(float timeLeft) override;
		void DecreaseShieldTime(float time) override;

	protected:
		/** @brief Actor that last attacked this player (cleared once @ref _lastAttackerTimeout elapses) */
		std::shared_ptr<ActorBase> _lastAttacker;
		/** @brief Remaining time before the last attacker reference is cleared */
		float _lastAttackerTimeout;
		/** @brief Whether the player can currently take damage */
		bool _canTakeDamage;
		/** @brief Whether the player has just warped */
		bool _justWarped;
		/** @brief Remaining cooldown before this player can be bumped by another player again */
		float _bumpCooldown;
		/** @brief Whether player-vs-player collision dispatch has been enabled (done lazily after activation) */
		bool _bumpInitialized;

		void OnUpdate(float timeMult) override;

		/** @brief Returns `true` if the player is currently performing a damaging move */
		bool IsAttacking() const;

	private:
		/** @brief Minimum number of frames between knockback resyncs to clients (throttles network traffic only) */
		static constexpr float BumpSyncIntervalFrames = 6.0f;
	};
}

#endif