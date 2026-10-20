#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
	 * @brief Shows the dialog that exports the level as a package to publish, a ZIP archive
	 *
	 * The archive has the files laid out as in the content of the game, so it can be unpacked into the user content:
	 * the level as it would be exported, optionally all levels of its episode with the episode file, their scripts,
	 * the tile sets and the music they use that aren't of the game itself, and the event descriptions of the user
	 * content. A `manifest.json` describes the package (name, author, version, description, tags, levels, files) and
	 * `preview.png` shows the level. What the package says of itself is kept in the level, in the editor-only chunk
	 * `pkgi`, so it's offered again next time.
	 */
	void ShowExportPackageDialog(IEditorContext& session);
}

#endif
