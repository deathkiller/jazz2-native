#pragma once

#include "../../../Main.h"

#if (defined(WITH_EDITOR) && defined(DEATH_TARGET_EMSCRIPTEN)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/Function.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Moves files between the level editor and the computer the browser runs on

		The web build has no files of the user, a level is opened by uploading it --- the browser asks for a file,
		which is copied into the memory of the game --- and saved by downloading it. What the editor keeps by itself, the
		settings and the copies of unsaved changes, is kept in the storage of the browser (see
		@ref Death::IO::FileSystem::MountAsPersistent()).
	*/
	struct WebFileBridge
	{
		/**
		 * @brief Asks the browser for a file of one of the extensions, as `.j2l;.j2t`
		 *
		 * The file is copied to a temporary place and @p onPicked gets its path, in a frame later. Nothing happens if
		 * the user doesn't choose any.
		 */
		static void PickFile(StringView extensions, Function<void(StringView)>&& onPicked);
		/** @brief Gives the browser a file to save to the computer, by the name it has */
		static bool DownloadFile(StringView path);
		/** @brief Passes the files picked since the last call on, call it every frame */
		static void Update();
	};
}

#endif
