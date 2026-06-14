#pragma once

#include "../../Main.h"
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
	/**
		@brief Default palette for original animation
		
		Selects which of the original game's built-in palettes an imported animation entry should be
		decoded against: the in-game sprite palette or the dedicated menu palette. Stored per entry in
		@ref AnimSetMapping::Entry.
	*/
	enum class JJ2DefaultPalette {
		Sprite,
		Menu
	};

	/**
		@brief Maps indices from original data file to organized entries
		
		Provides the lookup tables used during animation and audio import that translate the original
		game's set/item indices (from `Anims.j2a`) into named categories, asset names and palette
		settings for the converted output. The mapping is built per @ref JJ2Version.
	*/
	class AnimSetMapping
	{
	public:
		/** @{ @name Constants */

		/** @brief Specifies that the entry should be discarded */
		static constexpr char Discard[] = ":discard";

		/** @} */

		/** @brief Mapped entry */
		struct Entry {
			/** @brief Category name */
			String Category;
			/** @brief Entry name */
			String Name;
			/** @brief Sequential ordinal of the entry */
			std::uint32_t Ordinal;

			/** @brief Default palette used by the entry */
			JJ2DefaultPalette Palette;
			/** @brief Whether normal map generation should be skipped */
			bool SkipNormalMap;
			/** @brief Whether the entry allows realtime palette changes */
			bool AllowRealtimePalette;
		};

		/** @brief Returns the entry for the specified set and item, or `nullptr` if not found */
		Entry* Get(std::uint32_t set, std::uint32_t item);
		/** @brief Returns the entry with the specified ordinal index, or `nullptr` if not found */
		Entry* GetByOrdinal(std::uint32_t index);

		/** @brief Returns mapping of animations for the specified version */
		static AnimSetMapping GetAnimMapping(JJ2Version version);
		/** @brief Returns mapping of sounds for the specified version */
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
		void Add(JJ2Version appliesTo, StringView category, StringView name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
		void Add(StringView category, StringView name, JJ2DefaultPalette palette = JJ2DefaultPalette::Sprite, bool skipNormalMap = false, bool allowRealtimePalette = false);
	};
}