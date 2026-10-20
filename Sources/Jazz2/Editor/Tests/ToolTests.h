#pragma once

#if (defined(WITH_EDITOR) && defined(DEATH_DEBUG)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	/**
		@brief Tests of the tools of the level editor and of the level it shows, run by @ref EditorSelfTest

		The tools are driven through @ref FakeToolHost, as the pointer would drive them, and the level shown is made of
		the layers of a document only, so none of them needs any game data.
	*/
	namespace ToolTests
	{
		/** @brief Tests the tools, returns the number of problems found */
		std::int32_t TestTools();
		/** @brief Tests that the level shown, brought up to date by @ref RuntimeSync, is the one loaded again, returns the number of problems found */
		std::int32_t TestRuntimeSync();
	}
}

#endif
