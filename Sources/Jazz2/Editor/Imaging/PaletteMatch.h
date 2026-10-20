#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

namespace Jazz2::Editor
{
	/** @brief A color in the Oklab color space, where distances follow how different colors look */
	struct OkLab
	{
		/** @brief Lightness */
		float L;
		/** @brief Green to red */
		float A;
		/** @brief Blue to yellow */
		float B;

		/** @brief Converts a color of a palette, `R | G << 8 | B << 16`, alpha is ignored */
		static OkLab FromPaletteEntry(std::uint32_t entry);
		/** @brief Converts the color to an entry of a palette with a given alpha, colors outside of sRGB are clamped */
		std::uint32_t ToPaletteEntry(std::uint8_t alpha = 255) const;
		/** @brief Returns a color between two others, @p t from 0 to 1 */
		static OkLab Lerp(const OkLab& a, const OkLab& b, float t) {
			return OkLab{ a.L + (b.L - a.L) * t, a.A + (b.A - a.A) * t, a.B + (b.B - a.B) * t };
		}

		/** @brief Returns the squared distance to another color */
		float DistanceSquared(const OkLab& other) const {
			float dl = L - other.L, da = A - other.A, db = B - other.B;
			return dl * dl + da * da + db * db;
		}
	};

	/**
		@brief Finds the entries of a palette that look the closest to colors

		Only the entries 1 to 255 are searched, entry 0 is transparent. The entries are kept sorted by lightness, so a
		search compares only those of a lightness close enough to matter --- far fewer than all of them for most
		colors. It finds the same entry as comparing all of them would, the lowest one of equally close ones.
	*/
	class PaletteSearch
	{
	public:
		/** @brief Creates a search that finds nothing until @ref Reset() is called */
		PaletteSearch() = default;
		/** @brief Creates a search of the entries 1 to 255 of a palette as `R | G << 8 | B << 16` */
		explicit PaletteSearch(const std::uint32_t* palette);

		/** @brief Searches the entries 1 to 255 of another palette */
		void Reset(const std::uint32_t* palette);
		/** @brief Returns the entry closest to a color, 1 to 255 */
		std::uint8_t FindNearest(const OkLab& color) const;

	private:
		struct Entry
		{
			OkLab Color;
			std::uint8_t Index;
		};

		// Entries 1 to 255 sorted by lightness
		Entry _entries[255] = {};
	};

	/**
		@brief Maps each entry of a palette to the entry of another palette that looks the closest

		Colors are compared in @ref OkLab. Entry 0 is transparent in both, so it maps to 0 and nothing else maps to it.
	*/
	void MatchPalette(const std::uint32_t (&from)[256], const std::uint32_t (&to)[256], std::uint8_t (&table)[256]);

	/**
		@brief Returns whether an entry of the palette of a level is the color the sprites have at the same index

		The sprites are drawn in the palette of the level too. Tile sets often tweak the colors of the sprites a little,
		so a close color counts too. Entry 0 is transparent, it never counts.
	*/
	bool IsSpriteColor(std::int32_t index, std::uint32_t entry);
}

#endif
