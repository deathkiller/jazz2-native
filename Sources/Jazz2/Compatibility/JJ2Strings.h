#pragma once

#include "../../Common.h"
#include "JJ2Level.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Compatibility
{
	class JJ2Strings // .j2s
	{
	public:
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

		JJ2Strings(const String& name)
			: Name(name)
		{
		}

		bool Open(const StringView& path);

		void Convert(const String& targetPath, std::function<JJ2Level::LevelToken(const StringView&)> levelTokenConversion);

		static String RecodeString(const StringView& text, bool stripFormatting = false, bool escaped = false);
	};
}