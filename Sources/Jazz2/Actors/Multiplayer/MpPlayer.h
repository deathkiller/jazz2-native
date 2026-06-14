#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Player.h"
#include "../../Multiplayer/PeerDescriptor.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Player in online session
		
		Base for any player participating in a multiplayer session. It extends the single-player @ref Player with a
		peer descriptor identifying the connected peer the player belongs to, and with shared networked behavior such
		as resolving standing on top of another player.
	*/
	class MpPlayer : public Player
	{
		DEATH_RUNTIME_OBJECT(Player);

		friend class Jazz2::Multiplayer::MpLevelHandler;

	public:
		/** @brief Returns session peer descriptor */
		std::shared_ptr<PeerDescriptor> GetPeerDescriptor();
		/** @overload */
		std::shared_ptr<const PeerDescriptor> GetPeerDescriptor() const;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::shared_ptr<PeerDescriptor> _peerDesc;
		/** @brief Whether this (locally-simulated) player is currently standing on another player */
		bool _stackCarrying = false;

		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;

		/**
		 * @brief Locally resolves standing on another player (one-way platform via the carrying object)
		 *
		 * This player rests on top and can jump off. Call before the base @ref Player::OnUpdate so jump input sees it.
		 *
		 * @param timeMult  Frame time multiplier
		 * @param snap      Whether to reposition onto the other player's head (the side that owns/simulates this
		 *                  player); `false` only grounds it (for the server's shadow of a remote player, whose
		 *                  position comes from its client)
		 */
		void UpdatePlayerStacking(float timeMult, bool snap);
#endif
	};
}

#endif