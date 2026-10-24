#include "UiReadableFont.h"

#if defined(WITH_EDITOR)

#include "UiTheme.h"
#include "../../ContentResolver.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/ServiceLocator.h"

#include <algorithm>
#include <cmath>

// Third-party code, only what's used of it is compiled in
#if defined(DEATH_TARGET_MSVC)
#	pragma warning(push)
#	pragma warning(disable: 4100 4127 4189 4244 4245 4456 4457 4701 4702)
#elif defined(DEATH_TARGET_GCC)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#	pragma GCC diagnostic ignored "-Wsign-compare"
#	pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>
#if defined(DEATH_TARGET_MSVC)
#	pragma warning(pop)
#elif defined(DEATH_TARGET_GCC)
#	pragma GCC diagnostic pop
#endif

namespace Jazz2::Editor
{
	using Death::Containers::Pair;
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;

	namespace
	{
		// Outline font the glyphs are drawn from, in "Editor/Fonts" of the content
		constexpr char FontFileName[] = "Roboto-Regular.ttf";

		// The font is measured in the units of the pixel font of the game: lines are as high and their baseline is at
		// the same place
		constexpr std::int32_t LineHeight = 20;
		constexpr float Baseline = 15.0f;
		// Size of the em square in units, 12 pixels at the interface scale of 100 %, as Segoe UI of 9 points is. It
		// makes capitals 10 units high, centered in the line, those of the pixel font are 12.
		constexpr float EmSize = 12.0f / UiTheme::TextScale;
		// Width of the dark rim around the glyphs in units, it's at least a pixel
		constexpr float RimWidth = 0.4f;
		constexpr float RimOpacity = 0.85f;
		// Gray of the glyphs and of their rim. Text is tinted as the pixel font is (see UiTheme), whose glyphs average
		// 0.61 inside of a black outline, so the colors of text look alike in both.
		constexpr float FillGray = 0.62f;
		constexpr float RimGray = 0.04f;
		// Largest scale of text the glyphs are drawn for, the largest glyphs at a larger one wouldn't fit their records
		// (see UI::FontFormat::Glyph), they're drawn for this one and enlarged then
		constexpr float MaxTextScale = 7.0f;
		// Smallest scale of text the glyphs are drawn for when they don't fit the largest texture
		constexpr float MinTextScale = 0.25f;
		// Space between glyphs in the atlas, so the smoothing of an enlarged glyph doesn't take in its neighbours
		constexpr std::int32_t GlyphSpacing = 2;
		// Largest atlas, even if textures can be larger
		constexpr std::int32_t MaxAtlasSize = 4096;
		// Number of scales whose glyphs are kept
		constexpr std::size_t MaxKeptFonts = 2;

		// Characters taken from the font, those it doesn't have are left out: Latin, Greek and Cyrillic as the languages
		// of the game need them, punctuation and a few symbols
		constexpr char32_t CharacterRanges[][2] = {
			{ 0x0020, 0x007E }, { 0x00A0, 0x017F }, { 0x0218, 0x021B }, { 0x0384, 0x03CE }, { 0x0400, 0x045F },
			{ 0x0490, 0x0491 }, { 0x2010, 0x2027 }, { 0x2030, 0x203A }, { 0x20AC, 0x20AC }, { 0x2116, 0x2116 },
			{ 0x2122, 0x2122 }, { 0x2190, 0x2195 }, { 0x2212, 0x2212 }, { 0x221E, 0x221E }, { 0x2248, 0x2248 },
			{ 0x2260, 0x2265 }
		};

		struct GlyphSource
		{
			char32_t Codepoint;
			std::int32_t GlyphIndex;
			// Box of the inked pixels relative to the pen on the baseline, without the rim
			std::int32_t X0, Y0, X1, Y1;
			std::int32_t Advance;
			// Where it's put in the atlas, with the rim
			std::int32_t AtlasX, AtlasY;
		};

		std::int32_t NextPowerOfTwo(std::int32_t value)
		{
			std::int32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}

