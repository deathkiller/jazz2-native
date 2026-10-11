#include "J2vRecompressor.h"
#include "../VideoFormat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <Containers/Array.h>
#include <Containers/Pair.h>
#include <Containers/GrowableArray.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#if defined(WITH_LZ4)
#	include <lz4.h>
#	include <lz4hc.h>
#endif

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;

namespace Jazz2::Compatibility
{
	namespace
	{
		constexpr std::int32_t StreamCount = 4;
		/** @brief Header size: 16 bytes of signature, then width, height, bpp, delay, frames and 20 reserved */
		constexpr std::int32_t HeaderSize = 16 + 4 + 4 + 2 + 2 + 4 + 20;
		/** @brief Size of the chunks the re-encoded streams are split into, matching the player's read-ahead */
		constexpr std::int32_t TargetChunkSize = 16 * 1024;

		/** @brief One decoded stream, already inflated into memory */
		struct SourceStream
		{
			Array<std::uint8_t> Data;
			std::size_t Position = 0;
			bool Exhausted = false;

			std::uint8_t ReadByte()
			{
				if (Position >= Data.size()) {
					Exhausted = true;
					return 0;
				}
				return Data[Position++];
			}

			std::uint16_t ReadUint16()
			{
				std::uint16_t result = std::uint16_t(ReadByte());
				result |= std::uint16_t(ReadByte()) << 8;
				return result;
			}

			void Read(std::uint8_t* destination, std::size_t length)
			{
				std::size_t available = (Position < Data.size() ? std::min(length, Data.size() - Position) : 0);
				if (available > 0) {
					std::memcpy(destination, &Data[Position], available);
				}
				if (available < length) {
					std::memset(destination + available, 0, length - available);
					Exhausted = true;
				}
				Position += length;
			}

			void Skip(std::size_t length)
			{
				Position += length;
				if (Position > Data.size()) {
					Exhausted = true;
				}
			}
		};

		void WriteUint16(Array<std::uint8_t>& target, std::uint16_t value)
		{
			arrayAppend(target, std::uint8_t(value & 0xFF));
			arrayAppend(target, std::uint8_t((value >> 8) & 0xFF));
		}



