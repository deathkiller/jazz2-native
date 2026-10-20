#include "PngCodec.h"

#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers;
using namespace Death::IO;
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
using namespace Death::IO::Compression;
#endif

namespace nCine
{
	namespace
	{
		constexpr std::uint8_t Signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		// Image data is read in pieces of this size as it comes, so a damaged length can't allocate more than the data has
		constexpr std::uint32_t ReadPieceSize = 1 << 20;
		// Deflate can't make more than about 1032 bytes of one, so less data than that can't be a whole image
		constexpr std::uint64_t MaxDeflateRatio = 1032;

		enum ColorType : std::uint8_t
		{
			Gray = 0,
			Rgb = 2,
			Palette = 3,
			GrayAlpha = 4,
			Rgba = 6
		};

		constexpr std::uint32_t ChunkTag(char a, char b, char c, char d)
		{
			return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) | (std::uint32_t(std::uint8_t(c)) << 8) | std::uint32_t(std::uint8_t(d));
		}

#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		// The CRC of chunks is the one of zlib, which is linked for Deflate anyway; it starts from 0
		std::uint32_t UpdateCrc(std::uint32_t crc, const std::uint8_t* data, std::size_t size)
		{
			// zlib returns the initial value instead for no data, whatever the CRC so far was
			if (size == 0) {
				return crc;
			}
			// Chunks are below 2 GB, the size fits
			return std::uint32_t(::crc32(crc, data, static_cast<unsigned int>(size)));
		}
#endif

		std::uint32_t ReadBigEndian(const std::uint8_t* p)
		{
			return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
		}

