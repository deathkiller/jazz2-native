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
		std::shared_ptr<ActorBase> _owner;
		float _timeLeft;
		float _lightIntensity;
		bool _isExploded;
		std::int32_t _preexplosionTime;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}