		// Places the glyphs in rows, the tallest first, returns the height they take, or -1 if a glyph is wider than the atlas
		std::int32_t PackGlyphs(SmallVectorImpl<GlyphSource>& glyphs, const SmallVectorImpl<std::int32_t>& order, std::int32_t rim, std::int32_t atlasWidth)
		{
			std::int32_t x = 0, y = 0, rowHeight = 0;
			for (std::int32_t i : order) {
				GlyphSource& glyph = glyphs[i];
				if (glyph.X1 <= glyph.X0 || glyph.Y1 <= glyph.Y0) {
					continue;
				}
				std::int32_t w = glyph.X1 - glyph.X0 + rim * 2;
				std::int32_t h = glyph.Y1 - glyph.Y0 + rim * 2;
				if (w + GlyphSpacing > atlasWidth) {
					return -1;
				}
				if (x + w + GlyphSpacing > atlasWidth) {
					x = 0;
					y += rowHeight;
					rowHeight = 0;
				}
				glyph.AtlasX = x + GlyphSpacing / 2;
				glyph.AtlasY = y + GlyphSpacing / 2;
				x += w + GlyphSpacing;
				rowHeight = std::max(rowHeight, h + GlyphSpacing);
			}
			return y + rowHeight;
		}
	}

	struct UiReadableFont::Face
	{
		std::unique_ptr<std::uint8_t[]> Data;
		stbtt_fontinfo Info;
	};

	UiReadableFont::UiReadableFont()
		: _failed(false)
	{
	}

	UiReadableFont::~UiReadableFont()
	{
	}

	UI::Font* UiReadableFont::Get(float textScale)
	{
		std::uint32_t frame = theApplication().GetFrameCount();
		for (Entry& entry : _fonts) {
			if (entry.TextScale == textScale) {
				entry.LastFrame = frame;
				return entry.Font.get();
			}
		}
		if (!LoadFace()) {
			return nullptr;
		}

		// The one used the longest ago goes, unless it was used in this frame
		if (_fonts.size() >= MaxKeptFonts) {
			std::int32_t oldest = -1;
			for (std::int32_t i = 0; i < std::int32_t(_fonts.size()); i++) {
				if (_fonts[i].LastFrame != frame && (oldest < 0 || _fonts[i].LastFrame < _fonts[oldest].LastFrame)) {
					oldest = i;
				}
			}
			if (oldest >= 0) {
				_fonts.erase(_fonts.begin() + oldest);
			}
		}

		// Kept even if it couldn't be made, so it's not tried again in every frame
		Entry& entry = _fonts.emplace_back();
		entry.TextScale = textScale;
		entry.LastFrame = frame;
		entry.Font = Build(textScale);
		return entry.Font.get();
	}

	bool UiReadableFont::LoadFace()
	{
		if (_face != nullptr) {
			return true;
		}
		if (_failed) {
			return false;
		}

		// Tried only once, whatever happens
		_failed = true;

		String path = fs::CombinePath({ "Editor"_s, "Fonts"_s, StringView(FontFileName) });
		auto s = ContentResolver::Get().OpenContentFile(path);
		if (s == nullptr || !s->IsValid()) {
			LOGE("\"{}\" cannot be opened", path);
			return false;
		}
		std::int64_t size = s->GetSize();
		if (size < 12 || size > 16 * 1024 * 1024) {
			LOGE("\"{}\" has an implausible size of {} bytes", path, size);
			return false;
		}

		auto face = std::make_unique<Face>();
		face->Data = std::make_unique<std::uint8_t[]>(std::size_t(size));
		if (s->Read(face->Data.get(), size) != size) {
			LOGE("\"{}\" cannot be read", path);
			return false;
		}
		std::int32_t offset = stbtt_GetFontOffsetForIndex(face->Data.get(), 0);
		if (offset < 0 || !stbtt_InitFont(&face->Info, face->Data.get(), offset)) {
			LOGE("\"{}\" is not a supported font file", path);
			return false;
		}

		_face = Death::move(face);
		_failed = false;
		return true;
	}