		/** @brief Leaves a span of the previous frame in place */
		void EmitSkip(Array<std::uint8_t>& payload, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortSkip) {
					arrayAppend(payload, std::uint8_t(VideoFormat::CommandSkipBase + length - 1));
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandSkipLong));
				WriteUint16(payload, std::uint16_t(part));
				length -= part;
			}
		}

		/** @brief Stores a span of pixels as they are */
		void EmitLiterals(Array<std::uint8_t>& payload, const std::uint8_t* source, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortLiteral) {
					arrayAppend(payload, std::uint8_t(length - 1));
					arrayAppend(payload, arrayView(source, length));
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandLiteralLong));
				WriteUint16(payload, std::uint16_t(part));
				arrayAppend(payload, arrayView(source, part));
				source += part;
				length -= part;
			}
		}

		/** @brief Repeats one pixel value */
		void EmitRun(Array<std::uint8_t>& payload, std::uint8_t value, std::int32_t length)
		{
			while (length > 0) {
				if (length <= VideoFormat::MaxShortRun) {
					arrayAppend(payload, std::uint8_t(VideoFormat::CommandRunBase + length - VideoFormat::CommandRunMinLength));
					arrayAppend(payload, value);
					break;
				}
				std::int32_t part = std::min(length, std::int32_t(0xFFFF));
				if (length - part > 0 && length - part < VideoFormat::CommandRunMinLength) {
					// A remainder of 1-2 pixels has no run encoding, so leave a full short run instead
					part = length - VideoFormat::CommandRunMinLength;
				}
				arrayAppend(payload, std::uint8_t(VideoFormat::CommandRunLong));
				WriteUint16(payload, std::uint16_t(part));
				arrayAppend(payload, value);
				length -= part;
			}
		}

		/**
			@brief Encodes one frame as changes against the previous one

			Unchanged spans are skipped (the player leaves the previous frame's pixels in place), repeated bytes
			become runs, and anything else is stored literally. Decoding is memcpy/memset only - see VideoFormat.
		*/
		void EncodeFrame(Array<std::uint8_t>& payload, const std::uint8_t* scaled, const std::uint8_t* previousScaled,
			std::int32_t framePixels, bool firstFrame, const std::uint8_t* palette)
		{
			arrayAppend(payload, std::uint8_t(palette != nullptr ? VideoFormat::FrameFlagPalette : 0x00));
			if (palette != nullptr) {
				arrayAppend(payload, arrayView(palette, 256 * 4));
			}

			std::int32_t i = 0;
			while (i < framePixels) {
				// Anything identical to the previous frame costs nothing but the skip itself
				if (!firstFrame) {
					std::int32_t skip = 0;
					while (i + skip < framePixels && scaled[i + skip] == previousScaled[i + skip]) {
						skip++;
					}
					if (skip > 0) {
						EmitSkip(payload, skip);
						i += skip;
						continue;
					}
				}

				// A run of one repeated value, but only where it beats storing the bytes themselves
				std::int32_t run = 1;
				while (i + run < framePixels && scaled[i + run] == scaled[i]
						&& (firstFrame || scaled[i + run] != previousScaled[i + run])) {
					run++;
				}
				if (run >= VideoFormat::CommandRunMinLength) {
					EmitRun(payload, scaled[i], run);
					i += run;
					continue;
				}

				// Otherwise gather literals up to the next skip or run worth emitting
				std::int32_t literal = 0;
				while (i + literal < framePixels) {
					std::int32_t j = i + literal;
					if (!firstFrame && scaled[j] == previousScaled[j]) {
						std::int32_t skip = 0;
						while (j + skip < framePixels && scaled[j + skip] == previousScaled[j + skip]) {
							skip++;
						}
						if (skip >= 4) {
							break;
						}
					}
					std::int32_t sameRun = 1;
					while (j + sameRun < framePixels && scaled[j + sameRun] == scaled[j]) {
						sameRun++;
					}
					if (sameRun >= VideoFormat::CommandRunMinLength + 1) {
						break;
					}
					literal += sameRun;
				}
				if (literal <= 0) {
					literal = 1;
				}
				EmitLiterals(payload, &scaled[i], literal);
				i += literal;
			}
			arrayAppend(payload, std::uint8_t(VideoFormat::CommandEndOfFrame));
		}

		/** @brief A stretch of frames that shares one palette in the original video */
		struct Segment
		{
			std::int32_t FirstFrame;
			/** @brief The original palette, as R, G, B, X */
			std::uint8_t SourcePalette[256 * 4];
			/** @brief The palette fitted to the downscaled frames, as R, G, B, X */
			std::uint8_t Palette[256 * 4];
			/** @brief Number of entries of @ref Palette in use, the rest is black and never mapped to */
			std::int32_t PaletteCount;
		};

		/**
			@brief Downscales indexed frames by an integer factor, averaging the area of each output pixel in linear light

			Averaging the stored values directly would darken every edge between a light and a dark color and
			turn the dithered gradients muddy, because they are gamma-encoded: the eye blends the light the
			pixels give off, not the numbers.
		*/
		class AreaDownscaler
		{
		public:
			AreaDownscaler()
				: _toSrgb(std::make_unique<std::uint8_t[]>(LinearSteps + 1))
			{
				for (std::int32_t i = 0; i <= LinearSteps; i++) {
					const double linear = double(i) / LinearSteps;
					const double encoded = (linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055);
					_toSrgb[i] = std::uint8_t(std::clamp(std::int32_t(encoded * 255.0 + 0.5), std::int32_t(0), std::int32_t(255)));
				}
				for (std::int32_t i = 0; i < 256; i++) {
					const double encoded = i / 255.0;
					_toLinear[i] = float(encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4));
				}
			}

			/** @brief Writes the downscaled frame to @p rgb as R, G, B triplets */
			void Downscale(const std::uint8_t* indices, std::int32_t width, std::int32_t height, const std::uint8_t* palette,
				std::int32_t factor, std::uint8_t* rgb)
			{
				float linearPalette[256 * 3];
				for (std::int32_t i = 0; i < 256; i++) {
					linearPalette[i * 3 + 0] = _toLinear[palette[i * 4 + 0]];
					linearPalette[i * 3 + 1] = _toLinear[palette[i * 4 + 1]];
					linearPalette[i * 3 + 2] = _toLinear[palette[i * 4 + 2]];
				}

				const std::int32_t targetWidth = width / factor;
				const std::int32_t targetHeight = height / factor;
				const float scale = float(LinearSteps) / float(factor * factor);
				_sums.resize(std::size_t(targetWidth) * 3);
				for (std::int32_t y = 0; y < targetHeight; y++) {
					std::fill(_sums.begin(), _sums.end(), 0.0f);
					for (std::int32_t sy = 0; sy < factor; sy++) {
						const std::uint8_t* row = &indices[std::size_t(y * factor + sy) * width];
						for (std::int32_t x = 0; x < targetWidth; x++) {
							float* sum = &_sums[std::size_t(x) * 3];
							for (std::int32_t sx = 0; sx < factor; sx++) {
								const float* color = &linearPalette[row[x * factor + sx] * 3];
								sum[0] += color[0];
								sum[1] += color[1];
								sum[2] += color[2];
							}
						}
					}
					std::uint8_t* dst = &rgb[std::size_t(y) * targetWidth * 3];
					for (std::int32_t i = 0; i < targetWidth * 3; i++) {
						dst[i] = _toSrgb[std::min(std::int32_t(_sums[i] * scale + 0.5f), LinearSteps)];
					}
				}
			}

		private:
			/** @brief Resolution of the linear-to-sRGB table, fine enough that the darkest levels still round correctly */
			static constexpr std::int32_t LinearSteps = 65535;

			std::unique_ptr<std::uint8_t[]> _toSrgb;
			float _toLinear[256];
			std::vector<float> _sums;
		};

		/** @brief Counts the colors of downscaled frames, in 6-bit-per-channel bins that keep the exact mean of their colors */
		class ColorHistogram
		{
		public:
			static constexpr std::int32_t BinCount = 64 * 64 * 64;

			struct Bin {
				std::uint32_t Count;
				std::uint64_t Sum[3];
			};

			ColorHistogram()
				: _bins(std::make_unique<Bin[]>(BinCount))
			{
			}

			void Clear()
			{
				std::memset(_bins.get(), 0, sizeof(Bin) * BinCount);
			}

			void Add(const std::uint8_t* rgb, std::size_t pixels)
			{
				for (std::size_t i = 0; i < pixels; i++, rgb += 3) {
					Bin& bin = _bins[((rgb[0] >> 2) << 12) | ((rgb[1] >> 2) << 6) | (rgb[2] >> 2)];
					bin.Count++;
					bin.Sum[0] += rgb[0];
					bin.Sum[1] += rgb[1];
					bin.Sum[2] += rgb[2];
				}
			}

			const Bin& operator[](std::int32_t index) const
			{
				return _bins[index];
			}

		private:
			std::unique_ptr<Bin[]> _bins;
		};

		/** @brief Weighted color of the palette fitting, the mean of one histogram bin */
		struct WeightedColor
		{
			double Color[3];
			double Weight;
			std::int32_t Id;
		};

		/**
			@brief Fits a 256-color palette to the colors counted in @p histogram

			A median cut first - the box of colors with the largest spread is split in two at the weighted
			median of its widest channel, until there are 256 of them - then a few rounds of k-means move every
			color to the mean of the colors closest to it. The cut alone already keeps every frequent color
			nearly exact, since a box holding most of the weight of a frame is split until it is small; the
			refinement then takes out what the axis-aligned boxes cannot express. Every step is deterministic
			(no random seeding, every ordering broken by the bin), so a tree converted twice is the same.
		*/
		std::int32_t FitPalette(const ColorHistogram& histogram, std::uint8_t* palette)
		{
			constexpr std::int32_t PaletteSize = 256;
			constexpr std::int32_t Iterations = 8;

			std::vector<WeightedColor> colors;
			for (std::int32_t i = 0; i < ColorHistogram::BinCount; i++) {
				const auto& bin = histogram[i];
				if (bin.Count > 0) {
					const double weight = double(bin.Count);
					colors.push_back(WeightedColor{ { bin.Sum[0] / weight, bin.Sum[1] / weight, bin.Sum[2] / weight }, weight, i });
				}
			}

			std::memset(palette, 0, PaletteSize * 4);
			if (colors.empty()) {
				return 1;
			}

			// Median cut: boxes are index ranges of `colors`, which is reordered as they are split
			struct Box {
				std::size_t First, Last;
				double Error;
				std::int32_t Channel;
			};
			auto measure = [&](Box& box) {
				double sum[3] = {}, sumSq[3] = {}, weight = 0.0;
				for (std::size_t i = box.First; i < box.Last; i++) {
					const auto& c = colors[i];
					for (std::int32_t k = 0; k < 3; k++) {
						sum[k] += c.Color[k] * c.Weight;
						sumSq[k] += c.Color[k] * c.Color[k] * c.Weight;
					}
					weight += c.Weight;
				}
				box.Error = 0.0;
				box.Channel = 0;
				double widest = -1.0;
				for (std::int32_t k = 0; k < 3; k++) {
					const double error = sumSq[k] - sum[k] * sum[k] / weight;
					box.Error += error;
					if (error > widest) {
						widest = error;
						box.Channel = k;
					}
				}
				if (box.Last - box.First < 2) {
					box.Error = 0.0;
				}
			};

			std::vector<Box> boxes;
			boxes.push_back(Box{ 0, colors.size(), 0.0, 0 });
			measure(boxes[0]);
			while (std::int32_t(boxes.size()) < PaletteSize) {
				std::size_t target = 0;
				for (std::size_t i = 1; i < boxes.size(); i++) {
					if (boxes[i].Error > boxes[target].Error) {
						target = i;
					}
				}
				Box& box = boxes[target];
				if (box.Error <= 0.0) {
					break;
				}
				const std::int32_t channel = box.Channel;
				std::sort(colors.begin() + std::ptrdiff_t(box.First), colors.begin() + std::ptrdiff_t(box.Last),
					[channel](const WeightedColor& a, const WeightedColor& b) {
						return (a.Color[channel] < b.Color[channel] || (a.Color[channel] == b.Color[channel] && a.Id < b.Id));
					});
				double total = 0.0;
				for (std::size_t i = box.First; i < box.Last; i++) {
					total += colors[i].Weight;
				}
				double running = 0.0;
				std::size_t split = box.First + 1;
				for (std::size_t i = box.First; i < box.Last - 1; i++) {
					running += colors[i].Weight;
					split = i + 1;
					if (running >= total * 0.5) {
						break;
					}
				}
				Box upper{ split, box.Last, 0.0, 0 };
				box.Last = split;
				measure(box);
				measure(upper);
				boxes.push_back(upper);
			}

			std::vector<std::array<double, 3>> centers(boxes.size());
			for (std::size_t b = 0; b < boxes.size(); b++) {
				double sum[3] = {}, weight = 0.0;
				for (std::size_t i = boxes[b].First; i < boxes[b].Last; i++) {
					for (std::int32_t k = 0; k < 3; k++) {
						sum[k] += colors[i].Color[k] * colors[i].Weight;
					}
					weight += colors[i].Weight;
				}
				for (std::int32_t k = 0; k < 3; k++) {
					centers[b][k] = sum[k] / weight;
				}
			}

			// K-means refinement over the (now box-ordered) colors
			std::vector<std::array<double, 4>> accumulated(centers.size());
			for (std::int32_t iteration = 0; iteration < Iterations; iteration++) {
				for (auto& a : accumulated) {
					a = { 0.0, 0.0, 0.0, 0.0 };
				}
				for (const auto& c : colors) {
					std::size_t best = 0;
					double bestDistance = HUGE_VAL;
					for (std::size_t b = 0; b < centers.size(); b++) {
						const double dr = c.Color[0] - centers[b][0];
						const double dg = c.Color[1] - centers[b][1];
						const double db = c.Color[2] - centers[b][2];
						const double distance = dr * dr + dg * dg + db * db;
						if (distance < bestDistance) {
							bestDistance = distance;
							best = b;
						}
					}
					auto& a = accumulated[best];
					a[0] += c.Color[0] * c.Weight;
					a[1] += c.Color[1] * c.Weight;
					a[2] += c.Color[2] * c.Weight;
					a[3] += c.Weight;
				}
				for (std::size_t b = 0; b < centers.size(); b++) {
					if (accumulated[b][3] > 0.0) {
						for (std::int32_t k = 0; k < 3; k++) {
							centers[b][k] = accumulated[b][k] / accumulated[b][3];
						}
					}
				}
			}

			for (std::size_t b = 0; b < centers.size(); b++) {
				for (std::int32_t k = 0; k < 3; k++) {
					palette[b * 4 + k] = std::uint8_t(std::clamp(std::int32_t(centers[b][k] + 0.5), std::int32_t(0), std::int32_t(255)));
				}
			}
			return std::int32_t(centers.size());
		}

		/** @brief Maps colors onto the nearest entry of a palette, remembering every answer */
		class NearestColorCache
		{
		public:
			NearestColorCache()
				: _cache(std::make_unique<std::uint16_t[]>(std::size_t(1) << 24)), _count(0)
			{
			}

			void Reset(const std::uint8_t* palette, std::int32_t count)
			{
				std::memcpy(_palette, palette, sizeof(_palette));
				_count = count;
				std::memset(_cache.get(), 0xFF, (std::size_t(1) << 24) * sizeof(std::uint16_t));
			}

			std::uint8_t Map(std::uint8_t r, std::uint8_t g, std::uint8_t b)
			{
				std::uint16_t& cached = _cache[(std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b];
				if (cached == 0xFFFF) {
					std::int32_t best = 0, bestDistance = INT32_MAX;
					for (std::int32_t i = 0; i < _count; i++) {
						const std::int32_t dr = std::int32_t(r) - _palette[i * 4 + 0];
						const std::int32_t dg = std::int32_t(g) - _palette[i * 4 + 1];
						const std::int32_t db = std::int32_t(b) - _palette[i * 4 + 2];
						const std::int32_t distance = dr * dr + dg * dg + db * db;
						if (distance < bestDistance) {
							bestDistance = distance;
							best = i;
						}
					}
					cached = std::uint16_t(best);
				}
				return std::uint8_t(cached);
			}

		private:
			std::unique_ptr<std::uint16_t[]> _cache;
			std::uint8_t _palette[256 * 4];
			std::int32_t _count;
		};

		/** @brief Inflates one of the four interleaved streams from the whole set of its chunks */
		bool InflateStream(Stream& input, const Array<Pair<std::int64_t, std::int32_t>>& chunks, Array<std::uint8_t>& target)
		{
			Array<std::uint8_t> compressed;
			for (const auto& chunk : chunks) {
				if (chunk.second() <= 0) {
					continue;
				}
				input.Seek(chunk.first(), SeekOrigin::Begin);
				std::size_t offset = compressed.size();
				arrayResize(compressed, NoInit, offset + chunk.second());
				if (input.Read(&compressed[offset], chunk.second()) != chunk.second()) {
					return false;
				}
			}

			if (compressed.size() <= 2) {
				return true;
			}

			// The first two bytes are the zlib header, the payload itself is raw deflate
			MemoryStream ms(compressed.data() + 2, std::int64_t(compressed.size() - 2));
			DeflateStream ds(ms);
			std::uint8_t buffer[16 * 1024];
			while (true) {
				std::int32_t bytesRead = (std::int32_t)ds.Read(buffer, sizeof(buffer));
				if (bytesRead <= 0) {
					break;
				}
				arrayAppend(target, arrayView(buffer, bytesRead));
			}
			return true;
		}
	}

	bool J2vRecompressor::DecodeFrames(StringView sourcePath, Function<bool(const J2vVideoInfo&, std::int32_t, const std::uint8_t*, const std::uint8_t*, bool)>&& onFrame, J2vVideoInfo* info)
	{
		auto input = fs::Open(sourcePath, FileAccess::Read);
		if (!input->IsValid() || input->GetSize() < HeaderSize) {
			LOGE("Cannot open \"{}\"", sourcePath);
			return false;
		}

		char signature[16];
		input->Read(signature, sizeof(signature));
		if (std::strncmp(signature, "CineFeed", sizeof("CineFeed") - 1) != 0) {
			LOGE("\"{}\" is not a cinematic file", sourcePath);
			return false;
		}

		J2vVideoInfo video;
		video.Width = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		video.Height = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		input->ReadValueAsLE<std::uint16_t>();	// Bits per pixel
		video.FrameDelay = input->ReadValueAsLE<std::uint16_t>();
		video.FrameCount = std::int32_t(input->ReadValueAsLE<std::uint32_t>());
		std::uint8_t reserved[20];
		input->Read(reserved, sizeof(reserved));
		video.FileSize = input->GetSize();

		const std::int32_t width = video.Width;
		const std::int32_t height = video.Height;
		if (width <= 0 || height <= 0 || video.FrameCount <= 0) {
			LOGE("\"{}\" has unexpected dimensions", sourcePath);
			return false;
		}
		if (info != nullptr) {
			*info = video;
		}

		// Index the interleaved chunks, then inflate each stream as a whole
		Array<Pair<std::int64_t, std::int32_t>> chunks[StreamCount];
		std::int64_t offset = input->GetPosition();
		const std::int64_t fileSize = video.FileSize;
		while (offset + 4 <= fileSize) {
			for (std::int32_t i = 0; i < StreamCount && offset + 4 <= fileSize; i++) {
				input->Seek(offset, SeekOrigin::Begin);
				std::int32_t length = input->ReadValueAsLE<std::int32_t>();
				offset += 4;
				if (length < 0 || offset + length > fileSize) {
					length = std::int32_t(std::max(std::int64_t(0), fileSize - offset));
				}
				arrayAppend(chunks[i], Pair(offset, length));
				offset += length;
			}
		}

		SourceStream streams[StreamCount];
		for (std::int32_t i = 0; i < StreamCount; i++) {
			if (!InflateStream(*input, chunks[i], streams[i].Data)) {
				LOGE("Cannot decompress stream {} of \"{}\"", i, sourcePath);
				return false;
			}
		}

		auto frame = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
		auto previousFrame = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
		std::memset(previousFrame.get(), 0, std::size_t(width) * height);
		std::uint8_t palette[256 * 4] = {};

		for (std::int32_t f = 0; f < video.FrameCount; f++) {
			// Decode one frame exactly the way the player does
			bool paletteChanged = (streams[0].ReadByte() == 0x01);
			if (paletteChanged) {
				streams[3].Read(palette, sizeof(palette));
			}

			const std::int32_t totalPixels = width * height;
			for (std::int32_t y = 0; y < height; y++) {
				std::uint8_t* row = &frame[std::size_t(y) * width];
				std::uint8_t c;
				std::int32_t x = 0;
				while ((c = streams[0].ReadByte()) != 0x80) {
					// A dead stream keeps returning zeros (c = 0 with a zero run length), which would spin
					// here forever - bail out as soon as the short read is detected, like the player does
					if (streams[0].Exhausted) {
						LOGE("\"{}\" is truncated at frame {}", sourcePath, f);
						return false;
					}
					if (c < 0x80) {
						std::int32_t u = (c == 0x00 ? streams[0].ReadUint16() : c);
						std::int32_t fits = std::min(u, std::int32_t(width - x));
						if (fits > 0) {
							streams[3].Read(&row[x], std::size_t(fits));
						}
						// A run overhanging the row would be a corrupted frame; its bytes are still consumed
						// so the stream stays aligned with the following runs
						if (u > fits) {
							streams[3].Skip(std::size_t(u - fits));
						}
						x += u;
					} else {
						std::int32_t u = (c == 0x81 ? streams[0].ReadUint16() : c - 0x6A);
						std::int32_t n = streams[1].ReadUint16() + (streams[2].ReadByte() + y - 127) * width;
						std::int32_t fits = std::min(u, std::int32_t(width - x));
						if (fits > 0 && n >= 0 && n + fits <= totalPixels) {
							std::memcpy(&row[x], &previousFrame[n], std::size_t(fits));
						}
						x += u;
					}
				}
			}

			std::memcpy(previousFrame.get(), frame.get(), std::size_t(width) * height);

			if (!onFrame(video, f, frame.get(), palette, paletteChanged || f == 0)) {
				return false;
			}
		}
		return true;
	}

	bool J2vRecompressor::Recompress(StringView sourcePath, StringView targetPath, std::int32_t downscale, bool lz4)
	{
		if (downscale < 1) {
			downscale = 1;
		}
#if !defined(WITH_LZ4)
		if (lz4) {
			LOGW("This build has no LZ4 encoder, so \"{}\" is written uncompressed", targetPath);
			lz4 = false;
		}
#endif

		J2vVideoInfo video;
		std::int32_t width = 0, height = 0, targetWidth = 0, targetHeight = 0;
		std::unique_ptr<std::uint8_t[]> scaled, previousScaled;
		Array<std::uint32_t> frameSizes;
		Array<std::uint8_t> frameData;
		std::int32_t framesWritten = 0;

		auto setUpFrames = [&](const J2vVideoInfo& info) {
			width = info.Width;
			height = info.Height;
			targetWidth = width / downscale;
			targetHeight = height / downscale;
			scaled = std::make_unique<std::uint8_t[]>(std::size_t(targetWidth) * targetHeight);
			previousScaled = std::make_unique<std::uint8_t[]>(std::size_t(targetWidth) * targetHeight);
			std::memset(previousScaled.get(), 0, std::size_t(targetWidth) * targetHeight);
		};

		Array<std::uint8_t> compressed;
		auto encodeFrame = [&](std::int32_t f, const std::uint8_t* palette) {
			Array<std::uint8_t> payload;
			EncodeFrame(payload, scaled.get(), previousScaled.get(), targetWidth * targetHeight, f == 0, palette);
#if defined(WITH_LZ4)
			if (lz4) {
				// The player decompresses one block per frame (see VideoFormat::CodecDeltaRleLz4), and a block that
				// would come out larger than the payload is stored as it is, behind a size of zero
				const std::int32_t bound = LZ4_compressBound(std::int32_t(payload.size()));
				arrayResize(compressed, NoInit, std::size_t(bound));
				const std::int32_t size = LZ4_compress_HC(reinterpret_cast<const char*>(payload.data()),
					reinterpret_cast<char*>(compressed.data()), std::int32_t(payload.size()), bound, LZ4HC_CLEVEL_MAX);
				const bool smaller = (size > 0 && std::size_t(size) < payload.size());
				const std::uint32_t storedSize = std::uint32_t(smaller ? size : std::int32_t(payload.size()));
				arrayAppend(frameSizes, std::uint32_t(4 + storedSize));
				const std::uint32_t decompressedSize = (smaller ? std::uint32_t(payload.size()) : 0);
				for (std::int32_t i = 0; i < 4; i++) {
					arrayAppend(frameData, std::uint8_t((decompressedSize >> (i * 8)) & 0xFF));
				}
				arrayAppend(frameData, arrayView(smaller ? compressed.data() : payload.data(), storedSize));
			} else
#endif
			{
				arrayAppend(frameSizes, std::uint32_t(payload.size()));
				arrayAppend(frameData, arrayView(payload));
			}
			std::memcpy(previousScaled.get(), scaled.get(), std::size_t(targetWidth) * targetHeight);
			framesWritten++;
		};

		bool decoded;
		if (downscale == 1) {
			// At the original size the frames are taken over exactly as they are, indices and palettes alike
			decoded = DecodeFrames(sourcePath, [&](const J2vVideoInfo& info, std::int32_t f, const std::uint8_t* frame,
				const std::uint8_t* palette, bool paletteChanged) {
				if (f == 0) {
					setUpFrames(info);
				}
				std::memcpy(scaled.get(), frame, std::size_t(targetWidth) * targetHeight);
				encodeFrame(f, paletteChanged ? palette : nullptr);
				return true;
			}, &video);
		} else {
			// A smaller picture is not taken by picking every n-th pixel out of the original. The cinematics were
			// rendered with heavy dithering (the skies, the smoke, every gradient), and point sampling a dithered
			// image aliases it into noise, while every edge breaks up into stairs. Instead, each output pixel is
			// the average of the area it covers, in linear light, which is the color the eye would blend those
			// pixels into from further away. Those averages are colors the original palette does not have, so
			// each stretch of frames that shares a palette in the original gets a new palette fitted to what its
			// downscaled frames actually contain (see FitPalette), and the frames are then mapped onto it.
			// It takes two passes over the video - one to see all the colors of a stretch before its palette is
			// made, one to encode - which costs a second decode, but no memory for the downscaled frames.
			AreaDownscaler downscaler;
			ColorHistogram histogram;
			Array<Segment> segments;
			std::unique_ptr<std::uint8_t[]> rgb;

			decoded = DecodeFrames(sourcePath, [&](const J2vVideoInfo& info, std::int32_t f, const std::uint8_t* frame,
				const std::uint8_t* palette, bool paletteChanged) {
				if (f == 0) {
					setUpFrames(info);
					rgb = std::make_unique<std::uint8_t[]>(std::size_t(targetWidth) * targetHeight * 3);
				}
				if (f == 0 || (paletteChanged && std::memcmp(segments.back().SourcePalette, palette, sizeof(Segment::SourcePalette)) != 0)) {
					if (f > 0) {
						segments.back().PaletteCount = FitPalette(histogram, segments.back().Palette);
					}
					Segment& segment = arrayAppend(segments, Segment{});
					segment.FirstFrame = f;
					std::memcpy(segment.SourcePalette, palette, sizeof(segment.SourcePalette));
					histogram.Clear();
				}
				downscaler.Downscale(frame, width, height, palette, downscale, rgb.get());
				histogram.Add(rgb.get(), std::size_t(targetWidth) * targetHeight);
				return true;
			}, &video);
			if (!decoded || segments.empty()) {
				return false;
			}
			segments.back().PaletteCount = FitPalette(histogram, segments.back().Palette);

			NearestColorCache cache;
			std::size_t segmentIndex = 0;
			decoded = DecodeFrames(sourcePath, [&](const J2vVideoInfo& info, std::int32_t f, const std::uint8_t* frame,
				const std::uint8_t* palette, bool paletteChanged) {
				static_cast<void>(info);
				static_cast<void>(paletteChanged);
				const bool segmentStart = (segmentIndex < segments.size() && segments[segmentIndex].FirstFrame == f);
				if (segmentStart) {
					cache.Reset(segments[segmentIndex].Palette, segments[segmentIndex].PaletteCount);
					segmentIndex++;
				}
				downscaler.Downscale(frame, width, height, palette, downscale, rgb.get());
				const std::size_t pixels = std::size_t(targetWidth) * targetHeight;
				for (std::size_t i = 0; i < pixels; i++) {
					scaled[i] = cache.Map(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
				}
				encodeFrame(f, segmentStart ? segments[segmentIndex - 1].Palette : nullptr);
				return true;
			}, &video);
		}
		if (!decoded) {
			return false;
		}
		const std::uint16_t frameDelay = video.FrameDelay;
		const std::int64_t fileSize = video.FileSize;

		auto output_ = fs::Open(targetPath, FileAccess::Write);
		if (!output_->IsValid()) {
			LOGE("Cannot open \"{}\" for writing", targetPath);
			return false;
		}

		output_->WriteValueAsLE<std::uint64_t>(VideoFormat::Signature);
		output_->WriteValue<std::uint8_t>(ContentFileType::Video);
		output_->WriteValueAsLE<std::uint16_t>(VideoFormat::CurrentVersion);
		output_->WriteValueAsLE<std::uint16_t>(std::uint16_t(targetWidth));
		output_->WriteValueAsLE<std::uint16_t>(std::uint16_t(targetHeight));
		output_->WriteValueAsLE<std::uint16_t>(frameDelay);
		output_->WriteValueAsLE<std::uint32_t>(std::uint32_t(framesWritten));
		output_->WriteValue<std::uint8_t>(VideoFormat::PixelFormatIndexed8);
		output_->WriteValue<std::uint8_t>(lz4 ? VideoFormat::CodecDeltaRleLz4 : VideoFormat::CodecDeltaRle);
		output_->WriteValueAsLE<std::uint16_t>(0);	// No extension fields yet

		std::size_t position = 0;
		for (std::uint32_t frameSize : frameSizes) {
			output_->WriteValueAsLE<std::uint32_t>(frameSize);
			output_->Write(&frameData[position], std::int64_t(frameSize));
			position += frameSize;
		}

		if (!output_->IsValid()) {
			LOGE("Cannot write \"{}\"", targetPath);
			return false;
		}

		LOGI("Recompressed \"{}\": {}x{} -> {}x{}, {} frames{}, {} KB -> {} KB", fs::GetFileName(sourcePath),
			width, height, targetWidth, targetHeight, framesWritten, lz4 ? " in LZ4"_s : ""_s,
			std::int32_t(fileSize / 1024), std::int32_t(output_->GetPosition() / 1024));
		return true;
	}
}
