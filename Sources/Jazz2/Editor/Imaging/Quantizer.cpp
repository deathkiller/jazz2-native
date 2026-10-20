#include "Quantizer.h"

#if defined(WITH_EDITOR)

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cstring>
#include <limits>

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
		// Most colors the median cut and k-means work with, more are binned first
		constexpr std::size_t MaxPoints = 32768;

		// A center of k-means or a locked entry, which is a center that doesn't move
		struct Seed
		{
			OkLab Color;
			// Index of the center, -1 for a locked entry
			std::int32_t Center;
		};

		// Returns the seed closest to a color among seeds sorted by lightness, looking only at those whose lightness alone
		// isn't further than the closest one found, as PaletteSearch does. A center wins over an equally close locked
		// entry, a lower center over a higher one.
		const Seed* FindNearestSeed(const Seed* seeds, std::int32_t count, const OkLab& color)
		{
			std::int32_t right = std::int32_t(std::lower_bound(seeds, seeds + count, color.L, [](const Seed& seed, float l) {
				return seed.Color.L < l;
			}) - seeds);
			std::int32_t left = right - 1;
			const Seed* best = nullptr;
			float bestDistance = std::numeric_limits<float>::infinity();
			auto consider = [&](const Seed& seed) {
				float distance = color.DistanceSquared(seed.Color);
				bool better = (distance < bestDistance);
				if (!better && distance == bestDistance && best != nullptr) {
					better = (best->Center < 0 ? seed.Center >= 0 : (seed.Center >= 0 && seed.Center < best->Center));
				}
				if (better) {
					bestDistance = distance;
					best = &seed;
				}
			};
			while (left >= 0 || right < count) {
				if (right < count) {
					float dl = color.L - seeds[right].Color.L;
					if (dl * dl > bestDistance) {
						right = count;
					} else {
						consider(seeds[right]);
						right++;
					}
				}
				if (left >= 0) {
					float dl = color.L - seeds[left].Color.L;
					if (dl * dl > bestDistance) {
						left = -1;
					} else {
						consider(seeds[left]);
						left--;
					}
				}
			}
			return best;
		}
	}

	std::int32_t BuildPalette(Death::Containers::ArrayView<const std::uint32_t> colors, std::uint32_t (&palette)[256], const bool (&locked)[256],
		Death::Containers::Function<bool(float)>&& progress)
	{
		auto report = [&progress](float value) {
			return (!progress || progress(value));
		};

		// The same colors once, with how many times they're there
		SmallVector<std::uint32_t, 0> sorted;
		sorted.reserve(colors.size());
		for (std::uint32_t color : colors) {
			sorted.push_back(color & 0x00FFFFFFu);
		}
		std::sort(sorted.begin(), sorted.end());
		if (!report(0.1f)) {
			return -1;
		}

		// Colors that are an entry already don't need another one, the locked colors are sorted to be looked up quickly
		SmallVector<std::uint32_t, 256> lockedRgb;
		SmallVector<OkLab, 256> lockedColors;
		for (std::int32_t i = 1; i < 256; i++) {
			if (locked[i]) {
				lockedRgb.push_back(palette[i] & 0x00FFFFFFu);
				lockedColors.push_back(OkLab::FromPaletteEntry(palette[i]));
			}
		}
		std::sort(lockedRgb.begin(), lockedRgb.end());

		SmallVector<std::uint32_t, 0> unique;
		SmallVector<double, 0> counts;
		for (std::size_t i = 0; i < sorted.size(); ) {
			std::size_t j = i;
			while (j < sorted.size() && sorted[j] == sorted[i]) {
				j++;
			}
			if (!std::binary_search(lockedRgb.begin(), lockedRgb.end(), sorted[i])) {
				unique.push_back(sorted[i]);
				counts.push_back(double(j - i));
			}
			i = j;
		}

		SmallVector<std::int32_t, 256> free;
		for (std::int32_t i = 1; i < 256; i++) {
			if (!locked[i]) {
				free.push_back(i);
			}
		}
		if (free.empty() || unique.empty()) {
			return 0;
		}

		// Few enough colors to have an entry each
		std::int32_t freeCount = std::int32_t(free.size());
		if (std::int32_t(unique.size()) <= freeCount) {
			for (std::size_t i = 0; i < unique.size(); i++) {
				palette[free[i]] = unique[i] | 0xFF000000u;
			}
			return std::int32_t(unique.size());
		}

		// A photo-like image has hundreds of thousands of colors, too many for the rounds below to be quick. Colors
		// that close to each other are then taken together, as their average, 5 bits of each channel are differences
		// smaller than 255 entries can tell apart anyway.
		SmallVector<WeightedColor, 0> points;
		if (unique.size() > MaxPoints) {
			struct Bin
			{
				double R, G, B, Weight;
			};
			SmallVector<Bin, 0> bins;
			bins.resize(std::size_t(1) << 15, Bin{});
			for (std::size_t i = 0; i < unique.size(); i++) {
				std::uint32_t c = unique[i];
				std::uint32_t key = ((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10);
				Bin& bin = bins[key];
				bin.R += double(c & 0xFF) * counts[i];
				bin.G += double((c >> 8) & 0xFF) * counts[i];
				bin.B += double((c >> 16) & 0xFF) * counts[i];
				bin.Weight += counts[i];
			}
			for (const Bin& bin : bins) {
				if (bin.Weight > 0.0) {
					std::uint32_t r = std::uint32_t(bin.R / bin.Weight + 0.5), g = std::uint32_t(bin.G / bin.Weight + 0.5), b = std::uint32_t(bin.B / bin.Weight + 0.5);
					points.push_back({ OkLab::FromPaletteEntry(r | (g << 8) | (b << 16)), bin.Weight });
				}
			}
		} else {
			points.reserve(unique.size());
			for (std::size_t i = 0; i < unique.size(); i++) {
				points.push_back({ OkLab::FromPaletteEntry(unique[i]), counts[i] });
			}
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
		if (!report(0.3f)) {
			return -1;
		}

		// K-means: each color goes to the closest center, a locked entry is a center that doesn't move. The centers and
		// the locked entries are sorted by lightness for each round, so the closest one is found among a few of them.
		constexpr std::int32_t Rounds = 8;
		SmallVector<double, 256> sums;
		SmallVector<Seed, 512> seeds;
		for (std::int32_t round = 0; round < Rounds; round++) {
			std::int32_t centerCount = std::int32_t(centers.size());
			seeds.clear();
			for (std::int32_t c = 0; c < centerCount; c++) {
				seeds.push_back(Seed{ centers[c], c });
			}
			for (const OkLab& lockedColor : lockedColors) {
				seeds.push_back(Seed{ lockedColor, -1 });
			}
			std::sort(seeds.begin(), seeds.end(), [](const Seed& a, const Seed& b) {
				return a.Color.L < b.Color.L;
			});
			sums.clear();
			sums.resize(std::size_t(centerCount) * 4, 0.0);
			for (const WeightedColor& point : points) {
				const Seed* nearest = FindNearestSeed(seeds.data(), std::int32_t(seeds.size()), point.Color);
				if (nearest == nullptr || nearest->Center < 0) {
					continue;
				}
				std::int32_t best = nearest->Center;
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
			if (!report(0.3f + 0.7f * float(round + 1) / float(Rounds))) {
				return -1;
			}
		}

		for (std::size_t c = 0; c < centers.size(); c++) {
			palette[free[c]] = centers[c].ToPaletteEntry(255);
		}
		return std::int32_t(centers.size());
	}

	PaletteMapper::PaletteMapper(const std::uint32_t* palette, bool dither)
		: _search(palette), _dither(dither)
	{
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
		std::uint8_t index = _search.FindNearest(target);
		entry.Key = key;
		entry.Index = index;
		entry.Used = true;
		return index;
	}
}

#endif
