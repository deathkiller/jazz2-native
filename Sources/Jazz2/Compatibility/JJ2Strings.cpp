#include "JJ2Strings.h"

#include "../../nCine/Base/Algorithms.h"

#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

static constexpr uint16_t Windows1250_Utf8[256] = {
	0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
	0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017, 0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
	0x20ac, 0xffff, 0x201a, 0xffff, 0x201e, 0x2026, 0x2020, 0x2021, 0xffff, 0x2030, 0x0160, 0x2039, 0x015a, 0x0164, 0x017d, 0x0179,
	0xffff, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0xffff, 0x2122, 0x0161, 0x203a, 0x015b, 0x0165, 0x017e, 0x017a,
	0x00a0, 0x02c7, 0x02d8, 0x0141, 0x00a4, 0x0104, 0x00a6, 0x00a7, 0x00a8, 0x00a9, 0x015e, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x017b,
	0x00b0, 0x00b1, 0x02db, 0x0142, 0x00b4, 0x00b5, 0x00b6, 0x00b7, 0x00b8, 0x0105, 0x015f, 0x00bb, 0x013d, 0x02dd, 0x013e, 0x017c,
	0x0154, 0x00c1, 0x00c2, 0x0102, 0x00c4, 0x0139, 0x0106, 0x00c7, 0x010c, 0x00c9, 0x0118, 0x00cb, 0x011a, 0x00cd, 0x00ce, 0x010e,
	0x0110, 0x0143, 0x0147, 0x00d3, 0x00d4, 0x0150, 0x00d6, 0x00d7, 0x0158, 0x016e, 0x00da, 0x0170, 0x00dc, 0x00dd, 0x0162, 0x00df,
	0x0155, 0x00e1, 0x00e2, 0x0103, 0x00e4, 0x013a, 0x0107, 0x00e7, 0x010d, 0x00e9, 0x0119, 0x00eb, 0x011b, 0x00ed, 0x00ee, 0x010f,
	0x0111, 0x0144, 0x0148, 0x00f3, 0x00f4, 0x0151, 0x00f6, 0x00f7, 0x0159, 0x016f, 0x00fa, 0x0171, 0x00fc, 0x00fd, 0x0163, 0x02d9
};

static constexpr uint32_t DefaultFontColors[] = {
	0x707485,
	0x409062,
	0x629040,
	0x804040,
	0x6270d0,
	0xc07050,
	0xa05c6a
};

namespace Jazz2::Compatibility
{
	bool JJ2Strings::Open(const StringView& path)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		RETURNF_ASSERT_MSG(s->IsValid(), "Cannot open file for reading");

		Name = fs::GetFileNameWithoutExtension(path);
		lowercaseInPlace(Name);

		uint32_t offsetArraySize = s->ReadValue<uint32_t>();

		uint32_t textArraySize = s->ReadValue<uint32_t>();
		std::unique_ptr<char[]> textArray = std::make_unique<char[]>(textArraySize);
		s->Read(textArray.get(), textArraySize);

		CommonTexts.reserve(offsetArraySize);
		for (uint32_t i = 0; i < offsetArraySize; i++) {
			uint32_t offset = s->ReadValue<uint32_t>();
			CommonTexts.emplace_back(&textArray[offset]);
		}

		uint32_t levelEntryCount = s->ReadValue<uint32_t>();

		SmallVector<uint32_t, 0> counts, offsets;
		counts.reserve(levelEntryCount);
		offsets.reserve(levelEntryCount + 1);

		LevelTexts.reserve(levelEntryCount);
		for (uint32_t i = 0; i < levelEntryCount; i++) {
			char levelName[8 + 1];
			s->Read(levelName, 8);
			levelName[8] = '\0';

			LevelEntry& levelEntry = LevelTexts.emplace_back(String(levelName));
			lowercaseInPlace(levelEntry.Name);

			/*uint8_t unknown =*/ s->ReadValue<uint8_t>();
			counts.emplace_back(s->ReadValue<uint8_t>());
			offsets.emplace_back(s->ReadValue<uint16_t>());
		}

		uint32_t textArray2Size = s->ReadValue<uint32_t>();
		offsets.emplace_back(textArray2Size);

		uint32_t k = 0;
		for (uint32_t i = 0; i < levelEntryCount; i++) {
			auto offset = offsets[i + 1];
			auto count = counts[i];

			auto& level = LevelTexts[i];
			level.TextEvents.resize(count + 1);

			while (k < offset) {
				uint8_t index = s->ReadValue<uint8_t>() % 16;
				k++;
				uint8_t size = s->ReadValue<uint8_t>();
				k++;

				String text = String(NoInit, size);
				s->Read(text.data(), size);
				level.TextEvents[index] = text;
				k += size;
			}
		}

