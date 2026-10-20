#pragma once

#if (defined(WITH_EDITOR) && defined(DEATH_DEBUG)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
#endif

	/**
		@brief Self-test of the level editor, run by the `/editor-selftest [directory]` command-line switch

		Tests of the parts of the editor that need no game data run first: the document and its commands, the files,
		the images, the event catalog, the tools driven through a host without the shown level, and more. With
		`/editor-selftest --unit` only those run (see @ref RunUnit()), so no converted game is needed. Then every
		level in the directory (`Cache/Episodes` by default) is loaded into a @ref LevelDocument and:
		- exported again, which has to give the same file --- or, for a level written by another tool, a file that
		  reads back to the same level
		- saved with editor data, which has to survive being loaded again and be dropped by the export
		- changed by a random sequence of edit commands, which, undone, have to give the original level back, and
		  redone, the changed one

		Only in debug builds.
	*/
	class EditorSelfTest
	{
	public:
		EditorSelfTest() = delete;

		/** @brief Runs the whole self-test with the levels of a directory, returns `false` if anything failed */
		static bool Run(StringView directory);
		/** @brief Runs only the tests that need no game data, returns `false` if anything failed */
		static bool RunUnit();
	};
}

#endif
