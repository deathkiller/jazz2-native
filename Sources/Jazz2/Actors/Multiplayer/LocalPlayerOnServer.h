#pragma once

#if defined(WITH_MULTIPLAYER)

#include "PlayerOnServer.h"

namespace Jazz2::Actors::Multiplayer
{
	class LocalPlayerOnServer : public PlayerOnServer
	{
		DEATH_RUNTIME_OBJECT(PlayerOnServer);

	public:
		LocalPlayerOnServer();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		void SyncWithServer(const Vector2f& pos, const Vector2f& speed, bool isVisible, bool isFacingLeft, bool isActivelyPushing);

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

		void OnHitSpring(const Vector2f& pos, const Vector2f& force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove) override;

		bool TakeDamage(std::int32_t amount, float pushForce) override;

		bool AddAmmo(WeaponType weaponType, std::int16_t count) override;
		void AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade) override;
		bool FireCurrentWeapon(WeaponType weaponType) override;
		void SetCurrentWeapon(WeaponType weaponType) override;
	};
}

#endif