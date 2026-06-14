#pragma once

#include "../../Main.h"
#include "JJ2Level.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/**
		@brief Parses original `.j2s` localization files
		
		Reads an original localization file containing the shared and per-level text strings, recodes
		them from the original encoding, and writes the converted localization to the target path. Level
		names can be remapped through the supplied conversion callback.
	*/
	class JJ2Strings
	{
	public:
		/** @brief Texts for specific level */
		struct LevelEntry
		{
			/** @brief Level name */
			String Name;
			/** @brief Texts of the level's text events */
			SmallVector<String, 0> TextEvents;

			/** @brief Creates a new instance for the specified level name */
			LevelEntry(String name)
				: Name(name)
			{
			}
		};

		/** @brief Name of the localization */
		String Name;
		/** @brief Texts shared across all levels */
		SmallVector<String, 0> CommonTexts;
		/** @brief Per-level texts */
		SmallVector<LevelEntry, 0> LevelTexts;

		/** @brief Creates a new instance */
		JJ2Strings() { }

		/** @brief Creates a new instance with the specified name */
		JJ2Strings(StringView name)
			: Name(name)
		{
		}

		/** @brief Opens and parses the specified localization file */
		bool Open(StringView path);

		/** @brief Converts the localization and writes the result to the specified target path */
		void Convert(StringView targetPath, Function<JJ2Level::LevelToken(StringView)>&& levelTokenConversion);

		/** @brief Recodes a string from the original encoding, optionally stripping formatting or escaping it */
		static String RecodeString(StringView text, bool stripFormatting = false, bool escaped = false);
	};
}