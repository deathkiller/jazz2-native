#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/LevelDocument.h"
#include "../../EventType.h"

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Finds tiles or events of a kind and replaces them, or selects them

		A tile is taken from the brush, and matches the same tile of the same tile set --- flipped the same way, or
		any way. A replaced tile keeps how it was flipped and whether it was translucent. Events are found by their
		kind, a replaced one gets the parameters of the new kind and keeps its difficulties and flags.
	*/
	void ShowFindReplaceDialog(EditorSession& session);

	/**
		@brief Replaces a tile with another one in the layers of a level, as a step that can be undone

		All layers if @p layer is -1. Returns the number of tiles replaced.
	*/
	std::int32_t ReplaceTiles(EditorSession& session, std::int32_t layer, const TileRef& find, const TileRef& replace, bool anyFlip);
	/** @brief Replaces events of a kind with events of another kind, as a step that can be undone, returns how many */
	std::int32_t ReplaceEvents(EditorSession& session, EventType find, EventType replace);
}

#endif
