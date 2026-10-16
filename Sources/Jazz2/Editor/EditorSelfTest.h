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

		Every level in the directory (`Cache/Episodes` by default) is loaded into a @ref LevelDocument and:
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

		/** @brief Runs the self-test, returns `false` if anything failed */
		static bool Run(StringView directory);
	};
}

#endif
