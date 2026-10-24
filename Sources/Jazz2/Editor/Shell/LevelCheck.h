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

	class IEditorContext;
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

	/** @brief How a problem of a level can be fixed by a click, see @ref LevelProblem::Fix */
	enum class LevelProblemFix : std::uint8_t
	{
		/** Not by a click */
		None,
		/** Removes the warp of @ref LevelProblem::FixValue, all of its tiles */
		RemoveWarp,
		/** Places a target of the warp of @ref LevelProblem::FixValue in the middle of the view */
		AddWarpTarget,
		/** Places a start of the characters of @ref LevelProblem::FixValue in the middle of the view, as the event has them */
		AddStart,
		/** Places a multiplayer start of the team of @ref LevelProblem::FixValue in the middle of the view */
		AddMultiplayerStart,
		/** Places a CTF base of the team of @ref LevelProblem::FixValue in the middle of the view */
		AddCtfBase
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
		/** @brief Index of the event off the grid it's about, `-1` if it isn't about any, @ref Cell is the tile it's in then */
		std::int32_t OffGrid = -1;
		/** @brief Index of the layer it's about, `-1` if it isn't about any */
		std::int32_t Layer = -1;
		/** @brief How it's fixed by a click, see @ref FixLevelProblem() */
		LevelProblemFix Fix = LevelProblemFix::None;
		/** @brief Another way to fix it by a click, with the same @ref FixValue */
		LevelProblemFix AlternativeFix = LevelProblemFix::None;
		/** @brief What the fix is done with, as a warp identifier or a team */
		std::int32_t FixValue = 0;
	};

	/**
		@brief Looks for what's probably wrong in a level

		Starts missing for a character, warps without a target, coin warps needing more coins than there are, texts
		that don't exist, triggers that are set but change nothing and the other way around, rolling rocks that never
		roll, parameters out of their range, unknown events and more than one sky. For multiplayer, CTF bases missing
		or twice for a team, teams with a base but no start, too few multiplayer starts, a race with laps without
		waypoints, gaps in the order of the waypoints, and events only for multiplayer in a level
		that isn't. Errors come first. Problems of the same kind are told once, with how many there are and where the
		first one is.
	*/
	void CheckLevel(const LevelDocument& document, SmallVectorImpl<LevelProblem>& problems);

	/**
	 * @brief Adds what the player can't get to from the start: an exit, and things to collect, see
	 * @ref EditorSession::ComputeReachability()
	 *
	 * With the events as the level was loaded last, as springs and poles take the player places, so the level
	 * should be loaded again first, see @ref EditorSession::ReloadRuntime(). Adds nothing without multiplayer
	 * compiled in, whose route tracer it uses.
	 */
	void CheckReachability(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems);

	/** @brief Adds problems of tiles that need their tile sets to tell, as tiles in palette colors on a layer drawn as Blur Behind */
	void CheckLayerTiles(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems);

	/**
		@brief Adds what makes a layer drawn black, wrong or not at all, the texts without the name of the layer

		A layer hidden in the game that has something to show, a color that multiplies its tiles nearly black, a layer that
		isn't drawn by its alpha or the transparent entry it's tinted with, a textured background that isn't 8x8 tiles or
		whose tiles are all empty, a wave without waves, the horizon of a reflection at its top or its bottom, x-ray tiles of
		a single frame. With @p aboutOthers also that a textured background or a reflection after it keeps it from being
		drawn (@ref CheckLevel() tells those once for the level), and that an empty layer is hidden in the game. The
		problems need no tile sets to tell, see @ref CountPaletteColorTiles() for those that do.
	*/
	void CheckLayerDrawing(const LevelDocument& document, std::int32_t layerIndex, bool aboutOthers, SmallVectorImpl<LevelProblem>& problems);

	/** @brief Returns how many tiles of a layer are in palette colors, which do nothing on a layer drawn as Blur Behind */
	std::int32_t CountPaletteColorTiles(IEditorContext& session, std::int32_t layerIndex);

	/**
	 * @brief Adds problems the shown level tells, events inside solid tiles and warp targets in walls, and files
	 *
	 * The levels that follow and the music that aren't found, and a script that can't refer to all tile IDs.
	 */
	void CheckLevelSurroundings(IEditorContext& session, SmallVectorImpl<LevelProblem>& problems);

	/**
	 * @brief Fixes a problem by @ref LevelProblem::Fix or @ref LevelProblem::AlternativeFix with @ref LevelProblem::FixValue
	 *
	 * As an undoable step, what's placed goes to the empty tile nearest to the middle of the view. Returns `false` if
	 * it couldn't be fixed.
	 */
	bool FixLevelProblem(IEditorContext& session, LevelProblemFix fix, std::int32_t value);
	/** @brief Returns the label of the button that fixes a problem */
	String GetLevelProblemFixLabel(LevelProblemFix fix);

	/** @brief Shows the problems of the level, each can be shown in the view */
	void ShowCheckLevelDialog(IEditorContext& session);
}

#endif
