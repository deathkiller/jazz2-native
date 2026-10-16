#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

namespace Jazz2::AssetPacker
{
	using Death::Containers::StringView;

	/**
		@brief Checks that every level and tile set in a directory survives being read and written again unchanged

		Each native level (`*.j2l` with the game's own signature) under the directory is read completely by
		@ref Jazz2::Formats::LevelReader and written back by @ref Jazz2::Formats::LevelWriter into memory, and
		each native tile set (`*.j2t`) by @ref Jazz2::Formats::TileSetFile. The result has to be byte for byte
		the file it came from --- or, for a file written by another tool, which compresses differently and may
		store some things the long way, read back to the same content. Original JJ2 files share the extensions
		and are skipped. This exercises both halves of the formats on real data, which is what the level and
		tile set editors rely on when they save a file they opened.
	*/
	class FormatVerifier
	{
	public:
		FormatVerifier() = delete;

		/** @brief Verifies all levels and tile sets under the directory (recursively), returns `false` if any of them differs */
		static bool VerifyDirectory(StringView path);
	};
}
