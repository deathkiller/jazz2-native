#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"

#include <Containers/Function.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
#endif

	/**
		@brief Keeps a copy of unsaved changes, so they aren't lost if the game ends unexpectedly

		While a level has unsaved changes, it's written every minute --- and when the application is suspended ---
		into `Editor/Recovery` in the configuration directory, one file for each level file. The copy is removed
		when the level is saved or its changes are discarded, so a copy that is still there when the level is
		opened again holds changes that would be lost otherwise.
	*/
	class AutosaveService
	{
	public:
		/** @brief Chunk with the path of the level file the copy belongs to */
		static constexpr std::uint32_t RecoveryChunk = Formats::LevelFile::MakeChunkTag('r', 'c', 'o', 'v');

		AutosaveService();

		/** @brief Sets the level file the copy belongs to, empty for a level that wasn't saved yet */
		void SetSource(StringView filePath);
		/** @brief Writes the copy when it's time to */
		void Update(float timeMult, LevelDocument& document, CommandHistory& history);
		/** @brief Writes the copy now if there are unsaved changes */
		bool SaveNow(LevelDocument& document, CommandHistory& history);
		/** @brief Removes the copy */
		void Discard();

		/** @brief Called before the copy is written, to put into the level what isn't kept in it all the time */
		Function<void()> BeforeSave;

		/** @brief Returns the copy of a level file if it's newer than the file, or an empty string */
		static String FindRecovery(StringView filePath);
		/** @brief Returns where the copy of a level file is kept */
		static String GetRecoveryPath(StringView filePath);

	private:
		// Time between two copies, in frames at 60 FPS
		static constexpr float Interval = 60.0f * 60.0f;

		String _filePath;
		float _timer;
		std::uint32_t _writtenRevision;
	};
}

#endif