		std::uint8_t Paeth(std::int32_t a, std::int32_t b, std::int32_t c)
		{
			std::int32_t p = a + b - c;
			std::int32_t pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
			return std::uint8_t(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
		}

		// Undoes the filter of a row, the previous row is all zeros for the first one
		bool Unfilter(std::uint8_t filter, std::uint8_t* row, const std::uint8_t* previous, std::size_t size, std::size_t bpp)
		{
			switch (filter) {
				case 0: return true;
				case 1: for (std::size_t i = bpp; i < size; i++) row[i] = std::uint8_t(row[i] + row[i - bpp]); return true;
				case 2: for (std::size_t i = 0; i < size; i++) row[i] = std::uint8_t(row[i] + previous[i]); return true;
				case 3:
					for (std::size_t i = 0; i < size; i++) {
						std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
						row[i] = std::uint8_t(row[i] + ((left + previous[i]) >> 1));
					}
					return true;
				case 4:
					for (std::size_t i = 0; i < size; i++) {
						std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
						std::int32_t upLeft = (i >= bpp ? previous[i - bpp] : 0);
						row[i] = std::uint8_t(row[i] + Paeth(left, previous[i], upLeft));
					}
					return true;
				default:
					return false;
			}
		}

		struct Header
		{
			std::int32_t Width, Height;
			std::uint8_t BitDepth, ColorType, Interlace;
			std::int32_t Channels;
			bool HasColorKey = false;
			std::uint16_t ColorKey[3] = {};
		};

		// Where the decoded pixels go: indices, a byte per pixel, or RGBA, four bytes per pixel; an indexed image
		// decoded into RGBA takes the colors from the table, four bytes of each of the 256 entries
		struct Target
		{
			std::uint8_t* Pixels;
			std::int32_t Width;
			bool Indexed;
			const std::uint8_t* Colors;
		};

		// The value of a sample of a row, of any depth
		std::uint32_t GetSample(const std::uint8_t* row, std::int32_t index, std::int32_t bitDepth)
		{
			switch (bitDepth) {
				case 16: return (std::uint32_t(row[index * 2]) << 8) | row[index * 2 + 1];
				case 8: return row[index];
				default: {
					std::int32_t bit = index * bitDepth;
					std::int32_t shift = 8 - bitDepth - (bit & 7);
					return (row[bit >> 3] >> shift) & ((1u << bitDepth) - 1);
				}
			}
		}

		// Converts a pixel of an unfiltered row to the image
		void StorePixel(const Header& header, const std::uint8_t* row, std::int32_t sourceX, const Target& target, std::int32_t x, std::int32_t y)
		{
			std::size_t pos = std::size_t(y) * std::size_t(target.Width) + std::size_t(x);
			if (header.ColorType == Palette) {
				std::uint32_t index = GetSample(row, sourceX, header.BitDepth);
				if (target.Indexed) {
					target.Pixels[pos] = std::uint8_t(index);
				} else {
					std::memcpy(&target.Pixels[pos * 4], &target.Colors[index * 4], 4);
				}
				return;
			}

			std::int32_t depth = header.BitDepth;
			std::uint32_t maxValue = (1u << depth) - 1;
			auto to8 = [maxValue](std::uint32_t value) {
				return std::uint8_t((value * 255 + maxValue / 2) / maxValue);
			};
			std::uint32_t samples[4] = {};
			for (std::int32_t c = 0; c < header.Channels; c++) {
				samples[c] = GetSample(row, sourceX * header.Channels + c, depth);
			}
			std::uint8_t r, g, b, a = 255;
			switch (header.ColorType) {
				case Gray:
					r = g = b = to8(samples[0]);
					if (header.HasColorKey && samples[0] == header.ColorKey[0]) {
						a = 0;
					}
					break;
				case GrayAlpha:
					r = g = b = to8(samples[0]);
					a = to8(samples[1]);
					break;
				case Rgb:
					r = to8(samples[0]);
					g = to8(samples[1]);
					b = to8(samples[2]);
					if (header.HasColorKey && samples[0] == header.ColorKey[0] && samples[1] == header.ColorKey[1] && samples[2] == header.ColorKey[2]) {
						a = 0;
					}
					break;
				default:
					r = to8(samples[0]);
					g = to8(samples[1]);
					b = to8(samples[2]);
					a = to8(samples[3]);
					break;
			}
			std::uint8_t* out = &target.Pixels[pos * 4];
			out[0] = r;
			out[1] = g;
			out[2] = b;
			out[3] = a;
		}

		// Converts a whole unfiltered row of an image that isn't interlaced, the common kinds of 8 bits directly, as
		// a pixel at a time through StorePixel() was most of the time a large image took
		void StoreRow(const Header& header, const std::uint8_t* row, const Target& target, std::int32_t y)
		{
			std::size_t width = std::size_t(target.Width);
			std::size_t pos = std::size_t(y) * width;
			if (header.BitDepth == 8) {
				switch (header.ColorType) {
					case Palette:
						if (target.Indexed) {
							std::memcpy(&target.Pixels[pos], row, width);
						} else {
							std::uint8_t* out = &target.Pixels[pos * 4];
							for (std::size_t x = 0; x < width; x++, out += 4) {
								std::memcpy(out, &target.Colors[std::size_t(row[x]) * 4], 4);
							}
						}
						return;
					case Rgba:
						std::memcpy(&target.Pixels[pos * 4], row, width * 4);
						return;
					case Rgb:
						if (!header.HasColorKey) {
							std::uint8_t* out = &target.Pixels[pos * 4];
							for (std::size_t x = 0; x < width; x++, out += 4, row += 3) {
								out[0] = row[0];
								out[1] = row[1];
								out[2] = row[2];
								out[3] = 255;
							}
							return;
						}
						break;
					case Gray:
						if (!header.HasColorKey) {
							std::uint8_t* out = &target.Pixels[pos * 4];
							for (std::size_t x = 0; x < width; x++, out += 4) {
								out[0] = out[1] = out[2] = row[x];
								out[3] = 255;
							}
							return;
						}
						break;
					case GrayAlpha: {
						std::uint8_t* out = &target.Pixels[pos * 4];
						for (std::size_t x = 0; x < width; x++, out += 4, row += 2) {
							out[0] = out[1] = out[2] = row[0];
							out[3] = row[1];
						}
						return;
					}
					default:
						break;
				}
			}
			for (std::int32_t x = 0; x < target.Width; x++) {
				StorePixel(header, row, x, target, x, y);
			}
		}

		// Reads data a piece at a time as it comes and appends it, so a damaged length can't allocate more than there is
		bool ReadAppend(Stream& s, SmallVector<std::uint8_t, 0>& target, std::uint32_t length)
		{
			while (length > 0) {
				std::uint32_t piece = std::min<std::uint32_t>(length, ReadPieceSize);
				std::size_t at = target.size();
				target.resize_for_overwrite(at + piece);
				if (s.Read(target.data() + at, piece) != std::int64_t(piece)) {
					return false;
				}
				length -= piece;
			}
			return true;
		}

		// Skips data a piece at a time, a stream that can't seek can be read too
		bool Skip(Stream& s, std::uint32_t length)
		{
			std::uint8_t buffer[512];
			while (length > 0) {
				std::uint32_t piece = std::min<std::uint32_t>(length, std::uint32_t(sizeof(buffer)));
				if (s.Read(buffer, piece) != std::int64_t(piece)) {
					return false;
				}
				length -= piece;
			}
			return true;
		}

		/**
		 * Reads the chunks of an image and decodes it into the pixels `allocate(width, height, indexed)` returns for
		 * its size. An indexed image is decoded into RGBA if `expandPalette` is set. The palette is returned as the
		 * file has it, entries it doesn't list zero.
		 */
		template<class Allocate>
		PngError DecodeImage(Stream& s, const PngDecodeOptions& options, bool expandPalette, std::uint32_t (&palette)[256],
			std::int32_t& paletteSize, Allocate&& allocate)
		{
#if !defined(WITH_ZLIB) && !defined(WITH_MINIZ)
			static_cast<void>(s);
			static_cast<void>(options);
			static_cast<void>(expandPalette);
			static_cast<void>(allocate);
			std::memset(palette, 0, sizeof(palette));
			paletteSize = 0;
			return PngError::Unsupported;
#else
			std::memset(palette, 0, sizeof(palette));
			paletteSize = 0;

			std::uint8_t signature[8];
			if (s.Read(signature, sizeof(signature)) != std::int64_t(sizeof(signature)) || std::memcmp(signature, Signature, sizeof(Signature)) != 0) {
				return PngError::NotPng;
			}

			Header header = {};
			bool hasHeader = false;
			// Entries of the palette that tRNS gave an alpha, an entry past the colors of the palette can have one too
			std::uint32_t transparencyCount = 0;
			SmallVector<std::uint8_t, 0> compressed;
			// The largest chunk read whole is PLTE, the image data is read as it comes
			std::uint8_t data[768];

			for (;;) {
				std::uint8_t lengthAndType[8];
				if (s.Read(lengthAndType, sizeof(lengthAndType)) != std::int64_t(sizeof(lengthAndType))) {
					return PngError::EndsTooSoon;
				}
				std::uint32_t length = ReadBigEndian(lengthAndType);
				std::uint32_t type = ReadBigEndian(lengthAndType + 4);
				if (length > 0x7FFFFFFF) {
					return PngError::Damaged;
				}
				// A chunk that the image can't be read without, an ancillary one can be left out
				bool critical = ((type & 0x20000000u) == 0);
				if (!hasHeader && type != ChunkTag('I', 'H', 'D', 'R')) {
					return PngError::Damaged;
				}

				std::uint32_t crc = (options.VerifyCrc ? UpdateCrc(0, lengthAndType + 4, 4) : 0);
				std::uint8_t storedCrc[4];
				if (type == ChunkTag('I', 'D', 'A', 'T')) {
					std::size_t start = compressed.size();
					if (!ReadAppend(s, compressed, length) || s.Read(storedCrc, 4) != 4) {
						return PngError::EndsTooSoon;
					}
					if (options.VerifyCrc && UpdateCrc(crc, compressed.data() + start, length) != ReadBigEndian(storedCrc)) {
						return PngError::Damaged;
					}
					continue;
				}

				std::uint32_t maxLength;
				switch (type) {
					case ChunkTag('I', 'H', 'D', 'R'): maxLength = 13; break;
					case ChunkTag('P', 'L', 'T', 'E'): maxLength = 768; break;
					case ChunkTag('t', 'R', 'N', 'S'): maxLength = 256; break;
					case ChunkTag('I', 'E', 'N', 'D'): maxLength = 0; break;
					default: {
						if (critical) {
							return PngError::Unsupported;
						}
						// An ancillary chunk that isn't used, skipped without looking at its CRC
						if (!Skip(s, length) || !Skip(s, 4)) {
							return PngError::EndsTooSoon;
						}
						continue;
					}
				}
				if (length > maxLength) {
					// Only the transparency of a damaged image can be left out, the rest of it is needed
					if (!critical) {
						if (!Skip(s, length) || !Skip(s, 4)) {
							return PngError::EndsTooSoon;
						}
						continue;
					}
					return PngError::Damaged;
				}
				if (s.Read(data, length) != std::int64_t(length) || s.Read(storedCrc, 4) != 4) {
					return PngError::EndsTooSoon;
				}
				if (options.VerifyCrc && UpdateCrc(crc, data, length) != ReadBigEndian(storedCrc)) {
					if (critical) {
						return PngError::Damaged;
					}
					// A damaged ancillary chunk is ignored, as other programs do
					continue;
				}

				if (type == ChunkTag('I', 'H', 'D', 'R')) {
					if (hasHeader || length != 13) {
						return PngError::Damaged;
					}
					header.Width = std::int32_t(ReadBigEndian(data));
					header.Height = std::int32_t(ReadBigEndian(data + 4));
					header.BitDepth = data[8];
					header.ColorType = data[9];
					header.Interlace = data[12];
					if (header.Width <= 0 || header.Height <= 0) {
						return PngError::Damaged;
					}
					if (std::int64_t(header.Width) * header.Height > options.MaxPixels) {
						return PngError::TooLarge;
					}
					if (data[10] != 0 || data[11] != 0 || header.Interlace > 1) {
						return PngError::UnknownKind;
					}
					switch (header.ColorType) {
						case Gray: header.Channels = 1; break;
						case Rgb: header.Channels = 3; break;
						case Palette: header.Channels = 1; break;
						case GrayAlpha: header.Channels = 2; break;
						case Rgba: header.Channels = 4; break;
						default: return PngError::UnknownKind;
					}
					bool validDepth = (header.ColorType == Gray ? (header.BitDepth == 1 || header.BitDepth == 2 || header.BitDepth == 4 || header.BitDepth == 8 || header.BitDepth == 16)
						: (header.ColorType == Palette ? (header.BitDepth == 1 || header.BitDepth == 2 || header.BitDepth == 4 || header.BitDepth == 8)
						: (header.BitDepth == 8 || header.BitDepth == 16)));
					if (!validDepth) {
						return PngError::UnknownKind;
					}
					hasHeader = true;
				} else if (type == ChunkTag('P', 'L', 'T', 'E')) {
					paletteSize = std::int32_t(length / 3);
					for (std::int32_t i = 0; i < paletteSize; i++) {
						// tRNS belongs after PLTE, but one that came before it still gives the entries their alpha, as other
						// readers take it, instead of being lost
						std::uint32_t alpha = (std::uint32_t(i) < transparencyCount ? (palette[i] & 0xFF000000u) : 0xFF000000u);
						palette[i] = std::uint32_t(data[i * 3]) | (std::uint32_t(data[i * 3 + 1]) << 8) | (std::uint32_t(data[i * 3 + 2]) << 16) | alpha;
					}
				} else if (type == ChunkTag('t', 'R', 'N', 'S')) {
					// Of a color with alpha the transparency isn't allowed, it's ignored then
					if (header.ColorType == Palette) {
						for (std::uint32_t i = 0; i < length; i++) {
							palette[i] = (palette[i] & 0x00FFFFFFu) | (std::uint32_t(data[i]) << 24);
						}
						transparencyCount = length;
					} else if (header.ColorType == Gray && length == 2) {
						header.HasColorKey = true;
						header.ColorKey[0] = std::uint16_t((data[0] << 8) | data[1]);
					} else if (header.ColorType == Rgb && length == 6) {
						header.HasColorKey = true;
						for (std::int32_t c = 0; c < 3; c++) {
							header.ColorKey[c] = std::uint16_t((data[c * 2] << 8) | data[c * 2 + 1]);
						}
					}
				} else if (type == ChunkTag('I', 'E', 'N', 'D')) {
					break;
				}
			}
			// The two bytes of the zlib header are needed at least
			if (!hasHeader || compressed.size() < 2) {
				return PngError::Damaged;
			}
			if (header.ColorType == Palette && paletteSize == 0) {
				return PngError::NoPalette;
			}

			// Passes of Adam7, or a single one of the whole image
			struct Pass { std::int32_t X, Y, StepX, StepY; };
			static const Pass Adam7[7] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
			static const Pass Single[1] = { { 0, 0, 1, 1 } };
			const Pass* passes = (header.Interlace != 0 ? Adam7 : Single);
			std::int32_t passCount = (header.Interlace != 0 ? 7 : 1);

			std::size_t bitsPerPixel = std::size_t(header.BitDepth) * std::size_t(header.Channels);
			std::size_t bpp = std::max<std::size_t>(1, bitsPerPixel / 8);
			std::uint64_t rawSize = 0;
			std::size_t maxRowSize = 0;
			for (std::int32_t p = 0; p < passCount; p++) {
				std::int32_t width = (header.Width - passes[p].X + passes[p].StepX - 1) / passes[p].StepX;
				std::int32_t height = (header.Height - passes[p].Y + passes[p].StepY - 1) / passes[p].StepY;
				if (width > 0 && height > 0) {
					std::size_t rowSize = (std::size_t(width) * bitsPerPixel + 7) / 8;
					rawSize += std::uint64_t(height) * (1 + rowSize);
					maxRowSize = std::max<std::size_t>(maxRowSize, rowSize);
				}
			}
			// Too little data for the size the image claims, it's found before the image takes its memory
			if (rawSize > std::uint64_t(compressed.size()) * MaxDeflateRatio + 1024) {
				return PngError::Damaged;
			}

			// An indexed image decoded into RGBA takes its colors from this, as bytes, the same on every machine; an entry
			// the palette doesn't list is opaque black, as viewers show it, unless tRNS gave it an alpha
			bool indexed = (header.ColorType == Palette && !expandPalette);
			std::uint8_t colors[256 * 4];
			if (header.ColorType == Palette && !indexed) {
				for (std::int32_t i = 0; i < 256; i++) {
					std::uint32_t color = (i < paletteSize ? palette[i] : (std::uint32_t(i) < transparencyCount ? (palette[i] & 0xFF000000u) : 0xFF000000u));
					colors[i * 4] = std::uint8_t(color);
					colors[i * 4 + 1] = std::uint8_t(color >> 8);
					colors[i * 4 + 2] = std::uint8_t(color >> 16);
					colors[i * 4 + 3] = std::uint8_t(color >> 24);
				}
			}

			Target target;
			target.Pixels = allocate(header.Width, header.Height, indexed);
			target.Width = header.Width;
			target.Indexed = indexed;
			target.Colors = colors;

			// Row by row as it's inflated, straight into the pixels, with only the row before it kept, instead of all of
			// the data at once, which is twice the size of the image for 16 bits per channel. The zlib header is skipped
			// and its checksum never reached, some images of the game were written with it left zero.
			MemoryStream input(compressed.data() + 2, std::int64_t(compressed.size() - 2));
			DeflateStream inflater(input, std::int32_t(std::min<std::size_t>(compressed.size() - 2, INT32_MAX)), true);
			SmallVector<std::uint8_t, 0> rows;
			rows.resize_for_overwrite(2 * (maxRowSize + 1));
			std::uint8_t* current = rows.data();
			std::uint8_t* previous = rows.data() + maxRowSize + 1;
			for (std::int32_t p = 0; p < passCount; p++) {
				std::int32_t width = (header.Width - passes[p].X + passes[p].StepX - 1) / passes[p].StepX;
				std::int32_t height = (header.Height - passes[p].Y + passes[p].StepY - 1) / passes[p].StepY;
				if (width <= 0 || height <= 0) {
					continue;
				}
				std::size_t rowSize = (std::size_t(width) * bitsPerPixel + 7) / 8;
				// The row before the first one is all zeros, after the filter byte
				std::memset(previous, 0, rowSize + 1);
				for (std::int32_t y = 0; y < height; y++) {
					if (inflater.Read(current, std::int64_t(rowSize + 1)) != std::int64_t(rowSize + 1)) {
						return PngError::Damaged;
					}
					std::uint8_t* row = current + 1;
					if (!Unfilter(current[0], row, previous + 1, rowSize, bpp)) {
						return PngError::Damaged;
					}
					if (passCount == 1) {
						StoreRow(header, row, target, y);
					} else {
						for (std::int32_t x = 0; x < width; x++) {
							StorePixel(header, row, x, target, passes[p].X + x * passes[p].StepX, passes[p].Y + y * passes[p].StepY);
						}
					}
					std::swap(current, previous);
				}
			}
			return PngError::None;
#endif
		}

#if defined(WITH_PNG_ENCODER) && (defined(WITH_ZLIB) || defined(WITH_MINIZ))
		void WriteBigEndian(std::uint8_t* p, std::uint32_t value)
		{
			p[0] = std::uint8_t(value >> 24);
			p[1] = std::uint8_t(value >> 16);
			p[2] = std::uint8_t(value >> 8);
			p[3] = std::uint8_t(value);
		}

		// Filters a row by the given filter into out, which has the filter byte first
		void Filter(std::uint8_t filter, const std::uint8_t* row, const std::uint8_t* previous, std::size_t size, std::size_t bpp, std::uint8_t* out)
		{
			out[0] = filter;
			for (std::size_t i = 0; i < size; i++) {
				std::int32_t left = (i >= bpp ? row[i - bpp] : 0);
				std::int32_t up = previous[i];
				std::int32_t upLeft = (i >= bpp ? previous[i - bpp] : 0);
				std::int32_t predicted;
				switch (filter) {
					case 1: predicted = left; break;
					case 2: predicted = up; break;
					case 3: predicted = (left + up) >> 1; break;
					case 4: predicted = Paeth(left, up, upLeft); break;
					default: predicted = 0; break;
				}
				out[1 + i] = std::uint8_t(row[i] - predicted);
			}
		}

		// Writes indices with a palette, or RGBA, 8 bits per channel
		bool EncodeImage(Stream& s, std::int32_t width, std::int32_t height, const std::uint8_t* pixels, bool indexed,
			const std::uint32_t* palette, std::int32_t paletteSize)
		{
			// Every write is checked, a full disk must not report a written image
			bool written = true;
			auto write = [&s, &written](const void* data, std::size_t size) {
				if (written && size > 0) {
					written = (s.Write(data, std::int64_t(size)) == std::int64_t(size));
				}
			};
			auto writeChunk = [&write](const char* type, const std::uint8_t* data, std::size_t size) {
				std::uint8_t lengthBytes[4];
				WriteBigEndian(lengthBytes, std::uint32_t(size));
				write(lengthBytes, 4);
				std::uint32_t value = UpdateCrc(0, reinterpret_cast<const std::uint8_t*>(type), 4);
				value = UpdateCrc(value, data, size);
				write(type, 4);
				write(data, size);
				std::uint8_t crcBytes[4];
				WriteBigEndian(crcBytes, value);
				write(crcBytes, 4);
			};

			write(Signature, sizeof(Signature));
			std::uint8_t ihdr[13];
			WriteBigEndian(ihdr, std::uint32_t(width));
			WriteBigEndian(ihdr + 4, std::uint32_t(height));
			ihdr[8] = 8;
			ihdr[9] = (indexed ? Palette : Rgba);
			ihdr[10] = 0;
			ihdr[11] = 0;
			ihdr[12] = 0;
			writeChunk("IHDR", ihdr, sizeof(ihdr));

			if (indexed) {
				paletteSize = std::clamp<std::int32_t>(paletteSize, 1, 256);
				std::uint8_t plte[256 * 3];
				std::uint8_t trns[256];
				std::int32_t trnsSize = 0;
				for (std::int32_t i = 0; i < paletteSize; i++) {
					std::uint32_t color = palette[i];
					plte[i * 3] = std::uint8_t(color);
					plte[i * 3 + 1] = std::uint8_t(color >> 8);
					plte[i * 3 + 2] = std::uint8_t(color >> 16);
					trns[i] = std::uint8_t(color >> 24);
					if (trns[i] != 255) {
						trnsSize = i + 1;
					}
				}
				writeChunk("PLTE", plte, std::size_t(paletteSize) * 3);
				if (trnsSize > 0) {
					writeChunk("tRNS", trns, std::size_t(trnsSize));
				}
			}

			// Each row with the filter that leaves the smallest differences, which compress best, as the specification
			// suggests --- long runs of the same value survive into the deflate stream
			std::size_t bpp = (indexed ? 1 : 4);
			std::size_t rowSize = std::size_t(width) * bpp;
			MemoryStream compressed;
			{
				DeflateWriter deflater(compressed, 9, false);
				SmallVector<std::uint8_t, 0> zeros;
				zeros.resize(rowSize, 0);
				SmallVector<std::uint8_t, 0> candidate, best;
				candidate.resize_for_overwrite(rowSize + 1);
				best.resize_for_overwrite(rowSize + 1);
				for (std::int32_t y = 0; y < height; y++) {
					const std::uint8_t* row = &pixels[std::size_t(y) * rowSize];
					const std::uint8_t* previous = (y > 0 ? row - rowSize : zeros.data());
					// Indexed pixels aren't magnitudes, differences of them don't help
					std::uint8_t lastFilter = (indexed ? 0 : 4);
					std::uint64_t bestCost = UINT64_MAX;
					for (std::uint8_t filter = 0; filter <= lastFilter; filter++) {
						Filter(filter, row, previous, rowSize, bpp, candidate.data());
						std::uint64_t cost = 0;
						for (std::size_t i = 1; i <= rowSize; i++) {
							cost += std::uint64_t(std::abs(std::int32_t(std::int8_t(candidate[i]))));
						}
						if (cost < bestCost) {
							bestCost = cost;
							std::swap(candidate, best);
						}
					}
					deflater.Write(best.data(), std::int64_t(rowSize + 1));
				}
				deflater.Dispose();
			}
			writeChunk("IDAT", compressed.GetBuffer(), std::size_t(compressed.GetSize()));
			writeChunk("IEND", nullptr, 0);
			return written;
		}
#endif
	}

