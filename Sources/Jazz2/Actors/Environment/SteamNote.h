#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Steam note
		
		Decorative musical note that periodically puffs into view with a sound effect and then
		fades out, repeating on a cycle. It lifts a player in it while it is shown, it has no other effect on the
		player.
	*/
	class SteamNote : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		SteamNote();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnAnimationFinished() override;
		bool OnHandleCollision(ActorBase* other) override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		/** @brief Speed the steam lifts a player at, 1 px per tick of the original */
		static constexpr float LiftSpeed = 70.0f / 60.0f;

		float _cooldown;
	};
}