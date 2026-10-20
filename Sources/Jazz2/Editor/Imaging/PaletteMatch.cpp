#include "PaletteMatch.h"
#include "../../Compatibility/JJ2Anims.Palettes.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace Jazz2::Editor
{
	namespace
	{
		// Linear values of the 256 levels of sRGB, a conversion is done for each color of an image, std::pow() for
		// each of its channels was most of the time it took
		struct LinearTable
		{
			float Values[256];

			LinearTable()
			{
				for (std::int32_t i = 0; i < 256; i++) {
					float c = i / 255.0f;
					Values[i] = (c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f));
				}
			}
		};

		const float* GetLinearTable()
		{
			static const LinearTable table;
			return table.Values;
		}
	}

	OkLab OkLab::FromPaletteEntry(std::uint32_t entry)
	{
		const float* linear = GetLinearTable();
		float r = linear[entry & 0xFF];
		float g = linear[(entry >> 8) & 0xFF];
		float b = linear[(entry >> 16) & 0xFF];

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
			entry |= std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(c * 255.0f)), 0, 255)) << (i * 8);
		}
		return entry;
	}

	PaletteSearch::PaletteSearch(const std::uint32_t* palette)
	{
		Reset(palette);
	}

	void PaletteSearch::Reset(const std::uint32_t* palette)
	{
		for (std::int32_t i = 1; i < 256; i++) {
			_entries[i - 1].Color = OkLab::FromPaletteEntry(palette[i]);
			_entries[i - 1].Index = std::uint8_t(i);
		}
		std::sort(std::begin(_entries), std::end(_entries), [](const Entry& a, const Entry& b) {
			return (a.Color.L < b.Color.L || (a.Color.L == b.Color.L && a.Index < b.Index));
		});
	}

	std::uint8_t PaletteSearch::FindNearest(const OkLab& color) const
	{
		// Not std::size() of the member, older MSVC doesn't take it as a constant inside a member function
		constexpr std::int32_t Count = std::int32_t(sizeof(_entries) / sizeof(_entries[0]));

		// From the entries of the same lightness both ways, until the difference of lightness alone is further than
		// the closest entry found. The squared difference of lightness is never more than the whole distance, even
		// as rounded, so no entry left out could be closer. Equally close ones are all looked at, the lowest wins.
		std::int32_t right = std::int32_t(std::lower_bound(std::begin(_entries), std::end(_entries), color.L, [](const Entry& entry, float l) {
			return entry.Color.L < l;
		}) - std::begin(_entries));
		std::int32_t left = right - 1;
		float bestDistance = std::numeric_limits<float>::infinity();
		std::uint8_t best = 0;
		auto consider = [&](const Entry& entry) {
			float distance = color.DistanceSquared(entry.Color);
			if (distance < bestDistance || (distance == bestDistance && entry.Index < best)) {
				bestDistance = distance;
				best = entry.Index;
			}
		};
		while (left >= 0 || right < Count) {
			if (right < Count) {
				float dl = color.L - _entries[right].Color.L;
				if (dl * dl > bestDistance) {
					right = Count;
				} else {
					consider(_entries[right]);
					right++;
				}
			}
			if (left >= 0) {
				float dl = color.L - _entries[left].Color.L;
				if (dl * dl > bestDistance) {
					left = -1;
				} else {
					consider(_entries[left]);
					left--;
				}
			}
		}
		return best;
	}

	bool IsSpriteColor(std::int32_t index, std::uint32_t entry)
	{
		constexpr float SpriteColorDistance = 0.06f;
		if (index <= 0 || index >= 256) {
			return false;
		}
		const Color& sprite = SpritePalette[index];
		std::uint32_t spriteColor = std::uint32_t(sprite.R) | (std::uint32_t(sprite.G) << 8) | (std::uint32_t(sprite.B) << 16);
		return (OkLab::FromPaletteEntry(entry).DistanceSquared(OkLab::FromPaletteEntry(spriteColor)) <= SpriteColorDistance * SpriteColorDistance);
	}

	void MatchPalette(const std::uint32_t (&from)[256], const std::uint32_t (&to)[256], std::uint8_t (&table)[256])
	{
		PaletteSearch search(to);
		table[0] = 0;
		for (std::int32_t i = 1; i < 256; i++) {
			table[i] = search.FindNearest(OkLab::FromPaletteEntry(from[i]));
		}
	}
}

#endif
