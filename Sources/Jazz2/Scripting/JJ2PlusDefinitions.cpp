#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusDefinitions.h"
#include "LevelScriptLoader.h"

#include "../LevelHandler.h"
#include "../ContentResolver.h"
#include "../Actors/ActorBase.h"
#include "../Actors/Player.h"
#include "../Compatibility/AnimSetMapping.h"
#include "../Compatibility/EventConverter.h"
#include "../Compatibility/JJ2Strings.h"
#include "../UI/HUD.h"
#include "../UI/InGameConsole.h"

#include "../../nCine/Audio/AudioBuffer.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"

#include "../../nCine/Application.h"
#include "../../nCine/Base/Random.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <regex>

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	static void Unimplemented(const char* sourceName) {
		auto ctx = asGetActiveContext();
		if (ctx != nullptr) {
			const char* sectionName;
			std::int32_t lineNumber = ctx->GetLineNumber(0, nullptr, &sectionName);
			LOGW("{} (called from \"{}:{}\")", sourceName, sectionName, lineNumber);
		} else {
			LOGW("{}", sourceName);
		}
	}

#if defined(DEATH_TRACE)
#	define noop() Unimplemented(NCINE_CURRENT_FUNCTION)
#else
#	define noop() do {} while (false)
#endif

	// Opens a file requested by a script: tries the converted content first, then the original "Source" directory
	// (where a level's custom/original files live), so scripts can load files by their original name
	static std::unique_ptr<Stream> OpenScriptFile(StringView path) {
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(path);
		if (s != nullptr && s->IsValid()) {
			return s;
		}
		return resolver.OpenSourceFile(path);
	}

	// Returns true if any pixel in the given AABB is solid in the sprite (collision) layer's mask. Read-only
	// (DestructType::None), so it never destroys tiles. Only the sprite layer carries collision masks in this engine.
	static bool IsRegionMasked(const AABBf& aabb) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap == nullptr) {
			return false;
		}
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, false };
		return !tileMap->IsTileEmpty(aabb, params);
	}

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

			// Converts a sprite-palette index (as used by the canvas drawing functions) to an RGBA color with the given alpha
			static Colorf PaletteIndexToColor(std::uint8_t index, float alpha) {
				std::uint32_t c = ContentResolver::Get().GetPalettes()[index];
				return Colorf((c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, alpha);
			}

			// Maps a JJ2 sprite blend mode + parameter to an alpha value for solid/canvas drawing (best effort: opaque
			// unless a translucent/blend mode supplies an explicit alpha)
			static float SpriteModeToAlpha(std::uint32_t mode, std::uint8_t param) {
				switch (mode) {
					case spriteType_BLENDNORMAL:
						return param / 255.0f;
					case spriteType_TRANSLUCENT:
						return 0.5f;
					default:
						return 1.0f;
				}
			}

			// Composite global indices for the animation model. A frame index packs [set | anim | frame] and an
			// animation index packs [set | anim], so the JJ2+ idioms still work arithmetically:
			//   jjAnimSets[set].firstAnim + n  -> animation index (set, n)
			//   jjAnimations[a].firstFrame + f -> frame index (set, anim, f)
			// set occupies the high bits, animation the middle 10, frame the low 10.
			static constexpr std::uint32_t AnimFrameIndexBits = 10;	// up to 1024 frames per animation
			static constexpr std::uint32_t AnimAnimIndexBits = 10;	// up to 1024 animations per set
			static constexpr std::uint32_t AnimFrameIndexMask = (1u << AnimFrameIndexBits) - 1;
			static constexpr std::uint32_t AnimAnimIndexMask = (1u << AnimAnimIndexBits) - 1;
			// JJ2Anims adds a 2px border around exported frames and stores hotspots negated+offset; the original JJ2
			// spot value is recovered as (AddBorder - engineSpot). See Compatibility::JJ2Anims::AddBorder.
			static constexpr std::int32_t AnimSpotBorder = 2;

			// Decodes a composite global frame index (as produced by jjAnimations[a].firstFrame + f) back to its
			// graphic resource and local frame number, for the "current frame" canvas draw functions
			static GenericGraphicResource* ResolveCurFrame(LevelScriptLoader* owner, std::uint32_t sprite, std::int32_t& frameOut) {
				std::uint32_t globalAnim = (sprite >> AnimFrameIndexBits);
				frameOut = (std::int32_t)(sprite & AnimFrameIndexMask);
				std::uint32_t setID = (globalAnim >> AnimAnimIndexBits);
				std::uint32_t animIdx = (globalAnim & AnimAnimIndexMask);
				return owner->ResolveSpriteGraphic((std::int32_t)setID, (std::int32_t)animIdx);
			}

			// Draws a single animation frame of a resolved graphic onto a HUD canvas, following the HUD::DrawElement
			// pattern. The sprite is anchored at the frame's hotspot, optionally flipped (direction < 0 or negative
			// scale), scaled and rotated (angle in JJ2 units where 1024 = a full turn). Indexed graphics draw through
			// the palette-remap shader against the live sprite palette (so they recolor with jjPalette).
			static void DrawSpriteFrame(UI::HUD* hud, std::uint16_t z, GenericGraphicResource* base, std::int32_t frame,
					float x, float y, std::int8_t direction, float scaleX, float scaleY, std::int32_t angle,
					std::uint32_t mode, std::uint8_t param) {
				if (base == nullptr || base->TextureDiffuse == nullptr || base->FrameCount <= 0 || base->FrameConfiguration.X <= 0) {
					return;
				}
				if (frame < 0) {
					frame = 0;
				} else if (frame >= base->FrameCount) {
					frame = base->FrameCount - 1;
				}

				bool flipX = (direction < 0);
				bool flipY = false;
				if (scaleX < 0.0f) { flipX = !flipX; scaleX = -scaleX; }
				if (scaleY < 0.0f) { flipY = true; scaleY = -scaleY; }

				Vector2f size(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
				// Anchor the sprite at its hotspot, so the given coordinates are where the hotspot lands
				Vector2f pos(x - base->Hotspot.X * scaleX, y - base->Hotspot.Y * scaleY);

				Vector2i texSize = base->TextureDiffuse->GetSize();
				std::int32_t col = frame % base->FrameConfiguration.X;
				std::int32_t row = frame / base->FrameConfiguration.X;
				float wFrac = (float)base->FrameDimensions.X / texSize.X;
				float hFrac = (float)base->FrameDimensions.Y / texSize.Y;
				Vector4f texCoords(wFrac, (float)(base->FrameDimensions.X * col) / texSize.X,
					hFrac, (float)(base->FrameDimensions.Y * row) / texSize.Y);
				if (flipX) { texCoords.X = -wFrac; texCoords.Y += wFrac; }
				if (flipY) { texCoords.Z = -hFrac; texCoords.W += hFrac; }

				Colorf color(1.0f, 1.0f, 1.0f, SpriteModeToAlpha(mode, param));
				float angleRad = angle * fTwoPi / 1024.0f;
				// Indexed sprites recolor via the shared sprite palette (offset 0); pre-baked RGBA sprites use -1
				std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? 0 : -1);
				hud->DrawTexture(*base->TextureDiffuse.get(), pos, z, size, texCoords, color,
					mode == spriteType_BLENDLIGHTEN, angleRad, paletteOffset);
			}
		}

		// Native actor that hosts a script-controlled jjOBJ: each frame it applies the object's own velocity, runs the
		// script behavior (a jjVOIDFUNCOBJ function or a jjBEHAVIORINTERFACE object) over the jjOBJ, then syncs the
		// resulting position back to the actor. This is what makes jjAddObject objects script-controllable.
		// NOTE: in-world sprite drawing isn't implemented yet (the object is logic/position-only for now).
		class ScriptLegacyObject : public Actors::ActorBase
		{
			DEATH_RUNTIME_OBJECT(Actors::ActorBase);

		public:
			ScriptLegacyObject(LevelScriptLoader* levelScripts, jjOBJ* obj)
				: _levelScripts(levelScripts), _obj(obj)
			{
				// Takes ownership of the single reference created by AddScriptControlledObject
				_obj->_wrapper = this;
			}
			~ScriptLegacyObject()
			{
				if (_obj != nullptr) {
					// Mark the script-facing object inactive, then drop the hosted reference
					_obj->_wrapper = nullptr;
					_obj->Release();
				}
			}

			jjOBJ* GetObject() const {
				return _obj;
			}

			// Configures the actor renderer to display the given JJ2 animation set + animation (resolved through the
			// anim mapping), mirroring ActorBase::RefreshAnimation but from a raw graphic resource. The renderer then
			// auto-advances and draws the animation in-world at the actor's position.
			void ApplyAnimation(std::int32_t setID, std::int32_t animation)
			{
				// Behaviors often call determineCurAnim every frame; only rebuild the renderer when it actually changes
				if (setID == _lastSetID && animation == _lastAnimation) {
					return;
				}

				auto* base = _levelScripts->ResolveSpriteGraphic(setID, animation);
				if (base == nullptr || base->TextureDiffuse == nullptr || base->FrameConfiguration.X <= 0) {
					return;
				}
				_lastSetID = setID;
				_lastAnimation = animation;

				// No GL renderer to configure on a dedicated/headless server
				if (ContentResolver::Get().IsHeadless()) {
					return;
				}

				_renderer.FrameConfiguration = base->FrameConfiguration;
				_renderer.FrameDimensions = base->FrameDimensions;
				_renderer.FirstFrame = 0;
				_renderer.FrameCount = base->FrameCount;
				_renderer.AnimDuration = base->AnimDuration;
				_renderer.LoopMode = AnimationLoopMode::Loop;
				_renderer.AnimTime = 0.0f;
				_renderer.Hotspot.X = (float)(IsFacingLeft() ? (base->FrameDimensions.X - base->Hotspot.X) : base->Hotspot.X);
				_renderer.Hotspot.Y = (float)base->Hotspot.Y;
				_renderer.setTexture(base->TextureDiffuse.get());
				_renderer.SetIndexed((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed, 0);
				_renderer.setAbsAnchorPoint(_renderer.Hotspot.X, _renderer.Hotspot.Y);
				_renderer.Initialize(Actors::ActorRendererType::Default);
				// The renderer recomputes the visible frame each frame in its own OnUpdate (UpdateVisibleFrames is private)
			}

			// Marks the object for removal at the end of the frame (SetState is protected, so the jjOBJ proxy routes
			// deleteObject/deactivate through here)
			void Remove() {
				SetState(Actors::ActorState::IsDestroyed, true);
			}
			// Toggles whether other actors/players collide with this object as a solid
			void SetSolid(bool solid) {
				SetState(Actors::ActorState::IsSolidObject, solid);
			}

		protected:
			Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override
			{
				_obj->xPos = _obj->xOrg = _pos.X;
				_obj->yPos = _obj->yOrg = _pos.Y;
				// The script behavior drives the object; don't apply engine gravity by default
				SetState(Actors::ActorState::ApplyGravitation, false);
				async_return true;
			}

			void OnUpdate(float timeMult) override
			{
				// JJ2-style automatic motion, so a behavior can simply set speeds/acceleration
				_obj->xSpeed += _obj->xAcc * timeMult;
				_obj->ySpeed += _obj->yAcc * timeMult;
				_obj->xPos += _obj->xSpeed * timeMult;
				_obj->yPos += _obj->ySpeed * timeMult;

				asIScriptFunction* behaviorFunc = _obj->behavior;
				asIScriptObject* behaviorObj = _obj->behavior;
				if (behaviorFunc != nullptr || behaviorObj != nullptr) {
					asIScriptEngine* engine = _levelScripts->GetEngine();
					asIScriptContext* ctx = engine->RequestContext();
					if (behaviorObj != nullptr) {
						asIScriptFunction* onBehave = behaviorObj->GetObjectType()->GetMethodByDecl("void onBehave(jjOBJ@ obj)");
						if (onBehave != nullptr) {
							ctx->Prepare(onBehave);
							ctx->SetObject(behaviorObj);
							ctx->SetArgObject(0, _obj);
							ExecuteBehavior(ctx);
						}
					} else {
						ctx->Prepare(behaviorFunc);
						ctx->SetArgObject(0, _obj);
						ExecuteBehavior(ctx);
					}
					engine->ReturnContext(ctx);
				}

				// Apply the (possibly script-adjusted) position/facing to the actor and expose the current frame
				MoveInstantly(Vector2f(_obj->xPos, _obj->yPos), Actors::MoveType::Absolute | Actors::MoveType::Force);
				SetFacingLeft(_obj->direction < 0);
				_obj->curFrame = (std::uint32_t)_renderer.CurrentFrame;
				_obj->age++;
			}

		private:
			LevelScriptLoader* _levelScripts;
			jjOBJ* _obj;
			std::int32_t _lastSetID = -1;
			std::int32_t _lastAnimation = -1;

			static void ExecuteBehavior(asIScriptContext* ctx)
			{
				std::int32_t r = ctx->Execute();
				if (r == asEXECUTION_EXCEPTION) {
					LOGE("An exception \"{}\" occurred in \"{}\". Please correct the code and try again.",
						ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
				}
			}
		};

		void jjTEXTAPPEARANCE::constructor(void* self) {
			new(self) jjTEXTAPPEARANCE{};
		}
		void jjTEXTAPPEARANCE::constructorMode(std::uint32_t mode, void* self) {
			new(self) jjTEXTAPPEARANCE{};
		}

		jjTEXTAPPEARANCE& jjTEXTAPPEARANCE::operator=(std::uint32_t other) {
			noop(); return *this;
		}

		void jjPALCOLOR::Create(void* self) {
			new(self) jjPALCOLOR{};
		}
		void jjPALCOLOR::CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self) {
			new(self) jjPALCOLOR{red, green, blue};
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
			return *this;
		}
		bool jjPALCOLOR::operator==(const jjPALCOLOR& other) {
			noop(); return (red == other.red && green == other.green && blue == other.blue);
		}

		jjPAL::jjPAL() : _refCount(1), _palette{} {
			noop();
		}
		jjPAL::~jjPAL() {
			noop();
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
			if (owner != nullptr && &owner->jjPalette != this) {
				owner->jjPalette = *this;
			}
		}
		bool jjPAL::load(const String& filename) {

			auto stream = OpenScriptFile(filename);
			if (stream == nullptr || !stream->IsValid()) {
				return false;
			}

			// Like the original, the raw bytes of the file are interpreted as a sequence of RGB triples (so any file
			// "loads", but non-palette files produce garbage colors). Reads up to 256 entries; any remainder is left
			// unchanged. Index 0 stays as-is so it can keep acting as the transparent color.
			std::uint8_t rgb[3];
			for (std::int32_t i = 0; i < 256; i++) {
				if (stream->Read(rgb, sizeof(rgb)) != (std::int64_t)sizeof(rgb)) {
					break;
				}
				_palette[i].red = rgb[0];
				_palette[i].green = rgb[1];
				_palette[i].blue = rgb[2];
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
			std::int32_t end = std::min((std::int32_t)start + (std::int32_t)length, 256);
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
			std::int32_t end = std::min((std::int32_t)start + (std::int32_t)length, 256);
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

		jjSTREAM::jjSTREAM() : _refCount(1) {
			noop();
		}
		jjSTREAM::~jjSTREAM() {
			noop();
		}

		jjSTREAM* jjSTREAM::Create() {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjSTREAM));
			return new(mem) jjSTREAM();
		}
		jjSTREAM* jjSTREAM::CreateFromFile(const String& filename) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjSTREAM));
			return new(mem) jjSTREAM();
		}

		void jjSTREAM::AddRef()
		{
			_refCount++;
		}

		void jjSTREAM::Release()
		{
			if (--_refCount == 0) {
				this->~jjSTREAM();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjSTREAM& jjSTREAM::operator=(const jjSTREAM& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		std::uint32_t jjSTREAM::getSize() const {
			noop();
			return 0;
		}

		bool jjSTREAM::isEmpty() const {
			noop();
			return false;
		}

		bool jjSTREAM::save(const String& tilename) const {
			noop();
			return true;
		}

		void jjSTREAM::clear() {
			noop();
		}

		bool jjSTREAM::discard(std::uint32_t count) {
			noop();
			return false;
		}

		bool jjSTREAM::write(const String& value) {
			noop();
			return true;
		}
		bool jjSTREAM::write(const jjSTREAM& value) {
			noop();
			return true;
		}
		bool jjSTREAM::get(String& value, std::uint32_t count) {
			noop();
			return false;
		}
		bool jjSTREAM::get(jjSTREAM& value, std::uint32_t count) {
			noop();
			return false;
		}
		bool jjSTREAM::getLine(String& value, const String& delim) {
			noop();
			return false;
		}

		bool jjSTREAM::push(bool value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint8_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int8_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint16_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int16_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint32_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int32_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint64_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int64_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(float value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(double value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(const String& value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(const jjSTREAM& value) {
			noop();
			return false;
		}

		bool jjSTREAM::pop(bool& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint8_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int8_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint16_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int16_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint32_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int32_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint64_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int64_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(float& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(double& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(String& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(jjSTREAM& value) {
			noop();
			return false;
		}

		jjRNG::jjRNG(std::uint64_t seed) : _refCount(1) {
			noop();
		}
		jjRNG::~jjRNG() {
			noop();
		}
		jjRNG* jjRNG::Create(std::uint64_t seed) {
			void* mem = asAllocMem(sizeof(jjRNG));
			return new(mem) jjRNG(seed);
		}
		void jjRNG::AddRef() {
			_refCount++;
		}
		void jjRNG::Release() {
			if (--_refCount == 0) {
				this->~jjRNG();
				asFreeMem(this);
			}
		}
		std::uint64_t jjRNG::operator()() {
			// Join two 32-bit values to single 64-bit value
			return _random.Next() | ((std::uint64_t)_random.Next() << 32ull);
		}
		jjRNG& jjRNG::operator=(const jjRNG& o) {
			_random = o._random;
			return *this;
		}
		bool jjRNG::operator==(const jjRNG& o) const {
			return (this == &o);
		}
		void jjRNG::seed(std::uint64_t value) {
			_random.Init(value, DefaultInitSequence);
		}
		void jjRNG::discard(std::uint64_t count) {
			for (std::uint64_t i = 0; i < count; i++) {
				_random.Next();
			}
		}

		jjBEHAVIOR* jjBEHAVIOR::Create(jjBEHAVIOR* self) {
			return new(self) jjBEHAVIOR{};
		}
		jjBEHAVIOR* jjBEHAVIOR::CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self) {
			auto* result = new(self) jjBEHAVIOR{};
			result->behaviorId = behavior;
			return result;
		}
		void jjBEHAVIOR::Destroy(jjBEHAVIOR* self) {
			self->~jjBEHAVIOR();
		}

		jjBEHAVIOR::~jjBEHAVIOR() {
			if (function != nullptr) {
				function->Release();
			}
			if (object != nullptr) {
				object->Release();
			}
		}

		jjBEHAVIOR::jjBEHAVIOR(const jjBEHAVIOR& other)
			: behaviorId(other.behaviorId), function(other.function), object(other.object) {
			if (function != nullptr) {
				function->AddRef();
			}
			if (object != nullptr) {
				object->AddRef();
			}
		}

		jjBEHAVIOR& jjBEHAVIOR::operator=(const jjBEHAVIOR& other) {
			if (this != &other) {
				if (other.function != nullptr) {
					other.function->AddRef();
				}
				if (other.object != nullptr) {
					other.object->AddRef();
				}
				if (function != nullptr) {
					function->Release();
				}
				if (object != nullptr) {
					object->Release();
				}
				behaviorId = other.behaviorId;
				function = other.function;
				object = other.object;
			}
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(std::uint32_t other) {
			if (function != nullptr) {
				function->Release();
				function = nullptr;
			}
			if (object != nullptr) {
				object->Release();
				object = nullptr;
			}
			behaviorId = other;
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptFunction* other) {
			if (other != nullptr) {
				other->AddRef();
			}
			if (function != nullptr) {
				function->Release();
			}
			if (object != nullptr) {
				object->Release();
				object = nullptr;
			}
			function = other;
			behaviorId = 0;
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptObject* other) {
			if (other != nullptr) {
				other->AddRef();
			}
			if (object != nullptr) {
				object->Release();
			}
			if (function != nullptr) {
				function->Release();
				function = nullptr;
			}
			object = other;
			behaviorId = 0;
			return *this;
		}
		bool jjBEHAVIOR::operator==(const jjBEHAVIOR& other) const {
			return (behaviorId == other.behaviorId && function == other.function && object == other.object);
		}
		bool jjBEHAVIOR::operator==(std::uint32_t other) const {
			return (function == nullptr && object == nullptr && behaviorId == other);
		}
		bool jjBEHAVIOR::operator==(const asIScriptFunction* other) const {
			return (function == other);
		}
		jjBEHAVIOR::operator std::uint32_t() {
			return behaviorId;
		}
		jjBEHAVIOR::operator asIScriptFunction* () {
			return function;
		}
		jjBEHAVIOR::operator asIScriptObject* () {
			return object;
		}

		jjANIMFRAME::jjANIMFRAME() : _refCount(1) {
			noop();
		}
		jjANIMFRAME::~jjANIMFRAME() {
			noop();
		}

		void jjANIMFRAME::AddRef()
		{
			_refCount++;
		}

		void jjANIMFRAME::Release()
		{
			if (--_refCount == 0) {
				this->~jjANIMFRAME();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjANIMFRAME& jjANIMFRAME::operator=(const jjANIMFRAME& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		jjANIMFRAME* jjANIMFRAME::get_jjAnimFrames(std::uint32_t index) {
			void* mem = asAllocMem(sizeof(jjANIMFRAME));
			auto* frame = new(mem) jjANIMFRAME();

			// Decode the composite frame index back to (set, animation) and resolve the graphic. The engine stores a
			// single hotspot/coldspot/gunspot per animation, so all frames of an animation report the same spots.
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t globalAnim = (index >> AnimFrameIndexBits);
			std::uint32_t setID = (globalAnim >> AnimAnimIndexBits);
			std::uint32_t animIdx = (globalAnim & AnimAnimIndexMask);
			auto* base = owner->ResolveSpriteGraphic((std::int32_t)setID, (std::int32_t)animIdx);
			if (base != nullptr) {
				frame->width = (std::int16_t)base->FrameDimensions.X;
				frame->height = (std::int16_t)base->FrameDimensions.Y;
				frame->hotSpotX = (std::int16_t)(AnimSpotBorder - base->Hotspot.X);
				frame->hotSpotY = (std::int16_t)(AnimSpotBorder - base->Hotspot.Y);
				if (base->Coldspot.X != ContentResolver::InvalidValue) {
					frame->coldSpotX = (std::int16_t)(AnimSpotBorder - base->Coldspot.X);
					frame->coldSpotY = (std::int16_t)(AnimSpotBorder - base->Coldspot.Y);
				}
				if (base->Gunspot.X != ContentResolver::InvalidValue) {
					frame->gunSpotX = (std::int16_t)(AnimSpotBorder - base->Gunspot.X);
					frame->gunSpotY = (std::int16_t)(AnimSpotBorder - base->Gunspot.Y);
				}
			}
			return frame;
		}

		bool jjANIMFRAME::get_transparent() const {
			noop();
			return false;
		}
		bool jjANIMFRAME::set_transparent(bool value) const {
			noop();
			return false;
		}
		bool jjANIMFRAME::doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const {
			noop();
			return false;
		}

		jjANIMATION::jjANIMATION(std::uint32_t index) : _refCount(1), _index(index) {
			noop();
		}
		jjANIMATION::~jjANIMATION() {
			noop();
		}

		void jjANIMATION::AddRef()
		{
			_refCount++;
		}

		void jjANIMATION::Release()
		{
			if (--_refCount == 0) {
				this->~jjANIMATION();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjANIMATION& jjANIMATION::operator=(const jjANIMATION& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		bool jjANIMATION::save(const String& filename, const jjPAL& palette) const {
			noop();
			return false;
		}
		bool jjANIMATION::load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite) {
			noop();
			return false;
		}

		jjANIMATION* jjANIMATION::get_jjAnimations(std::uint32_t index) {
			void* mem = asAllocMem(sizeof(jjANIMATION));
			auto* anim = new(mem) jjANIMATION(index);

			// Decode the composite animation index to (set, animation), resolve the graphic and fill frame count + fps
			// (the converter stores AnimDuration = 5 / FrameRate, so the original fps is 5 / AnimDuration)
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t setID = (index >> AnimAnimIndexBits);
			std::uint32_t animIdx = (index & AnimAnimIndexMask);
			auto* base = owner->ResolveSpriteGraphic((std::int32_t)setID, (std::int32_t)animIdx);
			if (base != nullptr) {
				anim->frameCount = (std::uint16_t)base->FrameCount;
				anim->fps = (base->AnimDuration > 0.0f ? (std::int16_t)(5.0f / base->AnimDuration + 0.5f) : 0);
			}
			return anim;
		}

		std::uint32_t jjANIMATION::get_firstFrame() const {
			// First frame's composite global index for this animation
			return (_index << AnimFrameIndexBits);
		}
		std::uint32_t jjANIMATION::set_firstFrame(std::uint32_t index) const {
			// The first frame is derived from the animation's global index, so it can't be reassigned here
			noop();
			return (_index << AnimFrameIndexBits);
		}

		std::uint32_t jjANIMATION::getAnimFirstFrame() {
			return (_index << AnimFrameIndexBits);
		}

		jjANIMSET::jjANIMSET(std::uint32_t index) : _refCount(1), _index(index) {
			noop();
		}
		jjANIMSET::~jjANIMSET() {
			noop();
		}

		void jjANIMSET::AddRef()
		{
			_refCount++;
		}

		void jjANIMSET::Release()
		{
			if (--_refCount == 0) {
				this->~jjANIMSET();
				asFreeMem(this);
			}
		}

		jjANIMSET* jjANIMSET::get_jjAnimSets(std::uint32_t index) {
			void* mem = asAllocMem(sizeof(jjANIMSET));
			auto* set = new(mem) jjANIMSET(index);
			// First animation's composite global index for this set (see the AnimAnimIndexBits packing scheme)
			set->firstAnim = (index << AnimAnimIndexBits);
			return set;
		}

		std::uint32_t jjANIMSET::convertAnimSetToUint() {
			return _index;
		}

		jjANIMSET* jjANIMSET::load(std::uint32_t fileSetID, const String& filename, std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite) {
			noop();
			return this;
		}
		jjANIMSET* jjANIMSET::allocate(const CScriptArray& frameCounts) {
			noop();
			return this;
		}

		jjCANVAS::jjCANVAS(UI::HUD* hud, const Rectf& view)
			: Hud(hud), View(view)
		{
		}

		void jjCANVAS::DrawPixel(std::int32_t xPixel, int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			Colorf c = PaletteIndexToColor(color, SpriteModeToAlpha(mode, param));
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), UI::HUD::MainLayer, Vector2f(1.0f, 1.0f), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawRectangle(std::int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			Colorf c = PaletteIndexToColor(color, SpriteModeToAlpha(mode, param));
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), UI::HUD::MainLayer, Vector2f((float)width, (float)height), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, owner->ResolveSpriteGraphic(setID, animation), frame,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param);
		}
		void jjCANVAS::DrawCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t frame = 0;
			auto* base = ResolveCurFrame(owner, sprite, frame);
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, base, frame,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param);
		}
		void jjCANVAS::DrawResizedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, owner->ResolveSpriteGraphic(setID, animation), frame,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param);
		}
		void jjCANVAS::DrawResizedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t frame = 0;
			auto* base = ResolveCurFrame(owner, sprite, frame);
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, base, frame,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param);
		}
		void jjCANVAS::DrawTransformedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, owner->ResolveSpriteGraphic(setID, animation), frame,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param);
		}
		void jjCANVAS::DrawTransformedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t frame = 0;
			auto* base = ResolveCurFrame(owner, sprite, frame);
			DrawSpriteFrame(Hud, UI::HUD::MainLayer, base, frame,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param);
		}
		void jjCANVAS::DrawSwingingVine(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t length, int32_t curvature, std::uint32_t mode, std::uint8_t param) {
			noop();
		}

		void jjCANVAS::ExternalDrawTile(std::int32_t xPixel, int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant) {
			noop();
		}
		void jjCANVAS::DrawTextBasicSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param) {
			float scale;
			switch (size) {
				case 1:	// SMALL
					scale = 0.6f;
					break;
				case 2:	// LARGE
					scale = 1.1f;
					break;
				default:
				case 0:	// MEDIUM
					scale = 0.8f;
					break;
			}

			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, UI::Font::DefaultColor, scale, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		void jjCANVAS::DrawTextExtSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param) {
			Colorf color = UI::Font::DefaultColor;
			if (mode == spriteType_BLENDNORMAL) {
				color.A = param / 255.0f;
			}

			float scale;
			switch (size) {
				case 1:	// SMALL
					scale = 0.6f;
					break;
				case 2:	// LARGE
					scale = 1.1f;
					break;
				default:
				case 0:	// MEDIUM
					scale = 0.8f;
					break;
			}

			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, color, scale, 0.0f, 0.0f, 0.0f, 0.0f);
		}

		void jjCANVAS::drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param) {
			noop();
			// TODO
			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, UI::Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		}

		void jjCANVAS::drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2) {
			noop();
		}

		void jjCANVAS::jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
			noop();
		}

		void jjCANVAS::jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
			noop();
		}

		int jjCANVAS::jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style) {
			noop();
			// TODO
			return text.size() * 12.0f;
		}

		jjOBJ::jjOBJ() : _refCount(1) {
			noop();
		}
		jjOBJ::~jjOBJ() {
			noop();
		}

		void jjOBJ::AddRef()
		{
			_refCount++;
		}

		void jjOBJ::Release()
		{
			if (--_refCount == 0) {
				this->~jjOBJ();
				asFreeMem(this);
			}
		}

		bool jjOBJ::get_isActive() const {
			// A script-controlled object is active while its hosting actor exists
			return (_wrapper != nullptr);
		}
		std::uint32_t jjOBJ::get_lightType() const {
			noop();
			return 0;
		}
		std::uint32_t jjOBJ::set_lightType(std::uint32_t value) const {
			noop();
			return 0;
		}

		jjOBJ* jjOBJ::objectHit(jjOBJ* target, std::uint32_t playerHandling) {
			noop();
			return nullptr;
		}
		void jjOBJ::blast(std::int32_t maxDistance, bool blastObjects) {
			noop();
		}

		void jjOBJ::behave1(std::uint32_t behavior, bool draw) {
			// Built-in behaviors aren't emulated, but store the id so reads are consistent
			this->behavior = behavior;
		}
		void jjOBJ::behave2(jjBEHAVIOR behavior, bool draw) {
			this->behavior = behavior;
		}
		void jjOBJ::behave3(jjVOIDFUNCOBJ behavior, bool draw) {
			// The funcdef handle arrives as an AngelScript function pointer (see jjAddObjectEx)
			this->behavior = reinterpret_cast<asIScriptFunction*>(behavior);
		}

		std::int32_t jjOBJ::jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior) {
			// The spawned actor runs its own native behavior; the custom `behavior`/creator arguments aren't applied
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->AddObjectFromEvent(eventID, xPixel, yPixel);
		}
		std::int32_t jjOBJ::jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
			// The funcdef handle is passed by AngelScript as an asIScriptFunction*; spawn a script-controlled object
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t id = owner->AddScriptControlledObject(eventID, xPixel, yPixel, reinterpret_cast<asIScriptFunction*>(behavior));
			// Record the creator on the persistent jjOBJ so scripts can read it back
			if (auto* wrapper = runtime_cast<ScriptLegacyObject>(owner->GetScriptObject(id))) {
				jjOBJ* obj = wrapper->GetObject();
				obj->_creatorID = creatorID;
				obj->_creatorType = creatorType;
			}
			return id;
		}

		void jjOBJ::jjDeleteObject(std::int32_t objectID) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* actor = owner->GetScriptObject(objectID);
			if (auto* wrapper = runtime_cast<ScriptLegacyObject>(actor)) {
				wrapper->Remove();		// silent removal
			} else if (actor != nullptr) {
				actor->DecreaseHealth(INT32_MAX);
			}
		}
		void jjOBJ::jjKillObject(std::int32_t objectID) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* actor = owner->GetScriptObject(objectID);
			if (actor != nullptr) {
				actor->DecreaseHealth(INT32_MAX);	// kill (triggers perish)
			}
		}

		std::uint32_t jjOBJ::determineCurFrame(bool change) {
			// The hosting actor's renderer advances the animation automatically; just report the current frame
			return curFrame;
		}

		std::uint16_t jjOBJ::get_creatorID() const {
			return _creatorID;
		}
		std::uint16_t jjOBJ::set_creatorID(std::uint16_t value) const {
			_creatorID = value;
			return _creatorID;
		}
		std::uint32_t jjOBJ::get_creatorType() const {
			return _creatorType;
		}
		std::uint32_t jjOBJ::set_creatorType(std::uint32_t value) const {
			_creatorType = value;
			return _creatorType;
		}

		int16_t jjOBJ::determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change) {
			// Selects the animation to display. `change` actually swaps the hosted actor's sprite; otherwise it only
			// records the selection. (curAnim is 16-bit, so it stores the within-set animation index here.)
			if (change && _wrapper != nullptr) {
				_wrapper->ApplyAnimation(setID, animation);
			}
			curAnim = (int16_t)animation;
			return curAnim;
		}

		std::uint32_t jjOBJ::get_bulletHandling() {
			return _bulletHandling;
		}
		std::uint32_t jjOBJ::set_bulletHandling(std::uint32_t value) {
			_bulletHandling = value;
			return _bulletHandling;
		}
		bool jjOBJ::get_ricochet() {
			return _ricochetProp;
		}
		bool jjOBJ::set_ricochet(bool value) {
			_ricochetProp = value;
			return _ricochetProp;
		}
		bool jjOBJ::get_freezable() {
			return _freezable;
		}
		bool jjOBJ::set_freezable(bool value) {
			_freezable = value;
			return _freezable;
		}
		bool jjOBJ::get_blastable() {
			return _blastable;
		}
		bool jjOBJ::set_blastable(bool value) {
			_blastable = value;
			return _blastable;
		}

		std::uint32_t jjOBJ::get_playerHandling() {
			return _playerHandling;
		}
		std::uint32_t jjOBJ::set_playerHandling(std::uint32_t value) {
			_playerHandling = value;
			return _playerHandling;
		}
		bool jjOBJ::get_isTarget() {
			return _isTarget;
		}
		bool jjOBJ::set_isTarget(bool value) {
			_isTarget = value;
			return _isTarget;
		}
		bool jjOBJ::get_triggersTNT() {
			return _triggersTNT;
		}
		bool jjOBJ::set_triggersTNT(bool value) {
			_triggersTNT = value;
			return _triggersTNT;
		}
		bool jjOBJ::get_deactivates() {
			return _deactivates;
		}
		bool jjOBJ::set_deactivates(bool value) {
			_deactivates = value;
			return _deactivates;
		}
		bool jjOBJ::get_scriptedCollisions() {
			return _scriptedCollisions;
		}
		bool jjOBJ::set_scriptedCollisions(bool value) {
			_scriptedCollisions = value;
			return _scriptedCollisions;
		}

		std::int32_t jjOBJ::get_var(std::uint8_t x) {
			return (x < MaxVars ? _vars[x] : 0);
		}
		std::int32_t jjOBJ::set_var(std::uint8_t x, std::int32_t value) {
			if (x < MaxVars) {
				_vars[x] = value;
			}
			return value;
		}

		std::int32_t jjOBJ::draw() {
			noop();
			return 0;
		}
		std::int32_t jjOBJ::beSolid(bool shouldCheckForStompingLocalPlayers) {
			if (_wrapper != nullptr) {
				_wrapper->SetSolid(true);
			}
			return 0;
		}
		void jjOBJ::bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height) {
			noop();
		}
		void jjOBJ::clearPlatform() {
			noop();
		}
		void jjOBJ::putOnGround(bool precise) {
			// Snap the object down onto (or up out of) the nearest solid ground below its reference point.
			// `precise` scans pixel-by-pixel; the coarse mode steps in 4px increments (faster, tile-ish accuracy).
			// Masks live only on the sprite layer here, so this works regardless of the object's own layer.
			const float x = xPos;
			float y = yPos;
			const float step = precise ? 1.0f : 4.0f;
			const float maxSearch = 256.0f;	// guard against an off-map object scanning the whole level
			float searched = 0.0f;
			if (IsRegionMasked(AABBf(x, y, x + 1.0f, y + 1.0f))) {
				// Embedded in ground: rise until the reference pixel is clear
				while (searched < maxSearch && IsRegionMasked(AABBf(x, y, x + 1.0f, y + 1.0f))) {
					y -= step;
					searched += step;
				}
			} else {
				// Floating: descend until the pixel directly below becomes solid
				while (searched < maxSearch && !IsRegionMasked(AABBf(x, y + 1.0f, x + 1.0f, y + 2.0f))) {
					y += step;
					searched += step;
				}
			}
			yPos = y;
			ySpeed = 0.0f;
		}
		bool jjOBJ::ricochet() {
			noop();
			return false;
		}
		std::int32_t jjOBJ::unfreeze(std::int32_t style) {
			// Thaw a frozen object; the shatter `style` effect isn't emulated for script-controlled objects
			std::int32_t wasFrozen = freeze;
			freeze = 0;
			return wasFrozen;
		}
		void jjOBJ::deleteObject() {
			if (_wrapper != nullptr) {
				_wrapper->Remove();
			}
		}
		void jjOBJ::deactivate() {
			// Script-controlled objects don't return to the event map, so deactivation despawns them
			if (_wrapper != nullptr) {
				_wrapper->Remove();
			}
		}
		void jjOBJ::pathMovement() {
			noop();
		}
		int32_t jjOBJ::fireBullet(std::uint8_t eventID) {
			noop();
			return 0;
		}
		void jjOBJ::particlePixelExplosion(std::int32_t style) {
			noop();
		}
		void jjOBJ::grantPickup(jjPLAYER* player, std::int32_t frequency) {
			noop();
		}

		std::int32_t jjOBJ::findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto players = owner->GetPlayers();
			std::int32_t nearest = -1;
			float bestSq = (float)maxDistance * (float)maxDistance;
			for (std::int32_t i = 0; i < (std::int32_t)players.size(); i++) {
				Vector2f p = players[i]->GetPos();
				float dx = p.X - xPos;
				float dy = p.Y - yPos;
				float distSq = dx * dx + dy * dy;
				if (distSq <= bestSq) {
					bestSq = distSq;
					nearest = i;
				}
			}
			foundDistance = (nearest >= 0 ? (std::int32_t)std::sqrt(bestSq) : maxDistance);
			return nearest;
		}
		std::int32_t jjOBJ::findNearestPlayer(std::int32_t maxDistance) const {
			std::int32_t foundDistance;
			return findNearestPlayerEx(maxDistance, foundDistance);
		}

		bool jjOBJ::doesCollide(const jjOBJ* object, bool always) const {
			noop();
			return false;
		}
		bool jjOBJ::doesCollidePlayer(const jjPLAYER* object, bool always) const {
			noop();
			return false;
		}

		std::uint8_t jjPARTICLEPIXEL::get_color(std::int32_t i) const {
			noop(); return 0;
		}
		std::uint8_t jjPARTICLEPIXEL::set_color(std::int32_t i, std::uint8_t value) {
			noop(); return 0;
		}

		String jjPARTICLESTRING::get_text() const {
			noop(); return {};
		}
		void jjPARTICLESTRING::set_text(String text) {
			noop();
		}

		jjPARTICLE::jjPARTICLE() : _refCount(1) {
			noop();
		}
		jjPARTICLE::~jjPARTICLE() {
			noop();
		}

		void jjPARTICLE::AddRef() {
			_refCount++;
		}

		void jjPARTICLE::Release() {
			if (--_refCount == 0) {
				this->~jjPARTICLE();
				asFreeMem(this);
			}
		}

		jjPLAYER::jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player)
			: _levelScriptLoader(levelScripts), _player(player), _timerCallback(nullptr),
				_timerState(0), _timerLeft(0.0f), _timerPersists(false) {
			noop();
		}
		jjPLAYER::~jjPLAYER() {
			noop();
		}

		jjPLAYER& jjPLAYER::operator=(const jjPLAYER& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		std::int32_t jjPLAYER::get_score() const {
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_score(std::int32_t value) {
			_player->_score = value;
			return _player->_score;
		}
		std::int32_t jjPLAYER::get_scoreDisplayed() const {
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_scoreDisplayed(std::int32_t value) {
			_player->_score = value;
			return _player->_score;
		}

		std::int32_t jjPLAYER::setScore(std::int32_t value) {
			_player->_score = value;
			return _player->_score;
		}

		float jjPLAYER::get_xPos() const {
			return _player->_pos.X;
		}
		float jjPLAYER::set_xPos(float value) {
			_player->_pos.X = value;
			return _player->_pos.X;
		}
		float jjPLAYER::get_yPos() const {
			return _player->_pos.Y;
		}
		float jjPLAYER::set_yPos(float value) {
			_player->_pos.Y = value;
			return _player->_pos.Y;
		}
		float jjPLAYER::get_xAcc() const {
			return _player->_externalForce.X;
		}
		float jjPLAYER::set_xAcc(float value) {
			_player->_externalForce.X = value;
			return _player->_externalForce.X;
		}
		float jjPLAYER::get_yAcc() const {
			return _player->_externalForce.Y;
		}
		float jjPLAYER::set_yAcc(float value) {
			_player->_externalForce.Y = value;
			return _player->_externalForce.Y;
		}
		float jjPLAYER::get_xOrg() const {
			// The "origin" is the position the player respawns at, i.e. the last checkpoint
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::set_xOrg(float value) {
			_player->_checkpointPos.X = value;
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::get_yOrg() const {
			return _player->_checkpointPos.Y;
		}
		float jjPLAYER::set_yOrg(float value) {
			_player->_checkpointPos.Y = value;
			return _player->_checkpointPos.Y;
		}
		float jjPLAYER::get_xSpeed() {
			return _player->_speed.X;
		}
		float jjPLAYER::set_xSpeed(float value) {
			_player->_speed.X = value;
			return value;
		}
		float jjPLAYER::get_ySpeed() {
			return _player->_speed.Y;
		}
		float jjPLAYER::set_ySpeed(float value) {
			_player->_speed.Y = value;
			return value;
		}

		void jjPLAYER::freeze(bool frozen) {
			if (frozen) {
				_player->_frozenTimeLeft = 180.0f;
				_player->_renderer.AnimPaused = true;
			} else if (_player->_frozenTimeLeft > 0.01f) {
				_player->_frozenTimeLeft = 0.01f;
			}
		}
		std::int32_t jjPLAYER::get_currTile() {
			auto pos = _player->_pos;
			return ((std::int32_t)pos.X / 32) + ((std::int32_t)pos.Y / 32 * 65536);
		}
		bool jjPLAYER::startSugarRush(std::int32_t time) {
			noop();
			// TODO: if boss active, return false
			_player->ActivateSugarRush((float)time * 60.0f / 70.0f);
			return true;
		}
		std::int8_t jjPLAYER::get_health() const {
			return (std::int8_t)_player->_health;
		}
		std::int8_t jjPLAYER::set_health(std::int8_t value) {
			_player->SetHealth(value);
			return value;
		}

		std::int32_t jjPLAYER::get_fastfire() const {
			return (_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		}
		std::int32_t jjPLAYER::set_fastfire(std::int32_t value) {
			_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (value << 1));
			return value;
		}

		std::int8_t jjPLAYER::get_currWeapon() const {
			return (std::int8_t)_player->_currentWeapon;
		}
		std::int8_t jjPLAYER::set_currWeapon(std::int8_t value) {
			if (value < 0 || value >= (int8_t)WeaponType::Count) {
				return (std::int8_t)_player->_currentWeapon;
			}
			_player->_currentWeapon = (WeaponType)value;
			return value;
		}

		/*std::int32_t jjPLAYER::get_lives() const {
			return _player->_lives;
		}
		std::int32_t jjPLAYER::set_lives(std::int32_t value) {
			_player->_lives = value;
			return _player->_lives;
		}*/

		std::int32_t jjPLAYER::get_invincibility() const {
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_invincibility(std::int32_t value) {
			_player->_invulnerableTime = value * FrameTimer::FramesPerSecond / 70.0f;
			_player->SetState(Actors::ActorState::IsInvulnerable, _player->_invulnerableTime > 0.0f);
			return value;
		}

		std::int32_t jjPLAYER::get_blink() const {
			return (std::int32_t)(_player->_invulnerableBlinkTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_blink(std::int32_t value) {
			_player->_invulnerableBlinkTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int32_t jjPLAYER::extendInvincibility(std::int32_t duration) {
			// Negative values are clamped so invincibility is never reduced below zero
			_player->_invulnerableTime += duration * FrameTimer::FramesPerSecond / 70.0f;
			if (_player->_invulnerableTime < 0.0f) {
				_player->_invulnerableTime = 0.0f;
			}
			_player->SetState(Actors::ActorState::IsInvulnerable, _player->_invulnerableTime > 0.0f);
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}

		std::int32_t jjPLAYER::get_food() const {
			noop(); return _player->_foodEaten;
		}
		std::int32_t jjPLAYER::set_food(std::int32_t value) {
			_player->_foodEaten = value;
			return _player->_foodEaten;
		}
		std::int32_t jjPLAYER::get_coins() const {
			return _player->_coins;
		}
		std::int32_t jjPLAYER::set_coins(std::int32_t value) {
			_player->_coins = value;
			return _player->_coins;
		}

		bool jjPLAYER::testForCoins(std::int32_t numberOfCoins) {
			if (numberOfCoins > _player->_coins) {
				return false;
			}
			_player->AddCoins(-numberOfCoins);
			return true;
		}
		std::int32_t jjPLAYER::get_gems(std::uint32_t type) const {
			return (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
		}
		std::int32_t jjPLAYER::set_gems(std::uint32_t type, std::int32_t value) {
			if (type < arraySize(_player->_gems)) {
				_player->_gems[type] = value;
			}
			return (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
		}
		bool jjPLAYER::testForGems(std::int32_t numberOfGems, std::uint32_t type) {
			noop();
			std::uint8_t currentCount = (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
			if (numberOfGems <= currentCount) {
				if (numberOfGems > 0) {
					_player->_gems[type] -= numberOfGems;
				}
				return true;
			}
			// TODO: Show warning message
			return false;
		}

		std::int32_t jjPLAYER::get_shieldType() const {
			return (std::int32_t)_player->_activeShield;
		}
		std::int32_t jjPLAYER::set_shieldType(std::int32_t value) {
			if (value >= 0 && value < (std::int32_t)ShieldType::Count) {
				_player->_activeShield = (ShieldType)value;
			}
			return value;
		}
		std::int32_t jjPLAYER::get_shieldTime() const {
			return (std::int32_t)(_player->_activeShieldTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_shieldTime(std::int32_t value) {
			_player->_activeShieldTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int32_t jjPLAYER::get_rolling() const {
			return (std::int32_t)(_player->_inTubeTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_rolling(std::int32_t value) {
		
			if (value > 0) {
				if (_player->_inTubeTime <= 0) {
					_player->EndDamagingMove();
					_player->SetAnimation(AnimState::Dash | AnimState::Jump);
					_player->_controllable = false;
					_player->SetState(Actors::ActorState::CanJump | Actors::ActorState::ApplyGravitation, false);
				}
				_player->_inTubeTime = value * FrameTimer::FramesPerSecond / 70.0f;
			} else {
				if (_player->_inTubeTime > 0) {
					_player->_controllable = true;
					_player->SetState(Actors::ActorState::ApplyGravitation | Actors::ActorState::CollideWithTileset, true);
					_player->_inTubeTime = 0.0f;
				}
			}
		
			return value;
		}

		std::int8_t jjPLAYER::get_playerID() const {
			noop(); return (std::int8_t)_player->_playerIndex;
		}
		std::int32_t jjPLAYER::get_localPlayerID() const {
			noop(); return _player->_playerIndex;
		}

		bool jjPLAYER::get_running() const {
			noop(); return _player->_isRunPressed;
		}
		bool jjPLAYER::set_running(bool value) {
			_player->_isRunPressed = value;
			return _player->_isRunPressed;
		}

		std::int32_t jjPLAYER::get_stoned() {
			return (std::int32_t)(_player->_dizzyTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_stoned(std::int32_t value) {
			_player->SetDizzy(value * FrameTimer::FramesPerSecond / 70.0f);
			return value;
		}

		void jjPLAYER::suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample) {
			noop();
		}
		void jjPLAYER::poleSpin(float xSpeed, float ySpeed, std::uint32_t delay) {
			noop();
		}
		void jjPLAYER::spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample) {
			noop();
		}

		bool jjPLAYER::get_isConnecting() const {
			noop();
			// TODO Whether the player is a client who has not finished joining the current online server yet.
			return false;
		}
		bool jjPLAYER::get_isIdle() const {
			noop();
			// TODO: Whether the player is idle and does not appear in the level or player list. Currently this can only ever be true of the server.
			return false;
		}
		bool jjPLAYER::get_isOut() const {
			noop();
			// TODO: Equals true if the player has lost all their lives (or joined too late) in an LRS-based gamemode.
			return false;
		}
		bool jjPLAYER::get_isSpectating() const {
			noop();
			// TODO: Equals true if the player is spectating normally, i.e. not forced into spectating by being out or an idle server.
			return false;
		}
		bool jjPLAYER::get_isInGame() const {
			noop();
			// TODO: Equals true if isActive is true but isConnecting, isIdle, isOut, and isSpectating are all false.
			return true;
		}

		String jjPLAYER::get_name() const {
			noop();
			return {};
		}
		String jjPLAYER::get_nameUnformatted() const {
			noop();
			return {};
		}
		bool jjPLAYER::setName(const String& name) {
			noop();
			return false;
		}
		std::int8_t jjPLAYER::get_light() const {
			noop();
			return 0;
		}
		std::int8_t jjPLAYER::set_light(std::int8_t value) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::get_fur() const {
			// The engine packs one fur-section start index per byte, matching the JJ2+ 4-byte fur model
			return _player->_furColor;
		}
		std::uint32_t jjPLAYER::set_fur(std::uint32_t value) {
			_player->_furColor = value;
			_player->RefreshColorPalette();
			return value;
		}
		void jjPLAYER::getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const {
			std::uint32_t fur = _player->_furColor;
			a = (std::uint8_t)(fur & 0xFF);
			b = (std::uint8_t)((fur >> 8) & 0xFF);
			c = (std::uint8_t)((fur >> 16) & 0xFF);
			d = (std::uint8_t)((fur >> 24) & 0xFF);
		}
		void jjPLAYER::setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
			_player->_furColor = (std::uint32_t)a | ((std::uint32_t)b << 8) | ((std::uint32_t)c << 16) | ((std::uint32_t)d << 24);
			_player->RefreshColorPalette();
		}

		bool jjPLAYER::get_noFire() const {
			return !_player->_weaponAllowed;
		}
		bool jjPLAYER::set_noFire(bool value) {
			_player->_weaponAllowed = !value;
			return value;
		}
		bool jjPLAYER::get_antiGrav() const {
			return !_player->GetState(Actors::ActorState::ApplyGravitation);
		}
		bool jjPLAYER::set_antiGrav(bool value) {
			_player->SetState(Actors::ActorState::ApplyGravitation, !value);
			return value;
		}
		bool jjPLAYER::get_invisibility() const {
			return _player->GetState(Actors::ActorState::IsInvulnerable);
		}
		bool jjPLAYER::set_invisibility(bool value) {
			noop();
			return false;
		}
		bool jjPLAYER::get_noclipMode() const {
			return !_player->GetState(Actors::ActorState::CollideWithTileset);
		}
		bool jjPLAYER::set_noclipMode(bool value) {
			// Toggles tileset collision so the player can pass through level geometry
			_player->SetState(Actors::ActorState::CollideWithTileset, !value);
			return value;
		}
		std::uint8_t jjPLAYER::get_lighting() const {
			noop();
			// TODO: Viewports
			return (std::uint8_t)(_levelScriptLoader->_levelHandler->GetAmbientLight(_player) * 64.0f);
		}
		std::uint8_t jjPLAYER::set_lighting(std::uint8_t value) {
			_levelScriptLoader->_levelHandler->SetAmbientLight(_player, value / 64.0f);
			return value;
		}
		std::uint8_t jjPLAYER::resetLight() {
			noop();
			return 0;
		}

		bool jjPLAYER::get_playerKeyLeftPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Left);
		}
		bool jjPLAYER::get_playerKeyRightPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Right);
		}
		bool jjPLAYER::get_playerKeyUpPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Up);
		}
		bool jjPLAYER::get_playerKeyDownPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Down);
		}
		bool jjPLAYER::get_playerKeyFirePressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Fire);
		}
		bool jjPLAYER::get_playerKeySelectPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::ChangeWeapon);
		}
		bool jjPLAYER::get_playerKeyJumpPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Jump);
		}
		bool jjPLAYER::get_playerKeyRunPressed() {
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Run);
		}
		void jjPLAYER::set_playerKeyLeftPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyRightPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyUpPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyDownPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyFirePressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeySelectPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyJumpPressed(bool value) {
			noop();
		}
		void jjPLAYER::set_playerKeyRunPressed(bool value) {
			noop();
		}

		bool jjPLAYER::get_powerup(std::uint8_t index) {
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			return (_player->_weaponUpgrades[index] & 0x01) == 0x01;
		}
		bool jjPLAYER::set_powerup(std::uint8_t index, bool value) {
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			_player->_weaponUpgrades[index] = (value ? 0x01 : 0x00);
			return value;
		}
		std::int32_t jjPLAYER::get_ammo(std::uint8_t index) const {
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			return _player->_weaponAmmo[index];
		}
		std::int32_t jjPLAYER::set_ammo(std::uint8_t index, std::int32_t value) {
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			_player->_weaponAmmo[index] = value * 256;
			return value;
		}

		bool jjPLAYER::offsetPosition(std::int32_t xPixels, std::int32_t yPixels) {

			Vector2f pos = _player->GetPos();
			_player->WarpToPosition(Vector2f(pos.X + xPixels, pos.Y + yPixels), WarpFlags::Fast);
			return true;
		}
		bool jjPLAYER::warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast) {

			_player->WarpToPosition(Vector2f(xTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2,
				yTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2),
				fast ? WarpFlags::Fast : WarpFlags::Default);
			return true;
		}
		bool jjPLAYER::warpToID(std::uint8_t warpID, bool fast) {

			auto events = _levelScriptLoader->_levelHandler->EventMap();
			Vector2f c = events->GetWarpTarget(warpID);
			if (c.X >= 0.0f && c.Y >= 0.0f) {
				_player->WarpToPosition(c, fast ? WarpFlags::Fast : WarpFlags::Default);
				return true;
			}
			return false;
		}

		std::uint32_t jjPLAYER::morph(bool rabbitsOnly, bool morphEffect) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::morphTo(std::uint32_t charNew, bool morphEffect) {
			noop();
			// TODO: morphEffect
			_player->MorphTo((PlayerType)charNew);
			return (std::uint32_t)_player->_playerType;
		}
		std::uint32_t jjPLAYER::revertMorph(bool morphEffect) {
			noop();
			// TODO: morphEffect
			_player->MorphRevert();
			return (std::uint32_t)_player->_playerType;
		}
		std::uint32_t jjPLAYER::get_charCurr() const {
			return (std::uint32_t)_player->_playerType;
		}

		void jjPLAYER::kill() {
			_player->DecreaseHealth(INT32_MAX);
		}
		bool jjPLAYER::hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker) {
			noop();

			// TODO: forceHurt and return value
			_player->TakeDamage(damage);
			return false;
		}

		std::uint32_t jjPLAYER::get_timerState() const {
			return _timerState;
		}
		bool jjPLAYER::get_timerPersists() const {
			return _timerPersists;
		}
		bool jjPLAYER::set_timerPersists(bool value) {
			_timerPersists = value;
			return value;
		}
		std::uint32_t jjPLAYER::timerStart(std::int32_t ticks, bool startPaused) {
			_timerLeft = (float)ticks * FrameTimer::FramesPerSecond / 70;
			_timerState = (startPaused ? 2 : 1);
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerPause() {
			_timerState = 2; // PAUSED
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerResume() {
			_timerState = 1; // STARTED
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerStop() {
			_timerLeft = 0.0f;
			_timerState = 0; // STOPPED
			return _timerState;
		}
		std::int32_t jjPLAYER::get_timerTime() const {
			std::int32_t jjTicksLeft = (std::int32_t)(_timerLeft * 70 / FrameTimer::FramesPerSecond);
			return jjTicksLeft;
		}
		std::int32_t jjPLAYER::set_timerTime(std::int32_t value) {
			_timerLeft = (float)value * FrameTimer::FramesPerSecond / 70;
			return value;
		}
		void jjPLAYER::timerFunction(const String& functionName) {
			asIScriptFunction* func = _levelScriptLoader->GetMainModule()->GetFunctionByName(String::nullTerminatedView(functionName).data());
			_timerCallback = func;
		}
		void jjPLAYER::timerFunctionPtr(void* function) {
			_timerCallback = function;
		}
		void jjPLAYER::timerFunctionFuncPtr(void* function) {
			_timerCallback = function;
		}

		bool jjPLAYER::activateBoss(bool activate) {
			noop();

			// TODO: activate
			_levelScriptLoader->_levelHandler->BroadcastTriggeredEvent(_player, EventType::AreaActivateBoss, nullptr);
			return true;
		}
		bool jjPLAYER::limitXScroll(std::uint16_t left, std::uint16_t width) {

			_levelScriptLoader->_levelHandler->LimitCameraView(_player, _player->_pos, left * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
			return true;
		}
		void jjPLAYER::cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xPixel, yPixel, !centered);
		}
		void jjPLAYER::cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, yPixel, !centered);
		}
		void jjPLAYER::cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xPixel, INFINITY, !centered);
		}
		void jjPLAYER::cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant) {
			noop();
			// TODO: instant, centered
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, INFINITY, false);
		}
		void jjPLAYER::cameraUnfreeze(bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, INFINITY, false);
		}
		void jjPLAYER::showText(const String& text, std::uint32_t size) {
			noop();

			// TODO: size
			// Input string must be recoded in Legacy context
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			_levelScriptLoader->_levelHandler->ShowLevelText(recodedText);
		}
		void jjPLAYER::showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size) {
			noop();

			// TODO: size
			auto text = _levelScriptLoader->_levelHandler->GetLevelText(textID, offset, '|');
			_levelScriptLoader->_levelHandler->ShowLevelText(text);
		}

		std::uint32_t jjPLAYER::get_fly() const {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::set_fly(std::uint32_t value) {
			noop();
			return 0;
		}

		std::int32_t jjPLAYER::fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction) {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle) {
			noop();
			return 0;
		}

		float jjPLAYER::get_cameraX() const {
			return _levelScriptLoader->_levelHandler->GetCameraPos(_player).X;
		}
		float jjPLAYER::get_cameraY() const {
			return _levelScriptLoader->_levelHandler->GetCameraPos(_player).Y;
		}
		std::int32_t jjPLAYER::get_deaths() const {
			noop();
			return 0;
		}

		bool jjPLAYER::get_isJailed() const {
			noop();
			return false;
		}
		bool jjPLAYER::get_isZombie() const {
			noop();
			return false;
		}
		std::int32_t jjPLAYER::get_lrsLives() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_roasts() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_laps() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeCurrent() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimes(std::uint32_t index) const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeBest() const {
			noop();
			return 0;
		}
		bool jjPLAYER::get_isAdmin() const {
			noop();
			return false;
		}
		bool jjPLAYER::hasPrivilege(const String& privilege, std::uint32_t moduleID) const {
			noop();
			return false;
		}

		bool jjPLAYER::doesCollide(const jjOBJ* object, bool always) const {
			noop();
			return false;
		}
		int jjPLAYER::getObjectHitForce(const jjOBJ& target) const {
			noop();
			return 0;
		}
		bool jjPLAYER::objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling) {
			noop();
			return false;
		}
		bool jjPLAYER::isEnemy(const jjPLAYER* victim) const {
			noop();
			return false;
		}

		void jjPLAYER::SyncPropertiesToBackingStore() {
			noop();
			// TODO
			_backingStoreDirty = true;

			lives = _player->_lives;
		}
		void jjPLAYER::SyncPropertiesFromBackingStore() {
			noop();
			// TODO
			if (!_backingStoreDirty) {
				return;
			}

			if (lives != _player->_lives) {
				LOGW("SYNC");
			}

			lives = _player->_lives;
		}

		jjPIXELMAP::jjPIXELMAP() : _refCount(1) {
			noop();
		}
		jjPIXELMAP::~jjPIXELMAP() {
			noop();
		}

		jjPIXELMAP* jjPIXELMAP::CreateFromTile() {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFrame(const jjANIMFRAME* animFrame) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromTexture(std::uint32_t animFrame) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}

		void jjPIXELMAP::AddRef()
		{
			_refCount++;
		}

		void jjPIXELMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjPIXELMAP();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjPIXELMAP& jjPIXELMAP::operator=(const jjPIXELMAP& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		// TODO: return type std::uint8_t& instead?
		std::uint8_t jjPIXELMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			noop();
			return 0;
		}

		bool jjPIXELMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
			noop();
			return false;
		}
		bool jjPIXELMAP::saveToFrame(jjANIMFRAME* frame) const {
			noop();
			return false;
		}
		bool jjPIXELMAP::saveToFile(const String& filename, const jjPAL& palette) const {
			noop();
			return false;
		}

		jjMASKMAP::jjMASKMAP() : _refCount(1) {
			noop();
		}
		jjMASKMAP::~jjMASKMAP() {
			noop();
		}

		jjMASKMAP* jjMASKMAP::CreateFromBool(bool filled) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjMASKMAP));
			return new(mem) jjMASKMAP();
		}
		jjMASKMAP* jjMASKMAP::CreateFromTile(std::uint16_t tileID) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjMASKMAP));
			return new(mem) jjMASKMAP();
		}

		void jjMASKMAP::AddRef()
		{
			_refCount++;
		}

		void jjMASKMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjMASKMAP();
				asFreeMem(this);
			}
		}

		jjMASKMAP& jjMASKMAP::operator=(const jjMASKMAP& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		// TODO: return type bool& instead?
		bool jjMASKMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			noop();
			return false;
		}

		bool jjMASKMAP::save(std::uint16_t tileID, bool hFlip) const {
			noop();
			return false;
		}

		jjLAYER::jjLAYER() : _refCount(1) {
			noop();
		}
		jjLAYER::~jjLAYER() {
			noop();
		}

		jjLAYER* jjLAYER::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			// A standalone layer (not part of the level); _layerIndex stays -1 so the per-frame sync and tile access skip it
			void* mem = asAllocMem(sizeof(jjLAYER));
			auto* layer = new(mem) jjLAYER();
			layer->width = layer->widthReal = (std::int32_t)width;
			layer->widthRounded = ((std::int32_t)width + 3) / 4;
			layer->height = (std::int32_t)height;
			return layer;
		}
		jjLAYER* jjLAYER::CreateCopy(jjLAYER* other) {

			void* mem = asAllocMem(sizeof(jjLAYER));
			auto* layer = new(mem) jjLAYER();
			if (other != nullptr) {
				*layer = *other;		// copies the public fields (operator= leaves _refCount/_layerIndex intact)
			}
			return layer;
		}

		void jjLAYER::AddRef()
		{
			_refCount++;
		}

		void jjLAYER::Release()
		{
			if (--_refCount == 0) {
				this->~jjLAYER();
				asFreeMem(this);
			}
		}

		jjLAYER& jjLAYER::operator=(const jjLAYER& o)
		{
			// Copy the public data only; _refCount stays ours and _layerIndex stays unset (a copy is a standalone layer,
			// not bound to the engine, so writes to it don't affect the level)
			width = o.width; widthReal = o.widthReal; widthRounded = o.widthRounded; height = o.height;
			xSpeed = o.xSpeed; ySpeed = o.ySpeed; xAutoSpeed = o.xAutoSpeed; yAutoSpeed = o.yAutoSpeed;
			xOffset = o.xOffset; yOffset = o.yOffset;
			xInnerSpeed = o.xInnerSpeed; yInnerSpeed = o.yInnerSpeed;
			xInnerAutoSpeed = o.xInnerAutoSpeed; yInnerAutoSpeed = o.yInnerAutoSpeed;
			rotationAngle = o.rotationAngle; rotationRadiusMultiplier = o.rotationRadiusMultiplier;
			tileHeight = o.tileHeight; tileWidth = o.tileWidth; limitVisibleRegion = o.limitVisibleRegion;
			hasTileMap = o.hasTileMap; hasTiles = o.hasTiles;
			SpeedModeX = o.SpeedModeX; SpeedModeY = o.SpeedModeY;
			return *this;
		}

		jjLAYER* jjLAYER::get_jjLayers(std::int32_t index) {
			// Returns the persistent proxy bound to engine layer `index` (already AddRef'd for the script); the loader
			// pushes the proxy's writable fields into the engine layer every frame
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjLAYER* layer = owner->GetLayerProxy(index);
			if (layer == nullptr) {
				// Out-of-range index: hand back an empty standalone layer rather than null so scripts don't crash
				void* mem = asAllocMem(sizeof(jjLAYER));
				layer = new(mem) jjLAYER();
			}
			return layer;
		}

		std::uint32_t jjLAYER::get_spriteMode() const {
			noop();
			return 0;
		}
		std::uint32_t jjLAYER::set_spriteMode(std::uint32_t value) const {
			noop();
			return 0;
		}
		std::uint8_t jjLAYER::get_spriteParam() const {
			noop();
			return 0;
		}
		std::uint8_t jjLAYER::set_spriteParam(std::uint8_t value) const {
			noop();
			return 0;
		}

		void jjLAYER::setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const {
			// const_cast: registered const, but it mutates the proxy fields the loader syncs to the engine each frame
			jjLAYER* self = const_cast<jjLAYER*>(this);
			if (newSpeedIsAnAutoSpeed) {
				self->xAutoSpeed = newspeed;
			} else {
				self->xSpeed = newspeed;
			}
		}
		void jjLAYER::setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const {
			jjLAYER* self = const_cast<jjLAYER*>(this);
			if (newSpeedIsAnAutoSpeed) {
				self->yAutoSpeed = newspeed;
			} else {
				self->ySpeed = newspeed;
			}
		}
		float jjLAYER::getXPosition(const jjPLAYER* play) const {
			// Approximate the layer's scrolled pixel position: parallax = camera * speed, plus the static offset.
			// (Auto-speed accumulation is tracked inside the engine and isn't reflected here.)
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			Actors::Player* player = (play != nullptr ? play->_player : nullptr);
			float cameraX = owner->GetLevelHandler()->GetCameraPos(player).X;
			return cameraX * xSpeed + xOffset;
		}
		float jjLAYER::getYPosition(const jjPLAYER* play) const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			Actors::Player* player = (play != nullptr ? play->_player : nullptr);
			float cameraY = owner->GetLevelHandler()->GetCameraPos(player).Y;
			return cameraY * ySpeed + yOffset;
		}

		std::int32_t jjLAYER::GetTextureMode() const {
			noop();
			return 0;
		}
		void jjLAYER::SetTextureMode(std::int32_t value) const {
			noop();
		}
		std::int32_t jjLAYER::GetTexture() const {
			noop();
			return 0;
		}
		void jjLAYER::SetTexture(std::int32_t value) const {
			noop();
		}

		CScriptArray* jjLAYER::jjLayerOrderGet() {
			// Returns the level's layers (in engine index order) as proxies, so scripts can enumerate jjLayers
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			std::int32_t count = (tileMap != nullptr ? tileMap->GetLayerCount() : 0);
			asITypeInfo* arrayType = owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>");
			CScriptArray* arr = CScriptArray::Create(arrayType, (std::uint32_t)count);
			for (std::int32_t i = 0; i < count; i++) {
				jjLAYER* proxy = owner->GetLayerProxy(i);	// AddRef'd for us
				if (proxy != nullptr) {
					arr->SetValue((std::uint32_t)i, &proxy);	// AddRefs the handle into the array
					proxy->Release();							// drop our extra reference
				}
			}
			return arr;
		}
		bool jjLAYER::jjLayerOrderSet(const CScriptArray& order) {
			noop();
			return false;
		}
		CScriptArray* jjLAYER::jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, std::int32_t tileIDAdjustmentFactor) {

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return CScriptArray::Create(owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>"), 16);
		}
		bool jjLAYER::jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping) {
			noop();
			return false;
		}

		std::uint16_t jjLAYER::tileGet(int xTile, int yTile) {
			// Reads the packed tile value from the bound engine layer (same packed format as jjTileGet / TileMap::GetTile)
			if (_layerIndex < 0) {
				return 0;	// standalone layer: no engine-backed tile data
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			return (tileMap != nullptr ? tileMap->GetTile(_layerIndex, xTile, yTile) : 0);
		}
		std::uint16_t jjLAYER::tileSet(int xTile, int yTile, std::uint16_t newTile) {
			if (_layerIndex < 0) {
				return 0;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap != nullptr) {
				tileMap->SetTile(_layerIndex, xTile, yTile, newTile);
			}
			return newTile;
		}
		void jjLAYER::generateSettableTileArea(int xTile, int yTile, int width, int height) {
			// In this engine every tile is already directly settable via tileSet, so there's no separate settable area to carve out
		}
		void jjLAYER::generateSettableTileAreaAll() {
			// No-op for the same reason as generateSettableTileArea
		}

		bool jjPLAYERDRAW::get_shield(std::int32_t shield) const {
			noop();
			return false;
		}
		bool jjPLAYERDRAW::set_shield(std::int32_t shield, bool enable) {
			noop();
			return false;
		}
		jjPLAYER* jjPLAYERDRAW::get_player() const {
			noop();
			return nullptr;
		}
	}

	jjOBJ* get_jjObjects(std::int32_t index)
	{
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto* actor = owner->GetScriptObject(index);

		// Script-controlled objects return their persistent, writable jjOBJ (so edits affect the live object)
		if (auto* wrapper = runtime_cast<Legacy::ScriptLegacyObject>(actor)) {
			jjOBJ* obj = wrapper->GetObject();
			obj->AddRef();
			return obj;
		}

		// Otherwise (a natively-spawned object or an unknown ID) return a read-only position snapshot
		void* mem = asAllocMem(sizeof(jjOBJ));
		auto* obj = new(mem) jjOBJ();
		obj->objectID = (std::int16_t)index;
		if (actor != nullptr) {
			Vector2f pos = actor->GetPos();
			Vector2f speed = actor->GetSpeed();
			obj->xPos = obj->xOrg = pos.X;
			obj->yPos = obj->yOrg = pos.Y;
			obj->xSpeed = speed.X;
			obj->ySpeed = speed.Y;
		}
		return obj;
	}

	jjOBJ* get_jjObjectPresets(std::int8_t id)
	{
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjOBJ));
		return new(mem) jjOBJ();
	}

	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle) {
		noop();
	}
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode) {
		noop();
	}

	jjPARTICLE* GetParticle(std::int32_t index) {
		noop();
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	jjPARTICLE* AddParticle(std::int32_t particleType) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	std::int32_t get_jjPlayerCount() {
		noop();
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}
	std::int32_t get_jjLocalPlayerCount() {
		noop();
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}

	jjPLAYER* get_jjP() {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(0);
	}
	jjPLAYER* get_jjPlayers(std::uint8_t index) {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index) {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}

	bool mlleSetup() {
		noop();
		return true;
	}

	void mlleReapplyPalette() {
		noop();
	}

	void mlleSpawnOffgrids() {
		noop();
	}

	void mlleSpawnOffgridsLocal() {
		noop();
	}

	float get_sinTable(std::uint32_t angle) {
		return sinf(angle * fTwoPi / 1024.0f);
	};
	float get_cosTable(std::uint32_t angle) {
		return cosf(angle * fTwoPi / 1024.0f);
	};
	std::uint32_t RandWord32() {
		return Random().Next();
	}
	std::uint64_t unixTimeSec() {
		return Containers::DateTime::UtcNow().ToUnixMilliseconds() / 1000;
	}
	std::uint64_t unixTimeMs() {
		return Containers::DateTime::UtcNow().ToUnixMilliseconds();
	}

	bool jjRegexIsValid(const String& expression) {

		try {
			std::regex r(expression.begin(), expression.end());
			return true;
		} catch (std::regex_error& e) {
			return false;
		}
	}
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase) {
		
		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			return std::regex_match(text.begin(), text.end(), r);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: {}", e.what());
			return false;
		}
	}
	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase) {

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::match_results<const char*> m;
			std::regex r(expression.begin(), expression.end(), flags);
			bool success = std::regex_match(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: {}", e.what());
			return false;
		}
	}
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase) {

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			return std::regex_search(text.begin(), text.end(), r);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: {}", e.what());
			return false;
		}
	}
	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase) {

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::match_results<const char*> m;
			std::regex r(expression.begin(), expression.end(), flags);
			bool success = std::regex_search(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: {}", e.what());
			return false;
		}
	}
	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase) {

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			std::string textStr = text;
			std::string replacementStr = replacement;
			return std::regex_replace(textStr, r, replacementStr);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: {}", e.what());
			return text;
		}
	}

	std::int32_t GetFPS() {
		return (std::int32_t)theApplication().GetFrameTimer().GetAverageFps();
	}

	bool isAdmin() {
		noop(); return false;
	}

	std::int32_t LevelScriptLoader::GetDifficulty() {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		switch (_this->_levelHandler->_difficulty) {
			case GameDifficulty::Easy: return 0;
			default:
			case GameDifficulty::Normal: return 1;
			case GameDifficulty::Hard: return 2;
		}
	}
	std::int32_t LevelScriptLoader::SetDifficulty(std::int32_t value) {

		if (value >= 0 && value <= 2) {
			auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			_this->_levelHandler->_difficulty = (GameDifficulty)value;
		}
		return value;
	}

	String getLevelFileName() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetLevelHandler()->GetLevelName();
	}
	String getCurrLevelName() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetLevelHandler()->GetLevelDisplayName();
	}
	void setCurrLevelName(const String& in) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->GetLevelHandler()->SetLevelDisplayName(in);
	}
	String LevelScriptLoader::get_jjMusicFileName() {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_musicCurrentPath;
	}
	String get_jjTilesetFileName() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap == nullptr) {
			return "";
		}
		auto paths = tileMap->GetUsedTileSetPaths();
		return (paths.empty() ? String{} : String(paths[0]));
	}

	String LevelScriptLoader::get_jjHelpStrings(std::uint32_t index) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetLevelText(index);
	}
	void LevelScriptLoader::set_jjHelpStrings(std::uint32_t index, const String& text) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->OverrideLevelText(index, text);
	}

	std::int32_t get_gameState() {
		noop(); return 0;
	}

	// TODO

	void LevelScriptLoader::jjAlert(const String& text, bool sendToAll, std::uint32_t size)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->ShowLevelText(text);
	}
	void LevelScriptLoader::jjPrint(const String& text, bool timestamp) {
		LOGW("{}", text);
	}
	void LevelScriptLoader::jjDebug(const String& text, bool timestamp) {
		LOGD("{}", text);
	}
	void LevelScriptLoader::jjChat(const String& text, bool teamchat) {
		noop();
		LOGW("{}", text);

		if (text.hasPrefix('/')) {
			// TODO: Process command
			return;
		}

		// TODO: teamchat
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Info, text);
	}
	void LevelScriptLoader::jjConsole(const String& text, bool sendToAll) {
		noop();
		LOGW("{}", text);

		// TODO: sendToAll
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Important, text);
	}
	void LevelScriptLoader::jjSpy(const String& text) {
		LOGD("{}", text);
	}

	// TODO

	std::int32_t getBorderWidth() {
		noop(); return 0;
	}
	std::int32_t getBorderHeight() {
		noop(); return 0;
	}
	bool getSplitscreenType() {
		noop(); return false;
	}
	bool setSplitscreenType(bool value) {
		noop(); return false;
	}

	// TODO

	std::int32_t get_teamScore(std::int32_t color) {
		noop(); return 0;
	}
	std::int32_t GetMaxHealth() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto players = owner->GetPlayers();
		return (players.size() == 0 ? 5 : players[0]->GetMaxHealth());
	}
	std::int32_t GetStartHealth() {
		// Players spawn at full health, so the starting health matches the maximum health
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto players = owner->GetPlayers();
		return (players.size() == 0 ? 5 : players[0]->GetMaxHealth());
	}

	// TODO

	float LevelScriptLoader::get_layerXOffset(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.OffsetX : 0);
	}
	float LevelScriptLoader::set_layerXOffset(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.OffsetX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYOffset(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.OffsetY : 0);
	}
	float LevelScriptLoader::set_layerYOffset(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.OffsetY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	std::int32_t LevelScriptLoader::get_layerWidth(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].LayoutSize.X : 0);
	}
	std::int32_t LevelScriptLoader::get_layerRealWidth(std::uint8_t id) {
		return get_layerWidth(id);
	}
	std::int32_t LevelScriptLoader::get_layerRoundedWidth(std::uint8_t id) {
		return (get_layerWidth(id) + 3) / 4;
	}
	std::int32_t LevelScriptLoader::get_layerHeight(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].LayoutSize.Y : 0);
	}
	float LevelScriptLoader::get_layerXSpeed(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.SpeedX : 0.0f);
	}
	float LevelScriptLoader::set_layerXSpeed(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.SpeedX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYSpeed(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.SpeedY : 0.0f);
	}
	float LevelScriptLoader::set_layerYSpeed(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.SpeedY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerXAutoSpeed(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.AutoSpeedX : 0.0f);
	}
	float LevelScriptLoader::set_layerXAutoSpeed(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.AutoSpeedX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYAutoSpeed(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.AutoSpeedY : 0.0f);
	}
	float LevelScriptLoader::set_layerYAutoSpeed(std::uint8_t id, float value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.AutoSpeedY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	bool LevelScriptLoader::get_layerHasTiles(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Visible);
	}
	bool LevelScriptLoader::set_layerHasTiles(std::uint8_t id, bool value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Visible = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerTileHeight(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.RepeatY);
	}
	bool LevelScriptLoader::set_layerTileHeight(std::uint8_t id, bool value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.RepeatY = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerTileWidth(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.RepeatX);
	}
	bool LevelScriptLoader::set_layerTileWidth(std::uint8_t id, bool value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.RepeatX = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerLimitVisibleRegion(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.UseInherentOffset);
	}
	bool LevelScriptLoader::set_layerLimitVisibleRegion(std::uint8_t id, bool value) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.UseInherentOffset = value;
			return value;
		} else {
			return false;
		}
	}

	void LevelScriptLoader::setLayerXSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			if (newSpeedIsAnAutoSpeed) {
				tileMap->_layers[id].Description.AutoSpeedX = newspeed;
			} else {
				tileMap->_layers[id].Description.SpeedX = newspeed;
			}
		}
	}
	void LevelScriptLoader::setLayerYSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			if (newSpeedIsAnAutoSpeed) {
				tileMap->_layers[id].Description.AutoSpeedY = newspeed;
			} else {
				tileMap->_layers[id].Description.SpeedY = newspeed;
			}
		}
	}

	// TODO

	void LevelScriptLoader::jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	std::int32_t LevelScriptLoader::jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style) {
		//noop();
		
		float scale;
		switch (size) {
			case 1:	// SMALL
				scale = 0.6f;
				break;
			case 2:	// LARGE
				scale = 1.1f;
				break;
			default:
			case 0:	// MEDIUM
				scale = 0.8f;
				break;
		}

		// The font lives in ContentResolver (the HUD just borrows it), so this works even when called during script
		// initialization / onLevelLoad, before the HUD has been created
		auto font = ContentResolver::Get().GetFont(FontType::Small);
		if (font == nullptr) {
			return 0;
		}
		auto measuredSize = font->MeasureString(text, scale);
		return (std::int32_t)measuredSize.X;
	}

	void jjSetDarknessColor(jjPALCOLOR color) {
		noop();
	}
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
		noop();
	}
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID) {
		noop();
	}
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color) {
		noop();
	}
	jjPALCOLOR jjGetFadeColors() {
		noop(); return {};
	}
	void jjUpdateTexturedBG() {
		noop();
	}
	std::int32_t get_jjTexturedBGTexture() {
		noop(); return 0;
	}
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture) {
		noop(); return 0;
	}
	std::int32_t get_jjTexturedBGStyle() {
		noop(); return 0;
	}
	std::int32_t set_jjTexturedBGStyle(std::int32_t style) {
		noop(); return 0;
	}
	bool get_jjTexturedBGUsed() {
		noop(); return false;
	}
	bool set_jjTexturedBGUsed(bool used) {
		noop(); return false;
	}
	bool get_jjTexturedBGStars() {
		noop(); return false;
	}
	bool set_jjTexturedBGStars(bool used) {
		noop(); return false;
	}
	float get_jjTexturedBGFadePositionX() {
		noop(); return 0.0f;
	}
	float set_jjTexturedBGFadePositionX(float value) {
		noop(); return 0.0f;
	}
	float get_jjTexturedBGFadePositionY() {
		noop(); return 0.0f;
	}
	float set_jjTexturedBGFadePositionY(float value) {
		noop(); return 0.0f;
	}

	bool LevelScriptLoader::get_jjTriggers(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetTrigger(id);
	}
	bool LevelScriptLoader::set_jjTriggers(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, value);
		return value;
	}
	bool LevelScriptLoader::jjSwitchTrigger(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, !_this->_levelHandler->GetTrigger(id));
		return _this->_levelHandler->GetTrigger(id);
	}

	bool LevelScriptLoader::isNumberedASFunctionEnabled(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_enabledCallbacks[id];
	}
	bool LevelScriptLoader::setNumberedASFunctionEnabled(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.set(id, value);
		return value;
	}
	void LevelScriptLoader::reenableAllNumberedASFunctions() {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.setAll();
	}

	float LevelScriptLoader::getWaterLevel() {
		noop();

		// TODO
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}
	float LevelScriptLoader::getWaterLevel2() {
		noop();

		// TODO
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}
	float LevelScriptLoader::setWaterLevel(float value, bool instant) {
		noop();

		// TODO: instant
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_waterLevel = value;
		return value;
	}
	float LevelScriptLoader::get_waterChangeSpeed() {
		noop(); return 0;
	}
	float LevelScriptLoader::set_waterChangeSpeed(float value) {
		noop(); return 0;
	}
	std::int32_t LevelScriptLoader::get_waterLayer() {
		noop(); return 0;
	}
	std::int32_t LevelScriptLoader::set_waterLayer(std::int32_t value) {
		noop(); return 0;
	}
	void LevelScriptLoader::setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientToTBG() {
		noop();
	}
	void LevelScriptLoader::resetWaterGradient() {
		noop();
	}

	void LevelScriptLoader::triggerRock(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint8_t eventParams[] = { id };
		_this->_levelHandler->BroadcastTriggeredEvent(nullptr, EventType::RollingRockTrigger, eventParams);
	}

	void LevelScriptLoader::cycleTo(const String& filename, bool warp, bool fast) {
		noop();
	}
	void LevelScriptLoader::jjNxt(bool warp, bool fast)
	{
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginLevelChange(nullptr, exitType, {});
	}

	bool getEnabledTeam(std::uint8_t team) {
		noop();
		return false;
	}

	bool getKeyDown(std::uint8_t key) {
		//noop();
		return false;
	}
	std::int32_t getCursorX() {
		//noop();
		return 0;
	}
	std::int32_t getCursorY() {
		//noop();
		return 0;
	}

	bool LevelScriptLoader::jjMusicLoad(const String& filename, bool forceReload, bool temporary) {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginPlayMusic(filename, !temporary, forceReload);
#endif
		return false;
	}
	void LevelScriptLoader::jjMusicStop() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPlay() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->play();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPause() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicResume() {

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->isPaused()) {
			_this->_levelHandler->_music->play();
		}
