#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/StringView.h>

#include <cstdint>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
	using nCine::Vector2f;
#endif

	class EditorSession;

	/**
		@brief Shows the whole level small, with the bookmarks and the notes

		A click or a drag in the small level moves the view there. Each bookmark can be gone to, set to the middle
		of the view or cleared, each note gone to, changed or removed.
	*/
	void ShowNavigator(EditorSession& session);

	/** @brief Remembers the middle of the view as a bookmark, from 1 to 9 */
	void SetBookmark(EditorSession& session, std::int32_t slot);
	/** @brief Moves the view to a bookmark, from 1 to 9, returns `false` if it isn't set */
	bool GoToBookmark(EditorSession& session, std::int32_t slot);
	/** @brief Adds a note at a position in the level, it's written in @ref ShowNavigator() */
	void AddNote(EditorSession& session, Vector2f worldPos, StringView text = {});
}

#endif
