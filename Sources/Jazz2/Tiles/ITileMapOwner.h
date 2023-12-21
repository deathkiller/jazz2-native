#pragma once

#include "../Actors/ActorBase.h"

namespace Jazz2::Tiles
{
	class ITileMapOwner
	{
	public:
		virtual std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(const StringView identifier, const Vector3f& pos, float gain = 1.0f, float pitch = 1.0f) = 0;

		virtual void OnAdvanceDestructibleTileAnimation(std::int32_t tx, std::int32_t ty, std::int32_t amount) = 0;
		virtual void OnTileFrozen(std::int32_t x, std::int32_t y) = 0;

		virtual Vector2f GetCameraPos() const = 0;
		virtual Vector2i GetViewSize() const = 0;
	};
}