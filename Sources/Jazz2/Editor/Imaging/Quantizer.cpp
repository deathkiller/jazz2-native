#include "Quantizer.h"

#if defined(WITH_EDITOR)

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cstring>

using Death::Containers::SmallVector;

namespace Jazz2::Editor
{
	namespace
	{
		struct WeightedColor
		{
			OkLab Color;
			double Weight;
		};

		// A box of the median cut, a range of the colors
		struct Box
		{
			std::int32_t First, Last;
			double Weight;
			double Spread;
			std::int32_t Axis;
		};

		float GetAxis(const OkLab& color, std::int32_t axis)
		{
			return (axis == 0 ? color.L : (axis == 1 ? color.A : color.B));
		}

		void MeasureBox(const SmallVector<WeightedColor, 0>& colors, Box& box)
		{
			// The axis of the largest weighted variance is cut, lightness counts a little more, it's seen more
			constexpr double AxisScale[3] = { 1.5, 1.0, 1.0 };
			double weight = 0.0, mean[3] = {}, variance[3] = {};
			for (std::int32_t i = box.First; i <= box.Last; i++) {
				weight += colors[i].Weight;
				for (std::int32_t a = 0; a < 3; a++) {
					mean[a] += GetAxis(colors[i].Color, a) * colors[i].Weight;
				}
			}
			box.Weight = weight;
			if (weight <= 0.0) {
				box.Spread = 0.0;
				box.Axis = 0;
				return;
			}
			for (std::int32_t a = 0; a < 3; a++) {
				mean[a] /= weight;
			}
			for (std::int32_t i = box.First; i <= box.Last; i++) {
				for (std::int32_t a = 0; a < 3; a++) {
					double d = GetAxis(colors[i].Color, a) - mean[a];
					variance[a] += d * d * colors[i].Weight;
				}
			}
			box.Axis = 0;
			box.Spread = variance[0] * AxisScale[0];
			for (std::int32_t a = 1; a < 3; a++) {
				if (variance[a] * AxisScale[a] > box.Spread) {
					box.Spread = variance[a] * AxisScale[a];
					box.Axis = a;
				}
			}
		}

		// The ordered pattern, 0 to 15
		constexpr std::uint8_t Bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
		// How far the pattern shifts a color, in Oklab, about the distance between two close entries of a palette
		constexpr float DitherSpread = 0.045f;
	}

