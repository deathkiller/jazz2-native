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
		@brief Maps each entry of a palette to the entry of another palette that looks the closest

		Colors are compared in @ref OkLab. Entry 0 is transparent in both, so it maps to 0 and nothing else maps to it.
	*/
	void MatchPalette(const std::uint32_t (&from)[256], const std::uint32_t (&to)[256], std::uint8_t (&table)[256]);
}

#endif
