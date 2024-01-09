#pragma once

#include "../../Common.h"
#include "JJ2Version.h"

#include "../../nCine/Base/HashMap.h"

#include <Containers/Pair.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Compatibility
{
	enum class JJ2DefaultPalette {
		Sprite,
		Menu
	};

	class AnimSetMapping
	{
	public:
		static constexpr char Discard[] = ":discard";

		struct Entry {
			String Category;
			String Name;
			std::uint32_t Ordinal;

			JJ2DefaultPalette Palette;
			bool SkipNormalMap;
			bool AllowRealtimePalette;
		};

		Entry* Get(std::uint32_t set, std::uint32_t item);
		Entry* GetByOrdinal(std::uint32_t index);

		static AnimSetMapping GetAnimMapping(JJ2Version version);
		static AnimSetMapping GetSampleMapping(JJ2Version version);

	private:
		JJ2Version _version;
		HashMap<std::uint32_t, Entry> _entries;
		std::uint32_t _currentItem;
		std::uint32_t _currentSet;
		std::uint32_t _currentOrdinal;

		AnimSetMapping(JJ2Version version);

		void DiscardItems(std::uint32_t advanceBy, JJ2Version appliesTo = JJ2Version::All);
		void SkipItems(std::uint32_t advanceBy = 1);
		void NextSet(std::uint32_t advanceBy = 1, JJ2Version appliesTo = JJ2Version::All);
		void Add(JJ2Version appliesTo, const StringView& category, const StringView& name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
		void Add(const StringView& category, const StringView& name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
	};
}