#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Enums.h"

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Returns the file name of the current level */
	String getLevelFileName();
	/** @brief Returns the display name of the current level */
	String getCurrLevelName();
	/** @brief Sets the display name of the current level */
	void setCurrLevelName(const String& in);
	/** @brief Returns the file name of the current tileset */
	String get_jjTilesetFileName();

	/** @brief Returns the current game state */
	std::int32_t get_gameState();

	// TODO

	std::int32_t getBorderWidth();
	/** @brief Returns the height of the screen border */
	std::int32_t getBorderHeight();
	/** @brief Returns whether split-screen is forced */
	bool getSplitscreenType();
	/** @brief Sets whether split-screen is forced */
	bool setSplitscreenType(bool value);

	// TODO

	std::int32_t get_teamScore(std::int32_t color);
	/** @brief Returns the maximum player health */
	std::int32_t GetMaxHealth();
	/** @brief Returns the starting player health */
	std::int32_t GetStartHealth();

	/** @brief Returns whether the given team is enabled */
	bool getEnabledTeam(std::uint8_t team);

	/** @brief Returns whether the given key is currently held down */
	bool getKeyDown(std::uint8_t key);
	/** @brief Returns the cursor's horizontal position */
	std::int32_t getCursorX();
	/** @brief Returns the cursor's vertical position */
	std::int32_t getCursorY();
}

#endif
