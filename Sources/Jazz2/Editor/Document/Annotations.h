#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

namespace Jazz2::Editor
{
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;

	class LevelDocument;

	/**
		@brief A bookmark or a note placed in a level, for the editor only

		A bookmark remembers a place of the level under a number from 1 to 9, a note is a text at a place. Both are
		kept in the editor-only chunk `anno` (see @ref Formats::LevelFile::IsEditorOnlyChunk()), so the exported level
		has none of them. They're not changes of the level, so they aren't undone, but they make it unsaved.
	*/
	struct Annotation
	{
		/** @brief Tag of the chunk they're kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('a', 'n', 'n', 'o');

		/** @brief Position in the level in pixels */
		std::int32_t X = 0;
		/** @copydoc X */
		std::int32_t Y = 0;
		/** @brief Number of a bookmark, from 1 to 9, 0 for a note */
		std::uint8_t Slot = 0;
		/** @brief Text of a note, or the name of a bookmark, may be empty */
		String Text;

		/** @brief Returns `true` if it's a bookmark */
		bool IsBookmark() const {
			return (Slot != 0);
		}

		/** @brief Reads all of them from a level */
		static void Read(const LevelDocument& document, SmallVectorImpl<Annotation>& annotations);
		/** @brief Replaces all of them in a level */
		static void Write(LevelDocument& document, const SmallVectorImpl<Annotation>& annotations);
	};
}

#endif
