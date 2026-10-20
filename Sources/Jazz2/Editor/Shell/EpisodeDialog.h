#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
	 * @brief Shows the episode of the level being edited
	 *
	 * The episode is the directory the level is in, described by the `.j2e` file next to that directory. Its display
	 * name, first level, the episodes before and after it, its position in the list and its images can be changed and
	 * saved, a new episode file is made if there's none. The levels of the episode are listed in the order the links
	 * between them lead from the first one --- next, secret and bonus levels --- with the links that lead nowhere and
	 * the levels nothing leads to marked. A level can be renamed, the links to it in the other levels of the episode
	 * are changed along, their files are kept as `.bak`. Opening the level being edited only closes the dialog. A level
	 * that wasn't saved yet is offered to be saved first.
	 *
	 * The directory `unknown` isn't an episode, it's where the levels of no episode are, so it has no episode file
	 * and nothing of an episode to set. For a level there, it's asked first whether to make a new episode for it, to
	 * move it into an episode that can be written to, or to list the levels of `unknown`. The level is moved as
	 * @ref EditorSession::SaveAsFile() moves it --- only copied if its file can't be changed --- its own links to
	 * levels of `unknown` by just their names get `unknown/`, and the links of the other levels to it are changed to
	 * lead into the episode, their files kept as `.bak`.
	 */
	void ShowEpisodeDialog(IEditorContext& session);
}

#endif