#endif
	}

	LevelScriptLoader::~LevelScriptLoader()
	{
		// Release the reference each cached jjLAYER proxy holds (the script side may still hold its own references)
		for (auto& pair : _layerProxies) {
			if (pair.second != nullptr) {
				pair.second->Release();
			}
		}
	}

	Legacy::jjLAYER* LevelScriptLoader::GetLayerProxy(std::int32_t index)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || index < 0 || index >= (std::int32_t)tileMap->_layers.size()) {
			return nullptr;
		}

		Legacy::jjLAYER* layer;
		auto it = _layerProxies.find(index);
		if (it != _layerProxies.end()) {
			layer = it->second;
		} else {
			// First access: create the proxy and populate it from the engine layer
			void* mem = asAllocMem(sizeof(Legacy::jjLAYER));
			layer = new(mem) Legacy::jjLAYER();
			layer->_layerIndex = index;

			const auto& src = tileMap->_layers[index];
			layer->width = layer->widthReal = src.LayoutSize.X;
			layer->widthRounded = (src.LayoutSize.X + 3) / 4;
			layer->height = src.LayoutSize.Y;
			layer->xSpeed = src.Description.SpeedX;
			layer->ySpeed = src.Description.SpeedY;
			layer->xAutoSpeed = src.Description.AutoSpeedX;
			layer->yAutoSpeed = src.Description.AutoSpeedY;
			layer->xOffset = layer->_syncedOffsetX = src.Description.OffsetX;
			layer->yOffset = layer->_syncedOffsetY = src.Description.OffsetY;
			layer->tileWidth = src.Description.RepeatX;
			layer->tileHeight = src.Description.RepeatY;
			layer->SpeedModeX = (std::int32_t)src.Description.SpeedModelX;
			layer->SpeedModeY = (std::int32_t)src.Description.SpeedModelY;
			layer->hasTileMap = true;
			layer->hasTiles = (src.Layout != nullptr);

			_layerProxies.emplace(index, layer);
			// The map keeps one reference; AddRef again below for the one we hand to the script
		}

		layer->AddRef();
		return layer;
	}

	void LevelScriptLoader::SyncLayerProperties()
	{
		if (_layerProxies.empty()) {
			return;	// no script has touched jjLayers - nothing to push
		}
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr) {
			return;
		}
		for (auto& pair : _layerProxies) {
			std::int32_t index = pair.first;
			Legacy::jjLAYER* layer = pair.second;
			if (layer == nullptr || index < 0 || index >= (std::int32_t)tileMap->_layers.size()) {
				continue;
			}
			// These are static config the engine never changes itself, so a straight proxy -> engine push is safe
			auto& desc = tileMap->_layers[index].Description;
			desc.SpeedX = layer->xSpeed;
			desc.SpeedY = layer->ySpeed;
			desc.AutoSpeedX = layer->xAutoSpeed;
			desc.AutoSpeedY = layer->yAutoSpeed;
			desc.RepeatX = layer->tileWidth;
			desc.RepeatY = layer->tileHeight;
			desc.SpeedModelX = (Tiles::LayerSpeedModel)layer->SpeedModeX;
			desc.SpeedModelY = (Tiles::LayerSpeedModel)layer->SpeedModeY;

			// The engine advances OffsetX/Y itself for auto-scrolling layers (TileMap::OnUpdate), so only push when the
			// script actually changed the proxy; otherwise pull the engine's current value so reads stay live
			if (layer->xOffset != layer->_syncedOffsetX) {
				desc.OffsetX = layer->xOffset;
			} else {
				layer->xOffset = desc.OffsetX;
			}
			layer->_syncedOffsetX = layer->xOffset;
			if (layer->yOffset != layer->_syncedOffsetY) {
				desc.OffsetY = layer->yOffset;
			} else {
				layer->yOffset = desc.OffsetY;
			}
			layer->_syncedOffsetY = layer->yOffset;
		}
	}

	AudioBuffer* LevelScriptLoader::ResolveSampleBuffer(std::int32_t sampleId)
	{
#if defined(WITH_AUDIO)
		auto it = _scriptSamples.find(sampleId);
		if (it != _scriptSamples.end()) {
			// A cached null entry means the sample previously failed to load - don't keep retrying
			return it->second.get();
		}

		// The sample mapping is the same one the content pipeline uses to convert the original sound bank, so the
		// original sample index maps directly to the converted "Animations/{Category}/{Name}.wav" asset
		if (_sampleMapping == nullptr) {
			Compatibility::JJ2Version version = (versionTSF ? Compatibility::JJ2Version::TSF : Compatibility::JJ2Version::BaseGame);
			_sampleMapping = std::make_unique<Compatibility::AnimSetMapping>(Compatibility::AnimSetMapping::GetSampleMapping(version));
		}

		std::unique_ptr<AudioBuffer> buffer;
		auto* entry = _sampleMapping->GetByOrdinal((std::uint32_t)sampleId);
		if (entry != nullptr && entry->Category != Compatibility::AnimSetMapping::Discard && !entry->Name.empty()) {
			String path = String("Animations/"_s) + entry->Category + '/' + entry->Name + ".wav"_s;
			auto stream = ContentResolver::Get().OpenContentFile(path);
			if (stream->IsValid()) {
				buffer = std::make_unique<AudioBuffer>(std::move(stream), path);
			}
		}

		AudioBuffer* result = buffer.get();
		_scriptSamples.emplace(sampleId, std::move(buffer));
		return result;
#else
		return nullptr;
#endif
	}

	std::shared_ptr<AudioBufferPlayer> LevelScriptLoader::PlaySample(const Vector2f& pos, std::int32_t sampleId, std::int32_t volume, std::int32_t frequency, bool sourceRelative, std::int32_t loopChannel)
	{
#if defined(WITH_AUDIO)
		AudioBuffer* buffer = ResolveSampleBuffer(sampleId);
		if (buffer == nullptr) {
			return nullptr;
		}

		// JJ2 volume is roughly 0-64 (0x40 = full); frequency 0 means the sample's natural rate
		float gain = (volume <= 0 ? 0.0f : volume / 64.0f);
		float pitch = (frequency <= 0 || buffer->frequency() <= 0 ? 1.0f : (float)frequency / (float)buffer->frequency());

		auto player = _levelHandler->PlaySfx(nullptr, {}, buffer, Vector3f(pos.X, pos.Y, 0.0f), sourceRelative, gain, pitch);
		if (player != nullptr && loopChannel >= 0) {
			player->setLooping(true);

			auto prev = _loopedSamples.find(loopChannel);
			if (prev != _loopedSamples.end()) {
				if (prev->second != nullptr) {
					prev->second->stop();
				}
				prev->second = player;
			} else {
				_loopedSamples.emplace(loopChannel, player);
			}
		}
		return player;
#else
		return nullptr;
#endif
	}

	bool LevelScriptLoader::IsSampleLoaded(std::int32_t sampleId)
	{
		return ResolveSampleBuffer(sampleId) != nullptr;
	}

	bool LevelScriptLoader::LoadSample(std::int32_t sampleId, StringView path)
	{
#if defined(WITH_AUDIO)
		auto stream = OpenScriptFile(path);
		if (stream == nullptr || !stream->IsValid()) {
			return false;
		}
		auto buffer = std::make_unique<AudioBuffer>(std::move(stream), path);

		auto it = _scriptSamples.find(sampleId);
		if (it != _scriptSamples.end()) {
			it->second = std::move(buffer);
		} else {
			_scriptSamples.emplace(sampleId, std::move(buffer));
		}
		return true;
#else
		return false;
#endif
	}

	Resources::GenericGraphicResource* LevelScriptLoader::ResolveSpriteGraphic(std::int32_t setID, std::int32_t animation)
	{
		if (setID < 0 || animation < 0) {
			return nullptr;
		}

		// Same mapping the content pipeline uses to convert the original animation sets, so the original (set, anim)
		// index maps directly to the converted "Animations/{Category}/{Name}.aura" asset
		if (_animMapping == nullptr) {
			Compatibility::JJ2Version version = (versionTSF ? Compatibility::JJ2Version::TSF : Compatibility::JJ2Version::BaseGame);
			_animMapping = std::make_unique<Compatibility::AnimSetMapping>(Compatibility::AnimSetMapping::GetAnimMapping(version));
		}

		auto* entry = _animMapping->Get((std::uint32_t)setID, (std::uint32_t)animation);
		if (entry == nullptr || entry->Category == Compatibility::AnimSetMapping::Discard || entry->Name.empty()) {
			return nullptr;
		}

		// Load as indexed (keepIndexed = true) so it recolors through the live sprite palette at draw time, like every
		// other in-game sprite; ContentResolver caches the result, so repeated draws of the same animation don't reload
		String path = entry->Category + '/' + entry->Name + ".aura"_s;
		return ContentResolver::Get().RequestGraphics(path, 0, true);
	}

	std::int32_t LevelScriptLoader::AddObjectFromEvent(std::uint8_t eventId, float xPixel, float yPixel)
	{
		// A few original events are converted using import-time level context (tileset name, level name, ambient
		// light, game version); their converters dereference a JJ2Level that doesn't exist at runtime, so they can't
		// be spawned this way. They're level-structure events (checkpoints, end-of-level, scenery, lighting), not the
		// game objects scripts normally spawn.
		auto jj2Event = (Compatibility::JJ2Event)eventId;
		switch (jj2Event) {
			case Compatibility::JJ2Event::SAVE_POINT:
			case Compatibility::JJ2Event::SCENERY_DESTRUCT:
			case Compatibility::JJ2Event::AREA_EOL:
			case Compatibility::JJ2Event::AREA_EOL_WARP:
			case Compatibility::JJ2Event::AREA_SECRET_WARP:
			case Compatibility::JJ2Event::LIGHT_RESET:
			case Compatibility::JJ2Event::POWERUP_SWAP:
				return 0;
			default:
				break;
		}

		if (_eventConverter == nullptr) {
			_eventConverter = std::make_unique<Compatibility::EventConverter>();
		}

		auto result = _eventConverter->TryConvert(nullptr, jj2Event, 0);
		if (result.Type == EventType::Empty) {
			return 0;
		}

		auto actor = _levelHandler->EventSpawner()->SpawnEvent(result.Type, result.Params, Actors::ActorState::None,
			Vector3i((std::int32_t)xPixel, (std::int32_t)yPixel, ILevelHandler::MainPlaneZ));
		if (actor == nullptr) {
			return 0;
		}
		_levelHandler->AddActor(actor);

		std::int32_t id = _nextScriptObjectId++;
		_scriptObjects.emplace(id, std::weak_ptr<Actors::ActorBase>(actor));
		return id;
	}

	Actors::ActorBase* LevelScriptLoader::GetScriptObject(std::int32_t objectId)
	{
		auto it = _scriptObjects.find(objectId);
		if (it == _scriptObjects.end()) {
			return nullptr;
		}
		// Returns nullptr if the actor has since been destroyed (the registry holds only weak references)
		return it->second.lock().get();
	}

	std::int32_t LevelScriptLoader::AddScriptControlledObject(std::uint8_t eventId, float xPixel, float yPixel, asIScriptFunction* behaviorFunc)
	{
		// The script-facing object; one reference is created here and handed to the hosting actor
		void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
		auto* obj = new(mem) Legacy::jjOBJ();
		obj->eventID = eventId;
		obj->xPos = obj->xOrg = xPixel;
		obj->yPos = obj->yOrg = yPixel;
		if (behaviorFunc != nullptr) {
			obj->behavior = behaviorFunc;
		}

		auto wrapper = std::make_shared<Legacy::ScriptLegacyObject>(this, obj);
		wrapper->OnActivated(Actors::ActorActivationDetails(_levelHandler,
			Vector3i((std::int32_t)xPixel, (std::int32_t)yPixel, ILevelHandler::MainPlaneZ)));
		_levelHandler->AddActor(wrapper);

		std::int32_t id = _nextScriptObjectId++;
		_scriptObjects.emplace(id, std::weak_ptr<Actors::ActorBase>(wrapper));
		obj->objectID = (std::int16_t)id;
		return id;
	}

	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(xPixel, yPixel), sample, volume, frequency, false, -1);
	}
	int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(xPixel, yPixel), sample, volume, frequency, false, channel);
		return channel;
	}
	void playPrioritySample(std::int32_t sample) {
		// Priority samples play non-positionally (relative to the listener) at full volume
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->PlaySample(Vector2f(0.0f, 0.0f), sample, 63, 0, true, -1);
	}
	bool isSampleLoaded(std::int32_t sample) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->IsSampleLoaded(sample);
	}
	bool loadSample(std::int32_t sample, const String& filename) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->LoadSample(sample, filename);
	}

	bool getUseLayer8Speeds() {
		noop(); return false;
	}
	bool setUseLayer8Speeds(bool value) {
		noop(); return false;
	}

	jjWEAPON* get_jjWEAPON(int index) {
		static jjWEAPON dummy;
		return &dummy;
	}

	jjCHARACTER* get_jjCHARACTER(int index) {
		static jjCHARACTER dummy;
		return &dummy;
	}

	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty) {
		noop(); return 0;
	}
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length) {
		noop(); return 0;
	}
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId) {
		noop();
	}
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, std::int8_t offset, std::int8_t length, std::int32_t newValue) {
		noop();
	}
	std::int8_t GetTileType(std::uint16_t tile) {
		noop(); return 0;
	}
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value) {
		noop(); return 0;
	}

	// TODO


	bool LevelScriptLoader::jjSendPacket(const jjSTREAM& packet, std::int32_t toClientID, std::uint32_t toScriptModuleID) {
		noop(); return true;
	}

	// TODO

	std::uint16_t jjGetStaticTile(std::uint16_t tileID) {
		noop();
		return 0;
	}
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		return (tileMap != nullptr ? tileMap->GetTile(layer, xTile, yTile) : 0);
	}
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap != nullptr && tileMap->SetTile(layer, xTile, yTile, newTile)) {
			return newTile;
		}
		return 0;
	}
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height) {
		noop();
	}

	// TODO

	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel) {
		return IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)(xPixel + 1), (float)(yPixel + 1)));
	}
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer) {
		// Only the sprite layer has collision masks in this engine, so the layer argument is ignored
		return jjMaskedPixel(xPixel, yPixel);
	}
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel) {
		if (lineLength <= 0) {
			return false;
		}
		return IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)(xPixel + lineLength), (float)(yPixel + 1)));
	}
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer) {
		return jjMaskedHLine(xPixel, lineLength, yPixel);
	}
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		if (lineLength <= 0) {
			return false;
		}
		return IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)(xPixel + 1), (float)(yPixel + lineLength)));
	}
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		return jjMaskedVLine(xPixel, yPixel, lineLength);
	}
	bool jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		// The "top" variant behaves like a regular vertical line here; one-way (top-solid) tiles aren't distinguished
		return jjMaskedVLine(xPixel, yPixel, lineLength);
	}
	bool jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		return jjMaskedVLine(xPixel, yPixel, lineLength);
	}

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset) {
		noop();
	}
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) {
		noop();
	}
	std::int32_t jjGetModOrder() {
		noop();
		return 0;
	}
	std::int32_t jjGetModRow() {
		noop();
		return 0;
	}
	std::int32_t jjGetModTempo() {
		noop();
		return 0;
	}
	void jjSetModTempo(std::uint8_t speed) {
		noop();
	}
	std::int32_t jjGetModSpeed() {
		noop();
		return 0;
	}
	void jjSetModSpeed(std::uint8_t speed) {
		noop();
	}

	std::uint32_t getCustomSetID(std::uint8_t index) {
		return mCOUNT + index;
	}
}

#endif