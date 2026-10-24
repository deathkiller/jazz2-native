#if defined(WITH_ANGELSCRIPT)

#include "Palette.h"
#include "AnimationTables.h"
#include "Internal.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		namespace
		{
			// Clamps a float to a 0-255 color channel with rounding
			static std::uint8_t ClampChannel(float value) {
				if (value <= 0.0f) return 0;
				if (value >= 255.0f) return 255;
				return (std::uint8_t)(value + 0.5f);
			}

			// Linearly blends an original channel toward a target by the given opacity (1.0 = fully replace)
			static std::uint8_t BlendChannel(std::uint8_t orig, std::uint8_t target, float opacity) {
				if (opacity <= 0.0f) return orig;
				if (opacity >= 1.0f) return target;
				return ClampChannel(orig * (1.0f - opacity) + target * opacity);
			}

			// Converts an RGB color to HSL with all components normalized to the 0..1 range
			static void RgbToHsl(std::uint8_t R, std::uint8_t G, std::uint8_t B, float& h, float& s, float& l) {
				float r = R / 255.0f, g = G / 255.0f, b = B / 255.0f;
				float mx = std::max(r, std::max(g, b));
				float mn = std::min(r, std::min(g, b));
				l = (mx + mn) * 0.5f;
				if (mx == mn) {
					h = 0.0f;
					s = 0.0f;
					return;
				}
				float d = mx - mn;
				s = (l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn));
				if (mx == r) {
					h = (g - b) / d + (g < b ? 6.0f : 0.0f);
				} else if (mx == g) {
					h = (b - r) / d + 2.0f;
				} else {
					h = (r - g) / d + 4.0f;
				}
				h /= 6.0f;
			}

			static float Hue2Rgb(float p, float q, float t) {
				if (t < 0.0f) t += 1.0f;
				if (t > 1.0f) t -= 1.0f;
				if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
				if (t < 1.0f / 2.0f) return q;
				if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
				return p;
			}

			// Converts an HSL color (all components in the 0..1 range) back to RGB
			static void HslToRgb(float h, float s, float l, std::uint8_t& R, std::uint8_t& G, std::uint8_t& B) {
				float rf, gf, bf;
				if (s <= 0.0f) {
					rf = gf = bf = l;
				} else {
					float q = (l < 0.5f ? l * (1.0f + s) : l + s - l * s);
					float p = 2.0f * l - q;
					rf = Hue2Rgb(p, q, h + 1.0f / 3.0f);
					gf = Hue2Rgb(p, q, h);
					bf = Hue2Rgb(p, q, h - 1.0f / 3.0f);
				}
				R = ClampChannel(rf * 255.0f);
				G = ClampChannel(gf * 255.0f);
				B = ClampChannel(bf * 255.0f);
			}
		}

		void jjPALCOLOR::Create(void* self) {
			new(self) jjPALCOLOR{};
		}
		void jjPALCOLOR::CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self) {
			new(self) jjPALCOLOR{red, green, blue};
		}
		void jjPALCOLOR::CreateFromRgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, void* self) {
			new(self) jjPALCOLOR{red, green, blue, alpha};
		}

		std::uint8_t jjPALCOLOR::getHue() {
			float h, s, l;
			RgbToHsl(red, green, blue, h, s, l);
			return ClampChannel(h * 255.0f);
		}
		std::uint8_t jjPALCOLOR::getSat() {
			float h, s, l;
			RgbToHsl(red, green, blue, h, s, l);
			return ClampChannel(s * 255.0f);
		}
		std::uint8_t jjPALCOLOR::getLight() {
			float h, s, l;
			RgbToHsl(red, green, blue, h, s, l);
			return ClampChannel(l * 255.0f);
		}

		void jjPALCOLOR::swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec) {

			std::uint8_t r = red;
			std::uint8_t g = green;
			std::uint8_t b = blue;

			switch (redc) {
				case 1: red = g; break;
				case 2: red = b; break;
			}
			switch (greenc) {
				case 0: green = r; break;
				case 2: green = b; break;
			}
			switch (bluec) {
				case 0: blue = r; break;
				case 1: blue = g; break;
			}
		}
		void jjPALCOLOR::setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light) {
			// Hue wraps around the 0..255 range
			HslToRgb((hue & 0xFF) / 255.0f, sat / 255.0f, light / 255.0f, red, green, blue);
		}

		jjPALCOLOR& jjPALCOLOR::operator=(const jjPALCOLOR& other) {

			red = other.red;
			green = other.green;
			blue = other.blue;
			// The colors of a jjCOLORMAP rely on it, a palette's alpha is never read
			alpha = other.alpha;
			return *this;
		}
		bool jjPALCOLOR::operator==(const jjPALCOLOR& other) {
			return (red == other.red && green == other.green && blue == other.blue);
		}

		jjPAL::jjPAL() : _refCount(1), _palette{} {
		}
		jjPAL::~jjPAL() {
		}

		jjPAL* jjPAL::Create() {
			void* mem = asAllocMem(sizeof(jjPAL));
			return new(mem) jjPAL();
		}

		void jjPAL::AddRef()
		{
			_refCount++;
		}

		void jjPAL::Release()
		{
			if (--_refCount == 0) {
				this->~jjPAL();
				asFreeMem(this);
			}
		}

		jjPAL& jjPAL::operator=(const jjPAL& o) {
			if (this != &o) {
				// Copy only the palette colors, never the reference count
				std::memcpy(_palette, o._palette, sizeof(_palette));
			}
			return *this;
		}

		bool jjPAL::operator==(const jjPAL& o) {
			return (this == &o);
		}

		jjPALCOLOR& jjPAL::getColor(std::uint8_t idx) {
			return _palette[idx];
		}

		const jjPALCOLOR& jjPAL::getConstColor(std::uint8_t idx) const {
			return _palette[idx];
		}

		jjPALCOLOR& jjPAL::setColorEntry(std::uint8_t idx, jjPALCOLOR& value) {
			_palette[idx] = value;
			return _palette[idx];
		}

		void jjPAL::reset() {
			// Restores this palette to the level's original palette (captured at script load)
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (owner != nullptr && &owner->jjBackupPalette != this) {
				*this = owner->jjBackupPalette;
			}
		}
		void jjPAL::apply() {
			// Push this palette to the live sprite palette so it affects rendering from the next frame
			std::uint32_t packed[ContentResolver::ColorsPerPalette];
			for (std::int32_t i = 0; i < ContentResolver::ColorsPerPalette; i++) {
				// Palette index 0 is the transparent color; all others are fully opaque
				packed[i] = (std::uint32_t)_palette[i].red
					| ((std::uint32_t)_palette[i].green << 8)
					| ((std::uint32_t)_palette[i].blue << 16)
					| (i == 0 ? 0u : 0xFF000000u);
			}
			ContentResolver::Get().SetSpritePalette(ArrayView<const std::uint32_t>(packed, ContentResolver::ColorsPerPalette));

			// Keep the script-visible current palette (jjPalette) in sync with what was applied
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (owner != nullptr) {
				if (&owner->jjPalette != this) {
					owner->jjPalette = *this;
				}
				owner->GetAnimationTables().OnPaletteChanged();
			}
		}
		bool jjPAL::load(const String& filename) {
			if (StringUtils::lowercase(filename).hasSuffix(".j2t"_s)) {
				// The palette of a tile set, the converted one has it as the original
				String name = GetTileSetName(filename);
				std::unique_ptr<Stream> s = (!name.empty() ? ContentResolver::Get().OpenTileSetFile(name) : nullptr);
				std::uint32_t palette[256];
				if (s == nullptr || !Formats::TileSetFile::ReadPalette(*s, palette)) {
					return false;
				}
				for (std::int32_t i = 0; i < 256; i++) {
					_palette[i].red = std::uint8_t(palette[i]);
					_palette[i].green = std::uint8_t(palette[i] >> 8);
					_palette[i].blue = std::uint8_t(palette[i] >> 16);
				}
				return true;
			}

			auto stream = OpenScriptFile(filename);
			if (stream == nullptr || !stream->IsValid()) {
				return false;
			}

			// No palette file is longer than a RIFF one, which is a palette of Windows in a chunk
			std::uint8_t data[1056];
			std::int64_t size = stream->Read(data, sizeof(data));
			std::int64_t offset = 0;
			if (size >= 20 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "PAL data", 8) == 0) {
				offset = 20;
			}

			const std::uint8_t* entries;
			std::int64_t count, stride;
			if (size >= offset + 4 && data[offset] == 0x00 && data[offset + 1] == 0x03) {
				// A palette of Windows (LOGPALETTE), as JJ2+ saves it - version 0x300 and the number of entries, then red,
				// green, blue and flags of each
				entries = data + offset + 4;
				stride = 4;
				count = std::min<std::int64_t>(data[offset + 2] | (data[offset + 3] << 8), (size - offset - 4) / stride);
			} else {
				// Anything else is taken as red, green and blue of each entry
				entries = data;
				stride = 3;
				count = size / stride;
			}

			// The entries the file doesn't have are left as they are
			count = std::min<std::int64_t>(count, 256);
			for (std::int64_t i = 0; i < count; i++) {
				_palette[i].red = entries[i * stride];
				_palette[i].green = entries[i * stride + 1];
				_palette[i].blue = entries[i * stride + 2];
			}
			return true;
		}
		void jjPAL::fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity) {
			for (std::int32_t i = 0; i < 256; i++) {
				_palette[i].red = BlendChannel(_palette[i].red, red, opacity);
				_palette[i].green = BlendChannel(_palette[i].green, green, opacity);
				_palette[i].blue = BlendChannel(_palette[i].blue, blue, opacity);
			}
		}
		void jjPAL::fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity) {
			std::int32_t end = std::min<std::int32_t>((std::int32_t)start + (std::int32_t)length, 256);
			for (std::int32_t i = start; i < end; i++) {
				_palette[i].red = BlendChannel(_palette[i].red, red, opacity);
				_palette[i].green = BlendChannel(_palette[i].green, green, opacity);
				_palette[i].blue = BlendChannel(_palette[i].blue, blue, opacity);
			}
		}
		void jjPAL::fillFromColor(jjPALCOLOR color, float opacity) {
			fill(color.red, color.green, color.blue, opacity);
		}
		void jjPAL::fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity) {
			fillTint(color.red, color.green, color.blue, start, length, opacity);
		}
		void jjPAL::gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive) {
			if (length == 0) {
				return;
			}
			// When inclusive, the last index reaches color2 exactly; otherwise color2 is the (excluded) color past the range
			float denom = (inclusive ? (length > 1 ? (float)(length - 1) : 1.0f) : (float)length);
			std::int32_t end = std::min<std::int32_t>((std::int32_t)start + (std::int32_t)length, 256);
			for (std::int32_t i = start; i < end; i++) {
				float t = (i - start) / denom;
				if (t > 1.0f) t = 1.0f;
				_palette[i].red = BlendChannel(_palette[i].red, ClampChannel(red1 + (red2 - red1) * t), opacity);
				_palette[i].green = BlendChannel(_palette[i].green, ClampChannel(green1 + (green2 - green1) * t), opacity);
				_palette[i].blue = BlendChannel(_palette[i].blue, ClampChannel(blue1 + (blue2 - blue1) * t), opacity);
			}
		}
		void jjPAL::gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive) {
			gradient(color1.red, color1.green, color1.blue, color2.red, color2.green, color2.blue, start, length, opacity, inclusive);
		}
		void jjPAL::copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity) {
			for (std::int32_t i = 0; i < (std::int32_t)length; i++) {
				std::int32_t dst = start + i;
				std::int32_t src = start2 + i;
				if (dst >= 256 || src >= 256) {
					break;
				}
				_palette[dst].red = BlendChannel(_palette[dst].red, source._palette[src].red, opacity);
				_palette[dst].green = BlendChannel(_palette[dst].green, source._palette[src].green, opacity);
				_palette[dst].blue = BlendChannel(_palette[dst].blue, source._palette[src].blue, opacity);
			}
		}
		std::uint8_t jjPAL::findNearestColor(jjPALCOLOR color) {
			std::int32_t bestIndex = 0;
			std::int32_t bestDistance = INT32_MAX;
			for (std::int32_t i = 0; i < 256; i++) {
				std::int32_t dr = (std::int32_t)_palette[i].red - color.red;
				std::int32_t dg = (std::int32_t)_palette[i].green - color.green;
				std::int32_t db = (std::int32_t)_palette[i].blue - color.blue;
				std::int32_t distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					bestIndex = i;
					if (distance == 0) {
						break;
					}
				}
			}
			return (std::uint8_t)bestIndex;
		}
		std::uint8_t jjPAL::FindNearestOpaqueColor(const jjPALCOLOR& color) const {
			std::int32_t bestIndex = 1;
			std::int32_t bestDistance = INT32_MAX;
			for (std::int32_t i = 1; i < 256; i++) {
				std::int32_t dr = (std::int32_t)_palette[i].red - color.red;
				std::int32_t dg = (std::int32_t)_palette[i].green - color.green;
				std::int32_t db = (std::int32_t)_palette[i].blue - color.blue;
				std::int32_t distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					bestIndex = i;
					if (distance == 0) {
						break;
					}
				}
			}
			return (std::uint8_t)bestIndex;
		}
	}

	LevelScriptLoader::SpriteModeMapping& LevelScriptLoader::GetSpriteModeMapping(std::uint8_t index)
	{
		if (_spriteModeMappings == nullptr) {
			_spriteModeMappings = std::make_unique<SpriteModeMapping[]>(256);
		}
		return _spriteModeMappings[index];
	}

	void LevelScriptLoader::jjSpriteModeSetMapping(std::uint8_t index, const CScriptArray& indexMapping, const jjPAL& rgbMapping)
	{
		jjSpriteModeSetIndexMapping(index, indexMapping);
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		SpriteModeMapping& mapping = _this->GetSpriteModeMapping(index);
		mapping.HasPalette = true;
		for (std::int32_t i = 0; i < 256; i++) {
			mapping.Colors[i] = rgbMapping.getConstColor(std::uint8_t(i));
		}
	}

	void LevelScriptLoader::jjSpriteModeSetIndexMapping(std::uint8_t index, const CScriptArray& indexMapping)
	{
		// Nothing is drawn by it yet, the mapping is kept for the scripts that read it back; the indices it doesn't have map
		// to themselves
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		SpriteModeMapping& mapping = _this->GetSpriteModeMapping(index);
		mapping.Used = true;
		mapping.HasPalette = false;
		for (std::uint32_t i = 0; i < 256; i++) {
			mapping.Indices[i] = (i < indexMapping.GetSize() ? *static_cast<const std::uint8_t*>(indexMapping.At(i)) : std::uint8_t(i));
		}
	}

	bool LevelScriptLoader::jjSpriteModeIsMappingUsed(std::uint8_t index)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return (_this->_spriteModeMappings != nullptr && _this->_spriteModeMappings[index].Used);
	}

	std::int32_t LevelScriptLoader::jjSpriteModeFirstFreeMapping()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		for (std::int32_t i = 0; i < 256; i++) {
			if (_this->_spriteModeMappings == nullptr || !_this->_spriteModeMappings[i].Used) {
				return i;
			}
		}
		return -1;
	}

	CScriptArray* LevelScriptLoader::jjSpriteModeGetIndexMapping(std::uint8_t index)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		const SpriteModeMapping& mapping = _this->GetSpriteModeMapping(index);
		CScriptArray* arr = CScriptArray::Create(_this->GetEngine()->GetTypeInfoByDecl("array<uint8>"), 256);
		for (std::uint32_t i = 0; i < 256; i++) {
			*static_cast<std::uint8_t*>(arr->At(i)) = (mapping.Used ? mapping.Indices[i] : std::uint8_t(i));
		}
		return arr;
	}

	jjPAL* LevelScriptLoader::jjSpriteModeGetColorMapping(std::uint8_t index)
	{
		// A mapping without a palette of its own maps the indices to the colors of the palette in effect
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		const SpriteModeMapping& mapping = _this->GetSpriteModeMapping(index);
		jjPAL* palette = jjPAL::Create();
		for (std::int32_t i = 0; i < 256; i++) {
			jjPALCOLOR color = (mapping.HasPalette ? mapping.Colors[i] : _this->jjPalette.getConstColor(mapping.Used ? mapping.Indices[i] : std::uint8_t(i)));
			palette->setColorEntry(std::uint8_t(i), color);
		}
		return palette;
	}

	void LevelScriptLoader::RegisterLegacyPalette(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjPALCOLOR", sizeof(jjPALCOLOR), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<jjPALCOLOR>());
		engine->RegisterObjectBehaviour("jjPALCOLOR", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(jjPALCOLOR::Create), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjPALCOLOR", asBEHAVE_CONSTRUCT, "void f(uint8 red, uint8 green, uint8 blue)", asFUNCTION(jjPALCOLOR::CreateFromRgb), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjPALCOLOR", asBEHAVE_CONSTRUCT, "void f(uint8 red, uint8 green, uint8 blue, uint8 alpha)", asFUNCTION(jjPALCOLOR::CreateFromRgba), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 red", asOFFSET(jjPALCOLOR, red));
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 green", asOFFSET(jjPALCOLOR, green));
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 blue", asOFFSET(jjPALCOLOR, blue));
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 alpha", asOFFSET(jjPALCOLOR, alpha));
		engine->RegisterObjectMethod("jjPALCOLOR", "jjPALCOLOR& opAssign(const jjPALCOLOR &in)", asMETHODPR(jjPALCOLOR, operator=, (const jjPALCOLOR&), jjPALCOLOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "bool opEquals(const jjPALCOLOR &in) const", asMETHOD(jjPALCOLOR, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getHue() const", asMETHOD(jjPALCOLOR, getHue), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getSat() const", asMETHOD(jjPALCOLOR, getSat), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getLight() const", asMETHOD(jjPALCOLOR, getLight), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "void setHSL(int hue, uint8 sat, uint8 light)", asMETHOD(jjPALCOLOR, setHSL), asCALL_THISCALL);
		engine->SetDefaultNamespace("COLOR");
		engine->RegisterEnum("Component");
		engine->RegisterEnumValue("Component", "RED", 0);
		engine->RegisterEnumValue("Component", "GREEN", 1);
		engine->RegisterEnumValue("Component", "BLUE", 2);
		engine->RegisterEnumValue("Component", "ALPHA", 3);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjPALCOLOR", "void swizzle(COLOR::Component red, COLOR::Component green, COLOR::Component blue)", asMETHOD(jjPALCOLOR, swizzle), asCALL_THISCALL);

		engine->RegisterObjectType("jjPAL", sizeof(jjPAL), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_FACTORY, "jjPAL@ f()", asFUNCTION(jjPAL::Create), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_ADDREF, "void f()", asMETHOD(jjPAL, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_RELEASE, "void f()", asMETHOD(jjPAL, Release), asCALL_THISCALL);
		engine->RegisterGlobalProperty("jjPAL jjPalette", &jjPalette);
		engine->RegisterGlobalProperty("const jjPAL jjBackupPalette", &jjBackupPalette);
		engine->RegisterObjectMethod("jjPAL", "jjPAL& opAssign(const jjPAL &in)", asMETHOD(jjPAL, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "bool opEquals(const jjPAL &in) const", asMETHOD(jjPAL, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "jjPALCOLOR& get_color(uint8)", asMETHOD(jjPAL, getColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "const jjPALCOLOR& get_color(uint8) const", asMETHOD(jjPAL, getConstColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "jjPALCOLOR& set_color(uint8, const jjPALCOLOR &in)", asMETHOD(jjPAL, setColorEntry), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void reset()", asMETHOD(jjPAL, reset), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void apply() const", asMETHOD(jjPAL, apply), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "bool load(string &in filename)", asMETHOD(jjPAL, load), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(uint8 red, uint8 green, uint8 blue, float opacity)", asMETHOD(jjPAL, fill), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(uint8 red, uint8 green, uint8 blue, uint8 start = 1, uint8 length = 254, float opacity = 1.0)", asMETHOD(jjPAL, fillTint), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(jjPALCOLOR color, float opacity)", asMETHOD(jjPAL, fillFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(jjPALCOLOR color, uint8 start = 1, uint8 length = 254, float opacity = 1.0)", asMETHOD(jjPAL, fillTintFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void gradient(uint8 red1, uint8 green1, uint8 blue1, uint8 red2, uint8 green2, uint8 blue2, uint8 start = 176, uint8 length = 32, float opacity = 1.0, bool inclusive = false)", asMETHOD(jjPAL, gradient), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void gradient(jjPALCOLOR color1, jjPALCOLOR color2, uint8 start = 176, uint8 length = 32, float opacity = 1.0, bool inclusive = false)", asMETHOD(jjPAL, gradientFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void copyFrom(uint8 start, uint8 length, uint8 start2, const jjPAL &in source, float opacity = 1.0)", asMETHOD(jjPAL, copyFrom), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "uint8 findNearestColor(jjPALCOLOR color) const", asMETHOD(jjPAL, findNearestColor), asCALL_THISCALL);

		engine->SetDefaultNamespace("SPRITE");
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NORMAL", spriteType_NORMAL);
		engine->RegisterEnumValue("Mode", "TRANSLUCENT", spriteType_TRANSLUCENT);
		engine->RegisterEnumValue("Mode", "TINTED", spriteType_TINTED);
		engine->RegisterEnumValue("Mode", "GEM", spriteType_GEM);
		engine->RegisterEnumValue("Mode", "INVISIBLE", spriteType_INVISIBLE);
		engine->RegisterEnumValue("Mode", "SINGLECOLOR", spriteType_SINGLECOLOR);
		engine->RegisterEnumValue("Mode", "RESIZED", spriteType_RESIZED);
		engine->RegisterEnumValue("Mode", "NEONGLOW", spriteType_NEONGLOW);
		engine->RegisterEnumValue("Mode", "FROZEN", spriteType_FROZEN);
		engine->RegisterEnumValue("Mode", "PLAYER", spriteType_PLAYER);
		engine->RegisterEnumValue("Mode", "PALSHIFT", spriteType_PALSHIFT);
		engine->RegisterEnumValue("Mode", "SHADOW", spriteType_SHADOW);
		engine->RegisterEnumValue("Mode", "SINGLEHUE", spriteType_SINGLEHUE);
		engine->RegisterEnumValue("Mode", "BRIGHTNESS", spriteType_BRIGHTNESS);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTCOLOR", spriteType_TRANSLUCENTCOLOR);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTPLAYER", spriteType_TRANSLUCENTPLAYER);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTPALSHIFT", spriteType_TRANSLUCENTPALSHIFT);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTSINGLEHUE", spriteType_TRANSLUCENTSINGLEHUE);
		engine->RegisterEnumValue("Mode", "ALPHAMAP", spriteType_ALPHAMAP);
		engine->RegisterEnumValue("Mode", "MENUPLAYER", spriteType_MENUPLAYER);
		engine->RegisterEnumValue("Mode", "BLEND_NORMAL", spriteType_BLENDNORMAL);
		engine->RegisterEnumValue("Mode", "BLEND_DARKEN", spriteType_BLENDDARKEN);
		engine->RegisterEnumValue("Mode", "BLEND_LIGHTEN", spriteType_BLENDLIGHTEN);
		engine->RegisterEnumValue("Mode", "BLEND_HUE", spriteType_BLENDHUE);
		engine->RegisterEnumValue("Mode", "BLEND_SATURATION", spriteType_BLENDSATURATION);
		engine->RegisterEnumValue("Mode", "BLEND_COLOR", spriteType_BLENDCOLOR);
		engine->RegisterEnumValue("Mode", "BLEND_LUMINANCE", spriteType_BLENDLUMINANCE);
		engine->RegisterEnumValue("Mode", "BLEND_MULTIPLY", spriteType_BLENDMULTIPLY);
		engine->RegisterEnumValue("Mode", "BLEND_SCREEN", spriteType_BLENDSCREEN);
		engine->RegisterEnumValue("Mode", "BLEND_DISSOLVE", spriteType_BLENDDISSOLVE);
		engine->RegisterEnumValue("Mode", "BLEND_OVERLAY", spriteType_BLENDOVERLAY);
		engine->RegisterEnumValue("Mode", "BLEND_HARDLIGHT", spriteType_BLENDHARDLIGHT);
		engine->RegisterEnumValue("Mode", "BLEND_SOFTLIGHT", spriteType_BLENDSOFTLIGHT);
		engine->RegisterEnumValue("Mode", "BLEND_DIFFERENCE", spriteType_BLENDDIFFERENCE);
		engine->RegisterEnumValue("Mode", "BLEND_DODGE", spriteType_BLENDDODGE);
		engine->RegisterEnumValue("Mode", "BLEND_BURN", spriteType_BLENDBURN);
		engine->RegisterEnumValue("Mode", "BLEND_EXCLUSION", spriteType_BLENDEXCLUSION);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTTILE", spriteType_TRANSLUCENTTILE);
		engine->RegisterEnumValue("Mode", "CHROMAKEY", spriteType_CHROMAKEY);
		engine->RegisterEnumValue("Mode", "MAPPING", spriteType_MAPPING);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTMAPPING", spriteType_TRANSLUCENTMAPPING);
		engine->RegisterEnum("Direction");
		engine->RegisterEnumValue("Direction", "FLIPNONE", 0x00);
		engine->RegisterEnumValue("Direction", "FLIPH", 0xFF - 0x100);
		engine->RegisterEnumValue("Direction", "FLIPV", 0x40);
		engine->RegisterEnumValue("Direction", "FLIPHV", 0xBF - 0x100);
		engine->SetDefaultNamespace("TILE");
		engine->RegisterEnum("Quadrant");
		engine->RegisterEnumValue("Quadrant", "TOPLEFT", 0);
		engine->RegisterEnumValue("Quadrant", "TOPRIGHT", 1);
		engine->RegisterEnumValue("Quadrant", "BOTTOMLEFT", 2);
		engine->RegisterEnumValue("Quadrant", "BOTTOMRIGHT", 3);
		engine->RegisterEnumValue("Quadrant", "ALLQUADRANTS", 4);
		engine->RegisterEnum("Flags");
		engine->RegisterEnumValue("Flags", "RAWRANGE", FLAG_HFLIPPED_TILE - 1);
		engine->RegisterEnumValue("Flags", "HFLIPPED", FLAG_HFLIPPED_TILE);
		engine->RegisterEnumValue("Flags", "VFLIPPED", FLAG_VFLIPPED_TILE);
		engine->RegisterEnumValue("Flags", "ANIMATED", FLAG_ANIMATED_TILE);
		engine->SetDefaultNamespace("");

		engine->RegisterObjectMethod("jjPLAYER", "SPRITE::Mode get_spriteMode() const", asMETHOD(jjPLAYER, get_spriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "SPRITE::Mode set_spriteMode(SPRITE::Mode)", asMETHOD(jjPLAYER, set_spriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_spriteParam() const", asMETHOD(jjPLAYER, get_spriteParam), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_spriteParam(uint8)", asMETHOD(jjPLAYER, set_spriteParam), asCALL_THISCALL);

		engine->RegisterGlobalFunction("void jjSpriteModeSetMapping(uint8 index, const array<uint8> &in indexMapping, const jjPAL &in rgbMapping)", asFUNCTION(jjSpriteModeSetMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSpriteModeSetMapping(uint8 index, const array<uint8> &in indexMapping)", asFUNCTION(jjSpriteModeSetIndexMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSpriteModeIsMappingUsed(uint8 index)", asFUNCTION(jjSpriteModeIsMappingUsed), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjSpriteModeFirstFreeMapping()", asFUNCTION(jjSpriteModeFirstFreeMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("array<uint8>@ jjSpriteModeGetIndexMapping(uint8 index)", asFUNCTION(jjSpriteModeGetIndexMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPAL@ jjSpriteModeGetColorMapping(uint8 index)", asFUNCTION(jjSpriteModeGetColorMapping), asCALL_CDECL);
	}
}

#endif
