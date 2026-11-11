#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Weapons
{
	/**
		@brief TNT
		
		Placeable explosive that is dropped in place rather than fired. It detonates after a short fuse (sooner if an
		enemy or another exploding TNT comes near), pulsing and growing as the timer runs down, then damages all
		actors and destructible tiles within its blast radius.
	*/
	class TNT : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		TNT();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Returns owner of the TNT */
		Player* GetOwner();

		/** @brief Called when the TNT is deployed */
		void OnFire(const std::shared_ptr<ActorBase>& owner);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	private:
		/** @brief Converts a duration in ticks of the original to frames */
		static constexpr float TicksToFrames = 60.0f / 70.0f;
		/** @brief Ticks from placing to the blast */
		static constexpr float FuseTicks = 276.0f;
		/** @brief Ticks the countdown takes once something sets the TNT off */
		static constexpr float PreBlastTicks = 20.0f;
		/** @brief Ticks between two looks for something that sets the TNT off */
		static constexpr float ProximityTicks = 4.0f;
		/** @brief Radius of the blast in px */
		static constexpr float BlastRadius = 96.0f;
		/** @brief Most damage the blast does to an enemy */
		static constexpr std::int32_t MaxBlastDamage = 9;

		std::shared_ptr<ActorBase> _owner;
		float _proximityLeft;
		float _timeLeft;
		float _lightIntensity;
		bool _isExploded;
		std::int32_t _preexplosionTime;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}