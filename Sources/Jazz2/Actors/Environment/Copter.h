#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Copter
		
		Flight item (the Lizard's spinning helicopter pack) that the player can grab to gain
		temporary hovering flight. Touching it attaches the copter to the rabbit; once its time
		runs out it detaches, sputters, and fades away.
	*/
	class Copter : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Copter();
		~Copter();

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Unmounts from the assigned actor */
		void Unmount(float timeLeft);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnDetach(ActorBase* parent) override;

	private:
		enum class State {
			Free,
			Unmounted,
			Mounted
		};

		Vector2f _originPos;
		float _phase;
		State _state;
#if defined(WITH_AUDIO)
		std::shared_ptr<AudioBufferPlayer> _noise;
#endif
		float _noiseDec;
	};
}