		return true;
	}

	void JJ2Strings::Convert(const String& targetPath, std::function<JJ2Level::LevelToken(const StringView&)> levelTokenConversion)
	{
		auto so = fs::Open(targetPath, FileAccessMode::Write);
		ASSERT_MSG(so->IsValid(), "Cannot open file for writing");

		so->Write("\xEF\xBB\xBF// Common\n", sizeof("\xEF\xBB\xBF// Common\n") - 1);

		for (size_t i = 0; i < CommonTexts.size(); i++) {
			String formattedText = RecodeString(CommonTexts[i], false, true);
			if (formattedText.empty()) {
				so->Write("// [Empty text]\n", sizeof("// [Empty text]\n") - 1);
				continue;
			}
			so->Write("//__(\"", sizeof("//__(\"") - 1);
			so->Write(formattedText.data(), formattedText.size());
			so->Write("\"); // ", sizeof("\"); // ") - 1);

			char buffer[32];
			u32tos((uint32_t)i, buffer);
			so->Write(buffer, std::strlen(buffer));

			so->Write("\n", sizeof("\n") - 1);
		}

		so->Write("\n// Levels\n", sizeof("\n// Levels\n") - 1);

		for (auto& level : LevelTexts) {
			if (level.TextEvents.empty()) {
				continue;
			}

			String levelName;
			if (levelTokenConversion != nullptr) {
				auto token = levelTokenConversion(level.Name);
				if (!token.Episode.empty()) {
					levelName = "/"_s.joinWithoutEmptyParts({ token.Episode, token.Level });
				} else {
					levelName = "unknown/"_s + token.Level;
				}
			} else {
				levelName = "unknown/"_s + level.Name;
			}

			for (size_t i = 0; i < level.TextEvents.size(); i++) {
				String formattedText = RecodeString(level.TextEvents[i], false, true);
				if (formattedText.empty()) {
					so->Write("// [Empty text]\n", sizeof("// [Empty text]\n") - 1);
					continue;
				}
				so->Write("_x(\"", sizeof("__(\"") - 1);
				so->Write(levelName.data(), levelName.size());
				so->Write("\", \"", sizeof("\", \"") - 1);
				so->Write(formattedText.data(), formattedText.size());
				so->Write("\"); // ", sizeof("\"); // ") - 1);

				char buffer[32];
				u32tos((uint32_t)i, buffer);
				so->Write(buffer, std::strlen(buffer));

				so->Write("\n", sizeof("\n") - 1);
			}

			so->Write("\n", sizeof("\n") - 1);
		}
	}

	String JJ2Strings::RecodeString(const StringView& text, bool stripFormatting, bool escaped)
	{
		if (text.empty()) {
			return { };
		}

		char buffer[1024];
		int colorIndex = 0;
		bool colorRandom = false;
		bool colorEmitted = true;
		bool colorFrozen = true;
		size_t length = text.size();
		size_t j = 0;
		for (size_t i = 0; i < length && j < sizeof(buffer) - 16; i++) {
			char current = text[i];

			if (current == '@') {
				// New line
				if (!stripFormatting) {
					if (escaped) {
						buffer[j++] = '\\';
						buffer[j++] = 'n';
					} else {
						buffer[j++] = '\n';
					}
				} else {
					buffer[j++] = ' ';
				}
			} else if (current == '\xA7' && (std::isdigit(text[i + 1]) || text[i + 1] == '/')) {
				// Char spacing (§)
				i++;
				if (!stripFormatting) {
					if (escaped) {
						buffer[j++] = '\\';
						buffer[j++] = 'f';
					} else {
						buffer[j++] = '\f';
					}

					if (text[i] == '/') {
						buffer[j++] = '[';
						buffer[j++] = 'w';
						buffer[j++] = ']';
					} else {
						uint32_t spacing = text[i] - '0';
						uint32_t converted = 100 - (spacing * 10);
						j += formatString(&buffer[j], 16, "[w:%i]", converted);
						if (colorRandom && !colorFrozen) {
							colorIndex++;
						}
					}
				}
			} else if (current == '#') {
				// Turn on colorization
				colorRandom = true;
				colorEmitted = false;
				colorFrozen = false;
			} else if (current == '~' && colorRandom) {
				// Freeze active color
				if (!colorFrozen) {
					colorFrozen = true;
				} else {
					colorRandom = false;
					colorEmitted = false;
					colorFrozen = false;
					colorIndex = 0;
				}
			} else if (current == '|' && colorRandom) {
				// Skip one color
				if (!colorFrozen) {
					colorIndex++;
					colorEmitted = false;
				}
			} else {
				if (!stripFormatting && !colorEmitted) {
					colorEmitted = true;
					if (escaped) {
						buffer[j++] = '\\';
						buffer[j++] = 'f';
					} else {
						buffer[j++] = '\f';
					}
					int colorIndex2 = colorIndex % (countof(DefaultFontColors) + 1);
					if (colorIndex2 == 0) {
						buffer[j++] = '[';
						buffer[j++] = 'c';
						buffer[j++] = ']';
					} else {
						j += formatString(&buffer[j], 16, "[c:0x%08x]", DefaultFontColors[colorIndex2 - 1]);
					}
				}

				const uint16_t c = Windows1250_Utf8[(uint8_t)current];
				if (c < 0x80) {
					buffer[j++] = c;
				} else if (c < 0x800) {
					buffer[j++] = 0xc0 | (c >> 6);
					buffer[j++] = 0x80 | (c & 0x3f);
				} else if (c < 0xffff) {
					buffer[j++] = 0xe0 | (c >> 12);
					buffer[j++] = 0x80 | ((c >> 6) & 0x3f);
					buffer[j++] = 0x80 | (c & 0x3f);
				}

				if (colorRandom && !colorFrozen && c != ' ' && text[i + 1] != '~') {
					colorIndex++;
					colorEmitted = false;
				}
			}
		}

		return String(buffer, j);
	}
}