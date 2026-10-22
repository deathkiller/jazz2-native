#pragma once

#include "LevelFile.h"
#include "../LevelDescriptor.h"

namespace Jazz2::Formats
{
	/**
		@brief Chunk with the texts of a level translated to other languages

		The level texts are written in one language, this chunk holds their translations, so the game can show the
		texts in the language it's in (see @ref LevelHandler::GetLevelText()). It's an ancillary chunk, a reader that
		doesn't know it --- the game before 4.0.0 --- skips it and shows the texts of the level as they are. It's
		kept when the level is exported.

		The payload is @cpp u8 @ce version (1), @cpp varuint @ce number of languages, then for each a @cpp varuint @ce
		size of its record and the record: @cpp str8 @ce language, @cpp varuint @ce number of texts, and for each
		@cpp varuint @ce number of the text, @cpp varuint @ce length and the UTF-8 bytes. A newer version may add
		fields at the end of a record, a reader skips those it doesn't know.
	*/
	struct LevelTextsChunk
	{
		/** @brief Tag of the chunk */
		static constexpr std::uint32_t Tag = LevelFile::MakeChunkTag('L', 'T', 'X', 'T');
		/** @brief Version this build writes */
		static constexpr std::uint8_t Version = 1;
		/** @brief Most languages a level can have */
		static constexpr std::uint32_t MaxLanguages = 64;
		/** @brief Most texts a level can have, as an event refers to one by a byte */
		static constexpr std::uint32_t MaxTexts = 256;
		/** @brief Longest translated text in bytes */
		static constexpr std::uint32_t MaxTextLength = 65535;

		LevelTextsChunk() = delete;

		/** @brief Reads the payload, returns `false` if it's damaged, what was read until then is kept */
		static bool Read(ArrayView<const std::uint8_t> data, SmallVectorImpl<LocalizedLevelTexts>& languages);
		/** @brief Writes the payload, nothing for no languages, a language without any translated text is left out */
		static void Write(ArrayView<const LocalizedLevelTexts> languages, SmallVectorImpl<std::uint8_t>& data);

		/**
		 * @brief Finds the texts of a language
		 *
		 * Looks for the language itself, then for the language without its region (`pt` for `pt-br`). Returns
		 * `nullptr` if neither is there.
		 */
		static const LocalizedLevelTexts* Find(ArrayView<const LocalizedLevelTexts> languages, StringView language);
	};
}