	void PngImage::Create(std::int32_t width, std::int32_t height, bool indexed)
	{
		Width = width;
		Height = height;
		Indexed = indexed;
		Pixels.clear();
		Pixels.resize(std::size_t(width) * std::size_t(height) * (indexed ? 1 : 4), 0);
	}

	std::uint32_t PngImage::GetColor(std::int32_t x, std::int32_t y) const
	{
		std::size_t pos = std::size_t(y) * std::size_t(Width) + std::size_t(x);
		if (Indexed) {
			return Palette[Pixels[pos]];
		}
		const std::uint8_t* p = &Pixels[pos * 4];
		return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
	}

	PngError PngCodec::Decode(Stream& s, PngImage& image, const PngDecodeOptions& options)
	{
		// Nothing of an image read before is left, whether this one can be read or not
		image = PngImage();
		std::uint32_t palette[256];
		std::int32_t paletteSize;
		PngError error = DecodeImage(s, options, false, palette, paletteSize, [&image](std::int32_t width, std::int32_t height, bool indexed) {
			image.Create(width, height, indexed);
			return image.Pixels.data();
		});
		if (error != PngError::None) {
			image = PngImage();
			return error;
		}
		std::memcpy(image.Palette, palette, sizeof(palette));
		image.PaletteSize = paletteSize;
		return PngError::None;
	}

