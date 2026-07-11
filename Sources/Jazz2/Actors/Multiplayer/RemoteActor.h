#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../ActorBase.h"
#include "StateInterpolationBuffer.h"
#include "../../ShieldType.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Remote object in online session
		
		A generic actor whose transform, animation and miscellaneous state are synchronized from a remote authority
		(the server). It has no gameplay logic of its own; the received position is buffered and interpolated on the
		receiving side to smooth out network delay.
	*/
	class RemoteActor : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		RemoteActor();
		~RemoteActor();

		/** @brief Initializes the actor from metadata received from the server */
		void AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType);
		/** @brief Sets the per-player recolor (0 = none); reloads the sprites indexed and (re)applies the palette */
		void SetPlayerColor(std::uint32_t furColor);
		/** @brief Changes the metadata (e.g., on character change), keeping the current recolor applied */
		void ChangeMetadata(StringView path);
		/** @brief Synchronizes the position with the server */
		void SyncPositionWithServer(Vector2f pos);
		/** @brief Synchronizes the animation and transform with the server */
		void SyncAnimationWithServer(AnimState anim, float rotation, float scaleX, float scaleY, Actors::ActorRendererType rendererType);
		/** @brief Synchronizes miscellaneous state flags with the server */
		void SyncMiscWithServer(std::uint8_t flags);
		/** @brief Sets the active shield shown around this remote player (synced from the server) */
		void SetShield(ShieldType shieldType, float timeLeft);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		StateInterpolationBuffer _stateBuffer;
		AnimState _lastAnim;
		bool _isAttachedLocally;
		std::uint32_t _furColor;
		// Allocated palette offset into the shared palette texture for this player's recolor (-1 = none)
		std::int32_t _paletteOffset;
		// Active shield shown around this player (synced from the server). The decoration is drawn in OnDraw; the
		// time decays locally, mirroring the owning player (the server only sends shield state changes).
		ShieldType _activeShield;
		float _activeShieldTime;
		std::unique_ptr<RenderCommand> _shieldRenderCommands[2];
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnAttach(ActorBase* parent) override;
		void OnDetach(ActorBase* parent) override;

		// Allocates/updates/releases this player's palette row from _furColor and selects it on the renderer
		void RefreshColorPalette();
	};
}

#endif