#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../ActorBase.h"

namespace Jazz2::Actors::Multiplayer
{
	/** @brief Remote object in online session */
	class RemoteActor : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		RemoteActor();

		void AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType);
		/** @brief Sets the per-player recolor (0 = none); reloads the sprites indexed and (re)applies the palette */
		void SetPlayerColor(std::uint32_t furColor);
		/** @brief Changes the metadata (e.g. on character change), keeping the current recolor applied */
		void ChangeMetadata(StringView path);
		void SyncPositionWithServer(Vector2f pos);
		void SyncAnimationWithServer(AnimState anim, float rotation, float scaleX, float scaleY, Actors::ActorRendererType rendererType);
		void SyncMiscWithServer(std::uint8_t flags);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		struct StateFrame {
			std::int64_t Time;
			Vector2f Pos;
		};

		static constexpr std::int64_t ServerDelay = 64;

		StateFrame _stateBuffer[8];
		std::int32_t _stateBufferPos;
		AnimState _lastAnim;
		bool _isAttachedLocally;
		std::uint32_t _furColor;
		std::unique_ptr<Texture> _colorPalette;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnAttach(ActorBase* parent) override;
		void OnDetach(ActorBase* parent) override;
	};
}

#endif