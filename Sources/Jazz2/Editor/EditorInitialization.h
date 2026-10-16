#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/String.h>

namespace Jazz2::Editor
{
	using Death::Containers::String;

	/**
		@brief Tells the level editor what to open
	*/
	struct EditorInitialization
	{
		/** @brief Level to open as `episode/level`, looked up as the game does */
		String LevelName;
		/** @brief Level file to open, it takes precedence over @ref LevelName */
		String FilePath;
		/** @brief Default tile set of a new level, used when neither a level nor a file is given */
		String NewLevelTileSet;
	};
}

#endif
