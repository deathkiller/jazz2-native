#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Weapons
{
	class TNT : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		TNT();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		Player* GetOwner();

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
		int _preexplosionTime;
		std::shared_ptr<AudioBufferPlayer> _noise;
	};
}