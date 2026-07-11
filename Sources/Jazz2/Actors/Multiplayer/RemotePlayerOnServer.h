#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayerOnServer.h"
#include "StateInterpolationBuffer.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Remote player in online session
		
		A connected client's player as simulated and shadowed on the server. Its input (pressed keys) and state are
		received from the owning client and applied here, with the reported position buffered and interpolated to
		smooth out network delay.
	*/
	class RemotePlayerOnServer : public PlayerOnServer
	{
		DEATH_RUNTIME_OBJECT(PlayerOnServer);

	public:
		/** @brief State flags of the remote player */
		enum class PlayerFlags {
			None = 0,					/**< None */

			SpecialMoveMask = 0x07,		/**< Mask of the current special move */

			IsFacingLeft = 0x10,		/**< Player is facing left */
			IsVisible = 0x20,			/**< Player is visible */
			IsActivelyPushing = 0x40,	/**< Player is actively pushing against something */

			JustWarped = 0x100,			/**< Player has just warped */

			EnableContinuousJump = 0x200,	/**< Continuous jump is enabled */

			InMenu = 0x1000,			/**< Player has a menu open */
			InConsole = 0x2000			/**< Player has the console open */
		};

		/** @brief State flags */
		PlayerFlags Flags;
		/** @brief Pressed keys */
		std::uint64_t PressedKeys;
		/** @brief Previously pressed keys */
		std::uint64_t PressedKeysLast;
		/** @brief Last frame when pressed keys were updated */
		std::uint32_t UpdatedFrame;

		DEATH_PRIVATE_ENUM_FLAGS(PlayerFlags);

		/**
		 * @brief Creates a new instance
		 *
		 * @param peerDesc  Descriptor of the peer this player belongs to
		 */
		RemotePlayerOnServer(std::shared_ptr<PeerDescriptor> peerDesc);

		bool IsContinuousJumpAllowed() const override;
		bool IsLedgeClimbAllowed() const override;

		bool OnHandleCollision(ActorBase* other) override;
		bool OnLevelChanging(Actors::ActorBase* initiator, ExitType exitType) override;
		PlayerCarryOver PrepareLevelCarryOver() override;

		void WarpToPosition(Vector2f pos, WarpFlags flags) override;
		bool SetModifier(Modifier modifier, const std::shared_ptr<ActorBase>& decor) override;
		bool Freeze(float timeLeft) override;
		void SetInvulnerability(float timeLeft, InvulnerableType type) override;

		void AddScore(std::int32_t amount) override;
		bool AddHealth(std::int32_t amount) override;
		bool AddLives(std::int32_t count) override;
		bool AddAmmo(WeaponType weaponType, std::int16_t count) override;
		void AddWeaponUpgrade(WeaponType weaponType, std::uint8_t upgrade) override;
		bool SetDizzy(float timeLeft) override;

		bool FireCurrentWeapon(WeaponType weaponType) override;
		void EmitWeaponFlare() override;
		void SetCurrentWeapon(WeaponType weaponType, SetCurrentWeaponReason reason) override;

		/** @brief Synchronizes the player with server */
		void SyncWithServer(Vector2f pos, Vector2f speed, PlayerFlags flags);

		/** @brief Forcefully resynchronizes the player with server (e.g., after respawning or warping) */
		void ForceResyncWithServer(Vector2f pos, Vector2f speed);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		StateInterpolationBuffer _stateBuffer;
		Vector2f _displayPos;
		/** @brief Last "being stood on" state sent to the owning client, to only resync on change */
		bool _beingStoodOnLastSent = false;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

		void OnPushSolidObject(float timeMult, float pushSpeedX);
		void OnHitSpring(Vector2f pos, Vector2f force, bool keepSpeedX, bool keepSpeedY, bool& removeSpecialMove) override;
	};
}

#endif