#pragma once

#if defined(WITH_MULTIPLAYER)

#include "../ActorBase.h"

namespace Jazz2::Actors::Multiplayer
{
	class RemoteActor : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		RemoteActor();

		void AssignMetadata(const StringView& path, AnimState anim, ActorState state);
		void SyncWithServer(const Vector2f& pos, AnimState anim, float rotation, bool isVisible, bool isFacingLeft, bool animPaused, Actors::ActorRendererType rendererType);

	protected:
		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		static constexpr std::int64_t ServerDelay = 64;

		StateFrame _stateBuffer[8];
		std::int32_t _stateBufferPos;
		AnimState _lastAnim;

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
	};
}

#endif