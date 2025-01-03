#pragma once

#include "../../Main.h"
#include "JJ2Level.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	/** @brief Parses original `.j2s` localization files */
	class JJ2Strings
	{
	public:
		/** @brief Texts for specific level */
		struct LevelEntry
		{
			String Name;
			SmallVector<String, 0> TextEvents;

			LevelEntry(String name)
				: Name(name)
			{
			}
		};

		String Name;
		SmallVector<String, 0> CommonTexts;
		SmallVector<LevelEntry, 0> LevelTexts;

		JJ2Strings() { }

		JJ2Strings(StringView name)
			: Name(name)
		{
		}

		bool Open(StringView path);

		void Convert(StringView targetPath, Function<JJ2Level::LevelToken(StringView)>&& levelTokenConversion);

		static String RecodeString(StringView text, bool stripFormatting = false, bool escaped = false);
	};
}