	std::int32_t BuildPalette(Death::Containers::ArrayView<const std::uint32_t> colors, std::uint32_t (&palette)[256], const bool (&locked)[256])
	{
		// The same colors once, with how many times they're there
		SmallVector<std::uint32_t, 0> sorted;
		sorted.reserve(colors.size());
		for (std::uint32_t color : colors) {
			sorted.push_back(color & 0x00FFFFFFu);
		}
		std::sort(sorted.begin(), sorted.end());
		SmallVector<std::uint32_t, 0> unique;
		SmallVector<WeightedColor, 0> points;
		for (std::size_t i = 0; i < sorted.size(); ) {
			std::size_t j = i;
			while (j < sorted.size() && sorted[j] == sorted[i]) {
				j++;
			}
			unique.push_back(sorted[i]);
			points.push_back({ OkLab::FromPaletteEntry(sorted[i]), double(j - i) });
			i = j;
		}

		SmallVector<std::int32_t, 256> free;
		for (std::int32_t i = 1; i < 256; i++) {
			if (!locked[i]) {
				free.push_back(i);
			}
		}
		if (free.empty() || points.empty()) {
			return 0;
		}

		// Colors that are an entry already don't need another one
		SmallVector<OkLab, 256> lockedColors;
		for (std::int32_t i = 1; i < 256; i++) {
			if (locked[i]) {
				lockedColors.push_back(OkLab::FromPaletteEntry(palette[i]));
			}
		}
		for (std::size_t i = 0; i < points.size(); ) {
			bool exact = false;
			for (std::int32_t e = 1; e < 256 && !exact; e++) {
				exact = (locked[e] && (palette[e] & 0x00FFFFFFu) == unique[i]);
			}
			if (exact) {
				points.erase(points.begin() + i);
				unique.erase(unique.begin() + i);
			} else {
				i++;
			}
		}
		if (points.empty()) {
			return 0;
		}

		// Few enough colors to have an entry each
		std::int32_t freeCount = std::int32_t(free.size());
		if (std::int32_t(points.size()) <= freeCount) {
			for (std::size_t i = 0; i < unique.size(); i++) {
				palette[free[i]] = unique[i] | 0xFF000000u;
			}
			return std::int32_t(unique.size());
		}

		// Median cut: the box that is spread the most is cut in two at the weighted median of its widest axis
		SmallVector<Box, 256> boxes;
		Box all = { 0, std::int32_t(points.size()) - 1, 0.0, 0.0, 0 };
		MeasureBox(points, all);
		boxes.push_back(all);
		while (std::int32_t(boxes.size()) < freeCount) {
			std::int32_t widest = -1;
			for (std::int32_t b = 0; b < std::int32_t(boxes.size()); b++) {
				if (boxes[b].Last > boxes[b].First && (widest < 0 || boxes[b].Spread > boxes[widest].Spread)) {
					widest = b;
				}
			}
			if (widest < 0 || boxes[widest].Spread <= 0.0) {
				break;
			}
			Box box = boxes[widest];
			std::sort(points.begin() + box.First, points.begin() + box.Last + 1, [axis = box.Axis](const WeightedColor& a, const WeightedColor& b) {
				return GetAxis(a.Color, axis) < GetAxis(b.Color, axis);
			});
			double half = box.Weight * 0.5, sum = 0.0;
			std::int32_t split = box.First;
			for (std::int32_t i = box.First; i < box.Last; i++) {
				sum += points[i].Weight;
				split = i;
				if (sum >= half) {
					break;
				}
			}
			Box lower = { box.First, split, 0.0, 0.0, 0 };
			Box upper = { split + 1, box.Last, 0.0, 0.0, 0 };
			MeasureBox(points, lower);
			MeasureBox(points, upper);
			boxes[widest] = lower;
			boxes.push_back(upper);
		}

		SmallVector<OkLab, 256> centers;
		for (const Box& box : boxes) {
			double weight = 0.0, l = 0.0, a = 0.0, b = 0.0;
			for (std::int32_t i = box.First; i <= box.Last; i++) {
				weight += points[i].Weight;
				l += points[i].Color.L * points[i].Weight;
				a += points[i].Color.A * points[i].Weight;
				b += points[i].Color.B * points[i].Weight;
			}
			centers.push_back(OkLab{ float(l / weight), float(a / weight), float(b / weight) });
		}

		// K-means: each color goes to the closest center, a locked entry is a center that doesn't move
		constexpr std::int32_t Rounds = 8;
		SmallVector<double, 256> sums;
		for (std::int32_t round = 0; round < Rounds; round++) {
			std::int32_t centerCount = std::int32_t(centers.size());
			sums.clear();
			sums.resize(std::size_t(centerCount) * 4, 0.0);
			for (const WeightedColor& point : points) {
				std::int32_t best = -1;
				float bestDistance = 0.0f;
				for (std::int32_t c = 0; c < centerCount; c++) {
					float distance = point.Color.DistanceSquared(centers[c]);
					if (best < 0 || distance < bestDistance) {
						best = c;
						bestDistance = distance;
					}
				}
				bool toLocked = false;
				for (const OkLab& lockedColor : lockedColors) {
					if (point.Color.DistanceSquared(lockedColor) < bestDistance) {
						toLocked = true;
						break;
					}
				}
				if (toLocked) {
					continue;
				}
				sums[best * 4] += point.Weight;
				sums[best * 4 + 1] += point.Color.L * point.Weight;
				sums[best * 4 + 2] += point.Color.A * point.Weight;
				sums[best * 4 + 3] += point.Color.B * point.Weight;
			}
			for (std::int32_t c = 0; c < centerCount; c++) {
				double weight = sums[c * 4];
				if (weight > 0.0) {
					centers[c] = OkLab{ float(sums[c * 4 + 1] / weight), float(sums[c * 4 + 2] / weight), float(sums[c * 4 + 3] / weight) };
				}
			}
		}

		for (std::size_t c = 0; c < centers.size(); c++) {
			palette[free[c]] = centers[c].ToPaletteEntry(255);
		}
		return std::int32_t(centers.size());
	}

	PaletteMapper::PaletteMapper(const std::uint32_t* palette, bool dither)
		: _dither(dither)
	{
		std::memcpy(_palette, palette, sizeof(_palette));
		for (std::int32_t i = 1; i < 256; i++) {
			_colors[i] = OkLab::FromPaletteEntry(palette[i]);
		}
		std::memset(_cache, 0, sizeof(_cache));
	}

	std::uint8_t PaletteMapper::Map(std::uint32_t color, std::int32_t x, std::int32_t y)
	{
		color &= 0x00FFFFFFu;
		std::uint32_t cell = (_dither ? std::uint32_t(Bayer4[(y & 3) * 4 + (x & 3)]) : 0u);
		std::uint32_t key = color | (cell << 24);
		// A small hash table of what was looked up, images have few colors over and over
		std::uint32_t slot = ((key * 2654435761u) >> 18) & (CacheSize - 1);
		CacheEntry& entry = _cache[slot];
		if (entry.Used && entry.Key == key) {
			return entry.Index;
		}

		OkLab target = OkLab::FromPaletteEntry(color);
		if (_dither) {
			// Lightness shifted by the pattern, so the two closest entries mix where the color is between them
			target.L += (float(cell) / 15.0f - 0.5f) * DitherSpread;
		}
		std::uint8_t index = FindNearest(target);
		entry.Key = key;
		entry.Index = index;
		entry.Used = true;
		return index;
	}

	std::uint8_t PaletteMapper::FindNearest(const OkLab& color) const
	{
		std::int32_t best = 1;
		float bestDistance = color.DistanceSquared(_colors[1]);
		for (std::int32_t i = 2; i < 256; i++) {
			float distance = color.DistanceSquared(_colors[i]);
			if (distance < bestDistance) {
				bestDistance = distance;
				best = i;
			}
		}
		return std::uint8_t(best);
	}
}

#endif
