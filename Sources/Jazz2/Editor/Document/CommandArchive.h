#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "CommandHistory.h"
#include "DocumentSnapshot.h"

namespace Jazz2::Editor
{
	/**
		@brief Keeps the history of changes in a recovery copy of a level, so they can be undone after it's restored

		The commands are written as they are, with what they hold to be done and undone. Only the newest ones that fit
		@ref MaxSize are kept, and none older than a command that can't be written. The history is written in the native
		byte order and layout of the records, a copy made by another build or on another machine is restored without it.
		The snapshots are kept as they are too, so the fingerprint includes @ref DocumentSnapshot::EncodingVersion. The
		layers they keep get new identities when they're read (see @ref LayerIdRemap).

		The chunk is @cpp u8 @ce version (3), a fingerprint of the layout of the records, @cpp varuint @ce number of
		commands, @cpp varuint @ce number of those done, @cpp varuint @ce number of those done in the saved state plus
		one (0 if it isn't among them), then the commands, each a @cpp varuint @ce size and the command. A command is
		written once and kept with its entry of the history while it stays the same, so writing the history again costs
		only the commands that changed.
	*/
	class CommandArchive
	{
	public:
		/** @brief Editor-only chunk of a recovery copy with the history */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('r', 'h', 's', 't');
		/** @brief Most bytes of the history written, the oldest commands are left out */
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
		static constexpr std::size_t MaxSize = 2 * 1024 * 1024;
#else
		static constexpr std::size_t MaxSize = 16 * 1024 * 1024;
#endif

		CommandArchive() = delete;

		/** @brief Writes the history, no bytes if there's nothing to keep */
		static void Write(const CommandHistory& history, SmallVectorImpl<std::uint8_t>& data);
		/**
		 * @brief Reads the history into a history just made for the document
		 *
		 * The document has to be as it was when the history was written. Returns `false` if the history can't be read,
		 * the history stays empty then.
		 */
		static bool Read(ArrayView<const std::uint8_t> data, LevelDocument& document, CommandHistory& history);

	private:
		class Writer;
		class Reader;

		static bool WriteCommand(Writer& w, const EditCommand& command);
		static std::unique_ptr<EditCommand> ReadCommand(Reader& r, LevelDocument& document, std::int32_t depth);
		static void WriteBuffer(Writer& w, const CompressedBuffer& buffer);
		static bool ReadBuffer(Reader& r, CompressedBuffer& buffer);
		static void WriteSnapshot(Writer& w, const DocumentSnapshot& snapshot);
		static bool ReadSnapshot(Reader& r, DocumentSnapshot& snapshot);
	};
}

#endif
