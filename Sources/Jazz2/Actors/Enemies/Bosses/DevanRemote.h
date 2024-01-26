#pragma once

#include "BossBase.h"
#include "Robot.h"

namespace Jazz2::Actors::Bosses
{
	class DevanRemote : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		DevanRemote();
		~DevanRemote();

		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		bool OnPlayerDied() override;
		void OnUpdate(float timeMult) override;

	private:
		uint8_t _introText, _endText;
		std::shared_ptr<Robot> _robot;
	};
}