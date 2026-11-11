#pragma once

#include "BossBase.h"
#include "Robot.h"

namespace Jazz2::Actors::Bosses
{
	/**
		@brief Devan with remote control (boss)
		
		Devan Shell in his remote-control form, where he hovers safely out of reach and pilots a giant Robot
		to fight in his place. The player cannot harm Devan directly; instead the Robot must be destroyed,
		after which Devan warps out and the fight ends. Devan's displayed health mirrors the Robot's.
	*/
	class DevanRemote : public BossBase
	{
		DEATH_RUNTIME_OBJECT(BossBase);

	public:
		/** @brief Creates a new instance */
		DevanRemote();
		~DevanRemote();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		bool OnActivatedBoss() override;
		bool OnPlayerDied() override;
		void OnUpdate(float timeMult) override;

	private:
		/** @brief Delay in frames between the end of the explosions and the warp out (71 ticks) */
		static constexpr float WarpOutDelay = 71.0f / TicksPerFrame;

		std::uint8_t _introText, _endText;
		bool _ending;
		float _endDelay;
		std::shared_ptr<Robot> _robot;
	};
}