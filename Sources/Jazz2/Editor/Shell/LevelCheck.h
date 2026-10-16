#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using nCine::Vector2i;
#endif

	class EditorSession;
	class LevelDocument;

	/** @brief How bad a problem of a level is */
	enum class LevelProblemSeverity : std::uint8_t
	{
		/** Something doesn't work at all, as a warp that leads nowhere */
		Error,
		/** Something probably doesn't work as it was meant to */
		Warning,
		/** Worth a look, but often meant so */
		Info
	};

	/**
		@brief A problem found in a level, see @ref CheckLevel()
	*/
	struct LevelProblem
	{
		/** @brief How bad it is */
		LevelProblemSeverity Severity;
		/** @brief What's wrong, translated */
		String Text;
		/** @brief Tile of the event grid it's at, `-1` if it isn't at any */
		Vector2i Cell = Vector2i(-1, -1);
	};

	/**
		@brief Looks for what's probably wrong in a level

		Starts missing for a character, warps without a target, coin warps needing more coins than there are, texts
		that don't exist, triggers that are set but change nothing and the other way around, rolling rocks that never
		roll, parameters out of their range, unknown events and more than one sky. Errors come first.
	*/
	void CheckLevel(const LevelDocument& document, SmallVectorImpl<LevelProblem>& problems);

	/**
	 * @brief Adds what the player can't get to from the start: an exit, and things to collect, see
	 * @ref EditorSession::ComputeReachability()
	 *
	 * With the events as the level was loaded last, as springs and poles take the player places.
	 */
	void CheckReachability(EditorSession& session, SmallVectorImpl<LevelProblem>& problems);

	/** @brief Adds problems of tiles that need their tile sets to tell, as tiles in palette colors on a layer drawn as Blur Behind */
	void CheckLayerTiles(EditorSession& session, SmallVectorImpl<LevelProblem>& problems);

	/** @brief Shows the problems of the level, each can be shown in the view */
	void ShowCheckLevelDialog(EditorSession& session);
}

#endif
