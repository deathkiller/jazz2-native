#pragma once

#if defined(WITH_MULTIPLAYER)

#include "ActorBase.h"

namespace Jazz2::Actors
{
	class RemoteActor : public ActorBase
	{
	public:
		RemoteActor();

		void SyncWithServer(const Vector2f& pos, AnimState anim, bool isVisible, bool isFacingLeft);

	protected:
		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		static constexpr std::int64_t ServerDelay = 64;

		StateFrame _stateBuffer[4];
		std::int32_t _stateBufferPos = 0;
		AnimState _lastAnim;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
	};
}

#endif