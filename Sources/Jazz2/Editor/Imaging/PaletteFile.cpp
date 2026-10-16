#include "PaletteFile.h"

#if defined(WITH_EDITOR)

#include "PngCodec.h"
#include "../../../nCine/I18n.h"

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		enum class Format { None, Jasc, Gimp, Act, Png };

		// A palette written as an image is a grid of 16 × 16 cells of this size
		constexpr std::int32_t PngCellSize = 8;

		Format GetFormat(StringView path)
		{
			String extension = StringUtils::lowercase(fs::GetExtension(path));
			if (extension == "pal"_s) return Format::Jasc;
			if (extension == "gpl"_s) return Format::Gimp;
			if (extension == "act"_s) return Format::Act;
			if (extension == "png"_s) return Format::Png;
			return Format::None;
		}

		// The palette of an indexed image, or the colors of a grid of 16 × 16 cells, or of up to 256 pixels in a row
		std::int32_t ReadPng(StringView path, std::uint32_t (&palette)[256], String* error)
		{
			auto s = fs::Open(path, FileAccess::Read);
			PngImage image;
			if (!s->IsValid() || !PngCodec::Decode(*s, image, error)) {
				if (error != nullptr && error->empty()) {
					*error = _("The file can't be read");
				}
				return 0;
			}
			if (image.Indexed) {
				for (std::int32_t i = 0; i < image.PaletteSize; i++) {
					palette[i] = image.Palette[i] | 0xFF000000u;
				}
				if (image.PaletteSize <= 0 && error != nullptr) {
					*error = _("The file has no colors");
				}
				return image.PaletteSize;
			}
			if (image.Width % 16 == 0 && image.Height % 16 == 0) {
				std::int32_t cellWidth = image.Width / 16, cellHeight = image.Height / 16;
				for (std::int32_t i = 0; i < 256; i++) {
					palette[i] = image.GetColor((i % 16) * cellWidth + cellWidth / 2, (i / 16) * cellHeight + cellHeight / 2) | 0xFF000000u;
				}
				return 256;
			}
			if (image.Width * image.Height <= 256) {
				std::int32_t count = image.Width * image.Height;
				for (std::int32_t i = 0; i < count; i++) {
					palette[i] = image.GetColor(i % image.Width, i / image.Width) | 0xFF000000u;
				}
				return count;
			}
			if (error != nullptr) {
				*error = _("The image isn't indexed, and isn't a grid of 16 × 16 colors");
			}
			return 0;
		}

		std::uint32_t MakeColor(std::int32_t r, std::int32_t g, std::int32_t b)
		{
			auto clamp = [](std::int32_t v) { return std::uint32_t(v < 0 ? 0 : (v > 255 ? 255 : v)); };
			return clamp(r) | (clamp(g) << 8) | (clamp(b) << 16) | 0xFF000000u;
		}

		// Reads up to three whole numbers at the start of a line, returns how many were there
		std::int32_t ReadNumbers(StringView line, std::int32_t (&values)[3])
		{
			std::int32_t count = 0;
			const char* p = line.data();
			const char* end = line.data() + line.size();
			while (count < 3 && p < end) {
				while (p < end && (*p == ' ' || *p == '\t')) {
					p++;
				}
				if (p >= end || *p < '0' || *p > '9') {
					break;
				}
				std::int32_t value = 0;
				while (p < end && *p >= '0' && *p <= '9') {
					value = value * 10 + (*p - '0');
					p++;
				}
				values[count++] = value;
			}
			return count;
		}
	}

	bool PaletteFile::IsSupported(StringView path)
	{
		return (GetFormat(path) != Format::None);
	}

	std::int32_t PaletteFile::Read(StringView path, std::uint32_t (&palette)[256], String* error)
	{
		Format format = GetFormat(path);
		if (format == Format::None) {
			if (error != nullptr) {
				*error = _("Only .pal, .gpl, .act and .png palettes can be read");
			}
			return 0;
		}
		if (format == Format::Png) {
			return ReadPng(path, palette, error);
		}
		auto s = fs::Open(path, FileAccess::Read);
		std::int64_t size = (s->IsValid() ? s->GetSize() : -1);
		if (size <= 0 || size > 1024 * 1024) {
			if (error != nullptr) {
				*error = _("The file can't be read");
			}
			return 0;
		}
		String data{NoInit, std::size_t(size)};
		s->Read(data.data(), size);

		if (format == Format::Act) {
			if (size < 768) {
				if (error != nullptr) {
					*error = _("The file is too short for a palette");
				}
				return 0;
			}
			std::int32_t count = 256;
			if (size >= 772) {
				// The number of colors that are used follows, if it's there
				std::int32_t used = (std::uint8_t(data[768]) << 8) | std::uint8_t(data[769]);
				if (used > 0 && used <= 256) {
					count = used;
				}
			}
			for (std::int32_t i = 0; i < count; i++) {
				palette[i] = MakeColor(std::uint8_t(data[i * 3]), std::uint8_t(data[i * 3 + 1]), std::uint8_t(data[i * 3 + 2]));
			}
			return count;
		}

		// A text file, line by line
		std::int32_t count = 0;
		std::int32_t lineIndex = 0;
		std::int32_t expected = 256;
		StringView rest = data;
		while (!rest.empty() && count < 256) {
			auto parts = rest.partition('\n');
			StringView line = parts[0].trimmed();
			rest = parts[2];
			lineIndex++;

			if (format == Format::Jasc) {
				// "JASC-PAL", "0100", the number of colors
				if (lineIndex == 1) {
					if (!line.hasPrefix("JASC-PAL"_s)) {
						if (error != nullptr) {
							*error = _("The file isn't a JASC palette");
						}
						return 0;
					}
					continue;
				}
				if (lineIndex == 2) {
					continue;
				}
				if (lineIndex == 3) {
					std::int32_t values[3];
					if (ReadNumbers(line, values) >= 1) {
						expected = std::min(values[0], 256);
					}
					continue;
				}
			} else {
				if (lineIndex == 1) {
					if (!line.hasPrefix("GIMP Palette"_s)) {
						if (error != nullptr) {
							*error = _("The file isn't a GIMP palette");
						}
						return 0;
					}
					continue;
				}
				if (line.empty() || line[0] == '#' || line.hasPrefix("Name:"_s) || line.hasPrefix("Columns:"_s)) {
					continue;
				}
			}

			std::int32_t values[3];
			if (ReadNumbers(line, values) == 3) {
				palette[count++] = MakeColor(values[0], values[1], values[2]);
				if (count >= expected) {
					break;
				}
			}
		}
		if (count == 0 && error != nullptr) {
			*error = _("The file has no colors");
		}
		return count;
	}

	bool PaletteFile::Write(StringView path, const std::uint32_t (&palette)[256], StringView name)
	{
		Format format = GetFormat(path);
		if (format == Format::None) {
			return false;
		}
		auto s = fs::Open(path, FileAccess::Write);
		if (!s->IsValid()) {
			return false;
		}

		if (format == Format::Png) {
			// Indexed, so it's read back exactly, and other programs show it as a grid of the colors
			PngImage image;
			image.Create(16 * PngCellSize, 16 * PngCellSize, true);
			for (std::int32_t y = 0; y < image.Height; y++) {
				for (std::int32_t x = 0; x < image.Width; x++) {
					image.Pixels[y * image.Width + x] = std::uint8_t((y / PngCellSize) * 16 + x / PngCellSize);
				}
			}
			for (std::int32_t i = 0; i < 256; i++) {
				image.Palette[i] = palette[i] | 0xFF000000u;
			}
			image.PaletteSize = 256;
			return PngCodec::Encode(*s, image);
		}

		if (format == Format::Act) {
			std::uint8_t data[772];
			for (std::int32_t i = 0; i < 256; i++) {
				data[i * 3] = std::uint8_t(palette[i]);
				data[i * 3 + 1] = std::uint8_t(palette[i] >> 8);
				data[i * 3 + 2] = std::uint8_t(palette[i] >> 16);
			}
			// All 256 used, the first one transparent, as it is in the game
			data[768] = 0x01; data[769] = 0x00;
			data[770] = 0x00; data[771] = 0x00;
			return (s->Write(data, sizeof(data)) == std::int64_t(sizeof(data)));
		}

		String text;
		char line[64];
		if (format == Format::Jasc) {
			text = "JASC-PAL\r\n0100\r\n256\r\n"_s;
			for (std::int32_t i = 0; i < 256; i++) {
				std::int32_t length = std::snprintf(line, sizeof(line), "%u %u %u\r\n", palette[i] & 0xFF, (palette[i] >> 8) & 0xFF, (palette[i] >> 16) & 0xFF);
				text += StringView(line, std::size_t(length));
			}
		} else {
			text = "GIMP Palette\nName: "_s + (name.empty() ? "Jazz2 Resurrection"_s : name) + "\nColumns: 16\n#\n"_s;
			for (std::int32_t i = 0; i < 256; i++) {
				std::int32_t length = std::snprintf(line, sizeof(line), "%3u %3u %3u\tIndex %d\n", palette[i] & 0xFF, (palette[i] >> 8) & 0xFF, (palette[i] >> 16) & 0xFF, i);
				text += StringView(line, std::size_t(length));
			}
		}
		return (s->Write(text.data(), std::int64_t(text.size())) == std::int64_t(text.size()));
	}
}

#endif
