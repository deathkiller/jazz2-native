#include "FontPacker.h"
#include "PngCodec.h"

#include "../Jazz2/ContentFileTypes.h"
#include "../Jazz2/UI/FontFormat.h"
#include "../Jazz2/Compatibility/JJ2Anims.h"
#include "../Jazz2/Compatibility/JJ2Anims.Palettes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Core/Logger.h>
#include <IO/FileStream.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;
using namespace Jazz2::UI;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Largest atlas the packer is willing to produce, matching the sprite sheets */
		constexpr std::int32_t MaxAtlasSize = 1024;

		/** @brief One character of a font, as it is being converted */
		struct Glyph
		{
			/** @brief Codepoint, or @ref FontFormat::FallbackCodepoint for the "unknown character" entry */
			char32_t Codepoint = 0;
			/** @brief Whether this is one of the characters of the contiguous ASCII range */
			bool IsAscii = false;
			/** @brief Where the inked pixels sit, in the source image or in the packed atlas */
			std::int32_t X = 0, Y = 0, Width = 0, Height = 0;
			/** @brief Where the inked pixels sit relative to the pen */
			std::int32_t BearingX = 0, BearingY = 0;
			/** @brief How far the pen moves after drawing this character */
			std::int32_t Advance = 0;
		};

		/** @brief Everything a font is made of, independent of which form it was read from */
		struct FontData
		{
			std::int32_t LineHeight = 0;
			std::int32_t BaseSpacing = 0;
			std::uint8_t AsciiFirst = 0;
			std::uint8_t AsciiCount = 0;
			SmallVector<Glyph, 0> Glyphs;
		};

		std::int32_t NextPowerOfTwo(std::int32_t value)
		{
			std::int32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}

		/**
			@brief Reads the character list that accompanies a grid image

			The list is the form the fonts have always been authored in: the cell size and how many of them fit
			in a row, the pen advance of every character, and the characters outside the ASCII range spelled out
			in UTF-8. An entry that decodes to nothing is the "unknown character" placeholder, which still owns a
			cell of its own.
		*/
		bool ReadCharacterList(StringView path, FontData& font, std::int32_t& cellWidth, std::int32_t& cellHeight, std::int32_t& columns)
		{
			FileStream s(path, FileAccess::Read);
			if (!s.IsValid()) {
				LOGE("Cannot open \"{}\" for reading", path);
				return false;
			}

			/*std::uint8_t flags =*/ s.ReadValue<std::uint8_t>();
			cellWidth = s.ReadValueAsLE<std::uint16_t>();
			cellHeight = s.ReadValueAsLE<std::uint16_t>();
			columns = s.ReadValue<std::uint8_t>();
			font.LineHeight = cellHeight;
			font.BaseSpacing = s.ReadValueAsLE<std::int16_t>();
			font.AsciiFirst = s.ReadValue<std::uint8_t>();
			font.AsciiCount = s.ReadValue<std::uint8_t>();

			if (cellWidth <= 0 || cellHeight <= 0 || columns <= 0) {
				LOGE("\"{}\" is corrupted", path);
				return false;
			}

			for (std::int32_t i = 0; i < font.AsciiCount; i++) {
				Glyph& glyph = font.Glyphs.emplace_back();
				glyph.Codepoint = char32_t(font.AsciiFirst + i);
				glyph.IsAscii = true;
				glyph.Advance = s.ReadValue<std::uint8_t>();
			}

			const std::int32_t unicodeCount = s.ReadValueAsLE<std::int32_t>();
			for (std::int32_t i = 0; i < unicodeCount; i++) {
				char encoded[5] {};
				s.Read(encoded, 1);

				const std::int32_t remainingBytes =
					((encoded[0] & 240) == 240) ? 3 : (
					((encoded[0] & 224) == 224) ? 2 : (
					((encoded[0] & 192) == 192) ? 1 : 0
				));

				Glyph& glyph = font.Glyphs.emplace_back();
				if (remainingBytes > 0) {
					s.Read(encoded + 1, remainingBytes);
					glyph.Codepoint = Utf8::NextChar(encoded, 0).first();
				} else {
					// Anything that isn't the start of a well-formed sequence is the placeholder
					glyph.Codepoint = FontFormat::FallbackCodepoint;
				}
				glyph.Advance = s.ReadValue<std::uint8_t>();
			}

			if (!s.IsValid()) {
				LOGE("\"{}\" is corrupted", path);
				return false;
			}
			return true;
		}

		/** @brief Writes the character list back out in the form @ref ReadCharacterList expects */
		bool WriteCharacterList(StringView path, const FontData& font, std::int32_t cellWidth, std::int32_t cellHeight, std::int32_t columns)
		{
			FileStream s(path, FileAccess::Write);
			if (!s.IsValid()) {
				LOGE("Cannot open \"{}\" for writing", path);
				return false;
			}

			s.WriteValue<std::uint8_t>(0x00);	// Flags
			s.WriteValueAsLE<std::uint16_t>(std::uint16_t(cellWidth));
			s.WriteValueAsLE<std::uint16_t>(std::uint16_t(cellHeight));
			s.WriteValue<std::uint8_t>(std::uint8_t(columns));
			s.WriteValueAsLE<std::int16_t>(std::int16_t(font.BaseSpacing));
			s.WriteValue<std::uint8_t>(font.AsciiFirst);
			s.WriteValue<std::uint8_t>(font.AsciiCount);

			for (std::int32_t i = 0; i < font.AsciiCount; i++) {
				s.WriteValue<std::uint8_t>(std::uint8_t(font.Glyphs[i].Advance));
			}

			const std::int32_t unicodeCount = std::int32_t(font.Glyphs.size()) - font.AsciiCount;
			s.WriteValueAsLE<std::int32_t>(unicodeCount);

			for (std::int32_t i = font.AsciiCount; i < std::int32_t(font.Glyphs.size()); i++) {
				const Glyph& glyph = font.Glyphs[i];
				if (glyph.Codepoint == FontFormat::FallbackCodepoint) {
					// The placeholder has no character of its own, so it is written as a single zero byte
					s.WriteValue<std::uint8_t>(0x00);
				} else {
					char encoded[4];
					const std::size_t length = Utf8::FromCodePoint(glyph.Codepoint, encoded);
					s.Write(encoded, std::int64_t(length));
				}
				s.WriteValue<std::uint8_t>(std::uint8_t(glyph.Advance));
			}

			return s.IsValid();
		}

		/**
			@brief Reduces every glyph to the pixels it actually inks

			The cell a character is authored in is only a container: the glyph inside it is usually narrower and
			almost always shorter. What is left of the cell is recorded as the glyph's bearing, so drawing it at
			the trimmed size in the trimmed place puts every pixel exactly where the full cell used to put it.
		*/
		void MeasureGlyphs(FontData& font, const Image& image, std::int32_t cellWidth, std::int32_t cellHeight, std::int32_t columns)
		{
			for (std::int32_t i = 0; i < std::int32_t(font.Glyphs.size()); i++) {
				Glyph& glyph = font.Glyphs[i];
				const std::int32_t cellX = (i % columns) * cellWidth;
				const std::int32_t cellY = (i / columns) * cellHeight;

				std::int32_t minX = cellWidth, minY = cellHeight, maxX = -1, maxY = -1;
				for (std::int32_t y = 0; y < cellHeight; y++) {
					const std::int32_t sourceY = cellY + y;
					if (sourceY >= image.Height) {
						break;
					}
					for (std::int32_t x = 0; x < cellWidth; x++) {
						const std::int32_t sourceX = cellX + x;
						if (sourceX >= image.Width) {
							break;
						}
						const std::uint8_t* pixel = &image.Pixels[(std::size_t(sourceY) * image.Width + sourceX) * 4];
						// A pixel counts as inked if it is both visible and not the transparent palette entry
						if (pixel[3] == 0 || pixel[0] == 0) {
							continue;
						}
						if (x < minX) minX = x;
						if (y < minY) minY = y;
						if (x > maxX) maxX = x;
						if (y > maxY) maxY = y;
					}
				}

				if (maxX < 0) {
					// Nothing inked at all, which is what a space looks like
					glyph.X = glyph.Y = glyph.Width = glyph.Height = 0;
					glyph.BearingX = glyph.BearingY = 0;
					continue;
				}

				glyph.X = cellX + minX;
				glyph.Y = cellY + minY;
				glyph.Width = maxX - minX + 1;
				glyph.Height = maxY - minY + 1;
				glyph.BearingX = minX;
				glyph.BearingY = minY;
			}
		}

		/**
			@brief Lays the measured glyphs out in an atlas of their own

			Tallest first, filling one shelf at a time, which keeps the glyphs in a row close in height and so
			wastes little above the shorter ones. Every glyph keeps @ref FontFormat::GlyphMargin of empty space
			on each side, putting two pixels between neighbours - enough that a bilinear sample taken at the very
			edge of one glyph cannot reach the next.

			Returns the position each glyph ended up at, in `positions`.
		*/
		bool PackGlyphs(const FontData& font, SmallVectorImpl<std::int32_t>& positions,
			std::int32_t& atlasWidth, std::int32_t& atlasHeight)
		{
			constexpr std::int32_t Margin = FontFormat::GlyphMargin;

			SmallVector<std::int32_t, 0> order;
			std::int32_t widest = 0;
			for (std::int32_t i = 0; i < std::int32_t(font.Glyphs.size()); i++) {
				const Glyph& glyph = font.Glyphs[i];
				if (glyph.Width <= 0 || glyph.Height <= 0) {
					continue;
				}
				order.push_back(i);
				widest = std::max(widest, glyph.Width);
			}

			if (order.empty()) {
				LOGE("The font has no glyphs with any pixels in them");
				return false;
			}

			std::sort(order.begin(), order.end(), [&font](std::int32_t a, std::int32_t b) {
				if (font.Glyphs[a].Height != font.Glyphs[b].Height) {
					return font.Glyphs[a].Height > font.Glyphs[b].Height;
				}
				return font.Glyphs[a].Width > font.Glyphs[b].Width;
			});

			// Try every atlas width that could hold the widest glyph and keep whichever wastes least. The width
			// is a power of two but the height is whatever the shelves come to: the backends that need a power
			// of two pad the texture themselves, so what they pay is the padded area - which is what decides
			// between the candidates - while everyone else pays only for the rows that exist.
			std::int32_t bestWidth = 0, bestHeight = 0;
			std::int64_t bestPaddedArea = INT64_MAX, bestArea = INT64_MAX;
			for (std::int32_t width = NextPowerOfTwo(widest + 2 * Margin); width <= MaxAtlasSize; width <<= 1) {
				std::int32_t x = 0, rowY = 0, rowHeight = 0;
				for (std::int32_t i : order) {
					const std::int32_t itemWidth = font.Glyphs[i].Width + 2 * Margin;
					const std::int32_t itemHeight = font.Glyphs[i].Height + 2 * Margin;
					if (x > 0 && x + itemWidth > width) {
						rowY += rowHeight;
						x = 0;
						rowHeight = 0;
					}
					x += itemWidth;
					rowHeight = std::max(rowHeight, itemHeight);
				}

				const std::int32_t height = rowY + rowHeight;
				if (NextPowerOfTwo(height) > MaxAtlasSize) {
					continue;
				}
				const std::int64_t paddedArea = std::int64_t(width) * NextPowerOfTwo(height);
				const std::int64_t area = std::int64_t(width) * height;
				if (paddedArea < bestPaddedArea || (paddedArea == bestPaddedArea && area < bestArea)) {
					bestPaddedArea = paddedArea;
					bestArea = area;
					bestWidth = width;
					bestHeight = height;
				}
			}

			if (bestWidth == 0) {
				LOGE("The font does not fit into a {}x{} atlas", MaxAtlasSize, MaxAtlasSize);
				return false;
			}

			positions.clear();
			positions.resize_for_overwrite(font.Glyphs.size() * 2);
			std::memset(positions.data(), 0, positions.size() * sizeof(std::int32_t));

			std::int32_t x = 0, rowY = 0, rowHeight = 0;
			for (std::int32_t i : order) {
				const std::int32_t itemWidth = font.Glyphs[i].Width + 2 * Margin;
				const std::int32_t itemHeight = font.Glyphs[i].Height + 2 * Margin;
				if (x > 0 && x + itemWidth > bestWidth) {
					rowY += rowHeight;
					x = 0;
					rowHeight = 0;
				}
				positions[i * 2 + 0] = x + Margin;
				positions[i * 2 + 1] = rowY + Margin;
				x += itemWidth;
				rowHeight = std::max(rowHeight, itemHeight);
			}

			atlasWidth = bestWidth;
			atlasHeight = bestHeight;
			return true;
		}
	}

	bool FontPacker::Pack(StringView sourcePath, StringView targetPath)
	{
		Image image;
		if (!PngCodec::Read(sourcePath, image)) {
			return false;
		}

		FontData font;
		std::int32_t cellWidth, cellHeight, columns;
		if (!ReadCharacterList(String(sourcePath + ".font"_s), font, cellWidth, cellHeight, columns)) {
			return false;
		}

		MeasureGlyphs(font, image, cellWidth, cellHeight, columns);

		for (const Glyph& glyph : font.Glyphs) {
			if (glyph.Width > FontFormat::MaxGlyphSize || glyph.Height > FontFormat::MaxGlyphSize) {
				LOGE("Glyph U+{:.4X} is {}x{}, larger than the {} pixels a glyph may be",
					std::uint32_t(glyph.Codepoint), glyph.Width, glyph.Height, FontFormat::MaxGlyphSize);
				return false;
			}
			if (glyph.BearingX > FontFormat::MaxGlyphBearing || glyph.BearingY > FontFormat::MaxGlyphBearing) {
				LOGE("Glyph U+{:.4X} sits at [{}, {}] in its cell, further than the {} pixels a bearing may be",
					std::uint32_t(glyph.Codepoint), glyph.BearingX, glyph.BearingY, FontFormat::MaxGlyphBearing);
				return false;
			}
		}

		// A pixel is invisible if it has no coverage or if it is the transparent palette entry, and the source
		// images use both spellings - a handful of pixels even combine one with the other. They all become the
		// same thing here, so that "no pixel" has a single representation and a font whose coverage is implied
		// by its indices is recognised as such below.
		for (std::size_t i = 0; i < std::size_t(image.Width) * image.Height; i++) {
			std::uint8_t* pixel = &image.Pixels[i * 4];
			if (pixel[0] == 0 || pixel[3] == 0) {
				pixel[0] = 0;
				pixel[3] = 0;
			}
		}

		// A glyph that antialiases its edges needs its coverage kept alongside the palette index; one with hard
		// edges is fully described by the index alone, and stores half as much
		bool hasAlpha = false;
		for (std::size_t i = 0; i < std::size_t(image.Width) * image.Height && !hasAlpha; i++) {
			const std::uint8_t index = image.Pixels[(i * 4) + 0];
			const std::uint8_t alpha = image.Pixels[(i * 4) + 3];
			hasAlpha = (alpha != (index != 0 ? 255 : 0));
		}
		const std::int32_t channelCount = (hasAlpha ? 2 : 1);

		SmallVector<std::int32_t, 0> positions;
		std::int32_t atlasWidth, atlasHeight;
		if (!PackGlyphs(font, positions, atlasWidth, atlasHeight)) {
			return false;
		}

		// Copy every glyph to where it landed, leaving the margins as the transparent palette entry
		auto atlas = std::make_unique<std::uint8_t[]>(std::size_t(atlasWidth) * atlasHeight * channelCount);
		std::memset(atlas.get(), 0, std::size_t(atlasWidth) * atlasHeight * channelCount);

		for (std::int32_t i = 0; i < std::int32_t(font.Glyphs.size()); i++) {
			Glyph& glyph = font.Glyphs[i];
			if (glyph.Width <= 0 || glyph.Height <= 0) {
				continue;
			}

			const std::int32_t targetX = positions[i * 2 + 0];
			const std::int32_t targetY = positions[i * 2 + 1];
			for (std::int32_t y = 0; y < glyph.Height; y++) {
				for (std::int32_t x = 0; x < glyph.Width; x++) {
					const std::uint8_t* source = &image.Pixels[(std::size_t(glyph.Y + y) * image.Width + (glyph.X + x)) * 4];
					std::uint8_t* target = &atlas[(std::size_t(targetY + y) * atlasWidth + (targetX + x)) * channelCount];
					target[0] = source[0];
					if (channelCount >= 2) {
						target[1] = source[3];
					}
				}
			}

			glyph.X = targetX;
			glyph.Y = targetY;
		}

		FileStream so(targetPath, FileAccess::Write);
		if (!so.IsValid()) {
			LOGE("Cannot open \"{}\" for writing", targetPath);
			return false;
		}

		// Everything but the few identifying bytes is deflated. The image encoding on its own leaves a lot on
		// the table for this kind of picture - a glyph atlas is small, high-contrast and repetitive, which is
		// what deflate is good at and what the per-pixel encoding cannot see - and the glyph records compress
		// well too, being mostly small numbers that barely change from one entry to the next.
		MemoryStream ms(256 * 1024);
		{
			DeflateWriter co(ms);

			co.WriteValueAsLE<std::uint16_t>(std::uint16_t(atlasWidth));
			co.WriteValueAsLE<std::uint16_t>(std::uint16_t(atlasHeight));
			co.WriteValueAsLE<std::uint16_t>(std::uint16_t(font.LineHeight));
			co.WriteValueAsLE<std::int16_t>(std::int16_t(font.BaseSpacing));
			co.WriteValue<std::uint8_t>(font.AsciiFirst);
			co.WriteValue<std::uint8_t>(font.AsciiCount);
			co.WriteValueAsLE<std::uint16_t>(std::uint16_t(font.Glyphs.size() - font.AsciiCount));

			for (std::int32_t i = 0; i < std::int32_t(font.Glyphs.size()); i++) {
				const Glyph& glyph = font.Glyphs[i];
				if (i >= font.AsciiCount) {
					co.WriteValueAsLE<std::uint32_t>(std::uint32_t(glyph.Codepoint));
				}
				co.WriteValueAsLE<std::uint16_t>(std::uint16_t(glyph.X));
				co.WriteValueAsLE<std::uint16_t>(std::uint16_t(glyph.Y));
				co.WriteValue<std::uint8_t>(std::uint8_t(glyph.Width));
				co.WriteValue<std::uint8_t>(std::uint8_t(glyph.Height));
				co.WriteValue<std::int8_t>(std::int8_t(glyph.BearingX));
				co.WriteValue<std::int8_t>(std::int8_t(glyph.BearingY));
				co.WriteValue<std::uint8_t>(std::uint8_t(glyph.Advance));
			}

			Compatibility::JJ2Anims::WriteImageContent(co, atlas.get(), atlasWidth, atlasHeight, channelCount);
		}

		so.WriteValueAsLE<std::uint64_t>(FontFormat::Signature);
		so.WriteValue<std::uint8_t>(ContentFileType::Font);
		so.WriteValue<std::uint8_t>(FontFormat::CurrentVersion);
		so.WriteValue<std::uint8_t>(hasAlpha ? FontFormat::Flags::HasAlpha : 0x00);
		so.WriteValueAsLE<std::int32_t>(std::int32_t(ms.GetSize()));
		so.Write(ms.GetBuffer(), ms.GetSize());

		const std::int64_t sourceArea = std::int64_t(image.Width) * image.Height;
		const std::int64_t targetArea = std::int64_t(atlasWidth) * atlasHeight;
		LOGI("{} glyphs packed into {}x{} ({}% of the {}x{} grid), {} bytes",
			font.Glyphs.size(), atlasWidth, atlasHeight, (sourceArea > 0 ? targetArea * 100 / sourceArea : 0),
			image.Width, image.Height, so.GetSize());

		return so.IsValid();
	}

	bool FontPacker::Unpack(StringView sourcePath, StringView targetPath)
	{
		FileStream s(sourcePath, FileAccess::Read);
		if (!s.IsValid()) {
			LOGE("Cannot open \"{}\" for reading", sourcePath);
			return false;
		}

		const std::uint64_t signature = s.ReadValueAsLE<std::uint64_t>();
		const std::uint8_t fileType = s.ReadValue<std::uint8_t>();
		const std::uint8_t version = s.ReadValue<std::uint8_t>();
		const std::uint8_t flags = s.ReadValue<std::uint8_t>();
		if (signature != FontFormat::Signature || fileType != ContentFileType::Font) {
			LOGE("\"{}\" is not a packed font", sourcePath);
			return false;
		}
		if (version != FontFormat::CurrentVersion) {
			LOGE("\"{}\" is version {}, but only version {} is supported", sourcePath, version, FontFormat::CurrentVersion);
			return false;
		}

		const std::int32_t compressedSize = s.ReadValueAsLE<std::int32_t>();
		DeflateStream uc(s, compressedSize);

		const std::int32_t atlasWidth = uc.ReadValueAsLE<std::uint16_t>();
		const std::int32_t atlasHeight = uc.ReadValueAsLE<std::uint16_t>();

		FontData font;
		font.LineHeight = uc.ReadValueAsLE<std::uint16_t>();
		font.BaseSpacing = uc.ReadValueAsLE<std::int16_t>();
		font.AsciiFirst = uc.ReadValue<std::uint8_t>();
		font.AsciiCount = uc.ReadValue<std::uint8_t>();
		const std::int32_t unicodeCount = uc.ReadValueAsLE<std::uint16_t>();

		for (std::int32_t i = 0; i < font.AsciiCount + unicodeCount; i++) {
			Glyph& glyph = font.Glyphs.emplace_back();
			if (i < font.AsciiCount) {
				glyph.Codepoint = char32_t(font.AsciiFirst + i);
				glyph.IsAscii = true;
			} else {
				glyph.Codepoint = char32_t(uc.ReadValueAsLE<std::uint32_t>());
			}
			glyph.X = uc.ReadValueAsLE<std::uint16_t>();
			glyph.Y = uc.ReadValueAsLE<std::uint16_t>();
			glyph.Width = uc.ReadValue<std::uint8_t>();
			glyph.Height = uc.ReadValue<std::uint8_t>();
			glyph.BearingX = uc.ReadValue<std::int8_t>();
			glyph.BearingY = uc.ReadValue<std::int8_t>();
			glyph.Advance = uc.ReadValue<std::uint8_t>();
		}

		if (!uc.IsValid() || atlasWidth <= 0 || atlasHeight <= 0) {
			LOGE("\"{}\" is corrupted", sourcePath);
			return false;
		}

		const std::int32_t channelCount = ((flags & FontFormat::Flags::HasAlpha) != 0 ? 2 : 1);
		const std::size_t atlasPixels = std::size_t(atlasWidth) * atlasHeight;
		// The decoder always stores four bytes per pixel, however few of them the image carries, so the buffer
		// is sized for that and the result is spread out afterwards
		auto atlas = std::make_unique<std::uint8_t[]>(atlasPixels * 4);
		Compatibility::JJ2Anims::ReadImageContent(uc, atlas.get(), atlasWidth, atlasHeight, channelCount);

		// Rebuild the grid: cells as tall as a line and as wide as the widest glyph reaches, laid out in a
		// roughly square sheet. It only has to hold every glyph where its bearing says it sits - packing the
		// result again measures the pixels afresh and arrives back at the same font.
		std::int32_t cellWidth = 1;
		for (const Glyph& glyph : font.Glyphs) {
			cellWidth = std::max(cellWidth, glyph.BearingX + glyph.Width);
			cellWidth = std::max(cellWidth, glyph.Advance);
		}
		const std::int32_t cellHeight = std::max(font.LineHeight, 1);

		const std::int32_t glyphCount = std::int32_t(font.Glyphs.size());
		// As many columns as it takes for the sheet to come out roughly square, which is only a convenience for
		// whoever opens it - the column count is stored alongside, and nothing depends on the particular value
		std::int32_t columns = std::int32_t(std::lround(std::sqrt(double(glyphCount) * cellHeight / cellWidth)));
		columns = std::clamp(columns, 1, 255);
		const std::int32_t rows = (glyphCount + columns - 1) / columns;

		Image image;
		image.Width = columns * cellWidth;
		image.Height = rows * cellHeight;
		image.Pixels = std::make_unique<std::uint8_t[]>(std::size_t(image.Width) * image.Height * 4);
		std::memset(image.Pixels.get(), 0, std::size_t(image.Width) * image.Height * 4);

		for (std::int32_t i = 0; i < glyphCount; i++) {
			const Glyph& glyph = font.Glyphs[i];
			const std::int32_t cellX = (i % columns) * cellWidth;
			const std::int32_t cellY = (i / columns) * cellHeight;

			for (std::int32_t y = 0; y < glyph.Height; y++) {
				for (std::int32_t x = 0; x < glyph.Width; x++) {
					const std::uint8_t* source = &atlas[(std::size_t(glyph.Y + y) * atlasWidth + (glyph.X + x)) * channelCount];
					std::uint8_t* target = &image.Pixels[(std::size_t(cellY + glyph.BearingY + y) * image.Width
						+ (cellX + glyph.BearingX + x)) * 4];
					target[0] = target[1] = target[2] = source[0];
					target[3] = (channelCount >= 2 ? source[1] : (source[0] != 0 ? 255 : 0));
				}
			}
		}

		if (!PngCodec::Write(targetPath, image)) {
			return false;
		}
		if (!WriteCharacterList(String(targetPath + ".font"_s), font, cellWidth, cellHeight, columns)) {
			return false;
		}

		LOGI("{} glyphs unpacked into a {}x{} grid of {}x{} cells", glyphCount, columns, rows, cellWidth, cellHeight);
		return true;
	}

	bool FontPacker::ApplyPalette(StringView sourcePath, StringView targetPath)
	{
		Image image;
		if (!PngCodec::Read(sourcePath, image)) {
			return false;
		}

		for (std::size_t i = 0; i < std::size_t(image.Width) * image.Height; i++) {
			std::uint8_t* pixel = &image.Pixels[i * 4];
			const Color& color = SpritePalette[pixel[0]];
			pixel[3] = std::uint8_t(color.A * pixel[3] / 255);
			pixel[0] = color.R;
			pixel[1] = color.G;
			pixel[2] = color.B;
		}

		if (!PngCodec::Write(targetPath, image)) {
			return false;
		}

		LOGI("Palette applied to a {}x{} image", image.Width, image.Height);
		return true;
	}

	bool FontPacker::ConvertToIndices(StringView sourcePath, StringView targetPath)
	{
		Image image;
		if (!PngCodec::Read(sourcePath, image)) {
			return false;
		}

		// Only fully opaque entries are candidates: index 0 is the transparent one and stands for "no pixel",
		// and coverage is carried by the alpha channel rather than by picking a different index
		SmallVector<std::int32_t, 0> candidates;
		for (std::int32_t i = 1; i < 256; i++) {
			if (SpritePalette[i].A == 255) {
				candidates.push_back(i);
			}
		}

		std::int32_t worstDistance = 0;
		for (std::size_t i = 0; i < std::size_t(image.Width) * image.Height; i++) {
			std::uint8_t* pixel = &image.Pixels[i * 4];
			if (pixel[3] == 0) {
				pixel[0] = pixel[1] = pixel[2] = 0;
				continue;
			}

			std::int32_t bestIndex = candidates.empty() ? 0 : candidates[0];
			std::int32_t bestDistance = INT32_MAX;
			for (std::int32_t index : candidates) {
				const Color& color = SpritePalette[index];
				const std::int32_t dr = std::int32_t(color.R) - pixel[0];
				const std::int32_t dg = std::int32_t(color.G) - pixel[1];
				const std::int32_t db = std::int32_t(color.B) - pixel[2];
				const std::int32_t distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					bestIndex = index;
					if (distance == 0) {
						break;
					}
				}
			}

			worstDistance = std::max(worstDistance, bestDistance);
			pixel[0] = pixel[1] = pixel[2] = std::uint8_t(bestIndex);
		}

		if (!PngCodec::Write(targetPath, image)) {
			return false;
		}

		if (worstDistance > 0) {
			LOGW("Some colors are not in the palette and were replaced by the nearest one (off by up to {} per channel)",
				std::int32_t(std::sqrt(double(worstDistance))));
		}
		LOGI("A {}x{} image converted back to palette indices", image.Width, image.Height);
		return true;
	}
}
