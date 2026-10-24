#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Returns the event id at the given tile coordinates */
	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty);
	/** @brief Returns a field of the event at the given tile coordinates */
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length);
	/** @brief Sets the event id at the given tile coordinates */
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId);
	/** @brief Sets a field of the event at the given tile coordinates */
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, int8_t offset, std::int8_t length, std::int32_t newValue);
	/** @brief Returns the collision type of the given tile */
	std::int8_t GetTileType(std::uint16_t tile);
	/** @brief Sets the collision type of the given tile */
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value);
	/** @brief Returns whether the given tile uses 32-bit colors, `jjTileIs32Bit[]` */
	bool GetTileIs32Bit(std::uint16_t tile);
}

#endif
