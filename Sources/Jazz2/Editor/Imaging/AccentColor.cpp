#include "AccentColor.h"

#if defined(WITH_EDITOR)

#include <algorithm>
#include <cmath>

using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t HueBins = 24;

		struct HueBin
		{
			double Weight = 0.0;
			double R = 0.0, G = 0.0, B = 0.0;
		};

		void ToHsv(float r, float g, float b, float& h, float& s, float& v)
		{
			float max = std::max({ r, g, b });
			float min = std::min({ r, g, b });
			float delta = max - min;
			v = max;
			s = (max > 0.0f ? delta / max : 0.0f);
			if (delta <= 0.0f) {
				h = 0.0f;
			} else if (max == r) {
				h = std::fmod((g - b) / delta + 6.0f, 6.0f);
			} else if (max == g) {
				h = (b - r) / delta + 2.0f;
			} else {
				h = (r - g) / delta + 4.0f;
			}
			h /= 6.0f;
		}

		Colorf FromHsv(float h, float s, float v)
		{
			float sector = h * 6.0f;
			std::int32_t i = std::int32_t(std::floor(sector)) % 6;
			float f = sector - std::floor(sector);
			float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
			switch (i) {
				case 0: return Colorf(v, t, p, 1.0f);
				case 1: return Colorf(q, v, p, 1.0f);
				case 2: return Colorf(p, v, t, 1.0f);
				case 3: return Colorf(p, q, v, 1.0f);
				case 4: return Colorf(t, p, v, 1.0f);
				default: return Colorf(v, p, q, 1.0f);
			}
		}

		void AddColor(HueBin (&bins)[HueBins], std::uint8_t r8, std::uint8_t g8, std::uint8_t b8, double count)
		{
			float r = r8 / 255.0f, g = g8 / 255.0f, b = b8 / 255.0f;
			float h, s, v;
			ToHsv(r, g, b, h, s, v);
			// Grays and dark shades aren't what a tile set is known by
			if (s < 0.25f || v < 0.2f) {
				return;
			}
			double weight = count * s * s * v;
			HueBin& bin = bins[std::min(std::int32_t(h * HueBins), HueBins - 1)];
			bin.Weight += weight;
			bin.R += r * weight;
			bin.G += g * weight;
			bin.B += b * weight;
		}
	}

	bool FindAccentColor(const Formats::TileSetData& tileSet, const std::uint32_t* palette, Death::Containers::ArrayView<const float> tileWeights,
		Colorf& color)
	{
		constexpr std::int32_t PixelsPerTile = 32 * 32;

		// The 8-bit tiles are counted by palette index first, which is much faster than by color
		double indexCounts[256] = {};
		HueBin bins[HueBins];
		for (std::int32_t tile = 0; tile < tileSet.TileCount; tile++) {
			double weight = (tileWeights.empty() ? 1.0 : (tile < std::int32_t(tileWeights.size()) ? double(tileWeights[tile]) : 0.0));
			if (weight <= 0.0) {
				continue;
			}
			const std::uint8_t* image = &tileSet.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
			if (tileSet.TrueColor[tile] == 0) {
				for (std::int32_t i = 0; i < PixelsPerTile; i++) {
					indexCounts[image[i]] += weight;
				}
			} else {
				// Every other pixel is enough
				for (std::int32_t i = 0; i < PixelsPerTile; i += 2) {
					const std::uint8_t* pixel = &image[i * 4];
					if (pixel[3] >= 128) {
						AddColor(bins, pixel[0], pixel[1], pixel[2], 2.0 * weight);
					}
				}
			}
		}
		// Index 0 is transparent
		for (std::int32_t i = 1; i < 256; i++) {
			if (indexCounts[i] > 0.0) {
				std::uint32_t entry = palette[i];
				AddColor(bins, std::uint8_t(entry & 0xFF), std::uint8_t((entry >> 8) & 0xFF), std::uint8_t((entry >> 16) & 0xFF), indexCounts[i]);
			}
		}

		// A hue is helped by the hues next to it, so a color spread over two bins isn't missed
		std::int32_t best = -1;
		double bestWeight = 0.0;
		for (std::int32_t i = 0; i < HueBins; i++) {
			double weight = bins[i].Weight + (bins[(i + HueBins - 1) % HueBins].Weight + bins[(i + 1) % HueBins].Weight) * 0.5;
			if (weight > bestWeight) {
				bestWeight = weight;
				best = i;
			}
		}
		if (best < 0 || bins[best].Weight <= 0.0) {
			return false;
		}

		const HueBin& bin = bins[best];
		float h, s, v;
		ToHsv(float(bin.R / bin.Weight), float(bin.G / bin.Weight), float(bin.B / bin.Weight), h, s, v);
		// Soft and light, so it goes with the level instead of shouting over it
		color = FromHsv(h, std::clamp(s * 0.4f, 0.15f, 0.3f), std::max(v, 0.98f));
		return true;
	}
}

#endif
