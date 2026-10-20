#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
	 * @brief Shows all recovery copies of levels, those of levels that weren't saved yet and those whose file is gone
	 *
	 * A copy can be restored, with the history of its changes, or removed. See @ref AutosaveService.
	 */
	void ShowRecoveryDialog(IEditorContext& session);
}

#endif
