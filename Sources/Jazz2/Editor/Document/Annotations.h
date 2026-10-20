#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
#endif

	class LevelDocument;

	/**
		@brief A bookmark or a note placed in a level, for the editor only

		A bookmark remembers a place of the level under a number from 1 to 9, a note is a text at a place. Both are
		kept in the editor-only chunk `ann2` (see @ref Formats::LevelFile::IsEditorOnlyChunk()), so the exported level
		has none of them. They're changed by a @ref SetChunkCommand, so they're undone as any other change.

		The chunk is @cpp u8 @ce version (2), @cpp varuint @ce count, then for each one a @cpp varuint @ce size of its
		record and the record: @cpp i32 @ce X, @cpp i32 @ce Y, @cpp u8 @ce slot, @cpp varuint @ce length and the text.
		Version 2 adds to a record @cpp u32 @ce color, the author as @cpp varuint @ce length and text, @cpp i64 @ce
		time, @cpp u8 @ce flags (1 resolved), @cpp varuint @ce number of replies, each its author, @cpp i64 @ce time
		and text. A newer version may add fields at the end of a record, a reader skips those it doesn't know.
	*/
	struct Annotation
	{
		/** @brief Tag of the chunk they're kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('a', 'n', 'n', '2');
		/** @brief Tag of the chunk 3.9.0 kept them in, without a version, only read */
		static constexpr std::uint32_t LegacyChunk = Formats::LevelFile::MakeChunkTag('a', 'n', 'n', 'o');

		/** @brief Position in the level in pixels */
		std::int32_t X = 0;
		/** @copydoc X */
		std::int32_t Y = 0;
		/** @brief Number of a bookmark, from 1 to 9, 0 for a note */
		std::uint8_t Slot = 0;
		/** @brief Text of a note, or the name of a bookmark, may be empty */
		String Text;
		/** @brief Color as @cpp R | G << 8 | B << 16 | A << 24 @ce, 0 for the usual one */
		std::uint32_t Color = 0;
		/** @brief Who wrote the note, may be empty */
		String Author;
		/** @brief When the note was written, in seconds since 1970, 0 if it isn't known */
		std::int64_t Time = 0;
		/** @brief Whether what the note is about was done, the note is shown dimmer then */
		bool Resolved = false;

		/** @brief A reply to a note */
		struct Reply
		{
			/** @brief Who wrote it, may be empty */
			String Author;
			/** @brief When it was written, in seconds since 1970, 0 if it isn't known */
			std::int64_t Time = 0;
			/** @brief Text */
			String Text;
		};
		/** @brief Replies to the note, the oldest first */
		SmallVector<Reply, 0> Replies;

		/** @brief Returns `true` if it's a bookmark */
		bool IsBookmark() const {
			return (Slot != 0);
		}

		/**
		 * @brief Reads all of them from a level
		 *
		 * The legacy chunk wins over the current one if the level has both, it was written by 3.9.0 after this version
		 * saved the level, as this version never writes it.
		 */
		static void Read(const LevelDocument& document, SmallVectorImpl<Annotation>& annotations);
		/**
		 * @brief Replaces all of them in a level, directly
		 *
		 * Not undoable by itself, for a change made within another undoable step. Otherwise use @ref Serialize() and
		 * a @ref SetChunkCommand of @ref Chunk.
		 */
		static void Write(LevelDocument& document, const SmallVectorImpl<Annotation>& annotations);
		/** @brief Writes all of them into bytes of the chunk, no bytes for none */
		static void Serialize(const SmallVectorImpl<Annotation>& annotations, SmallVectorImpl<std::uint8_t>& data);
		/**
		 * @brief Converts the chunk of 3.9.0 to the current one, if the level has it
		 *
		 * What the legacy chunk can't hold is kept from the current one for the annotations that are the same in both.
		 */
		static void UpgradeLegacyChunk(LevelDocument& document);
	};
}

#endif
