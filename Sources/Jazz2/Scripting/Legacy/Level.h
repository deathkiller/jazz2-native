#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Common.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/**
	 * @brief Returns the event ID placed on a tile of the sprite layer
	 *
	 * Tiles are addressed by `xTile` and `yTile`. The number uses the event numbering of the original game, so it can
	 * be compared with @ref OBJECT::Object and @ref AREA::Area constants.
	 */
	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty);
	/**
	 * @brief Returns one parameter of the event placed on a tile
	 *
	 * The parameter is selected the same way the event definitions of the original level editor describe it.
	 * - `length` is the bit count of the parameter; a negative value marks a signed parameter.
	 * - `offset` is the sum of the absolute lengths of all parameters stored before the requested one, so the first
	 *   parameter is at offset 0.
	 *
	 * An `offset` of -4 with a `length` of 2 reads the difficulty bits of the event instead. Returns 0 if the
	 * requested bits fall outside the event data.
	 *
	 * @param offset Position of the first bit, relative to the first parameter
	 * @param length Number of bits, negative for a signed value
	 * @partialsupport The difficulty bits of the events of a converted level aren't kept, they read 0 unless a
	 * script wrote them.
	 */
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length);
	/**
	 * @brief Replaces the event placed on a tile
	 *
	 * `newEventID` is the new event number, e.g. an @ref OBJECT::Object or @ref AREA::Area constant. Only the event ID
	 * changes, the parameters already stored on the tile are kept.
	 * @partialsupport A generator event (an object that respawns) can't be written, it's refused with a warning.
	 */
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId);
	/**
	 * @brief Changes one parameter of the event placed on a tile
	 *
	 * `offset` and `length` select the bits exactly as in @ref jjParameterGet(); the new value is truncated to fit them.
	 * Objects that already exist may have read their parameters when they were created and won't notice the change,
	 * areas that affect the player and objects created later will.
	 *
	 * @param offset Position of the first bit, relative to the first parameter
	 * @param length Number of bits, negative for a signed value
	 * @param newValue Value to store
	 * @partialsupport The parameters of a generator event can't be written, they're refused with a warning.
	 */
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, int8_t offset, std::int8_t length, std::int32_t newValue);
	/**
	 * @brief Returns the type of a tile, indexed by tile ID
	 *
	 * 0 is an ordinary tile, 1 translucent, 3 invisible and 5 heat-distorted.
	 * @partialsupport Other types are kept as assigned, but such tiles are drawn as ordinary ones.
	 */
	std::int8_t GetTileType(std::uint16_t tile);
	/** @brief Sets the type of a tile, indexed by tile ID */
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value);
	/**
	 * @brief Whether a tile uses 32-bit colors, indexed by tile ID
	 *
	 * `false` for tiles that use palette colors and for empty or undefined tiles.
	 */
	bool GetTileIs32Bit(std::uint16_t tile);
}

#endif
