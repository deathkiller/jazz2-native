#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayerOnServer.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Remote player in online session */
	class RemotePlayerOnServer : public PlayerOnServer
	{
		DEATH_RUNTIME_OBJECT(PlayerOnServer);

	public:
		RemotePlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		bool OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType) override;

		void SyncWithServer(Vector2f pos, const Vector2f& speed, bool isVisible, bool isFacingLeft, bool isActivelyPushing);

	protected:
		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		static constexpr std::int64_t ServerDelay = 64;

		StateFrame _stateBuffer[8];
		std::int32_t _stateBufferPos;
		Vector2f _displayPos;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnPerish(ActorBase* collider) override;
		void OnUpdate(float timeMult) override;

		void OnHitSpring(Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove) override;

		void WarpToPosition(Vector2f pos, WarpFlags flags) override;
		bool TakeDamage(std::int32_t amount, float pushForce) override;

		bool AddAmmo(WeaponType weaponType, std::int16_t count) override;
		void AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade) override;
		bool FireCurrentWeapon(WeaponType weaponType) override;
		void EmitWeaponFlare() override;
		void SetCurrentWeapon(WeaponType weaponType) override;
	};
}

#endif