#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Enums.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/**
	 * @brief Returns the file name of the current level
	 *
	 * Only the name with the `.j2l` extension appended, e.g. `castle1.j2l`, without any directories. This is the name
	 * the original file had, not the episode path the game uses internally.
	 */
	String getLevelFileName();
	/**
	 * @brief Returns the title of the current level
	 *
	 * The name shown to players, e.g. on the loading screen. Scripts may assign a new title.
	 */
	String getCurrLevelName();
	/** @brief Sets the title of the current level */
	void setCurrLevelName(const String& in);
	/**
	 * @brief Returns the file name of the tile set used by the current level
	 *
	 * If the level combines several tile sets, the first one is returned. Empty if the level has no tile map.
	 */
	String get_jjTilesetFileName();

	/**
	 * @brief Returns the state of the multiplayer game
	 *
	 * @partialsupport Always returns @ref GAME::STOPPED (0).
	 */
	std::int32_t get_gameState();

	// TODO

	/**
	 * @brief Returns the width of the black border on the left and right side of each subscreen
	 *
	 * The value is the size of one border, not of both together.
	 * @partialsupport Always returns 0, there are no such borders.
	 */
	std::int32_t getBorderWidth();
	/**
	 * @brief Returns the height of the black border on the top and bottom side of each subscreen
	 *
	 * @partialsupport Always returns 0, there are no such borders.
	 */
	std::int32_t getBorderHeight();
	/**
	 * @brief Whether the screen is split vertically when there are exactly two local players
	 *
	 * @partialsupport Returns the value a script assigned, `false` before that, not how the screen is split.
	 */
	bool getSplitscreenType();
	/**
	 * @brief Sets whether the screen is split vertically
	 *
	 * @partialsupport The value is only stored, the split follows the player's settings.
	 */
	bool setSplitscreenType(bool value);

	// TODO

	/**
	 * @brief Returns the current score of a team in team-based game modes
	 *
	 * Indexed by @ref TEAM::Color.
	 * @partialsupport Always returns 0.
	 */
	std::int32_t get_teamScore(std::int32_t color);
	/**
	 * @brief Returns the highest health a player can have
	 *
	 * Taken from the first player, 5 if there is no player yet.
	 */
	std::int32_t GetMaxHealth();
	/**
	 * @brief Returns the health a player starts with
	 *
	 * Players always start at full health, so this is the same value as @ref jjMaxHealth.
	 */
	std::int32_t GetStartHealth();

	/**
	 * @brief Whether a team is enabled in the current game
	 *
	 * Indexed by @ref TEAM::Color.
	 * @partialsupport Always returns `false`.
	 */
	bool getEnabledTeam(std::uint8_t team);

	/**
	 * @brief Whether a key or a mouse button is currently held down
	 *
	 * Indexed by Windows virtual-key codes, e.g. `0x41` for <em>A</em> or `0x20` for <em>Space</em>. Codes 1, 2, 4, 5
	 * and 6 are the left, right, middle and the two extra mouse buttons. Codes `0x10`, `0x11` and `0x12` match
	 * <em>Shift</em>, <em>Ctrl</em> and <em>Alt</em> on either side. Keys without a counterpart are never reported
	 * as held.
	 */
	bool getKeyDown(std::uint8_t key);
	/**
	 * @brief Returns the horizontal position of the mouse cursor
	 *
	 * In pixels of the game view, measured from its left edge, so it stays consistent with drawing even when
	 * the window is scaled. Add @ref jjPLAYER::cameraX to get a position in the level.
	 */
	std::int32_t getCursorX();
	/**
	 * @brief Returns the vertical position of the mouse cursor
	 *
	 * In pixels of the game view, measured from its top edge. Add @ref jjPLAYER::cameraY to get a position in the level.
	 */
	std::int32_t getCursorY();
}

#endif