	std::unique_ptr<UI::Font> UiReadableFont::Build(float textScale)
	{
		const stbtt_fontinfo& info = _face->Info;
		std::int32_t maxTextureSize = std::min<std::int32_t>(theServiceLocator().GetRhiCapabilities().GetValue(RHI::IRhiCapabilities::IntValues::MaxTextureSize), MaxAtlasSize);
		if (maxTextureSize <= 0) {
			maxTextureSize = 1024;
		}

		SmallVector<GlyphSource, 0> glyphs;
		SmallVector<std::int32_t, 0> order;
		float rasterScale = std::min(textScale, MaxTextScale);
		while (true) {
			const float pixelScale = stbtt_ScaleForMappingEmToPixels(&info, EmSize * rasterScale);
			// The baseline is a whole pixel down, the rest of its position shifts the glyphs as they're drawn
			const float baseline = Baseline * rasterScale;
			const float baselinePixel = std::floor(baseline);
			const float shiftY = baseline - baselinePixel;
			const std::int32_t rim = std::max<std::int32_t>(1, std::int32_t(std::lround(RimWidth * rasterScale)));

			glyphs.clear();
			auto addGlyph = [&](char32_t codepoint, std::int32_t glyphIndex) {
				// stb_truetype takes `int`, which isn't `std::int32_t` with every compiler
				int advance, leftSideBearing, x0, y0, x1, y1;
				stbtt_GetGlyphHMetrics(&info, int(glyphIndex), &advance, &leftSideBearing);
				stbtt_GetGlyphBitmapBoxSubpixel(&info, int(glyphIndex), pixelScale, pixelScale, 0.0f, shiftY, &x0, &y0, &x1, &y1);
				GlyphSource glyph{};
				glyph.Codepoint = codepoint;
				glyph.GlyphIndex = glyphIndex;
				glyph.Advance = std::int32_t(std::lround(advance * pixelScale));
				glyph.X0 = x0;
				glyph.Y0 = y0;
				glyph.X1 = x1;
				glyph.Y1 = y1;
				// Left out if it doesn't fit its record, the fallback is drawn for it then
				bool inked = (glyph.X1 > glyph.X0 && glyph.Y1 > glyph.Y0);
				if (glyph.Advance <= 0 || glyph.Advance > 255 || (inked && (glyph.X1 - glyph.X0 + rim * 2 > UI::FontFormat::MaxGlyphSize ||
					glyph.Y1 - glyph.Y0 + rim * 2 > UI::FontFormat::MaxGlyphSize ||
					std::abs(glyph.X0 - rim) > UI::FontFormat::MaxGlyphBearing ||
					std::abs(std::int32_t(baselinePixel) + glyph.Y0 - rim) > UI::FontFormat::MaxGlyphBearing))) {
					return;
				}
				glyphs.push_back(glyph);
			};
			// What's drawn for a character the font doesn't have is the glyph the font has for that
			addGlyph(char32_t(UI::FontFormat::FallbackCodepoint), 0);
			for (const auto& range : CharacterRanges) {
				for (char32_t c = range[0]; c <= range[1]; c++) {
					std::int32_t glyphIndex = stbtt_FindGlyphIndex(&info, int(c));
					if (glyphIndex != 0) {
						addGlyph(c, glyphIndex);
					}
				}
			}

			order.resize(glyphs.size());
			std::int64_t totalArea = 0;
			for (std::int32_t i = 0; i < std::int32_t(glyphs.size()); i++) {
				order[i] = i;
				totalArea += std::int64_t(glyphs[i].X1 - glyphs[i].X0 + rim * 2 + GlyphSpacing) * (glyphs[i].Y1 - glyphs[i].Y0 + rim * 2 + GlyphSpacing);
			}
			std::sort(order.begin(), order.end(), [&glyphs](std::int32_t a, std::int32_t b) {
				return (glyphs[a].Y1 - glyphs[a].Y0 > glyphs[b].Y1 - glyphs[b].Y0);
			});

			// A width about the square root of the area, the rows waste some, then wider while it's too tall
			std::int32_t atlasWidth = std::min<std::int32_t>(NextPowerOfTwo(std::max<std::int32_t>(64, std::int32_t(std::sqrt(double(totalArea) * 1.2)))), maxTextureSize);
			std::int32_t usedHeight = PackGlyphs(glyphs, order, rim, atlasWidth);
			while ((usedHeight < 0 || usedHeight > maxTextureSize) && atlasWidth < maxTextureSize) {
				atlasWidth *= 2;
				usedHeight = PackGlyphs(glyphs, order, rim, atlasWidth);
			}
			if (usedHeight < 0 || usedHeight > maxTextureSize) {
				if (rasterScale <= MinTextScale) {
					LOGE("Glyphs of the readable font don't fit a texture");
					return nullptr;
				}
				// Drawn smaller and enlarged
				rasterScale = std::max(rasterScale * 0.8f, MinTextScale);
				continue;
			}
			std::int32_t atlasHeight = NextPowerOfTwo(std::max<std::int32_t>(usedHeight, 1));

			// Pixels left empty are of the color of the rim, so the smoothing of an enlarged glyph darkens its edge, not lightens it
			const std::uint8_t rimByte = std::uint8_t(RimGray * 255.0f + 0.5f);
			std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(atlasWidth) * std::size_t(atlasHeight) * 4);
			for (std::size_t i = 0; i < std::size_t(atlasWidth) * std::size_t(atlasHeight); i++) {
				pixels[i * 4 + 0] = rimByte;
				pixels[i * 4 + 1] = rimByte;
				pixels[i * 4 + 2] = rimByte;
				pixels[i * 4 + 3] = 0;
			}

			// Offsets the rim reaches, a disc
			SmallVector<Vector2i, 0> rimOffsets;
			float rimRadius = float(rim) + 0.5f;
			for (std::int32_t dy = -rim; dy <= rim; dy++) {
				for (std::int32_t dx = -rim; dx <= rim; dx++) {
					if (float(dx * dx + dy * dy) <= rimRadius * rimRadius) {
						rimOffsets.push_back(Vector2i(dx, dy));
					}
				}
			}

			SmallVector<std::uint8_t, 0> coverage;
			SmallVector<Pair<char32_t, UI::FontFormat::Glyph>, 0> records;
			records.reserve(glyphs.size());
			for (const GlyphSource& source : glyphs) {
				UI::FontFormat::Glyph glyph{};
				glyph.Advance = std::uint8_t(source.Advance);
				if (source.X1 > source.X0 && source.Y1 > source.Y0) {
					std::int32_t inkWidth = source.X1 - source.X0;
					std::int32_t inkHeight = source.Y1 - source.Y0;
					std::int32_t w = inkWidth + rim * 2;
					std::int32_t h = inkHeight + rim * 2;

					coverage.resize(std::size_t(inkWidth) * std::size_t(inkHeight));
					stbtt_MakeGlyphBitmapSubpixel(&info, coverage.data(), int(inkWidth), int(inkHeight), int(inkWidth), pixelScale, pixelScale, 0.0f, shiftY, int(source.GlyphIndex));
					auto getCoverage = [&](std::int32_t x, std::int32_t y) -> std::int32_t {
						x -= rim;
						y -= rim;
						return (x < 0 || y < 0 || x >= inkWidth || y >= inkHeight ? 0 : coverage[std::size_t(y) * std::size_t(inkWidth) + std::size_t(x)]);
					};

					for (std::int32_t y = 0; y < h; y++) {
						for (std::int32_t x = 0; x < w; x++) {
							std::int32_t around = 0;
							for (const Vector2i& offset : rimOffsets) {
								around = std::max(around, getCoverage(x + offset.X, y + offset.Y));
								if (around == 255) {
									break;
								}
							}
							// The glyph over its rim
							float fill = getCoverage(x, y) / 255.0f;
							float rimAlpha = (around / 255.0f) * RimOpacity;
							float alpha = fill + rimAlpha * (1.0f - fill);
							if (alpha <= 0.0f) {
								continue;
							}
							float gray = (FillGray * fill + RimGray * rimAlpha * (1.0f - fill)) / alpha;
							std::uint8_t* p = &pixels[(std::size_t(source.AtlasY + y) * std::size_t(atlasWidth) + std::size_t(source.AtlasX + x)) * 4];
							std::uint8_t grayByte = std::uint8_t(std::clamp(gray, 0.0f, 1.0f) * 255.0f + 0.5f);
							p[0] = grayByte;
							p[1] = grayByte;
							p[2] = grayByte;
							p[3] = std::uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
						}
					}

					glyph.X = std::uint16_t(source.AtlasX);
					glyph.Y = std::uint16_t(source.AtlasY);
					glyph.Width = std::uint8_t(w);
					glyph.Height = std::uint8_t(h);
					glyph.BearingX = std::int8_t(source.X0 - rim);
					glyph.BearingY = std::int8_t(std::int32_t(baselinePixel) + source.Y0 - rim);
				}
				records.push_back(Pair<char32_t, UI::FontFormat::Glyph>(source.Codepoint, glyph));
			}

			return std::make_unique<UI::Font>(FontFileName, pixels.get(), atlasWidth, atlasHeight, LineHeight,
				rasterScale, arrayView(records.data(), records.size()));
		}
	}
}

#endif
