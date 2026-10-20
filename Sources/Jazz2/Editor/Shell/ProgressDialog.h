#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Common/BackgroundJob.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
	 * @brief Runs work off the user interface and shows how far it is in a window that can stop it
	 *
	 * The work runs on a thread of its own where the platform has threads, see @ref BackgroundJob, it must not touch
	 * the document, the session or anything else the user interface uses, only what it was given. @p onDone runs on
	 * the main thread afterwards, with whether the work was stopped, the results are taken there. Without threads, the
	 * work runs right away and the window isn't shown at all. If the session goes away meanwhile, the work is stopped
	 * and waited for, @p onDone isn't called then.
	 */
	void RunWithProgress(IEditorContext& session, StringView title, Function<void(JobProgress&)>&& work, Function<void(bool)>&& onDone);

	/**
	 * @brief Returns `true` if @p path is @p root itself or a file or directory inside it
	 *
	 * Paths are compared in lowercase and with `/` between directories, as the editor compares them elsewhere, and only
	 * whole names count, `Episodes-old/x.j2l` isn't inside `Episodes`. It only compares the text, it can run on any
	 * thread.
	 */
	bool IsPathInside(StringView path, StringView root);

	/**
	 * @brief Returns `true` if a level other than @p ownPath seems to be open in another editor
	 *
	 * An editor keeps `<level>.lock` next to the level and writes it again every half a minute, a lock older than that
	 * was left behind. A level changed here while it's open there would be written over by saving it there. It only
	 * reads the file system, it can run on any thread.
	 */
	bool IsLevelOpenElsewhere(StringView levelPath, StringView ownPath);
}

#endif
