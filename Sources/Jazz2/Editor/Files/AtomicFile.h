#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/Function.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::StringView;
	using Death::IO::Stream;
#endif

	/**
		@brief Writes a file so that a failed write never damages the file that was there

		The content goes into a temporary file next to the target first. Every write is checked, the file
		is flushed to the device, and only then it replaces the target in one step. If anything fails, the
		target is left untouched and the temporary file is removed.
	*/
	class AtomicFile
	{
	public:
		AtomicFile() = delete;

		/**
		 * @brief Writes a file through a temporary file
		 *
		 * @param path         Target file
		 * @param write        Writes the content, returns `false` if it failed
		 * @param keepBackup   Whether the previous file is kept as `<path>.bak`
		 * @return `true` if the target holds the new content
		 */
		static bool Write(StringView path, Function<bool(Stream&)>&& write, bool keepBackup = false);

		/** @brief Writes a block of bytes into a file through a temporary file */
		static bool Write(StringView path, const void* data, std::int64_t size, bool keepBackup = false);

		/**
		 * @brief Writes the content only into the temporary file of @p path, the target isn't touched yet
		 *
		 * For several files that change together: all of them are staged first, and only if every one was written,
		 * they're committed (see @ref Commit()), otherwise discarded (see @ref Discard()). Returns `false` if the
		 * temporary file couldn't be written whole, it's removed then.
		 */
		static bool Stage(StringView path, Function<bool(Stream&)>&& write);
		/**
		 * @brief Replaces @p path with its temporary file written by @ref Stage()
		 *
		 * With @p keepBackup, the previous file is kept as `<path>.bak`, so the change can be reverted by
		 * @ref RestoreBackup(). Returns `false` if the target couldn't be replaced, it stays as it was then.
		 */
		static bool Commit(StringView path, bool keepBackup = false);
		/** @brief Removes the temporary file of @p path written by @ref Stage() */
		static void Discard(StringView path);
		/**
		 * @brief Puts back the file kept by @ref Commit() as `<path>.bak`
		 *
		 * If there was no file before, the file is removed. Returns `false` if it couldn't be put back.
		 */
		static bool RestoreBackup(StringView path, bool existedBefore);
	};
}

#endif
