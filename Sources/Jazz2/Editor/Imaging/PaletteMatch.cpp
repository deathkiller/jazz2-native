#include "PaletteMatch.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		float ToLinear(std::uint8_t value)
		{
			float c = value / 255.0f;
			return (c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f));
		}
	}

	OkLab OkLab::FromPaletteEntry(std::uint32_t entry)
	{
		float r = ToLinear(std::uint8_t(entry & 0xFF));
		float g = ToLinear(std::uint8_t((entry >> 8) & 0xFF));
		float b = ToLinear(std::uint8_t((entry >> 16) & 0xFF));

		// From linear sRGB as Björn Ottosson defines it
		float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
		float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
		float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

		OkLab result;
		result.L = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
		result.A = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
		result.B = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
		return result;
	}

	std::uint32_t OkLab::ToPaletteEntry(std::uint8_t alpha) const
	{
		float l = L + 0.3963377774f * A + 0.2158037573f * B;
		float m = L - 0.1055613458f * A - 0.0638541728f * B;
		float s = L - 0.0894841775f * A - 1.2914855480f * B;
		l = l * l * l;
		m = m * m * m;
		s = s * s * s;

		float rgb[3] = {
			+4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
			-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
			-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s
		};
		std::uint32_t entry = std::uint32_t(alpha) << 24;
		for (std::int32_t i = 0; i < 3; i++) {
			float c = std::clamp(rgb[i], 0.0f, 1.0f);
			c = (c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
			entry |= std::uint32_t(std::clamp(std::int32_t(std::round(c * 255.0f)), 0, 255)) << (i * 8);
		}
		return entry;
	}

	void MatchPalette(const std::uint32_t (&from)[256], const std::uint32_t (&to)[256], std::uint8_t (&table)[256])
	{
		OkLab target[256];
		for (std::int32_t i = 1; i < 256; i++) {
			target[i] = OkLab::FromPaletteEntry(to[i]);
		}

		table[0] = 0;
		for (std::int32_t i = 1; i < 256; i++) {
			OkLab color = OkLab::FromPaletteEntry(from[i]);
			std::int32_t best = 1;
			float bestDistance = color.DistanceSquared(target[1]);
			for (std::int32_t j = 2; j < 256; j++) {
				float distance = color.DistanceSquared(target[j]);
				if (distance < bestDistance) {
					bestDistance = distance;
					best = j;
				}
			}
			table[i] = std::uint8_t(best);
		}
	}
}

#endif