	PngError PngCodec::DecodeRgba(Stream& s, PngRgbaImage& image, const PngDecodeOptions& options)
	{
		image = PngRgbaImage();
		std::uint32_t palette[256];
		std::int32_t paletteSize;
		PngError error = DecodeImage(s, options, true, palette, paletteSize, [&image](std::int32_t width, std::int32_t height, bool indexed) {
			static_cast<void>(indexed);
			// Every pixel is written by the decoding, it doesn't need to be cleared first
			image.Width = width;
			image.Height = height;
			image.Pixels = std::unique_ptr<std::uint8_t[]>(new std::uint8_t[std::size_t(width) * std::size_t(height) * 4]);
			return image.Pixels.get();
		});
		if (error != PngError::None) {
			image = PngRgbaImage();
		}
		return error;
	}

	const char* PngCodec::GetErrorText(PngError error)
	{
		switch (error) {
			case PngError::None: return "No error";
			case PngError::NotPng: return "It isn't a PNG image";
			case PngError::EndsTooSoon: return "The image ends too soon";
			case PngError::Damaged: return "The image is damaged";
			case PngError::TooLarge: return "The image is too large";
			case PngError::UnknownKind: return "The image is of an unknown kind";
			case PngError::Unsupported: return "The image uses a feature that isn't supported";
			case PngError::NoPalette: return "The image has no palette";
			default: return "Unknown error";
		}
	}

#if defined(WITH_PNG_ENCODER)
	bool PngCodec::Encode(Stream& s, const PngImage& image)
	{
#	if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		if (image.Width <= 0 || image.Height <= 0 || image.Pixels.size() != std::size_t(image.Width) * std::size_t(image.Height) * (image.Indexed ? 1 : 4)) {
			return false;
		}
		return EncodeImage(s, image.Width, image.Height, image.Pixels.data(), image.Indexed, image.Palette, image.PaletteSize);
#	else
		static_cast<void>(s);
		static_cast<void>(image);
		return false;
#	endif
	}

	bool PngCodec::EncodeRgba(Stream& s, std::int32_t width, std::int32_t height, const std::uint8_t* pixels)
	{
#	if defined(WITH_ZLIB) || defined(WITH_MINIZ)
		if (width <= 0 || height <= 0 || pixels == nullptr) {
			return false;
		}
		return EncodeImage(s, width, height, pixels, false, nullptr, 0);
#	else
		static_cast<void>(s);
		static_cast<void>(width);
		static_cast<void>(height);
		static_cast<void>(pixels);
		return false;
#	endif
	}
#endif
}
