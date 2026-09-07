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

		bool IsLedgeClimbAllowed() const override;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		std::shared_ptr<PeerDescriptor> _peerDesc;

		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;

		/**
		 * @brief Drives the spectator camera, either free-flying or locked onto another player
		 *
		 * Extends the free camera of @ref Player::OnHandleSpectate() with a follow mode: @ref PlayerAction::Fire
		 * steps through everyone still playing and then back to the free camera, and while locked on, the movement
		 * keys (which aren't flying the camera then) step between players and @ref PlayerAction::Run releases it.
		 * Only ever runs for the player of the local session; the server-side shadow of a remote spectator keeps
		 * getting its position from the owning client.
		 */
		void OnHandleSpectate(float timeMult) override;
#endif
	};
}

#endif