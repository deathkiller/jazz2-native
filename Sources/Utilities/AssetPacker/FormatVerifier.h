#pragma once

#include "../../Main.h"

#include <Containers/StringView.h>

namespace Jazz2::AssetPacker
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::StringView;
#endif

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

		/**
		 * @brief Verifies all levels and tile sets under the directory (recursively), returns `false` if any of them differs
		 *
		 * With @p sweepTruncated, every level and tile set is also read cut short at many places, none of which may be
		 * read as something it isn't. That reads each file some 76 more times, so it's opt-in.
		 */
		static bool VerifyDirectory(StringView path, bool sweepTruncated = false);

		/** @brief Limits and reproducibility of @ref FuzzDirectory() */
		struct FuzzOptions {
			/** @brief Number of mutated inputs to try */
			std::int32_t Iterations = 10000;
			/** @brief Seed of the mutations, the same seed tries the same inputs again */
			std::uint64_t Seed = 1;
			/** @brief Seconds a single input may take before it counts as a hang */
			std::int32_t TimeoutSeconds = 30;
			/** @brief Megabytes the process may use before an input counts as asking for absurd memory (Windows only) */
			std::int32_t MemoryLimitMegabytes = 3072;
			/**
			 * @brief Only levels with translated texts, and their chunk attacked more
			 *
			 * Every level gets a chunk of translated texts made by its writer (see @ref Formats::LevelTextsChunk),
			 * otherwise only every fourth level gets a copy with one.
			 */
			bool FocusLevelTexts = false;
		};

		/**
		 * @brief Attacks the readers with mutated copies of all levels and tile sets under the directory
		 *
		 * Every input is a native level or tile set with seeded random mutations --- of the file itself, of the
		 * decompressed content (inflated, mutated and deflated again, so the readers are attacked rather than the
		 * framing of the compression), of the chunks of a level, of its records, and of the tile sets it embeds.
		 * Every reader is run on each of them, and what a reader accepts has to survive being written and read
		 * again. An input that crashes, asserts, hangs, asks for absurd memory or doesn't survive is saved into
		 * @p outputPath. If @p path is a file, it's only run once through the readers, to reproduce a finding.
		 * Returns `false` if anything was found.
		 */
		static bool FuzzDirectory(StringView path, StringView outputPath, const FuzzOptions& options);
	};
}
