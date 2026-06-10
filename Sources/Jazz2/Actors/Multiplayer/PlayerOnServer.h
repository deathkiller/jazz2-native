#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpPlayer.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Player on the server in online session */
	class PlayerOnServer : public MpPlayer
	{
		DEATH_RUNTIME_OBJECT(MpPlayer);

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		PlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool CanCauseDamage(ActorBase* collider) override;
		bool TakeDamage(std::int32_t amount, float pushForce = 0.0f, bool ignoreInvulnerable = false) override;
		bool AddLives(std::int32_t count) override;
		bool MorphTo(PlayerType type) override;

	protected:
		std::shared_ptr<ActorBase> _lastAttacker;
		float _lastAttackerTimeout;
		bool _canTakeDamage;
		bool _justWarped;
		/** @brief Remaining cooldown before this player can be bumped by another player again */
		float _bumpCooldown;
		/** @brief Whether player-vs-player collision dispatch has been enabled (done lazily after activation) */
		bool _bumpInitialized;

		void OnUpdate(float timeMult) override;

		bool IsAttacking() const;

	private:
		/** @brief Minimum number of frames between knockback resyncs to clients (throttles network traffic only) */
		static constexpr float BumpSyncIntervalFrames = 6.0f;
		/** @brief Maximum positional separation applied per frame when two players overlap, to avoid jarring jumps */
		static constexpr float MaxSeparationPerFrame = 4.0f;
		/** @brief Restitution (bounciness) of a player-vs-player bump; gameplay feel knob */
		static constexpr float BumpRestitution = 0.5f;
		/** @brief Minimum separating speed imparted by a bump so even gentle contact bounces noticeably; feel knob */
		static constexpr float BumpMinSeparationSpeed = 5.0f;

		/** @brief Keeps two overlapping players apart (so they can't pass through) and applies a bump impulse */
		void ApplyPlayerBump(PlayerOnServer& other);
	};
}

#endif