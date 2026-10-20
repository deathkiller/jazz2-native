#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"

#include <Containers/Function.h>

#include <memory>

// The copy is written on a thread of its own where there are threads and memory for a copy of the level
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_CONSTRAINED_MEMORY)
#	define NCINE_AUTOSAVE_THREADED
#	include "../Common/BackgroundJob.h"
#endif

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/**
		@brief Keeps a copy of unsaved changes, so they aren't lost if the game ends unexpectedly

		While a level has unsaved changes, it's written every minute --- and when the application is suspended ---
		into `Editor/Recovery` in the configuration directory, one file for each level file. The copy is removed
		when the level is saved, its changes are discarded, or they're all undone, so a copy that is still there when
		the level is opened again holds changes that would be lost otherwise. The copy is written through a temporary
		file, so a failed write never replaces a good copy. It keeps the history of the changes too (see
		@ref CommandArchive), so they can be undone after they're restored.

		Where there are threads, a copy of the level is taken and written on a thread of its own, the editor doesn't
		stop while it's compressed.
	*/
	class AutosaveService
	{
	public:
		/** @brief Chunk with the path of the level file the copy belongs to */
		static constexpr std::uint32_t RecoveryChunk = Formats::LevelFile::MakeChunkTag('r', 'c', 'o', 'v');

		AutosaveService();
		~AutosaveService();

		AutosaveService(const AutosaveService&) = delete;
		AutosaveService& operator=(const AutosaveService&) = delete;

		/** @brief Sets the level file the copy belongs to, empty for a level that wasn't saved yet */
		void SetSource(StringView filePath);
		/** @brief Writes the copy when it's time to */
		void Update(float timeMult, LevelDocument& document, CommandHistory& history);
		/**
		 * @brief Writes the copy now if there are unsaved changes
		 *
		 * With @p wait it's written before this returns, as when the application is suspended, otherwise it may be
		 * written on a thread of its own and taken as written by a later @ref Update().
		 */
		bool SaveNow(LevelDocument& document, CommandHistory& history, bool wait = false);
		/** @brief Removes the copy */
		void Discard();

		/** @brief Called before the copy is written, to put into the level what isn't kept in it all the time */
		Function<void()> BeforeSave;

		/**
		 * @brief Returns the copy of a level file if it's newer than the file, or an empty string
		 *
		 * For a level that wasn't saved yet, the newest copy of such a level is returned, of any run, and it becomes the
		 * copy of this run --- discarding the changes removes it, new changes are written over it.
		 */
		static String FindRecovery(StringView filePath);
		/** @brief Returns where the copy of a level file is kept, each run has its own for a level that wasn't saved yet */
		static String GetRecoveryPath(StringView filePath);

	private:
		// Name of the copy of a level that wasn't saved yet, of this run
		static String& GetUntitledName();

		// Time between two copies, in frames at 60 FPS
		static constexpr float Interval = 60.0f * 60.0f;

		String _filePath;
		float _timer;
		std::uint32_t _writtenRevision;
		std::uint32_t _writtenOutsideChanges;

#if defined(NCINE_AUTOSAVE_THREADED)
		// A copy being written on the thread, the thread owns it until it's done
		struct PendingCopy
		{
			LevelDocument Document;
			String Path;
			std::uint32_t Revision;
			std::uint32_t OutsideChanges;
			// Set by the thread, read once it's done
			bool Success = false;
		};

		std::unique_ptr<PendingCopy> _pending;
		// Writes the pending copy, there's one at most
		std::unique_ptr<BackgroundJob> _job;
#endif

		// Whether the copy holds the current state
		bool IsWritten(const CommandHistory& history) const;
		// Waits for a copy being written on the thread and takes it as written
		void FinishWriting();
		// Takes a copy as written, if it was
		bool Complete(bool success, StringView path, std::uint32_t revision, std::uint32_t outsideChanges);
	};
}

#endif
