#pragma once

#include "../Actors/ActorBase.h"

namespace Jazz2::Tiles
{
	/** @brief Interface used to notify tile map owner of various events */
	class ITileMapOwner
	{
	public:
		/** @brief Plays a common sound effect */
		virtual std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;

		/** @brief Called when a destructible tile animation is advanced */
		virtual void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) = 0;
		/** @brief Called when a tile is frozen */
		virtual void OnTileFrozen(std::int32_t x, std::int32_t y) = 0;
	};
}