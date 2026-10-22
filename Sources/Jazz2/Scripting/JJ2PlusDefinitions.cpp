#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusDefinitions.h"
#include "JJ2PlusAnimations.h"
#include "JJ2PlusEvents.h"
#include "JJ2PlusObjects.h"
#include "LevelScriptLoader.h"

#include "../LevelHandler.h"
#include "../ContentResolver.h"
#include "../Actors/ActorBase.h"
#include "../Actors/Player.h"
#include "../Compatibility/AnimSetMapping.h"
#include "../Compatibility/EventConverter.h"
#include "../Compatibility/JJ2Data.h"
#include "../Compatibility/JJ2Level.h"
#include "../Compatibility/JJ2Strings.h"
#include "../UI/HUD.h"
#include "../UI/InGameConsole.h"

#include "../../nCine/Audio/AudioBuffer.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/ITextureLoader.h"
#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Base/Random.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <regex>
#include <IO/FileSystem.h>

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>
#include <Containers/StringUtils.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	void Unimplemented(const char* sourceName) {
		const char* sectionName = nullptr;
		std::int32_t lineNumber = 0;
		auto ctx = asGetActiveContext();
		if (ctx != nullptr) {
			lineNumber = ctx->GetLineNumber(0, nullptr, &sectionName);
		}

		// Many of them are called every frame, each place in the script is reported only once
		static HashMap<std::uint64_t, bool> reported;
		std::uint64_t key = (std::uint64_t(reinterpret_cast<std::uintptr_t>(sourceName)) * 0x9E3779B97F4A7C15ull) ^
			(std::uint64_t(reinterpret_cast<std::uintptr_t>(sectionName)) * 0xC2B2AE3D27D4EB4Full) ^ std::uint32_t(lineNumber);
		if (!reported.emplace(key, true).second) {
			return;
		}

		if (ctx != nullptr) {
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
	std::unique_ptr<Stream> OpenScriptFile(StringView path) {
		auto& resolver = ContentResolver::Get();
		// A file that's only in "Source" isn't looked for in the content, the last place of it is opened without looking
		// and that logs an error for every file
		if (resolver.FindContentFile(path).empty() && fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), path)))) {
			return resolver.OpenSourceFile(path);
		}
		auto s = resolver.OpenContentFile(path);
		if (s != nullptr && s->IsValid()) {
			return s;
		}
		return resolver.OpenSourceFile(path);
	}

	// Returns the name of the converted tile set of a file name of the original - the tile sets are converted by their names
	// in lower case, without the extension (see Compatibility::JJ2Level) - or an empty string if it can't be one
	static String GetTileSetName(StringView fileName) {
		String name = StringUtils::lowercase(fileName);
		if (name.hasSuffix(".j2t"_s)) {
			name = String(name.exceptSuffix(".j2t"_s));
		}
		return (Formats::LevelFile::IsSafeTileSetPath(name) ? name : String());
	}

	// A filename that came from a script has to stay inside the game's own data directory. `fs::CombinePath()`
	// normalizes nothing, so without this a downloaded level could read or overwrite any file the process can
	// open just by prefixing enough `..` segments.
	bool IsScriptFileNameSafe(StringView filename) {
		if (filename.empty() || fs::IsAbsolutePath(filename)) {
			return false;
		}

		// Walked as path segments rather than searched as a substring, so only a real `..` segment is rejected
		// and a filename that merely contains dots ("save..1.asdat") still works
		const char* begin = filename.data();
		const char* end = begin + filename.size();
		const char* segment = begin;
		for (const char* it = begin; it <= end; it++) {
			if (it == end || *it == '/' || *it == '\\') {
				if (it - segment == 2 && segment[0] == '.' && segment[1] == '.') {
					return false;
				}
				segment = it + 1;
			}
		}
		return true;
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

	// LAYERSPEEDMODEL of JJ2+ for a speed model of the engine, they have other values. NORMAL and BOTHSPEEDS are both
	// the default model - NORMAL only ignores the speed of a layer that has an auto speed, which the converter does - so
	// a layer of the default model is NORMAL to a script.
	static std::int32_t ToScriptSpeedModel(Tiles::LayerSpeedModel model) {
		switch (model) {
			case Tiles::LayerSpeedModel::AlwaysOnTop: return 1;			// LAYER8
			case Tiles::LayerSpeedModel::FromStart: return 3;			// FROMSTART
			case Tiles::LayerSpeedModel::FitLevel: return 4;			// FITLEVEL
			case Tiles::LayerSpeedModel::SpeedMultipliers: return 5;	// SPEEDMULTIPLIERS
			default: return 0;											// NORMAL
		}
	}

	static Tiles::LayerSpeedModel FromScriptSpeedModel(std::int32_t model) {
		switch (model) {
			case 1: return Tiles::LayerSpeedModel::AlwaysOnTop;
			case 3: return Tiles::LayerSpeedModel::FromStart;
			case 4: return Tiles::LayerSpeedModel::FitLevel;
			case 5: return Tiles::LayerSpeedModel::SpeedMultipliers;
			default: return Tiles::LayerSpeedModel::Default;
		}
	}

	// Where a layer of the level is drawn for the view of a player - the layer coordinate at the top left corner of the
	// view, as jjLAYER::getXPosition() and getYPosition() of JJ2+ return it, so without the pixel every layer that moves
	// with the level is drawn higher here (see Tiles::LayerDescription::GetDrawOffsets()). The view is that of the player,
	// smaller in a split screen.
	static bool GetLayerPositionInView(LevelHandler* levelHandler, Actors::Player* player, std::int32_t layerIndex, Vector2f& result) {
		auto tileMap = levelHandler->TileMap();
		const Tiles::LayerDescription* description = (tileMap != nullptr ? tileMap->GetLayerDescription(layerIndex) : nullptr);
		if (description == nullptr) {
			return false;
		}
		Rectf view = levelHandler->GetCameraView(player);
		Vector2f origin = tileMap->GetGameplayLayerOrigin(layerIndex, view);
		result = Vector2f(view.X - origin.X, view.Y - origin.Y - (description->SpeedModelY != Tiles::LayerSpeedModel::AlwaysOnTop ? 1.0f : 0.0f));
		return true;
	}

	// How text of a size (STRING::Size) is drawn, as JJ2+ draws it - in the small font, in the medium one, or in one about twice
	// as large, the medium one scaled here; with its spacing, and moved by `OffsetY` so the glyphs are where they are against the
	// position the script gives there (it puts the small text on it, the larger ones mostly below it) - measured in JJ2+, and
	// then drawn smaller by `ScriptTextScale`
	struct ScriptTextStyle {
		UI::Font* Font;
		float Scale;
		float CharSpacing;
		float LineSpacing;
		float OffsetY;
	};

	// The text of scripts is drawn a quarter smaller than in JJ2+, as large as JJ2+ draws it, it looks too big in this game
	constexpr float ScriptTextScale = 0.75f;

	static ScriptTextStyle GetScriptTextStyle(std::uint32_t size) {
		auto& resolver = ContentResolver::Get();
		UI::Font* mediumFont = resolver.GetFont(FontType::Medium);
		ScriptTextStyle style;
		switch (size) {
			case 1:	// SMALL
				style = { resolver.GetFont(FontType::Small), 1.0f, 1.12f, 1.0f, -13.0f };
				break;
			case 2:	// LARGE
				style = (mediumFont != nullptr
					? ScriptTextStyle { mediumFont, 1.9f, 1.0f, 1.23f, -13.0f }
					: ScriptTextStyle { resolver.GetFont(FontType::Small), 2.9f, 1.0f, 1.0f, -13.0f });
				break;
			default:
			case 0:	// MEDIUM
				style = (mediumFont != nullptr
					? ScriptTextStyle { mediumFont, 1.0f, 1.0f, 1.23f, -10.0f }
					: ScriptTextStyle { resolver.GetFont(FontType::Small), 1.6f, 1.0f, 1.0f, -10.0f });
				break;
		}
		style.Scale *= ScriptTextScale;
		style.OffsetY *= ScriptTextScale;
		return style;
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

			// Draws a single animation frame of a resolved graphic onto a HUD canvas, following the HUD::DrawElement
			// pattern. The sprite is anchored at the frame's hotspot, optionally flipped (by `SPRITE::Direction` or a
			// negative scale), scaled and rotated (angle in JJ2 units where 1024 = a full turn). Indexed graphics draw
			// through the palette-remap shader against the live sprite palette (so they recolor with jjPalette).
			static void DrawSpriteFrame(UI::Canvas* canvas, std::uint16_t z, GenericGraphicResource* base, std::int32_t frame,
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

				// The sign flips horizontally, the next bit flips vertically when it differs from the sign
				std::uint8_t directionBits = std::uint8_t(direction);
				bool flipX = ((directionBits & 0x80) != 0);
				bool flipY = (((directionBits & 0x80) != 0) != ((directionBits & 0x40) != 0));
				if (scaleX < 0.0f) { flipX = !flipX; scaleX = -scaleX; }
				if (scaleY < 0.0f) { flipY = true; scaleY = -scaleY; }

				const Recti spriteRect = base->GetFrameRect(frame);
				Vector2f size(spriteRect.W * scaleX, spriteRect.H * scaleY);
				// Anchor the sprite at its hotspot, so the given coordinates are where the hotspot lands
				const Vector2i frameAnchor = base->GetFrameAnchor(frame, flipX, flipY);
				Vector2f pos(x - frameAnchor.X * scaleX, y - frameAnchor.Y * scaleY);

				Vector2i texSize = base->TextureDiffuse->GetSize();
				const Recti& frameRect = spriteRect;
				float wFrac = (float)frameRect.W / texSize.X;
				float hFrac = (float)frameRect.H / texSize.Y;
				Vector4f texCoords(wFrac, (float)frameRect.X / texSize.X,
					hFrac, (float)frameRect.Y / texSize.Y);
				if (flipX) { texCoords.X = -wFrac; texCoords.Y += wFrac; }
				if (flipY) { texCoords.Z = -hFrac; texCoords.W += hFrac; }

				Colorf color(1.0f, 1.0f, 1.0f, SpriteModeToAlpha(mode, param));
				float angleRad = angle * fTwoPi / 1024.0f;
				// Indexed sprites recolor via the shared sprite palette (offset 0); pre-baked RGBA sprites use -1
				std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? 0 : -1);
				canvas->DrawTexture(*base->TextureDiffuse.get(), pos, z, size, texCoords, color,
					mode == spriteType_BLENDLIGHTEN, angleRad, paletteOffset);
			}

			// JJ2+ names a character by its animation set (CHAR::Char), not by a player type - CHAR::BIRD is the set's
			// own and CHAR::BIRD2 borrows Chuck's. Returns PlayerType::None for a set that isn't a character.
			static PlayerType CharToPlayerType(std::uint32_t character) {
				// The set IDs depend on the version of the game (see AnimationTables)
				bool isTSF = ScriptLoader::FromActiveContext<LevelScriptLoader>()->IsTSF();
				std::int32_t setID = std::int32_t(character);
				if (setID == AnimationTables::GetSetID(mJAZZ, isTSF)) return PlayerType::Jazz;
				if (setID == AnimationTables::GetSetID(mSPAZ, isTSF)) return PlayerType::Spaz;
				if (setID == AnimationTables::GetSetID(mLORI, isTSF)) return PlayerType::Lori;
				if (setID == AnimationTables::GetSetID(mFROG, isTSF)) return PlayerType::Frog;
				if (setID == AnimationTables::GetSetID(mBIRD, isTSF)) return PlayerType::Bird;
				if (setID == AnimationTables::GetSetID(mCHUCK, isTSF)) return PlayerType::Bird2;
				return PlayerType::None;
			}

			// The other direction, for what a script reads back
			static std::uint32_t PlayerTypeToChar(PlayerType playerType) {
				bool isTSF = ScriptLoader::FromActiveContext<LevelScriptLoader>()->IsTSF();
				anim set;
				switch (playerType) {
					default:
					case PlayerType::Jazz: set = mJAZZ; break;
					case PlayerType::Spaz: set = mSPAZ; break;
					case PlayerType::Lori: set = mLORI; break;
					case PlayerType::Frog: set = mFROG; break;
					case PlayerType::Bird: set = mBIRD; break;
					case PlayerType::Bird2: set = mCHUCK; break;
				}
				return std::uint32_t(AnimationTables::GetSetID(set, isTSF));
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

			// Configures the actor renderer to display an animation of jjAnimations, mirroring ActorBase::RefreshAnimation
			// but from a raw graphic resource. The renderer then auto-advances and draws the animation in-world at the
			// actor's position.
			void ApplyAnimation(std::uint32_t animIndex)
			{
				// Behaviors often call determineCurAnim every frame; only rebuild the renderer when it actually changes
				if (animIndex == _lastAnimation) {
					return;
				}

				std::int32_t cellCount = 0;
				auto* base = _levelScripts->GetAnimationTables().ResolveAnimation(animIndex, cellCount);
				if (base == nullptr || base->TextureDiffuse == nullptr || base->FrameConfiguration.X <= 0 || cellCount <= 0) {
					return;
				}
				_lastAnimation = animIndex;

				// No GL renderer to configure on a dedicated/headless server
				if (ContentResolver::Get().IsHeadless()) {
					return;
				}

				_renderer.FrameConfiguration = base->FrameConfiguration;
				_renderer.FrameSource = base;
				_renderer.FrameDimensions = base->FrameDimensions;
				_renderer.FirstFrame = 0;
				_renderer.FrameCount = cellCount;
				_renderer.AnimDuration = base->AnimDuration;
				_renderer.LoopMode = AnimationLoopMode::Loop;
				_renderer.AnimTime = 0.0f;
				_renderer.Hotspot.X = (float)base->Hotspot.X;
				_renderer.Hotspot.Y = (float)base->Hotspot.Y;
				_renderer.setTexture(base->TextureDiffuse.get());
				_renderer.SetIndexed((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed, 0);
				_renderer.UpdateAnchor();
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
				// A behavior of the script moves the object itself (and counts its `age`), a behavior of the game that
				// this object can't run at least moves it by its speed
				// The behaviors of JJ2+ count in ticks, 70 of them in a second whatever the frame rate is, a few are caught up
				// after a slow frame (see ScriptObjectHost::OnUpdate())
				constexpr std::int32_t MaxTicksPerFrame = 4;
				std::int32_t currentTick = _levelScripts->GetGameTick();
				if (_nextTick < 0 || currentTick - _nextTick >= MaxTicksPerFrame) {
					_nextTick = currentTick;
				}
				while (_nextTick <= currentTick) {
					_nextTick++;
					// A copy keeps the behavior alive while it runs, it may replace itself
					jjBEHAVIOR behavior = _obj->behavior;
					_hasScriptBehavior = ScriptObjectHost::CallScriptBehavior(_levelScripts, *_obj, behavior);
					if (!_hasScriptBehavior) {
						// In units of the 60 frames in a second, as a tick is
						constexpr float TickTimeMult = 60.0f / 70.0f;
						_obj->xSpeed += _obj->xAcc * TickTimeMult;
						_obj->ySpeed += _obj->yAcc * TickTimeMult;
						_obj->xPos += _obj->xSpeed * TickTimeMult;
						_obj->yPos += _obj->ySpeed * TickTimeMult;
					}
				}

				// Apply the (possibly script-adjusted) position/facing to the actor
				MoveInstantly(Vector2f(_obj->xPos, _obj->yPos), Actors::MoveType::Absolute | Actors::MoveType::Force);
				SetFacingLeft(_obj->direction < 0);
				// A behavior of the script draws the frame it chooses (see jjOBJ::draw()), without one the animation plays by
				// itself and the current frame is the one it shows
				std::uint32_t frameCount, firstFrame;
				if (!_hasScriptBehavior && _lastAnimation != UINT32_MAX &&
					_levelScripts->GetAnimationTables().GetAnimationFrames(_lastAnimation, frameCount, firstFrame) && frameCount > 0) {
					_obj->curFrame = firstFrame + std::uint32_t(_renderer.CurrentFrame) % frameCount;
				}
			}

			bool OnDraw(RenderQueue& renderQueue) override
			{
				return _hasScriptBehavior;
			}

		private:
			LevelScriptLoader* _levelScripts;
			jjOBJ* _obj;
			std::uint32_t _lastAnimation = UINT32_MAX;
			// The next tick of the scripts the behavior runs in, -1 before the first one
			std::int32_t _nextTick = -1;
			bool _hasScriptBehavior = false;
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

		jjSTREAM::jjSTREAM() : _refCount(1), _readPos(0) {
		}
		jjSTREAM::~jjSTREAM() {
		}

		jjSTREAM* jjSTREAM::Create() {
			void* mem = asAllocMem(sizeof(jjSTREAM));
			return new(mem) jjSTREAM();
		}
		jjSTREAM* jjSTREAM::CreateFromFile(const String& filename) {
			void* mem = asAllocMem(sizeof(jjSTREAM));
			jjSTREAM* result = new(mem) jjSTREAM();

			// A missing file is not an error - scripts check isEmpty() to tell "no saved data yet" apart from
			// "saved data that happens to be empty", which is how the original's scripts use it. A rejected
			// path is reported the same way, so a script can't probe the filesystem for what exists either.
			if (!IsScriptFileNameSafe(filename)) {
				LOGW("Script tried to read \"{}\", which is outside the game's data directory", filename);
				return result;
			}

			auto s = fs::Open(fs::CombinePath(ContentResolver::Get().GetSourcePath(), filename), FileAccess::Read);
			if (s->IsValid()) {
				std::int64_t size = s->GetSize();
				if (size > 0 && size < std::numeric_limits<std::int32_t>::max()) {
					result->_data.resize_for_overwrite((std::size_t)size);
					// `resize_for_overwrite()` leaves the bytes uninitialized, so the stream is truncated to what
					// was actually read - the reported size is only an upper bound for a file that is being written
					// to concurrently, and the tail would otherwise hand uninitialized heap to the script
					std::int32_t read = s->Read(result->_data.data(), (std::int32_t)size);
					result->_data.truncate(read > 0 ? (std::size_t)read : 0);
				}
			}
			return result;
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
			// jjSTREAM is a reference type on the script side, so two handles can name one object and
			// `s = s;` is expressible - the assign() below would then read from storage it has already cleared
			if (&o == this) {
				// Still has to drop the consumed prefix, because that is the one visible effect of assigning
				if (_readPos > 0) {
					_data.erase(_data.begin(), _data.begin() + _readPos);
					_readPos = 0;
				}
				return *this;
			}

			// Copy only the content, not the script proxy class
			_data.assign(o._data.begin() + o._readPos, o._data.end());
			_readPos = 0;
			return *this;
		}

		const std::uint8_t* jjSTREAM::PeekFront(std::uint32_t count) const {
			if (count > getSize()) {
				return nullptr;
			}
			return _data.data() + _readPos;
		}

		bool jjSTREAM::Take(void* target, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			std::memcpy(target, front, count);
			_readPos += count;
			// Reclaim the consumed prefix once it dominates the buffer, so a stream that is written and read
			// in turns doesn't grow without bound
			if (_readPos > 4096 && _readPos * 2 > _data.size()) {
				_data.erase(_data.begin(), _data.begin() + _readPos);
				_readPos = 0;
			}
			return true;
		}

		void jjSTREAM::Append(const void* source, std::uint32_t count) {
			if (count == 0) {
				return;
			}

			// A stream can be appended to itself - `s.write(s)`, `s.push(s)`, `s.get(s, n)`, `s.pop(s)` all
			// reach here with `source` pointing into `_data` - and the resize below moves the buffer, so what
			// is carried across it is the offset rather than the pointer
			std::uintptr_t begin = (std::uintptr_t)_data.data();
			std::uintptr_t at = (std::uintptr_t)source;
			bool selfReferential = (at >= begin && at < begin + _data.size());
			std::size_t sourceOffset = (selfReferential ? (std::size_t)(at - begin) : 0);

			std::size_t offset = _data.size();
			_data.resize_for_overwrite(offset + count);
			std::memcpy(_data.data() + offset, (selfReferential ? _data.data() + sourceOffset : (const std::uint8_t*)source), count);
		}

		std::uint32_t jjSTREAM::getSize() const {
			return (std::uint32_t)(_data.size() - _readPos);
		}

		bool jjSTREAM::isEmpty() const {
			return (getSize() == 0);
		}

		bool jjSTREAM::save(const String& filename) const {
			// Written next to the game's other data, which is where the original puts it, so a script that
			// saves and reloads its own file finds it again - but only there, see IsScriptFileNameSafe()
			if (!IsScriptFileNameSafe(filename)) {
				LOGW("Script tried to write \"{}\", which is outside the game's data directory", filename);
				return false;
			}

			auto s = fs::Open(fs::CombinePath(ContentResolver::Get().GetSourcePath(), filename), FileAccess::Write);
			if (!s->IsValid()) {
				return false;
			}
			std::uint32_t size = getSize();
			if (size > 0) {
				s->Write(_data.data() + _readPos, size);
			}
			return true;
		}

		void jjSTREAM::clear() {
			_data.clear();
			_readPos = 0;
		}

		bool jjSTREAM::discard(std::uint32_t count) {
			if (count > getSize()) {
				return false;
			}
			_readPos += count;
			return true;
		}

		bool jjSTREAM::write(const String& value) {
			// Raw bytes, no length prefix - this is the text-friendly counterpart to push(String)
			Append(value.data(), (std::uint32_t)value.size());
			return true;
		}
		bool jjSTREAM::write(const jjSTREAM& value) {
			Append(value._data.data() + value._readPos, value.getSize());
			return true;
		}
		bool jjSTREAM::get(String& value, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			value = String((const char*)front, count);
			_readPos += count;
			return true;
		}
		bool jjSTREAM::get(jjSTREAM& value, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			value.Append(front, count);
			_readPos += count;
			return true;
		}
		bool jjSTREAM::getLine(String& value, const String& delim) {
			std::uint32_t size = getSize();
			if (size == 0 || delim.empty()) {
				return false;
			}

			const std::uint8_t* front = _data.data() + _readPos;
			StringView remaining((const char*)front, size);
			StringView found = remaining.find(delim);
			if (found == nullptr) {
				// No delimiter left, so the remainder is the last line
				value = String((const char*)front, size);
				_readPos += size;
				return true;
			}

			std::uint32_t lineLength = (std::uint32_t)(found.data() - (const char*)front);
			value = String((const char*)front, lineLength);
			// The delimiter itself is consumed but not returned
			_readPos += lineLength + (std::uint32_t)delim.size();
			return true;
		}

		bool jjSTREAM::push(bool value) {
			std::uint8_t raw = (value ? 1 : 0);
			return PushValue(raw);
		}
		bool jjSTREAM::push(std::uint8_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int8_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint16_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int16_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint32_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int32_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint64_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int64_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(float value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(double value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(const String& value) {
			// Length-prefixed, so pop(String) can recover it without a delimiter
			std::uint32_t length = (std::uint32_t)value.size();
			PushValue(length);
			Append(value.data(), length);
			return true;
		}
		bool jjSTREAM::push(const jjSTREAM& value) {
			return write(value);
		}

		bool jjSTREAM::pop(bool& value) {
			std::uint8_t raw;
			if (!PopValue(raw)) {
				return false;
			}
			value = (raw != 0);
			return true;
		}
		bool jjSTREAM::pop(std::uint8_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int8_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint16_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int16_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint32_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int32_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint64_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int64_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(float& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(double& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(String& value) {
			// The length prefix is validated before it is consumed, so a truncated payload leaves the stream
			// exactly where it was. Reading it first and rolling back afterwards cannot work: `Take()` may
			// compact the buffer and reset `_readPos` to 0 during that read, and the rollback then underflows
			// the unsigned position to ~4 GB instead of pointing at the length again.
			std::uint32_t length;
			const std::uint8_t* front = PeekFront(sizeof(length));
			if (front == nullptr) {
				return false;
			}
			std::memcpy(&length, front, sizeof(length));

			// `PeekFront()` above proved there are at least `sizeof(length)` bytes left, so this cannot underflow
			if (length > getSize() - sizeof(length)) {
				// Truncated or not written by push(String)
				return false;
			}

			_readPos += sizeof(length);
			return get(value, length);
		}
		bool jjSTREAM::pop(jjSTREAM& value) {
			return get(value, getSize());
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

		bool ScriptWorldCanvas::OnDraw(RenderQueue& renderQueue)
		{
			Canvas::OnDraw(renderQueue);

			for (const SolidDraw& draw : _solids) {
				Colorf color = PaletteIndexToColor(draw.Color, SpriteModeToAlpha(draw.Mode, draw.Param));
				DrawSolid(Vector2f(draw.X, draw.Y), draw.Z, Vector2f(draw.Width, draw.Height), color, draw.Mode == spriteType_BLENDLIGHTEN);
			}
			for (const SpriteDraw& draw : _sprites) {
				DrawSpriteFrame(this, draw.Z, draw.Graphics, draw.Cell, draw.X, draw.Y, draw.Direction, draw.ScaleX, draw.ScaleY,
					draw.Angle, draw.Mode, draw.Param);
			}
			return true;
		}

		jjCANVAS::jjCANVAS(UI::HUD* hud, const Rectf& view, std::uint16_t* drawCount)
			: Hud(hud), View(view), DrawCount(drawCount)
		{
		}

		std::uint16_t jjCANVAS::NextLayer() {
			// Commands of the same layer are drawn in no particular order, so every draw gets a layer of its own - from the
			// main layer of the HUD up to just below its font layer, the draws after that stay on the topmost one
			constexpr std::uint16_t MaxDraws = UI::HUD::FontLayer - UI::HUD::MainLayer - 1;
			std::uint16_t index = (DrawCount != nullptr ? std::min<std::uint16_t>((*DrawCount)++, MaxDraws) : 0);
			return std::uint16_t(UI::HUD::MainLayer + index);
		}

		void jjCANVAS::DrawPixel(std::int32_t xPixel, int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			Colorf c = PaletteIndexToColor(color, SpriteModeToAlpha(mode, param));
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), NextLayer(), Vector2f(1.0f, 1.0f), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawRectangle(std::int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			Colorf c = PaletteIndexToColor(color, SpriteModeToAlpha(mode, param));
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), NextLayer(), Vector2f((float)width, (float)height), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param);
		}
		void jjCANVAS::DrawCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param);
		}
		void jjCANVAS::DrawResizedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param);
		}
		void jjCANVAS::DrawResizedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param);
		}
		void jjCANVAS::DrawTransformedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param);
		}
		void jjCANVAS::DrawTransformedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param);
		}
		void jjCANVAS::DrawSwingingVine(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t length, int32_t curvature, std::uint32_t mode, std::uint8_t param) {
			noop();
		}

		void jjCANVAS::ExternalDrawTile(std::int32_t xPixel, int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant) {
			noop();
		}
		// The color of text drawn in a sprite mode, the font is drawn in its own colors only in the ordinary modes
		static Colorf GetTextColor(std::uint32_t mode, std::uint8_t param) {
			switch (mode) {
				case spriteType_TRANSLUCENT:
				case spriteType_TRANSLUCENTTILE:
					return UI::Font::TransparentDefaultColor;
				case spriteType_BLENDNORMAL: {
					Colorf color = UI::Font::DefaultColor;
					color.A = param / 255.0f;
					return color;
				}
				case spriteType_SINGLECOLOR:
					// Black is drawn as the shadow of text, as the shadows scripts draw with it are
					return PaletteIndexToColor(param, 1.0f);
				case spriteType_SHADOW:
					return Colorf(0.0f, 0.0f, 0.0f, 0.5f);
				default:
					return UI::Font::DefaultColor;
			}
		}

		void jjCANVAS::DrawTextBasicSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param) {
			ScriptTextStyle style = GetScriptTextStyle(size);
			if (style.Font == nullptr) {
				return;
			}
			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			style.Font->DrawString(Hud, recodedText, charOffset, float(xPixel), float(yPixel) + style.OffsetY, NextLayer(),
				UI::Alignment::TopLeft, GetTextColor(mode, param), style.Scale, 0.0f, 0.0f, 0.0f, 0.0f, style.CharSpacing, style.LineSpacing);
		}
		void jjCANVAS::DrawTextExtSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param) {
			// The appearance isn't taken yet, only the size
			DrawTextBasicSize(xPixel, yPixel, text, size, mode, param);
		}

		void jjCANVAS::drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param) {
			noop();
			// TODO
			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, NextLayer(),
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
		}
		jjOBJ::~jjOBJ() {
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
			// Active while its object exists, the one a script controls or the one of the game
			return (_wrapper != nullptr || !_actor.expired());
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
			// This is a bullet that hits the target as the game would have it, even where they aren't - the game handles the
			// collision of the two objects of the game, the script sees the energy the target has then
			std::shared_ptr<Actors::ActorBase> bullet = _actor.lock();
			std::shared_ptr<Actors::ActorBase> victim = (target != nullptr ? target->_actor.lock() : nullptr);
			if (bullet != nullptr && victim != nullptr) {
				if (!victim->OnHandleCollision(bullet.get())) {
					bullet->OnHandleCollision(victim.get());
				}
				target->energy = std::int8_t(std::clamp<std::int32_t>(victim->GetHealth(), INT8_MIN, INT8_MAX));
				target->_synced.Energy = target->energy;
			}
			// The handle is the function's to release, the result is the target
			return target;
		}
		void jjOBJ::blast(std::int32_t maxDistance, bool blastObjects) {
			noop();
		}

		void jjOBJ::behave1(std::uint32_t behavior, bool draw) {
			// Runs a behavior once, it doesn't become the behavior of the object. A behavior of the game is the object's own
			// update, the object has no other; an object a script controls entirely has none.
			if (_host != nullptr) {
				if (behavior == aUNKNOWN) {
					// BEHAVIOR::INACTIVE
					_host->Delete();
				} else {
					_host->RunOwnBehavior(draw);
				}
			}
		}
		void jjOBJ::behave2(jjBEHAVIOR behavior, bool draw) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (!ScriptObjectHost::CallScriptBehavior(owner, *this, behavior, draw)) {
				behave1(behavior.behaviorId, draw);
			}
		}
		void jjOBJ::behave3(jjVOIDFUNCOBJ behavior, bool draw) {
			// The funcdef handle arrives as an AngelScript function pointer (see jjAddObjectEx), with a reference that is
			// this function's to release - the temporary takes it over
			jjBEHAVIOR function;
			function.function = reinterpret_cast<asIScriptFunction*>(behavior);
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			ScriptObjectHost::CallScriptBehavior(owner, *this, function, draw);
		}

		std::int32_t jjOBJ::jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior) {
			// The spawned actor runs its own native behavior; the custom `behavior`/creator arguments aren't applied
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->AddObjectFromEvent(eventID, xPixel, yPixel);
		}
		std::int32_t jjOBJ::jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
			// The funcdef handle is passed by AngelScript as an asIScriptFunction*, with a reference that is this function's
			// to release - the temporary takes it over
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjBEHAVIOR function;
			function.function = reinterpret_cast<asIScriptFunction*>(behavior);

			// The object of the game the event has, driven by the behavior, so the behavior can run the object's own one
			std::int32_t id = owner->AddObjectFromEvent(eventID, xPixel, yPixel);
			if (id > 0) {
				jjOBJ* obj = owner->GetObjectById(id);
				obj->behavior = function;
				obj->_creatorID = creatorID;
				obj->_creatorType = creatorType;
				obj->Release();
				return id;
			}

			// The event has no object in the game, a script-controlled one stands in
			id = owner->AddScriptControlledObject(eventID, xPixel, yPixel, function.function);
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
				ScriptObjectHost::DeleteActor(*actor);
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
			// As JJ2+ documents it, the frame of curAnim that frameID says
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t frameCount, firstFrame, result = 0;
			if (curAnim >= 0 && owner->GetAnimationTables().GetAnimationFrames(std::uint32_t(curAnim), frameCount, firstFrame) && frameCount > 0) {
				result = firstFrame + std::uint32_t(frameID < 0 ? -frameID : frameID) % frameCount;
			}
			if (change) {
				curFrame = result;
			}
			return result;
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
			return determineCurAnimOfSet(setID, animation, change);
		}
		int16_t jjOBJ::determineCurAnimOfSet(std::int32_t setID, std::uint8_t animation, bool change) {
			// As JJ2+ documents it, the animation of the set in jjAnimations (a set of the original is always loaded), and
			// the hosted actor shows it
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t firstAnim = owner->GetAnimationTables().GetFirstAnim(setID);
			int16_t result = int16_t(firstAnim != 0 ? firstAnim + animation : 0);
			if (change) {
				curAnim = result;
				if (_wrapper != nullptr && result > 0) {
					_wrapper->ApplyAnimation(std::uint32_t(result));
				}
			}
			return result;
		}

		std::uint32_t jjOBJ::get_bulletHandling() {
			return _bulletHandling;
		}
		std::uint32_t jjOBJ::set_bulletHandling(std::uint32_t value) {
			_bulletHandling = value;
			_bulletHandlingSet = true;
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
			_playerHandlingSet = true;
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
			// An object of the game a behavior of the script drives is drawn only when the behavior asks, one a script controls
			// entirely draws its current frame
			if (_host != nullptr) {
				_host->Draw();
			} else if (_wrapper != nullptr && curFrame != 0) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				ScriptObjectHost::DrawFrame(owner, *this);
			}
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
			} else if (std::shared_ptr<Actors::ActorBase> actor = _actor.lock()) {
				ScriptObjectHost::DeleteActor(*actor);
			}
		}
		void jjOBJ::deactivate() {
			if (_wrapper != nullptr) {
				// Script-controlled objects don't return to the event map, so deactivation despawns them
				_wrapper->Remove();
			} else if (std::shared_ptr<Actors::ActorBase> actor = _actor.lock()) {
				ScriptObjectHost::DeactivateActor(*actor);
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
			// Both distances are squares of the distance in pixels, as JJ2+ documents them
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto players = owner->GetPlayers();
			std::int32_t nearest = -1;
			float bestSq = (float)maxDistance;
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
			foundDistance = (nearest >= 0 ? (std::int32_t)std::min(bestSq, float(INT32_MAX)) : maxDistance);
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
			// Without a player, it stands for the players that aren't in the game, see LevelScriptLoader::GetPlayerBackingStore()
			isLocal = (player != nullptr);
			isActive = (player != nullptr);
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
			if (_player == nullptr) {
				return {};
			}
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_score(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}
		std::int32_t jjPLAYER::get_scoreDisplayed() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_scoreDisplayed(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}

		std::int32_t jjPLAYER::setScore(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}

		float jjPLAYER::get_xPos() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_pos.X;
		}
		float jjPLAYER::set_xPos(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_pos.X = value;
			// A scripted move is a teleport, not a path the player walked, so nothing between the two positions
			// may be collided with or triggered (see Actors::ActorBase::ResetPathTracking())
			_player->ResetPathTracking();
			return _player->_pos.X;
		}
		float jjPLAYER::get_yPos() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_pos.Y;
		}
		float jjPLAYER::set_yPos(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_pos.Y = value;
			_player->ResetPathTracking();
			return _player->_pos.Y;
		}
		float jjPLAYER::get_xAcc() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_externalForce.X;
		}
		float jjPLAYER::set_xAcc(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_externalForce.X = value;
			return _player->_externalForce.X;
		}
		float jjPLAYER::get_yAcc() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_externalForce.Y;
		}
		float jjPLAYER::set_yAcc(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_externalForce.Y = value;
			return _player->_externalForce.Y;
		}
		float jjPLAYER::get_xOrg() const {
			if (_player == nullptr) {
				return {};
			}
			// The "origin" is the position the player respawns at, i.e. the last checkpoint
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::set_xOrg(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_checkpointPos.X = value;
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::get_yOrg() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_checkpointPos.Y;
		}
		float jjPLAYER::set_yOrg(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_checkpointPos.Y = value;
			return _player->_checkpointPos.Y;
		}
		float jjPLAYER::get_xSpeed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_speed.X;
		}
		float jjPLAYER::set_xSpeed(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_speed.X = value;
			return value;
		}
		float jjPLAYER::get_ySpeed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_speed.Y;
		}
		float jjPLAYER::set_ySpeed(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_speed.Y = value;
			return value;
		}

		void jjPLAYER::freeze(bool frozen) {
			if (_player == nullptr) {
				return;
			}
			if (frozen) {
				_player->_frozenTimeLeft = 180.0f;
				_player->_renderer.AnimPaused = true;
			} else if (_player->_frozenTimeLeft > 0.01f) {
				_player->_frozenTimeLeft = 0.01f;
			}
		}
		std::int32_t jjPLAYER::get_currTile() {
			if (_player == nullptr) {
				return {};
			}
			auto pos = _player->_pos;
			return ((std::int32_t)pos.X / 32) + ((std::int32_t)pos.Y / 32 * 65536);
		}
		bool jjPLAYER::startSugarRush(std::int32_t time) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: if boss active, return false
			_player->ActivateSugarRush((float)time * 60.0f / 70.0f);
			return true;
		}
		std::int8_t jjPLAYER::get_health() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int8_t)_player->_health;
		}
		std::int8_t jjPLAYER::set_health(std::int8_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->SetHealth(value);
			return value;
		}

		std::int32_t jjPLAYER::get_fastfire() const {
			if (_player == nullptr) {
				return {};
			}
			return (_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		}
		std::int32_t jjPLAYER::set_fastfire(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (value << 1));
			return value;
		}

		// The weapons of JJ2+ are numbered from 1 (`WEAPON::BLASTER`), ours from 0, returns -1 if there's no such weapon
		static std::int32_t ToWeaponIndex(std::int32_t number) {
			return (number >= 1 && number <= std::int32_t(WeaponType::Count) ? number - 1 : -1);
		}

		std::int8_t jjPLAYER::get_currWeapon() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int8_t(std::int32_t(_player->_currentWeapon) + 1);
		}
		std::int8_t jjPLAYER::set_currWeapon(std::int8_t value) {
			if (_player == nullptr) {
				return {};
			}
			std::int32_t index = ToWeaponIndex(value);
			if (index < 0) {
				return std::int8_t(std::int32_t(_player->_currentWeapon) + 1);
			}
			_player->_currentWeapon = (WeaponType)index;
			return value;
		}

		/*std::int32_t jjPLAYER::get_lives() const {
			return _player->_lives;
		}
		std::int32_t jjPLAYER::set_lives(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_lives = value;
			return _player->_lives;
		}*/

		std::int32_t jjPLAYER::get_invincibility() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_invincibility(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_invulnerableTime = value * FrameTimer::FramesPerSecond / 70.0f;
			_player->SetState(Actors::ActorState::IsInvulnerable, _player->_invulnerableTime > 0.0f);
			return value;
		}

		std::int32_t jjPLAYER::get_blink() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_invulnerableBlinkTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_blink(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_invulnerableBlinkTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int32_t jjPLAYER::extendInvincibility(std::int32_t duration) {
			if (_player == nullptr) {
				return {};
			}
			// Negative values are clamped so invincibility is never reduced below zero
			_player->_invulnerableTime += duration * FrameTimer::FramesPerSecond / 70.0f;
			if (_player->_invulnerableTime < 0.0f) {
				_player->_invulnerableTime = 0.0f;
			}
			_player->SetState(Actors::ActorState::IsInvulnerable, _player->_invulnerableTime > 0.0f);
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}

		std::int32_t jjPLAYER::get_food() const {
			if (_player == nullptr) {
				return {};
			}
			noop(); return _player->_inventory.FoodEaten;
		}
		std::int32_t jjPLAYER::set_food(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_inventory.FoodEaten = value;
			return _player->_inventory.FoodEaten;
		}
		std::int32_t jjPLAYER::get_coins() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_inventory.Coins;
		}
		std::int32_t jjPLAYER::set_coins(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_inventory.Coins = value;
			return _player->_inventory.Coins;
		}

		bool jjPLAYER::testForCoins(std::int32_t numberOfCoins) {
			if (_player == nullptr) {
				return {};
			}
			if (numberOfCoins > _player->_inventory.Coins) {
				return false;
			}
			_player->AddCoins(-numberOfCoins);
			return true;
		}
		std::int32_t jjPLAYER::get_gems(std::uint32_t type) const {
			if (_player == nullptr) {
				return {};
			}
			return (type < arraySize(_player->_inventory.Gems) ? _player->_inventory.Gems[type] : 0);
		}
		std::int32_t jjPLAYER::set_gems(std::uint32_t type, std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (type < arraySize(_player->_inventory.Gems)) {
				_player->_inventory.Gems[type] = value;
			}
			return (type < arraySize(_player->_inventory.Gems) ? _player->_inventory.Gems[type] : 0);
		}
		bool jjPLAYER::testForGems(std::int32_t numberOfGems, std::uint32_t type) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			std::uint8_t currentCount = (type < arraySize(_player->_inventory.Gems) ? _player->_inventory.Gems[type] : 0);
			if (numberOfGems <= currentCount) {
				if (numberOfGems > 0) {
					_player->_inventory.Gems[type] -= numberOfGems;
				}
				return true;
			}
			// TODO: Show warning message
			return false;
		}

		std::int32_t jjPLAYER::get_shieldType() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)_player->_activeShield;
		}
		std::int32_t jjPLAYER::set_shieldType(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (value >= 0 && value < (std::int32_t)ShieldType::Count) {
				_player->_activeShield = (ShieldType)value;
			}
			return value;
		}
		std::int32_t jjPLAYER::get_shieldTime() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_activeShieldTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_shieldTime(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_activeShieldTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int32_t jjPLAYER::get_rolling() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_inTubeTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_rolling(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
		
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
			if (_player == nullptr) {
				return {};
			}
			noop(); return (std::int8_t)_player->_playerIndex;
		}
		std::int32_t jjPLAYER::get_localPlayerID() const {
			if (_player == nullptr) {
				return {};
			}
			noop(); return _player->_playerIndex;
		}

		bool jjPLAYER::get_running() const {
			if (_player == nullptr) {
				return {};
			}
			noop(); return _player->_isRunPressed;
		}
		bool jjPLAYER::set_running(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_isRunPressed = value;
			return _player->_isRunPressed;
		}

		std::int32_t jjPLAYER::get_stoned() {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_dizzyTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_stoned(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
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
			// TODO: Equals true if isActive is true but isConnecting, isIdle, isOut, and isSpectating are all false.
			return (_player != nullptr);
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
			if (_player == nullptr) {
				return {};
			}
			// The engine packs one fur-section start index per byte, matching the JJ2+ 4-byte fur model
			return _player->_furColor;
		}
		std::uint32_t jjPLAYER::set_fur(std::uint32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_furColor = value;
			_player->RefreshColorPalette();
			return value;
		}
		void jjPLAYER::getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const {
			if (_player == nullptr) {
				return;
			}
			std::uint32_t fur = _player->_furColor;
			a = (std::uint8_t)(fur & 0xFF);
			b = (std::uint8_t)((fur >> 8) & 0xFF);
			c = (std::uint8_t)((fur >> 16) & 0xFF);
			d = (std::uint8_t)((fur >> 24) & 0xFF);
		}
		void jjPLAYER::setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
			if (_player == nullptr) {
				return;
			}
			_player->_furColor = (std::uint32_t)a | ((std::uint32_t)b << 8) | ((std::uint32_t)c << 16) | ((std::uint32_t)d << 24);
			_player->RefreshColorPalette();
		}

		bool jjPLAYER::get_noFire() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->_weaponAllowed;
		}
		bool jjPLAYER::set_noFire(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_weaponAllowed = !value;
			return value;
		}
		bool jjPLAYER::get_antiGrav() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->GetState(Actors::ActorState::ApplyGravitation);
		}
		bool jjPLAYER::set_antiGrav(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->SetState(Actors::ActorState::ApplyGravitation, !value);
			return value;
		}
		bool jjPLAYER::get_invisibility() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->GetState(Actors::ActorState::IsInvulnerable);
		}
		bool jjPLAYER::set_invisibility(bool value) {
			noop();
			return false;
		}
		bool jjPLAYER::get_noclipMode() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->GetState(Actors::ActorState::CollideWithTileset);
		}
		bool jjPLAYER::set_noclipMode(bool value) {
			if (_player == nullptr) {
				return {};
			}
			// Toggles tileset collision so the player can pass through level geometry
			_player->SetState(Actors::ActorState::CollideWithTileset, !value);
			return value;
		}
		std::uint8_t jjPLAYER::get_lighting() const {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: Viewports
			return (std::uint8_t)(_levelScriptLoader->_levelHandler->GetAmbientLight(_player) * 64.0f);
		}
		std::uint8_t jjPLAYER::set_lighting(std::uint8_t value) {
			if (_player == nullptr) {
				return {};
			}
			_levelScriptLoader->_levelHandler->SetAmbientLight(_player, value / 64.0f);
			return value;
		}
		std::uint8_t jjPLAYER::resetLight() {
			noop();
			return 0;
		}

		bool jjPLAYER::get_playerKeyLeftPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Left);
		}
		bool jjPLAYER::get_playerKeyRightPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Right);
		}
		bool jjPLAYER::get_playerKeyUpPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Up);
		}
		bool jjPLAYER::get_playerKeyDownPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Down);
		}
		bool jjPLAYER::get_playerKeyFirePressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Fire);
		}
		bool jjPLAYER::get_playerKeySelectPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::ChangeWeapon);
		}
		bool jjPLAYER::get_playerKeyJumpPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Jump);
		}
		bool jjPLAYER::get_playerKeyRunPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Run);
		}
		bool jjPLAYER::set_playerKeyLeftPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Left, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyRightPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Right, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyUpPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Up, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyDownPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Down, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyFirePressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Fire, value);
			return value;
		}
		bool jjPLAYER::set_playerKeySelectPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::ChangeWeapon, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyJumpPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Jump, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyRunPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Run, value);
			return value;
		}

		bool jjPLAYER::get_powerup(std::uint8_t number) {
			std::int32_t index = ToWeaponIndex(number);
			if (_player == nullptr || index < 0) {
				return false;
			}
			return (_player->_inventory.WeaponUpgrades[index] & 0x01) == 0x01;
		}
		bool jjPLAYER::set_powerup(std::uint8_t number, bool value) {
			std::int32_t index = ToWeaponIndex(number);
			if (_player == nullptr || index < 0) {
				return false;
			}
			// The rest of the bits are the fast fire of the blaster (see get_fastfire())
			std::uint8_t& upgrades = _player->_inventory.WeaponUpgrades[index];
			upgrades = std::uint8_t((upgrades & ~0x01) | (value ? 0x01 : 0x00));
			return value;
		}
		std::int32_t jjPLAYER::get_ammo(std::uint8_t number) const {
			std::int32_t index = ToWeaponIndex(number);
			if (_player == nullptr || index < 0) {
				return 0;
			}
			// In 1/256 units, the blaster has unlimited ammo, which JJ2+ doesn't count
			std::uint16_t ammo = _player->_inventory.WeaponAmmo[index];
			return (ammo == UINT16_MAX ? 0 : ammo / 256);
		}
		std::int32_t jjPLAYER::set_ammo(std::uint8_t number, std::int32_t value) {
			std::int32_t index = ToWeaponIndex(number);
			if (_player == nullptr || index < 0) {
				return value;
			}
			std::uint16_t& ammo = _player->_inventory.WeaponAmmo[index];
			if (ammo != UINT16_MAX) {
				ammo = std::uint16_t(std::clamp<std::int32_t>(value, 0, UINT16_MAX / 256 - 1) * 256);
			}
			return value;
		}

		bool jjPLAYER::offsetPosition(std::int32_t xPixels, std::int32_t yPixels) {
			if (_player == nullptr) {
				return {};
			}

			Vector2f pos = _player->GetPos();
			_player->WarpToPosition(Vector2f(pos.X + xPixels, pos.Y + yPixels), WarpFlags::Fast);
			return true;
		}
		bool jjPLAYER::warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast) {
			if (_player == nullptr) {
				return {};
			}

			_player->WarpToPosition(Vector2f(xTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2,
				yTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2),
				fast ? WarpFlags::Fast : WarpFlags::Default);
			return true;
		}
		bool jjPLAYER::warpToID(std::uint8_t warpID, bool fast) {
			if (_player == nullptr) {
				return {};
			}

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
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: morphEffect
			PlayerType playerType = CharToPlayerType(charNew);
			// MorphTo() leaves the player as it is if the character's sprites aren't available (the bird in the shareware data)
			if (playerType != PlayerType::None) {
				_player->MorphTo(playerType);
			}
			return PlayerTypeToChar(_player->_playerType);
		}
		std::uint32_t jjPLAYER::revertMorph(bool morphEffect) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: morphEffect
			// JJ2+ reverts to charOrig, the same as a Revert Morph area does
			_player->MorphTo(_player->_playerTypeOriginal);
			return PlayerTypeToChar(_player->_playerType);
		}
		std::uint32_t jjPLAYER::get_charCurr() const {
			if (_player == nullptr) {
				return {};
			}
			return PlayerTypeToChar(_player->_playerType);
		}
		std::uint32_t jjPLAYER::get_charOrig() const {
			if (_player == nullptr) {
				return {};
			}
			return PlayerTypeToChar(_player->_playerTypeOriginal);
		}
		std::uint32_t jjPLAYER::set_charOrig(std::uint32_t value) {
			if (_player == nullptr) {
				return {};
			}
			PlayerType playerType = CharToPlayerType(value);
			// Only a rabbit can be what the player reverts to, a temporary form is never anyone's original character
			if (playerType == PlayerType::Jazz || playerType == PlayerType::Spaz || playerType == PlayerType::Lori) {
				_player->_playerTypeOriginal = playerType;
			}
			return value;
		}

		void jjPLAYER::kill() {
			if (_player == nullptr) {
				return;
			}
			_player->DecreaseHealth(INT32_MAX);
		}
		bool jjPLAYER::hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker) {
			if (_player == nullptr) {
				return {};
			}
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
			if (_player == nullptr) {
				return {};
			}
			noop();

			// TODO: activate
			_levelScriptLoader->_levelHandler->BroadcastTriggeredEvent(_player, EventType::AreaActivateBoss, nullptr);
			return true;
		}
		bool jjPLAYER::limitXScroll(std::uint16_t left, std::uint16_t width) {
			if (_player == nullptr) {
				return {};
			}

			_levelScriptLoader->_levelHandler->LimitCameraView(_player, _player->_pos, left * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
			return true;
		}
		// An axis given by a bool is let go of if it's true, and left as it is otherwise
		void jjPLAYER::cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Fix, xPixel, CameraOverride::Fix, yPixel, centered, instant);
		}
		void jjPLAYER::cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f,
				CameraOverride::Fix, yPixel, centered, instant);
		}
		void jjPLAYER::cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Fix, xPixel,
				yUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f, centered, instant);
		}
		void jjPLAYER::cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f,
				yUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f, centered, instant);
		}
		void jjPLAYER::cameraUnfreeze(bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Follow, 0.0f, CameraOverride::Follow, 0.0f, false, instant);
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

		// The top left corner of the view, as JJ2+ has it - the layer coordinate of the sprite layer there (see GetLayerPositionInView())
		float jjPLAYER::get_cameraX() const {
			if (_player == nullptr) {
				return {};
			}
			return _levelScriptLoader->_levelHandler->GetCameraView(_player).X;
		}
		float jjPLAYER::get_cameraY() const {
			if (_player == nullptr) {
				return {};
			}
			return _levelScriptLoader->_levelHandler->GetCameraView(_player).Y;
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
			// The player collides with the target as the game would have it, even where they aren't, the script sees the
			// energy the target has then
			std::shared_ptr<Actors::ActorBase> victim = (target != nullptr ? target->_actor.lock() : nullptr);
			bool result = false;
			if (_player != nullptr && victim != nullptr) {
				result = (_player->OnHandleCollision(victim.get()) || victim->OnHandleCollision(_player));
				target->energy = std::int8_t(std::clamp<std::int32_t>(victim->GetHealth(), INT8_MIN, INT8_MAX));
				target->_synced.Energy = target->energy;
			}
			if (target != nullptr) {
				// A handle passed to a native function is the function's to release
				target->Release();
			}
			return result;
		}
		bool jjPLAYER::isEnemy(const jjPLAYER* victim) const {
			noop();
			return false;
		}

		std::uint32_t jjPLAYER::get_spriteMode() const {
			return _spriteMode;
		}
		std::uint32_t jjPLAYER::set_spriteMode(std::uint32_t value) {
			noop();
			// TODO: Only kept, the player is drawn as always - it decides how it's drawn every frame itself (blinking)
			_spriteMode = value;
			return value;
		}
		std::uint8_t jjPLAYER::get_spriteParam() const {
			return _spriteParam;
		}
		std::uint8_t jjPLAYER::set_spriteParam(std::uint8_t value) {
			_spriteParam = value;
			return value;
		}

		void jjPLAYER::SyncPropertiesToBackingStore() {
			if (_player == nullptr) {
				return;
			}
			noop();
			// TODO
			_backingStoreDirty = true;

			lives = _player->_lives;
		}
		void jjPLAYER::SyncPropertiesFromBackingStore() {
			if (_player == nullptr) {
				return;
			}
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

		// The largest pixel map, big enough for any area of a level a script takes in practice and small enough to allocate
		static constexpr std::uint64_t MaxPixelMapPixels = 8192ull * 8192ull;

		// Reads a tile as a layer holds it and `jjPIXELMAP(uint16)` takes it - its index, its flip flags and whether it's
		// animated, in which case it's the frame shown now - into 32 × 32 palette indices, transparent if it has none
		static void ReadTileImage(LevelScriptLoader* owner, std::uint16_t tile, const jjPAL* palette, std::uint8_t* pixels)
		{
			constexpr std::int32_t Size = TileSet::DefaultTileSize;
			std::memset(pixels, 0, Size * Size);

			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return;
			}
			std::int32_t tileId = (tile & TileMap::TileIndexMask);
			if ((tile & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
				if (tileId < 0) {
					return;
				}
			}

			std::uint8_t source[Size * Size];
			if (!owner->GetTilePixels(tileId, source, palette)) {
				return;
			}

			// JJ2 has the horizontally flipped tiles apart and gives an empty map for one it never made, here every
			// tile can be drawn flipped, so the flipped image is always there
			bool flipX = ((tile & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tile & TileMap::TileFlagFlipY) != 0);
			for (std::int32_t y = 0; y < Size; y++) {
				const std::uint8_t* src = &source[(flipY ? Size - 1 - y : y) * Size];
				for (std::int32_t x = 0; x < Size; x++) {
					pixels[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}
		}

		jjPIXELMAP::jjPIXELMAP() : _refCount(1) {
		}
		jjPIXELMAP::~jjPIXELMAP() {
		}

		jjPIXELMAP* jjPIXELMAP::Create(std::uint32_t width, std::uint32_t height) {
			if (std::uint64_t(width) * height > MaxPixelMapPixels) {
				asIScriptContext* ctx = asGetActiveContext();
				if (ctx != nullptr) {
					ctx->SetException("Pixel map is too large");
				}
				return nullptr;
			}

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			jjPIXELMAP* map = new(mem) jjPIXELMAP();
			map->width = width;
			map->height = height;
			// Value-initialized, so the map is transparent
			map->_pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
			return map;
		}

		jjPIXELMAP* jjPIXELMAP::CreateFromTile(std::uint16_t tileID, const jjPAL* palette) {
			jjPIXELMAP* map = Create(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
			ReadTileImage(ScriptLoader::FromActiveContext<LevelScriptLoader>(), tileID, palette, map->_pixels.get());
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			return Create(width, height);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromOther(const jjPIXELMAP& other) {
			jjPIXELMAP* map = Create(other.width, other.height);
			std::memcpy(map->_pixels.get(), other._pixels.get(), std::size_t(other.width) * other.height);
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL* palette) {
			if (animFrame == nullptr) {
				return Create(0, 0);
			}

			// The frame came from jjAnimFrames, which read its image already
			jjPIXELMAP* map = Create(animFrame->width, animFrame->height);
			if (map != nullptr && animFrame->_pixels != nullptr) {
				std::memcpy(map->_pixels.get(), animFrame->_pixels.get(), std::size_t(animFrame->width) * animFrame->height);
			}
			// A handle passed to a native function is the function's to release
			const_cast<jjANIMFRAME*>(animFrame)->Release();
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL* palette) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return CreateFromEngineLayer(left, top, width, height, owner->GetLayerIndex(std::int32_t(layer)), palette);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer, const jjPAL* palette) {
			// A layer a script made (see jjLAYER::CreateFromSize()) has no tiles, its map stays transparent
			return CreateFromEngineLayer(left, top, width, height, (layer != nullptr ? layer->_layerIndex : -1), palette);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromEngineLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::int32_t layerIndex, const jjPAL* palette) {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;

			jjPIXELMAP* map = Create(width, height);
			if (map == nullptr || layerIndex < 0) {
				return map;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}

			// The tiles out of the layer are empty, as GetTile() returns tile 0 for them
			std::uint8_t tilePixels[Size * Size];
			std::int32_t lastTile = -1;
			for (std::uint32_t y = 0; y < height; y++) {
				std::uint64_t py = std::uint64_t(top) + y;
				std::uint8_t* dst = &map->_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					std::uint64_t px = std::uint64_t(left) + x;
					std::int32_t tile = tileMap->GetTile(layerIndex, std::int32_t(px / Size), std::int32_t(py / Size));
					if (tile != lastTile) {
						ReadTileImage(owner, std::uint16_t(tile), palette, tilePixels);
						lastTile = tile;
					}
					dst[x] = tilePixels[(py % Size) * Size + (px % Size)];
				}
			}
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromTexture(std::uint32_t texture, const jjPAL* palette) {
			constexpr std::uint32_t Size = Formats::TexturedLayerChunk::TextureSize;
			if (texture == 0) {
				// TEXTURE::LAYER8, the tiles of the textured background
				return CreateFromLayer(0, 0, Size, Size, 8, palette);
			}
			jjPIXELMAP* map = Create(Size, Size);
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (map != nullptr && !owner->GetPlusTexture(std::int32_t(texture), map->_pixels.get())) {
				LOGW("Texture {} of JJ2+ isn't available, \"Plus.j2d\" of JJ2+ is needed in the \"Source\" directory", texture);
			}
			return map;
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold) {
			// The colors of the image become the nearest ones of the palette, a pixel of less alpha than `threshold` is
			// transparent
			jjCOLORMAP* colors = jjCOLORMAP::CreateFromFilename(filename);
			if (colors == nullptr) {
				return nullptr;
			}
			jjPIXELMAP* map = Create(colors->width, colors->height);
			if (map != nullptr) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				const jjPAL& target = (palette != nullptr ? *palette : owner->jjPalette);
				std::size_t count = std::size_t(colors->width) * colors->height;
				for (std::size_t i = 0; i < count; i++) {
					const jjPALCOLOR& color = colors->_pixels[i];
					if (color.alpha != 0 && color.alpha >= threshold) {
						map->_pixels[i] = target.FindNearestOpaqueColor(color);
					}
				}
			}
			colors->Release();
			return map;
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
			if (this != &o) {
				std::size_t size = std::size_t(o.width) * o.height;
				auto pixels = std::make_unique<std::uint8_t[]>(size);
				std::memcpy(pixels.get(), o._pixels.get(), size);
				width = o.width;
				height = o.height;
				_pixels = Death::move(pixels);
			}
			return *this;
		}

		std::uint8_t* jjPIXELMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= width || y >= height) {
				// Scripts of JJ2+ reach past the edge (`x <= 32` of a tile) and work there, a pixel out of the map is a
				// scratch one, what's written to it is lost
				_outOfRange = 0;
				return &_outOfRange;
			}
			return &_pixels[std::size_t(y) * width + x];
		}

		bool jjPIXELMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
			constexpr std::int32_t Size = TileSet::DefaultTileSize;

			// Saving into an animated tile is undefined in JJ2+
			if (width != Size || height != Size || (tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}

			// The image saved into a flipped tile is what the tile shows when it's drawn so. JJ2 has the horizontally flipped
			// tiles apart and changes only the one saved into unless `hFlip`, here a tile is flipped as it's drawn, so its
			// flipped version always changes along with it - as with `hFlip` in JJ2+
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			std::uint8_t image[Size * Size];
			for (std::int32_t y = 0; y < Size; y++) {
				const std::uint8_t* src = &_pixels[(flipY ? Size - 1 - y : y) * Size];
				for (std::int32_t x = 0; x < Size; x++) {
					image[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->SetTilePixels(tileID & TileMap::TileIndexMask, image);
		}
		bool jjPIXELMAP::saveToFrame(jjANIMFRAME* frame) const {
			if (frame == nullptr) {
				return false;
			}

			// The image and its size, the spots stay as they are; the sprites show it once the script returns
			bool result = (width <= UINT16_MAX && height <= UINT16_MAX);
			if (result) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				owner->GetAnimationTables().SetFramePixels(*frame, _pixels.get(), width, height);
			}
			// A handle passed to a native function is the function's to release
			frame->Release();
			return result;
		}
		bool jjPIXELMAP::saveToFile(const String& filename, const jjPAL& palette) const {
			noop();
			return false;
		}

		bool jjPIXELMAP::makeTexture(jjLAYER* layer) const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t layerIndex = (layer != nullptr ? layer->_layerIndex : owner->GetLayerIndex(8));
			// TEXTURE::CUSTOM, as JJ2+ reports a texture of a pixel map
			jjLAYER* proxy = (layer != nullptr ? layer : owner->GetLayerProxy(layerIndex));
			if (proxy == nullptr) {
				return false;
			}
			proxy->_texture = -1;

			constexpr std::uint32_t Size = Formats::TexturedLayerChunk::TextureSize;
			bool result = (width == Size && height == Size && proxy->SetCustomTexture(_pixels.get()));
			// A handle passed to a native function is the function's to release, and GetLayerProxy() gave one too
			proxy->Release();
			return result;
		}

		void jjPIXELMAP::Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, std::uint8_t fill) {
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::int64_t sy = top + y;
				std::uint8_t* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::int64_t sx = left + x;
					dst[x] = (sx >= 0 && sy >= 0 && sx < width && sy < height ? _pixels[std::size_t(sy) * width + std::size_t(sx)] : fill);
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
		}

		jjPIXELMAP& jjPIXELMAP::addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::uint8_t borderColor) {
			// Nothing changes if the image would be empty, or if more is cropped of it than it has
			std::int64_t newWidth = std::int64_t(width) + left + right;
			std::int64_t newHeight = std::int64_t(height) + top + bottom;
			std::int64_t cropX = std::int64_t(std::max<std::int32_t>(0, -left)) + std::max<std::int32_t>(0, -right);
			std::int64_t cropY = std::int64_t(std::max<std::int32_t>(0, -top)) + std::max<std::int32_t>(0, -bottom);
			if (newWidth <= 0 || newHeight <= 0 || cropX > width || cropY > height ||
				std::uint64_t(newWidth) * std::uint64_t(newHeight) > MaxPixelMapPixels) {
				return *this;
			}
			Reframe(-std::int64_t(left), -std::int64_t(top), std::uint32_t(newWidth), std::uint32_t(newHeight), borderColor);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::crop(std::uint32_t left, std::uint32_t top, std::uint32_t newWidth, std::uint32_t newHeight) {
			if (newWidth == 0 || newHeight == 0 || std::uint64_t(left) + newWidth > width || std::uint64_t(top) + newHeight > height) {
				return *this;
			}
			Reframe(left, top, newWidth, newHeight, 0);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::flip(std::int32_t directions) {
			// SPRITE::Direction is the direction byte of JJ2, its sign flips horizontally, and the bit below differing
			// from it vertically (FLIPH = 0xFF, FLIPV = 0x40, FLIPHV = 0xBF)
			std::uint8_t value = std::uint8_t(directions);
			bool flipX = ((value & 0x80) != 0);
			bool flipY = (((value & 0x40) != 0) != flipX);
			if (flipX) {
				for (std::uint32_t y = 0; y < height; y++) {
					std::uint8_t* row = &_pixels[std::size_t(y) * width];
					std::reverse(row, row + width);
				}
			}
			if (flipY) {
				for (std::uint32_t y = 0; y < height / 2; y++) {
					std::swap_ranges(&_pixels[std::size_t(y) * width], &_pixels[std::size_t(y) * width] + width,
						&_pixels[std::size_t(height - 1 - y) * width]);
				}
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::recolor(const CScriptArray& colorMapping) {
			if (colorMapping.GetSize() < 256) {
				return *this;
			}
			const std::uint8_t* mapping = static_cast<const std::uint8_t*>(colorMapping.At(0));
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				_pixels[i] = mapping[_pixels[i]];
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
			if (width == 0 || height == 0 || newWidth == 0 || newHeight == 0 || std::uint64_t(newWidth) * newHeight > MaxPixelMapPixels) {
				return *this;
			}
			// Nearest neighbor
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::size_t sy = std::size_t(std::uint64_t(y) * height / newHeight);
				std::uint8_t* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::size_t sx = std::size_t(std::uint64_t(x) * width / newWidth);
					dst[x] = _pixels[sy * width + sx];
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::rotate() {
			// 90 degrees clockwise, the bottom-left pixel ends up at the top-left
			auto pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < width; y++) {
				for (std::uint32_t x = 0; x < height; x++) {
					pixels[std::size_t(y) * height + x] = _pixels[std::size_t(height - 1 - x) * width + y];
				}
			}
			std::swap(width, height);
			_pixels = Death::move(pixels);
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimColor) {
			std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
			for (std::uint32_t y = 0; y < height; y++) {
				const std::uint8_t* row = &_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					if (row[x] != trimColor) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}

			if (minX > maxX || minY > maxY) {
				// Nothing but the trimmed color, as a special case the map is cropped to 1 × 1 instead of 0 × 0
				left = right = width;
				top = bottom = height;
				if (width > 0 && height > 0) {
					Reframe(0, 0, 1, 1, trimColor);
				}
				return *this;
			}

			left = minX;
			top = minY;
			right = width - 1 - maxX;
			bottom = height - 1 - maxY;
			if (left != 0 || top != 0 || right != 0 || bottom != 0) {
				Reframe(minX, minY, maxX - minX + 1, maxY - minY + 1, 0);
			}
			return *this;
		}
		jjPIXELMAP& jjPIXELMAP::trimBasic(std::uint8_t trimColor) {
			std::uint32_t left, top, right, bottom;
			return trim(left, top, right, bottom, trimColor);
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromColorMap(const jjCOLORMAP& other, const jjPAL* palette) {
			jjPIXELMAP* map = Create(other.width, other.height);
			if (map != nullptr) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				auto indices = other.ToIndices(palette != nullptr ? *palette : owner->jjPalette);
				std::memcpy(map->_pixels.get(), indices.get(), std::size_t(other.width) * other.height);
			}
			return map;
		}

		jjCOLORMAP::jjCOLORMAP() : _refCount(1) {
		}
		jjCOLORMAP::~jjCOLORMAP() {
		}

		jjCOLORMAP* jjCOLORMAP::Create(std::uint32_t width, std::uint32_t height) {
			if (std::uint64_t(width) * height > MaxPixelMapPixels) {
				asIScriptContext* ctx = asGetActiveContext();
				if (ctx != nullptr) {
					ctx->SetException("Color map is too large");
				}
				return nullptr;
			}

			void* mem = asAllocMem(sizeof(jjCOLORMAP));
			jjCOLORMAP* map = new(mem) jjCOLORMAP();
			map->width = width;
			map->height = height;
			// Value-initialized, so the map is transparent
			map->_pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(width) * height);
			return map;
		}

		jjCOLORMAP* jjCOLORMAP::CreateFromIndices(const std::uint8_t* indices, std::uint32_t width, std::uint32_t height, const jjPAL& palette) {
			jjCOLORMAP* map = Create(width, height);
			if (map == nullptr || indices == nullptr) {
				return map;
			}
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				if (indices[i] != 0) {
					map->_pixels[i] = palette.getConstColor(indices[i]);
					map->_pixels[i].alpha = 255;
				}
			}
			return map;
		}

		jjCOLORMAP* jjCOLORMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			return Create(width, height);
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromPixelMap(const jjPIXELMAP& other, const jjPAL& palette) {
			jjPIXELMAP& source = const_cast<jjPIXELMAP&>(other);
			return CreateFromIndices(other.width > 0 && other.height > 0 ? source.GetPixel(0, 0) : nullptr, other.width, other.height, palette);
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromOther(const jjCOLORMAP& other) {
			jjCOLORMAP* map = Create(other.width, other.height);
			if (map != nullptr) {
				std::memcpy(map->_pixels.get(), other._pixels.get(), std::size_t(other.width) * other.height * sizeof(jjPALCOLOR));
			}
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL& palette) {
			if (animFrame == nullptr) {
				return Create(0, 0);
			}
			// The frame came from jjAnimFrames, which read its image already; a 32-bit one has its own colors
			jjCOLORMAP* map;
			if (animFrame->_colors != nullptr) {
				map = Create(animFrame->width, animFrame->height);
				if (map != nullptr) {
					std::memcpy(map->_pixels.get(), animFrame->_colors.get(), std::size_t(animFrame->width) * animFrame->height * sizeof(jjPALCOLOR));
				}
			} else {
				map = CreateFromIndices(animFrame->_pixels.get(), animFrame->width, animFrame->height, palette);
			}
			// A handle passed to a native function is the function's to release
			const_cast<jjANIMFRAME*>(animFrame)->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromTexture(std::uint32_t texture, const jjPAL& palette) {
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromTexture(texture, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromTile(std::uint16_t tileID, const jjPAL& palette) {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;
			jjCOLORMAP* map = Create(Size, Size);
			if (map == nullptr) {
				return map;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}
			// An animated tile is the frame it shows now, as jjPIXELMAP has it
			std::int32_t tileId = (tileID & TileMap::TileIndexMask);
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
				if (tileId < 0) {
					return map;
				}
			}
			jjPALCOLOR source[Size * Size];
			if (!owner->GetTileColors(tileId, source, palette)) {
				return map;
			}
			// As jjPIXELMAP has it, every tile can be drawn flipped
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			for (std::uint32_t y = 0; y < Size; y++) {
				const jjPALCOLOR* src = &source[(flipY ? Size - 1 - y : y) * Size];
				for (std::uint32_t x = 0; x < Size; x++) {
					map->_pixels[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL& palette) {
			// TODO: A true color tile of the layer is in the nearest colors of the palette
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromLayer(left, top, width, height, layer, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER& layer, const jjPAL& palette) {
			jjPIXELMAP* indices = jjPIXELMAP::CreateFromLayerObject(left, top, width, height, &layer, &palette);
			if (indices == nullptr) {
				return nullptr;
			}
			jjCOLORMAP* map = CreateFromPixelMap(*indices, palette);
			indices->Release();
			return map;
		}
		jjCOLORMAP* jjCOLORMAP::CreateFromFilename(const String& filename) {
			// The real colors and alpha of the file, whatever kind of image it is; JJ2+ gives a 1 × 1 map for a file it can't read
			std::unique_ptr<ITextureLoader> loader;
			if (IsScriptFileNameSafe(filename)) {
				auto stream = OpenScriptFile(filename);
				if (stream != nullptr && stream->IsValid()) {
					loader = ITextureLoader::createFromStream(Death::move(stream), filename);
				}
			}
			if (loader == nullptr || !loader->hasLoaded() || loader->pixels() == nullptr) {
				return Create(1, 1);
			}

			const std::uint32_t channels = loader->texFormat().numChannels();
			const bool bgr = loader->texFormat().isBgr();
			jjCOLORMAP* map = Create(std::uint32_t(loader->width()), std::uint32_t(loader->height()));
			if (map == nullptr || channels < 1 || channels > 4) {
				return map;
			}
			const std::uint8_t* src = loader->pixels();
			std::size_t count = std::size_t(map->width) * map->height;
			for (std::size_t i = 0; i < count; i++, src += channels) {
				jjPALCOLOR& color = map->_pixels[i];
				if (channels <= 2) {
					color.red = color.green = color.blue = src[0];
					color.alpha = (channels == 2 ? src[1] : 255);
				} else {
					color.red = src[bgr ? 2 : 0];
					color.green = src[1];
					color.blue = src[bgr ? 0 : 2];
					color.alpha = (channels == 4 ? src[3] : 255);
				}
			}
			return map;
		}

		void jjCOLORMAP::AddRef()
		{
			_refCount++;
		}

		void jjCOLORMAP::Release()
		{
			if (--_refCount == 0) {
				this->~jjCOLORMAP();
				asFreeMem(this);
			}
		}

		jjCOLORMAP& jjCOLORMAP::operator=(const jjCOLORMAP& o)
		{
			// Copy only the content, not the script proxy class
			if (this != &o) {
				std::size_t size = std::size_t(o.width) * o.height;
				auto pixels = std::make_unique<jjPALCOLOR[]>(size);
				std::memcpy(pixels.get(), o._pixels.get(), size * sizeof(jjPALCOLOR));
				width = o.width;
				height = o.height;
				_pixels = Death::move(pixels);
			}
			return *this;
		}

		jjPALCOLOR* jjCOLORMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= width || y >= height) {
				// As of jjPIXELMAP, what's written to a pixel out of the map is lost
				_outOfRange = {};
				return &_outOfRange;
			}
			return &_pixels[std::size_t(y) * width + x];
		}

		std::unique_ptr<std::uint8_t[]> jjCOLORMAP::ToIndices(const jjPAL& palette) const {
			// The same color is looked up only once, an image has few of them
			std::size_t count = std::size_t(width) * height;
			std::unique_ptr<std::uint8_t[]> indices = std::make_unique<std::uint8_t[]>(count);
			HashMap<std::uint32_t, std::uint8_t> nearest;
			for (std::size_t i = 0; i < count; i++) {
				const jjPALCOLOR& color = _pixels[i];
				if (color.alpha == 0) {
					continue;
				}
				std::uint32_t key = std::uint32_t(color.red) | (std::uint32_t(color.green) << 8) | (std::uint32_t(color.blue) << 16);
				auto it = nearest.find(key);
				if (it == nearest.end()) {
					it = nearest.emplace(key, palette.FindNearestOpaqueColor(color)).first;
				}
				indices[i] = it->second;
			}
			return indices;
		}

		bool jjCOLORMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
			constexpr std::uint32_t Size = TileSet::DefaultTileSize;

			// Saving into an animated tile is undefined in JJ2+
			if (width != Size || height != Size || (tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}

			// The image saved into a flipped tile is what the tile shows when it's drawn so, see jjPIXELMAP::saveToTile()
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			jjPALCOLOR image[Size * Size];
			for (std::uint32_t y = 0; y < Size; y++) {
				const jjPALCOLOR* src = &_pixels[(flipY ? Size - 1 - y : y) * Size];
				for (std::uint32_t x = 0; x < Size; x++) {
					image[y * Size + x] = src[flipX ? Size - 1 - x : x];
				}
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->SetTileColors(tileID & TileMap::TileIndexMask, image);
		}
		bool jjCOLORMAP::saveToFrame(jjANIMFRAME* frame) const {
			if (frame == nullptr) {
				return false;
			}

			// As jjPIXELMAP::saveToFrame(), the frame keeps its spots and is a 32-bit one from now on
			bool result = (width <= UINT16_MAX && height <= UINT16_MAX);
			if (result) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				auto indices = ToIndices(owner->jjPalette);
				owner->GetAnimationTables().SetFrameColors(*frame, _pixels.get(), indices.get(), width, height);
			}
			// A handle passed to a native function is the function's to release
			frame->Release();
			return result;
		}
		bool jjCOLORMAP::saveToFile(const String& filename, bool allowCompression) const {
			noop();
			return false;
		}

		void jjCOLORMAP::Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, jjPALCOLOR fill) {
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::int64_t sy = top + y;
				jjPALCOLOR* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::int64_t sx = left + x;
					dst[x] = (sx >= 0 && sy >= 0 && sx < width && sy < height ? _pixels[std::size_t(sy) * width + std::size_t(sx)] : fill);
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
		}

		jjCOLORMAP& jjCOLORMAP::addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, jjPALCOLOR borderColor) {
			// The same as jjPIXELMAP::addBorders()
			std::int64_t newWidth = std::int64_t(width) + left + right;
			std::int64_t newHeight = std::int64_t(height) + top + bottom;
			std::int64_t cropX = std::int64_t(std::max<std::int32_t>(0, -left)) + std::max<std::int32_t>(0, -right);
			std::int64_t cropY = std::int64_t(std::max<std::int32_t>(0, -top)) + std::max<std::int32_t>(0, -bottom);
			if (newWidth <= 0 || newHeight <= 0 || cropX > width || cropY > height ||
				std::uint64_t(newWidth) * std::uint64_t(newHeight) > MaxPixelMapPixels) {
				return *this;
			}
			Reframe(-std::int64_t(left), -std::int64_t(top), std::uint32_t(newWidth), std::uint32_t(newHeight), borderColor);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::crop(std::uint32_t left, std::uint32_t top, std::uint32_t newWidth, std::uint32_t newHeight) {
			if (newWidth == 0 || newHeight == 0 || std::uint64_t(left) + newWidth > width || std::uint64_t(top) + newHeight > height) {
				return *this;
			}
			Reframe(left, top, newWidth, newHeight, {});
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::flip(std::int32_t directions) {
			// SPRITE::Direction as jjPIXELMAP::flip() takes it
			std::uint8_t value = std::uint8_t(directions);
			bool flipX = ((value & 0x80) != 0);
			bool flipY = (((value & 0x40) != 0) != flipX);
			if (flipX) {
				for (std::uint32_t y = 0; y < height; y++) {
					jjPALCOLOR* row = &_pixels[std::size_t(y) * width];
					std::reverse(row, row + width);
				}
			}
			if (flipY) {
				for (std::uint32_t y = 0; y < height / 2; y++) {
					std::swap_ranges(&_pixels[std::size_t(y) * width], &_pixels[std::size_t(y) * width] + width,
						&_pixels[std::size_t(height - 1 - y) * width]);
				}
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
			if (width == 0 || height == 0 || newWidth == 0 || newHeight == 0 || std::uint64_t(newWidth) * newHeight > MaxPixelMapPixels) {
				return *this;
			}
			// Nearest neighbor
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(newWidth) * newHeight);
			for (std::uint32_t y = 0; y < newHeight; y++) {
				std::size_t sy = std::size_t(std::uint64_t(y) * height / newHeight);
				jjPALCOLOR* dst = &pixels[std::size_t(y) * newWidth];
				for (std::uint32_t x = 0; x < newWidth; x++) {
					std::size_t sx = std::size_t(std::uint64_t(x) * width / newWidth);
					dst[x] = _pixels[sy * width + sx];
				}
			}
			width = newWidth;
			height = newHeight;
			_pixels = Death::move(pixels);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::rotate() {
			// 90 degrees clockwise, the bottom-left pixel ends up at the top-left
			auto pixels = std::make_unique<jjPALCOLOR[]>(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < width; y++) {
				for (std::uint32_t x = 0; x < height; x++) {
					pixels[std::size_t(y) * height + x] = _pixels[std::size_t(height - 1 - x) * width + y];
				}
			}
			std::swap(width, height);
			_pixels = Death::move(pixels);
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::swizzle(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha) {
			// COLOR::Component, RED to ALPHA
			std::size_t count = std::size_t(width) * height;
			for (std::size_t i = 0; i < count; i++) {
				jjPALCOLOR& color = _pixels[i];
				const std::uint8_t components[4] = { color.red, color.green, color.blue, color.alpha };
				color.red = components[red & 3];
				color.green = components[green & 3];
				color.blue = components[blue & 3];
				color.alpha = components[alpha & 3];
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimAlpha) {
			std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
			for (std::uint32_t y = 0; y < height; y++) {
				const jjPALCOLOR* row = &_pixels[std::size_t(y) * width];
				for (std::uint32_t x = 0; x < width; x++) {
					if (row[x].alpha > trimAlpha) {
						minX = std::min(minX, x);
						maxX = std::max(maxX, x);
						minY = std::min(minY, y);
						maxY = std::max(maxY, y);
					}
				}
			}

			if (minX > maxX || minY > maxY) {
				// Nothing to keep, as jjPIXELMAP::trim() the map is cropped to 1 × 1 instead of 0 × 0
				left = right = width;
				top = bottom = height;
				if (width > 0 && height > 0) {
					Reframe(0, 0, 1, 1, {});
				}
				return *this;
			}

			left = minX;
			top = minY;
			right = width - 1 - maxX;
			bottom = height - 1 - maxY;
			if (left != 0 || top != 0 || right != 0 || bottom != 0) {
				Reframe(minX, minY, maxX - minX + 1, maxY - minY + 1, {});
			}
			return *this;
		}
		jjCOLORMAP& jjCOLORMAP::trimBasic(std::uint8_t trimAlpha) {
			std::uint32_t left, top, right, bottom;
			return trim(left, top, right, bottom, trimAlpha);
		}

		jjMASKMAP::jjMASKMAP() : _refCount(1), _mask{} {
		}
		jjMASKMAP::~jjMASKMAP() {
		}

		jjMASKMAP* jjMASKMAP::CreateFromBool(bool filled) {
			void* mem = asAllocMem(sizeof(jjMASKMAP));
			jjMASKMAP* map = new(mem) jjMASKMAP();
			std::fill(map->_mask, map->_mask + Size * Size, filled);
			return map;
		}
		jjMASKMAP* jjMASKMAP::CreateFromTile(std::uint16_t tileID) {
			jjMASKMAP* map = CreateFromBool(false);

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return map;
			}
			std::int32_t tileId = (tileID & TileMap::TileIndexMask);
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				tileId = tileMap->ResolveStaticTileId(tileMap->GetStaticTileCount() + tileId);
			}
			const std::uint8_t* packed = tileMap->GetStaticTileMask(tileId);
			if (packed == nullptr) {
				return map;
			}

			// As with jjPIXELMAP, the flipped mask of every tile is there
			constexpr std::int32_t S = std::int32_t(Size);
			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			for (std::int32_t y = 0; y < S; y++) {
				for (std::int32_t x = 0; x < S; x++) {
					map->_mask[y * S + x] = TileSet::IsTileMaskBitSet(packed, flipX ? S - 1 - x : x, flipY ? S - 1 - y : y);
				}
			}
			return map;
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
			std::memcpy(_mask, o._mask, sizeof(_mask));
			return *this;
		}

		bool* jjMASKMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			if (x >= Size || y >= Size) {
				// The same as jjPIXELMAP::GetPixel()
				_outOfRange = false;
				return &_outOfRange;
			}
			return &_mask[y * Size + x];
		}

		bool jjMASKMAP::save(std::uint16_t tileID, bool hFlip) const {
			// The same as jjPIXELMAP::saveToTile(), the flipped version of a tile always changes along with it
			if ((tileID & TileMap::TileFlagAnimated) != 0) {
				return false;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return false;
			}

			bool flipX = ((tileID & TileMap::TileFlagFlipX) != 0);
			bool flipY = ((tileID & TileMap::TileFlagFlipY) != 0);
			std::uint8_t mask[Size * Size];
			for (std::uint32_t y = 0; y < Size; y++) {
				for (std::uint32_t x = 0; x < Size; x++) {
					mask[y * Size + x] = (_mask[(flipY ? Size - 1 - y : y) * Size + (flipX ? Size - 1 - x : x)] ? 1 : 0);
				}
			}
			return tileMap->OverrideTileMask(tileID & TileMap::TileIndexMask, mask);
		}

		jjLAYER::jjLAYER() : _refCount(1) {
			BindTextureParams();
		}
		jjLAYER::~jjLAYER() {
		}

		jjLAYER* jjLAYER::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			// A layer of the level that isn't drawn until it's in the order of the layers (see jjLayerOrderSet())
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjLAYER* layer = owner->CreateLayer(std::int32_t(std::min<std::uint32_t>(width, INT32_MAX)), std::int32_t(std::min<std::uint32_t>(height, INT32_MAX)));
			if (layer != nullptr) {
				return layer;
			}

			// A layer that can't be made is a standalone one (not part of the level), _layerIndex stays -1 so the per-frame
			// sync and tile access skip it
			void* mem = asAllocMem(sizeof(jjLAYER));
			layer = new(mem) jjLAYER();
			layer->width = layer->widthReal = (std::int32_t)width;
			layer->widthRounded = ((std::int32_t)width + 3) / 4;
			layer->height = (std::int32_t)height;
			return layer;
		}
		jjLAYER* jjLAYER::CreateCopy(jjLAYER* other) {
			if (other != nullptr) {
				// A copy of a layer of the level is one too, with the same tiles
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				if (jjLAYER* layer = owner->CopyLayer(*other)) {
					return layer;
				}
			}

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
			// A level layer's proxy doesn't keep the mode itself, it's read off the layer, unless a script set it
			_spriteMode = o.get_spriteMode(); _spriteParam = o._spriteParam; _hasSpriteMode = o._hasSpriteMode;
			// The texture, the parameters of the styles stay this layer's
			_textureStyle = o._textureStyle; _texture = o._texture; _textureSurface = o._textureSurface;
			std::memcpy(_textureColor, o._textureColor, sizeof(_textureColor)); _textureStars = o._textureStars;
			_customTexture = nullptr;
			if (o._customTexture != nullptr) {
				constexpr std::size_t Size = std::size_t(Formats::TexturedLayerChunk::TextureSize) * Formats::TexturedLayerChunk::TextureSize;
				_customTexture = std::make_unique<std::uint8_t[]>(Size);
				std::memcpy(_customTexture.get(), o._customTexture.get(), Size);
			}
			TextureStyleParams* params[] = { &warpHorizon, &tunnel, &menu, &tileMenu, &wave, &cylinder, &reflection };
			const TextureStyleParams* sources[] = { &o.warpHorizon, &o.tunnel, &o.menu, &o.tileMenu, &o.wave, &o.cylinder, &o.reflection };
			for (std::size_t i = 0; i < arraySize(params); i++) {
				params[i]->X = sources[i]->X;
				params[i]->Y = sources[i]->Y;
				params[i]->Fade = sources[i]->Fade;
				params[i]->TintColor = sources[i]->TintColor;
			}
			return *this;
		}

		jjLAYER* jjLAYER::get_jjLayers(std::int32_t index) {
			// Returns the persistent proxy bound to the engine layer of the original layer number `index` (already AddRef'd
			// for the script); the loader pushes the proxy's writable fields into the engine layer every frame
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjLAYER* layer = owner->GetLayerProxy(owner->GetLayerIndex(index));
			if (layer == nullptr) {
				// Out-of-range index: hand back an empty standalone layer rather than null so scripts don't crash
				void* mem = asAllocMem(sizeof(jjLAYER));
				layer = new(mem) jjLAYER();
			}
			return layer;
		}

		// How translucent a layer of SPRITE::TRANSLUCENT and SPRITE::TRANSLUCENTPALSHIFT is, as the translucent tiles are
		static constexpr float TranslucentLayerAlpha = 192.0f / 255.0f;

		std::uint32_t jjLAYER::get_spriteMode() const {
			if (_layerIndex < 0 || _hasSpriteMode) {
				return _spriteMode;
			}
			// The mode of a level layer is what it's drawn as
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			const Tiles::LayerDescription* description = (tileMap != nullptr ? tileMap->GetLayerDescription(_layerIndex) : nullptr);
			if (description == nullptr) {
				return spriteType_NORMAL;
			}
			const bool translucent = (description->Color.W < 1.0f);
			switch (description->RendererType) {
				// SPRITE::INVISIBLE is converted to an ordinary layer of no alpha
				case Tiles::LayerRendererType::Default: return (description->Color.W <= 0.0f ? spriteType_INVISIBLE : (translucent ? spriteType_TRANSLUCENT : spriteType_NORMAL));
				case Tiles::LayerRendererType::Tinted: return spriteType_TINTED;
				case Tiles::LayerRendererType::PaletteShift: return (translucent ? spriteType_TRANSLUCENTPALSHIFT : spriteType_PALSHIFT);
				case Tiles::LayerRendererType::SingleColor: return (translucent ? spriteType_TRANSLUCENTCOLOR : spriteType_SINGLECOLOR);
				case Tiles::LayerRendererType::ChromaKey: return spriteType_CHROMAKEY;
				default: return spriteType_NORMAL;
			}
		}
		std::uint32_t jjLAYER::set_spriteMode(std::uint32_t value) const {
			jjLAYER* self = const_cast<jjLAYER*>(this);
			if (_layerIndex < 0) {
				self->_spriteMode = value;
				return value;
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			const Tiles::LayerDescription* current = (tileMap != nullptr ? tileMap->GetLayerDescription(_layerIndex) : nullptr);
			if (current == nullptr) {
				return value;
			}

			// A textured layer is drawn over what is behind it as the mode says, if it's one it can be drawn with
			if (Tiles::LayerDescription::IsTexturedBackground(current->RendererType)) {
				self->_spriteMode = value;
				self->_hasSpriteMode = true;
				Tiles::LayerDescription description = *current;
				if (!SetTextureBlend(description)) {
					LOGW("Sprite mode {} isn't supported for textured layers, layer {} is drawn as it is", value, _layerIndex);
				}
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				return value;
			}

			// Only a layer drawn as tiles takes a mode. The others (x-ray, blur behind, reflection) are drawn by renderers of
			// their own that read the color their own way, so a mode would turn them into plain tiles.
			if (current->RendererType != Tiles::LayerRendererType::Default && current->RendererType != Tiles::LayerRendererType::Tinted &&
				current->RendererType != Tiles::LayerRendererType::PaletteShift && current->RendererType != Tiles::LayerRendererType::SingleColor &&
				current->RendererType != Tiles::LayerRendererType::ChromaKey) {
				LOGW("Sprite mode {} isn't supported for layer {}, it isn't drawn as tiles", value, _layerIndex);
				return value;
			}

			// The modes a layer can be drawn with, the rest of them aren't supported for layers
			Tiles::LayerDescription description = *current;
			const float param = _spriteParam / 255.0f;
			switch (value) {
				case spriteType_NORMAL: description.RendererType = Tiles::LayerRendererType::Default; description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f); break;
				case spriteType_TRANSLUCENT: description.RendererType = Tiles::LayerRendererType::Default; description.Color = Vector4f(1.0f, 1.0f, 1.0f, TranslucentLayerAlpha); break;
				case spriteType_INVISIBLE: description.RendererType = Tiles::LayerRendererType::Default; description.Color = Vector4f(1.0f, 1.0f, 1.0f, 0.0f); break;
				case spriteType_TINTED: description.RendererType = Tiles::LayerRendererType::Tinted; description.Color = Vector4f(param, 0.0f, 0.0f, 1.0f); break;
				case spriteType_PALSHIFT: description.RendererType = Tiles::LayerRendererType::PaletteShift; description.Color = Vector4f(param, 1.0f, 1.0f, 1.0f); break;
				case spriteType_TRANSLUCENTPALSHIFT: description.RendererType = Tiles::LayerRendererType::PaletteShift; description.Color = Vector4f(param, 1.0f, 1.0f, TranslucentLayerAlpha); break;
				// The parameter is how opaque the layer is
				case spriteType_BLENDNORMAL: description.RendererType = Tiles::LayerRendererType::Default; description.Color = Vector4f(1.0f, 1.0f, 1.0f, param); break;
				// The parameter is how bright the layer is, 128 as it is
				case spriteType_BRIGHTNESS: {
					const float brightness = _spriteParam / 128.0f;
					description.RendererType = Tiles::LayerRendererType::Default;
					description.Color = Vector4f(brightness, brightness, brightness, 1.0f);
					break;
				}
				// Every tile is as translucent as a translucent tile
				case spriteType_TRANSLUCENTTILE: description.RendererType = Tiles::LayerRendererType::Default; description.Color = Vector4f(1.0f, 1.0f, 1.0f, TranslucentLayerAlpha); break;
				// The parameter is the entry of the palette of the color, or of the key
				case spriteType_SINGLECOLOR: description.RendererType = Tiles::LayerRendererType::SingleColor; description.Color = Vector4f(param, 0.0f, 0.0f, 1.0f); break;
				case spriteType_TRANSLUCENTCOLOR: description.RendererType = Tiles::LayerRendererType::SingleColor; description.Color = Vector4f(param, 0.0f, 0.0f, TranslucentLayerAlpha); break;
				case spriteType_CHROMAKEY: description.RendererType = Tiles::LayerRendererType::ChromaKey; description.Color = Vector4f(param, 0.0f, 0.0f, 1.0f); break;
				default:
					LOGW("Sprite mode {} isn't supported for layers", value);
					return value;
			}
			tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			self->_spriteMode = value;
			self->_hasSpriteMode = true;
			return value;
		}
		std::uint8_t jjLAYER::get_spriteParam() const {
			return _spriteParam;
		}
		std::uint8_t jjLAYER::set_spriteParam(std::uint8_t value) const {
			jjLAYER* self = const_cast<jjLAYER*>(this);
			self->_spriteParam = value;
			if (_layerIndex < 0) {
				return value;
			}

			// Only the modes with a parameter take it right away, the others when they're switched to one of them
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			const Tiles::LayerDescription* current = (tileMap != nullptr ? tileMap->GetLayerDescription(_layerIndex) : nullptr);
			if (current != nullptr && Tiles::LayerDescription::IsTexturedBackground(current->RendererType)) {
				if (_hasSpriteMode) {
					Tiles::LayerDescription description = *current;
					SetTextureBlend(description);
					tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				}
			} else if (current != nullptr && (current->RendererType == Tiles::LayerRendererType::Tinted || current->RendererType == Tiles::LayerRendererType::PaletteShift ||
				current->RendererType == Tiles::LayerRendererType::SingleColor || current->RendererType == Tiles::LayerRendererType::ChromaKey)) {
				Tiles::LayerDescription description = *current;
				description.Color.X = value / 255.0f;
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			} else if (current != nullptr && _hasSpriteMode && _spriteMode == spriteType_BLENDNORMAL && current->RendererType == Tiles::LayerRendererType::Default) {
				Tiles::LayerDescription description = *current;
				description.Color.W = value / 255.0f;
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			} else if (current != nullptr && _hasSpriteMode && _spriteMode == spriteType_BRIGHTNESS && current->RendererType == Tiles::LayerRendererType::Default) {
				Tiles::LayerDescription description = *current;
				const float brightness = value / 128.0f;
				description.Color = Vector4f(brightness, brightness, brightness, description.Color.W);
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			}
			return value;
		}

		bool jjLAYER::SetTextureBlend(Tiles::LayerDescription& description) const {
			// The modes a textured layer can be drawn with over what is behind it, others are drawn as it is
			std::uint32_t mode = (_hasSpriteMode ? _spriteMode : std::uint32_t(spriteType_NORMAL));
			description.TextureBlendParam = 255;
			switch (mode) {
				case spriteType_NORMAL: description.TextureBlend = Tiles::TexturedLayerBlend::Normal; return true;
				// Half way between the texture and what is behind it, not as translucent tiles are, measured in JJ2+
				case spriteType_TRANSLUCENT:
				case spriteType_TRANSLUCENTTILE:
					description.TextureBlend = Tiles::TexturedLayerBlend::Translucent;
					description.TextureBlendParam = 128;
					return true;
				// The parameter is how opaque the layer is
				case spriteType_BLENDNORMAL: description.TextureBlend = Tiles::TexturedLayerBlend::Translucent; description.TextureBlendParam = _spriteParam; return true;
				case spriteType_INVISIBLE: description.TextureBlend = Tiles::TexturedLayerBlend::Translucent; description.TextureBlendParam = 0; return true;
				// The parameter is the entry of the palette of the key
				case spriteType_CHROMAKEY: description.TextureBlend = Tiles::TexturedLayerBlend::ChromaKey; description.TextureBlendParam = _spriteParam; return true;
				// The parameter is how far the result is mixed over what is behind it, measured in JJ2+
				case spriteType_BLENDSCREEN: description.TextureBlend = Tiles::TexturedLayerBlend::Screen; description.TextureBlendParam = _spriteParam; return true;
				case spriteType_BLENDMULTIPLY: description.TextureBlend = Tiles::TexturedLayerBlend::Multiply; description.TextureBlendParam = _spriteParam; return true;
				default: description.TextureBlend = Tiles::TexturedLayerBlend::Normal; return false;
			}
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
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			Actors::Player* player = (play != nullptr ? play->_player : nullptr);
			Vector2f position;
			if (GetLayerPositionInView(owner->GetLevelHandler(), player, _layerIndex, position)) {
				return position.X;
			}
			// A layer that isn't part of the level is approximated by its parallax and its offset alone
			float cameraX = owner->GetLevelHandler()->GetCameraView(player).X;
			return cameraX * xSpeed + xOffset;
		}
		float jjLAYER::getYPosition(const jjPLAYER* play) const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			Actors::Player* player = (play != nullptr ? play->_player : nullptr);
			Vector2f position;
			if (GetLayerPositionInView(owner->GetLevelHandler(), player, _layerIndex, position)) {
				return position.Y;
			}
			float cameraY = owner->GetLevelHandler()->GetCameraView(player).Y;
			return cameraY * ySpeed + yOffset;
		}

		std::int32_t jjLAYER::GetTextureMode() const {
			return _textureStyle;
		}
		void jjLAYER::SetTextureMode(std::int32_t value) {
			if (_textureStyle != value) {
				_textureStyle = value;
				if (value == tbgModeMENU) {
					LOGW("Texture style MENU isn't supported, the layer is drawn as a warp horizon");
				}
				ApplyTexture();
				// The image of the script is drawn from another half of the palette by another style
				if (_texture == -1 && _customTexture != nullptr) {
					SetCustomTexture(_customTexture.get());
				}
			}
		}
		bool jjLAYER::SetCustomTexture(const std::uint8_t* pixels) {
			constexpr std::size_t Size = std::size_t(Formats::TexturedLayerChunk::TextureSize) * Formats::TexturedLayerChunk::TextureSize;
			if (pixels != _customTexture.get()) {
				_customTexture = std::make_unique<std::uint8_t[]>(Size);
				std::memcpy(_customTexture.get(), pixels, Size);
			}
			auto* tileMap = (_layerIndex >= 0 ? ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetLevelHandler()->TileMap() : nullptr);
			if (tileMap == nullptr) {
				return false;
			}
			const bool upperHalf = (_textureStyle == tbgModeWARPHORIZON || _textureStyle == tbgModeTUNNEL || _textureStyle == tbgModeCYLINDER);
			std::unique_ptr<std::uint8_t[]> drawn = std::make_unique<std::uint8_t[]>(Size);
			for (std::size_t i = 0; i < Size; i++) {
				drawn[i] = std::uint8_t(upperHalf ? (_customTexture[i] | 0x80) : _customTexture[i]);
			}
			if (!tileMap->SetLayerTexture(_layerIndex, ArrayView<const std::uint8_t>(drawn.get(), Size))) {
				return false;
			}
			// The textured passes are drawn again with it
			tileMap->InvalidateTexturedBackground();
			return true;
		}
		std::int32_t jjLAYER::GetTexture() const {
			return _texture;
		}
		void jjLAYER::SetTexture(std::int32_t value) {
			_texture = value;
			// Not the image of the script any more, which it can't set again but by making it again
			_customTexture = nullptr;
			if (_layerIndex < 0) {
				return;
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			if (tileMap == nullptr) {
				return;
			}
			constexpr std::int32_t Size = Formats::TexturedLayerChunk::TextureSize;
			if (value == 0) {
				// TEXTURE::FROMTILES, the tiles at the top left of the layer
				tileMap->SetLayerTexture(_layerIndex, {});
			} else if (value > 0) {
				// One of the textures of JJ2+, TEXTURE::CUSTOM is what jjPIXELMAP::makeTexture() sets
				std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(Size) * Size);
				if (!owner->GetPlusTexture(value, pixels.get())) {
					LOGW("Texture {} of JJ2+ isn't available, \"Plus.j2d\" of JJ2+ is needed in the \"Source\" directory", value);
					return;
				}
				tileMap->SetLayerTexture(_layerIndex, ArrayView<const std::uint8_t>(pixels.get(), std::size_t(Size) * Size));
			}
			tileMap->InvalidateTexturedBackground();
		}
		std::int32_t jjLAYER::GetTextureSurface() const {
			return _textureSurface;
		}
		void jjLAYER::SetTextureSurface(std::int32_t value) {
			if (_textureSurface != value) {
				_textureSurface = value;
				ApplyTexture();
			}
		}

		void jjLAYER::SetTextureColor(std::int32_t index, std::uint8_t value) {
			if (_textureColor[index] != value) {
				_textureColor[index] = value;
				ApplyTexture();
			}
		}

		jjPALCOLOR jjLAYER::GetFadeColor(const TextureStyleParams* params) {
			jjPALCOLOR color = {};
			color.red = params->Owner->_textureColor[0];
			color.green = params->Owner->_textureColor[1];
			color.blue = params->Owner->_textureColor[2];
			return color;
		}
		void jjLAYER::SetFadeColor(TextureStyleParams* params, jjPALCOLOR color) {
			jjLAYER* layer = params->Owner;
			if (layer->_textureColor[0] != color.red || layer->_textureColor[1] != color.green || layer->_textureColor[2] != color.blue) {
				layer->_textureColor[0] = color.red;
				layer->_textureColor[1] = color.green;
				layer->_textureColor[2] = color.blue;
				layer->ApplyTexture();
			}
		}

		void jjLAYER::BindTextureParams() {
			for (TextureStyleParams* params : { &warpHorizon, &tunnel, &menu, &tileMenu, &wave, &cylinder, &reflection }) {
				params->Owner = this;
			}
		}

		void jjLAYER::InitializeTexture(const Tiles::TileMapLayer& layer) {
			const Tiles::LayerDescription& description = layer.Description;
			const Tiles::LayerRendererType type = description.RendererType;
			const std::uint8_t flags = description.TextureFlags;
			auto toByte = [](float value) {
				return std::uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
			};

			_texture = (layer.TexturePixels.empty() ? 0 : -1);
			xInnerSpeed = description.TextureInnerSpeed.X;
			yInnerSpeed = description.TextureInnerSpeed.Y;
			// Per tick of JJ2+, as scripts take them (see Tiles::LayerDescription::OriginalAutoSpeedScale)
			xInnerAutoSpeed = description.TextureInnerAutoSpeed.X / Tiles::LayerDescription::OriginalAutoSpeedScale;
			yInnerAutoSpeed = description.TextureInnerAutoSpeed.Y / Tiles::LayerDescription::OriginalAutoSpeedScale;
			if (!Tiles::LayerDescription::IsTexturedBackground(type) && type != Tiles::LayerRendererType::Reflection) {
				_textureSurface = Untextured;
				_textureStyle = tbgModeWARPHORIZON;
				return;
			}

			_textureStyle = std::int32_t(type) - std::int32_t(Tiles::LayerRendererType::Sky);
			if ((flags & Formats::TexturedLayerParams::InnerLayer) != 0) {
				_textureSurface = InnerLayer;
			} else if ((flags & Formats::TexturedLayerParams::InnerWindow) != 0) {
				_textureSurface = InnerWindow;
			} else {
				// Any layer without the perspective of JJ2+ is drawn as the sky layers always were here, the sky layer of the
				// original game with it is LEGACY, which JJ2+ draws as FULLSCREEN there (see ApplyTexture())
				_textureSurface = ((flags & Formats::TexturedLayerParams::Fullscreen) != 0 && layer.OriginalNumber != 8 ? FullScreen : Legacy);
			}
			_textureColor[0] = toByte(description.Color.X);
			_textureColor[1] = toByte(description.Color.Y);
			_textureColor[2] = toByte(description.Color.Z);

			// The fade position and whether it fades are the same for all the styles, as in JJ2+ (see TextureStyleParams), a
			// reflection keeps its tint color where the others have the fade
			for (TextureStyleParams* params : { &warpHorizon, &tunnel, &menu, &tileMenu, &wave, &cylinder, &reflection }) {
				params->X = description.TextureFadePosition.X;
				params->Y = description.TextureFadePosition.Y;
				if (_textureStyle != tbgModeREFLECTION) {
					params->Fade = (description.TextureFade != 0);
				}
			}
			switch (_textureStyle) {
				case tbgModeTUNNEL: _textureStars = ((flags & Formats::TexturedLayerParams::Spiral) != 0); break;
				case tbgModeWAVE: _textureStars = ((flags & Formats::TexturedLayerParams::DistortionAngle) != 0); break;
				case tbgModeCYLINDER: _textureStars = ((flags & Formats::TexturedLayerParams::HalfSize) != 0); break;
				case tbgModeREFLECTION:
					_textureStars = (description.Color.W > 0.0f);
					reflection.TintColor = description.TextureFade;
					break;
				// A warp horizon's stars, the full size of a tile menu
				default: _textureStars = (description.Color.W > 0.0f); break;
			}
		}

		void jjLAYER::ApplyTexture() {
			if (_layerIndex < 0) {
				return;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto tileMap = owner->GetLevelHandler()->TileMap();
			// The sprite layer is never drawn textured, SetLayerDescription() leaves it as it is
			const Tiles::LayerDescription* current = (tileMap != nullptr ? tileMap->GetLayerDescription(_layerIndex) : nullptr);
			if (current == nullptr) {
				return;
			}

			const bool wasTextured = (Tiles::LayerDescription::IsTexturedBackground(current->RendererType) ||
				current->RendererType == Tiles::LayerRendererType::Reflection);
			Tiles::LayerDescription description = *current;
			if (_textureSurface == Untextured) {
				if (wasTextured) {
					// Tiles again, as an ordinary layer
					description.RendererType = Tiles::LayerRendererType::Default;
					description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
					tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				}
				return;
			}

			// The game has no menu of JJ2+, it's a textured background at least (see SetTextureMode())
			std::int32_t style = _textureStyle;
			if (style == tbgModeMENU || style < tbgModeWARPHORIZON || style > tbgModeREFLECTION) {
				style = tbgModeWARPHORIZON;
			}
			const float red = _textureColor[0] / 255.0f;
			const float green = _textureColor[1] / 255.0f;
			const float blue = _textureColor[2] / 255.0f;
			const float stars = (_textureStars ? 1.0f : 0.0f);
			// Any style but a wave can be drawn only where the layer is, and over the whole view in the perspective of JJ2+ or
			// as the sky layers always were here (LEGACY) - but JJ2+ draws a sky, a tunnel or a cylinder of LEGACY of the sky
			// layer of the original game exactly as FULLSCREEN (measured), which places it where layer 5 is by LAYER8
			const bool isPlusLegacy = (_textureSurface == Legacy && tileMap->GetLayerOriginalNumber(_layerIndex) == 8 &&
				(style == tbgModeWARPHORIZON || style == tbgModeTUNNEL || style == tbgModeCYLINDER));
			std::uint8_t flags = 0;
			if (style != tbgModeWAVE) {
				if (_textureSurface == InnerLayer) {
					flags = Formats::TexturedLayerParams::InnerLayer;
				} else if (_textureSurface == InnerWindow) {
					flags = Formats::TexturedLayerParams::InnerWindow;
				} else if (_textureSurface == FullScreen || isPlusLegacy) {
					flags = Formats::TexturedLayerParams::Fullscreen;
				}
			}
			description.TextureFadePosition = Vector2f(0.5f, 0.5f);
			description.TextureFade = Formats::TexturedLayerParams::DefaultFade;
			switch (style) {
				case tbgModeWARPHORIZON:
					description.Color = Vector4f(red, green, blue, stars);
					description.TextureFadePosition = Vector2f(warpHorizon.X, warpHorizon.Y);
					description.TextureFade = (warpHorizon.Fade ? Formats::TexturedLayerParams::DefaultFade : 0);
					break;
				case tbgModeTUNNEL:
					description.Color = Vector4f(red, green, blue, 0.0f);
					description.TextureFadePosition = Vector2f(tunnel.X, tunnel.Y);
					description.TextureFade = (tunnel.Fade ? Formats::TexturedLayerParams::DefaultFade : 0);
					flags |= (_textureStars ? Formats::TexturedLayerParams::Spiral : 0);
					break;
				case tbgModeTILEMENU:
					// Whether it's drawn at full size, the auto speeds turn and zoom it, or the inner ones where it's drawn only
					// where the layer is
					description.Color = Vector4f(red, green, blue, stars);
					description.TextureFadePosition = Vector2f(tileMenu.X, tileMenu.Y);
					break;
				case tbgModeWAVE:
					// The wavelengths and the speed, the amplitudes are where the others have the fade position
					description.Color = Vector4f(red, green, blue, 0.0f);
					description.TextureFadePosition = Vector2f(wave.X, wave.Y);
					flags = (_textureStars ? Formats::TexturedLayerParams::DistortionAngle : 0);
					break;
				case tbgModeCYLINDER:
					description.Color = Vector4f(red, green, blue, 0.0f);
					description.TextureFadePosition = Vector2f(cylinder.X, cylinder.Y);
					description.TextureFade = (cylinder.Fade ? Formats::TexturedLayerParams::DefaultFade : 0);
					flags |= (_textureStars ? Formats::TexturedLayerParams::HalfSize : 0);
					break;
				case tbgModeREFLECTION:
					// The tint opacity, the distance and the distortion, and the blur
					description.Color = Vector4f(red, green, blue, stars);
					description.TextureFadePosition = Vector2f(reflection.X, reflection.Y);
					description.TextureFade = reflection.TintColor;
					break;
			}
			description.TextureFlags = flags;
			description.TextureInnerSpeed = Vector2f(xInnerSpeed, yInnerSpeed);
			description.TextureInnerAutoSpeed = Vector2f(xInnerAutoSpeed, yInnerAutoSpeed) * Tiles::LayerDescription::OriginalAutoSpeedScale;
			// The sprite mode a script set, also before the layer was textured
			SetTextureBlend(description);
			description.RendererType = Tiles::LayerRendererType(std::int32_t(Tiles::LayerRendererType::Sky) + style);
			tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			tileMap->InvalidateTexturedBackground();
		}

		CScriptArray* jjLAYER::jjLayerOrderGet() {
			// The level's layers in the order they're drawn, from the front, as proxies
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			SmallVector<std::int32_t, 16> order;
			owner->GetLayerOrder(order);
			asITypeInfo* arrayType = owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>");
			CScriptArray* arr = CScriptArray::Create(arrayType, (std::uint32_t)order.size());
			for (std::size_t i = 0; i < order.size(); i++) {
				jjLAYER* proxy = owner->GetLayerProxy(order[i]);	// AddRef'd for us
				if (proxy != nullptr) {
					arr->SetValue((std::uint32_t)i, &proxy);	// AddRefs the handle into the array
					proxy->Release();							// drop our extra reference
				}
			}
			return arr;
		}
		bool jjLAYER::jjLayerOrderSet(const CScriptArray& order) {
			// The layers a script made are layers of the level too, only a standalone one (one that couldn't be made) is
			// left out, it isn't drawn
			SmallVector<std::int32_t, 16> indices;
			bool hasStandaloneLayers = false;
			for (std::uint32_t i = 0; i < order.GetSize(); i++) {
				const jjLAYER* layer = *static_cast<jjLAYER* const*>(order.At(i));
				if (layer == nullptr) {
					// As in JJ2+, a null handle makes the order invalid
					return false;
				}
				if (layer->_layerIndex < 0) {
					hasStandaloneLayers = true;
					continue;
				}
				indices.push_back(layer->_layerIndex);
			}
			if (hasStandaloneLayers) {
				noop();
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->SetLayerOrder(indices);
		}
		CScriptArray* jjLAYER::jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, std::int32_t tileIDAdjustmentFactor) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			SmallVector<std::uint32_t, 8> layerNumbers;
			for (std::uint32_t i = 0; i < layerIDs.GetSize(); i++) {
				layerNumbers.push_back(*static_cast<const std::uint32_t*>(layerIDs.At(i)));
			}

			// As in JJ2+, no layers at all if one of them can't be had
			SmallVector<jjLAYER*, 8> layers;
			owner->CreateLayersFromLevel(filename, layerNumbers, tileIDAdjustmentFactor, layers);

			CScriptArray* arr = CScriptArray::Create(owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>"), std::uint32_t(layers.size()));
			for (std::size_t i = 0; i < layers.size(); i++) {
				arr->SetValue(std::uint32_t(i), &layers[i]);	// AddRefs the handle into the array
				layers[i]->Release();
			}
			return arr;
		}
		bool jjLAYER::jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			// As in JJ2+, a mapping of fewer than 256 colors isn't applied
			const std::uint8_t* mapping = (paletteColorMapping != nullptr && paletteColorMapping->GetSize() >= 256
				? static_cast<const std::uint8_t*>(paletteColorMapping->At(0)) : nullptr);
			bool result = owner->AddTilesFromTileSet(filename, firstTileID, tileCount, mapping);
			// A handle passed by value is the function's to release
			if (paletteColorMapping != nullptr) {
				paletteColorMapping->Release();
			}
			return result;
		}
		bool jjLAYER::jjTilesFromTileset32(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const jjPAL* palette) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			bool result = owner->AddTrueColorTilesFromTileSet(filename, firstTileID, tileCount, palette);
			// A handle passed by value is the function's to release
			if (palette != nullptr) {
				const_cast<jjPAL*>(palette)->Release();
			}
			return result;
		}

		jjTILE::jjTILE(std::uint16_t tileID) : _refCount(1), _tileID(tileID) {
		}

		void jjTILE::AddRef() {
			_refCount++;
		}

		void jjTILE::Release() {
			if (--_refCount == 0) {
				this->~jjTILE();
				asFreeMem(this);
			}
		}

		jjTILE* jjTILE::get_jjTiles(std::uint16_t tileID) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetTileProxy(tileID);
		}

		jjTILE* jjTILE::get_jjAnimatedTiles(std::uint16_t tileID) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetTileProxy(tileID | TileMap::TileFlagAnimated);
		}

		CScriptArray* jjTILE::getFrames() const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			SmallVector<std::uint16_t, 16> frames;
			owner->GetTileFrames(_tileID, frames);
			CScriptArray* arr = CScriptArray::Create(owner->GetEngine()->GetTypeInfoByDecl("array<uint16>"), std::uint32_t(frames.size()));
			for (std::size_t i = 0; i < frames.size(); i++) {
				*static_cast<std::uint16_t*>(arr->At(std::uint32_t(i))) = frames[i];
			}
			return arr;
		}

		bool jjTILE::setFrames(const CScriptArray& frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			const std::uint16_t* data = (frames.GetSize() > 0 ? static_cast<const std::uint16_t*>(frames.At(0)) : nullptr);
			return owner->SetTileFrames(_tileID, ArrayView<const std::uint16_t>(data, frames.GetSize()), pingPong, wait, randomWait, pingPongWait);
		}

		std::uint8_t jjTILE::get_fps() const {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetTileFps(_tileID);
		}

		std::uint8_t jjTILE::set_fps(std::uint8_t value) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->SetTileFps(_tileID, value);
			return value;
		}

		std::uint16_t jjTILE::get_tileID() const {
			return _tileID;
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
		// One persistent view of each object, what a script writes goes to the object (see LevelScriptLoader)
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetObjectById(index);
	}

	jjOBJ* get_jjObjectPresets(std::int8_t id)
	{
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetObjectPreset(std::uint8_t(id));
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

	// The converter does what MLLE::Setup() and MLLE::SpawnOffgrids() do, when the level is converted - the layers, their
	// properties and their order, the palette, the edited tiles and the off-grid events are all in the level already
	bool mlleSetup() {
		return true;
	}

	void mlleSpawnOffgrids() {
	}

	void mlleSpawnOffgridsLocal() {
	}

	Legacy::jjLAYER* mlleGetLayer(const String& name) {
		// The names MLLE gives the layers are converted into the names of the layers
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t index = owner->FindLayerByName(name);
		return (index >= 0 ? owner->GetLayerProxy(index) : nullptr);
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
		// The name of the file of the level as JJ2+ has it, which scripts take layers of (jjLayersFromLevel()), not the
		// episode and the level the game knows it by
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return String(fs::GetFileName(owner->GetLevelHandler()->GetLevelName())) + ".j2l"_s;
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

	// These are the same properties as those of jjLayers[id], so they read the jjLAYER proxy if a script has one and write
	// it too, otherwise the proxy would push its own value back at the end of the frame (see ResolveLayerNumber())
	float LevelScriptLoader::get_layerXOffset(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->xOffset : (layer != nullptr ? layer->Description.OffsetX : 0.0f));
	}
	float LevelScriptLoader::set_layerXOffset(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.OffsetX = value;
		if (proxy != nullptr) {
			proxy->xOffset = value;
		}
		return value;
	}
	float LevelScriptLoader::get_layerYOffset(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->yOffset : (layer != nullptr ? layer->Description.OffsetY : 0.0f));
	}
	float LevelScriptLoader::set_layerYOffset(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.OffsetY = value;
		if (proxy != nullptr) {
			proxy->yOffset = value;
		}
		return value;
	}
	std::int32_t LevelScriptLoader::get_layerWidth(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (layer != nullptr ? layer->LayoutSize.X : 0);
	}
	std::int32_t LevelScriptLoader::get_layerRealWidth(std::uint8_t id) {
		return get_layerWidth(id);
	}
	std::int32_t LevelScriptLoader::get_layerRoundedWidth(std::uint8_t id) {
		return (get_layerWidth(id) + 3) / 4;
	}
	std::int32_t LevelScriptLoader::get_layerHeight(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (layer != nullptr ? layer->LayoutSize.Y : 0);
	}
	float LevelScriptLoader::get_layerXSpeed(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->xSpeed : (layer != nullptr ? layer->Description.SpeedX : 0.0f));
	}
	float LevelScriptLoader::set_layerXSpeed(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.SpeedX = value;
		if (proxy != nullptr) {
			proxy->xSpeed = value;
		}
		return value;
	}
	float LevelScriptLoader::get_layerYSpeed(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->ySpeed : (layer != nullptr ? layer->Description.SpeedY : 0.0f));
	}
	float LevelScriptLoader::set_layerYSpeed(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.SpeedY = value;
		if (proxy != nullptr) {
			proxy->ySpeed = value;
		}
		return value;
	}
	float LevelScriptLoader::get_layerXAutoSpeed(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		// Per tick of JJ2+, as scripts take them (see Tiles::LayerDescription::FromOriginalAutoSpeed())
		return (proxy != nullptr ? proxy->xAutoSpeed : (layer != nullptr
			? Tiles::LayerDescription::ToOriginalAutoSpeed(layer->Description.AutoSpeedX, layer->Description.SpeedModelX) : 0.0f));
	}
	float LevelScriptLoader::set_layerXAutoSpeed(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.AutoSpeedX = Tiles::LayerDescription::FromOriginalAutoSpeed(value, layer->Description.SpeedModelX);
		if (proxy != nullptr) {
			proxy->xAutoSpeed = value;
		}
		return value;
	}
	float LevelScriptLoader::get_layerYAutoSpeed(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->yAutoSpeed : (layer != nullptr
			? Tiles::LayerDescription::ToOriginalAutoSpeed(layer->Description.AutoSpeedY, layer->Description.SpeedModelY) : 0.0f));
	}
	float LevelScriptLoader::set_layerYAutoSpeed(std::uint8_t id, float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return 0.0f;
		}
		layer->Description.AutoSpeedY = Tiles::LayerDescription::FromOriginalAutoSpeed(value, layer->Description.SpeedModelY);
		if (proxy != nullptr) {
			proxy->yAutoSpeed = value;
		}
		return value;
	}
	bool LevelScriptLoader::get_layerHasTiles(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->hasTiles : (layer != nullptr && layer->Visible));
	}
	bool LevelScriptLoader::set_layerHasTiles(std::uint8_t id, bool value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return false;
		}
		if (proxy != nullptr) {
			// The proxy decides, together with the order of the layers (see SyncLayerProperties())
			proxy->hasTiles = value;
		} else {
			layer->Visible = value;
		}
		return value;
	}
	bool LevelScriptLoader::get_layerTileHeight(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->tileHeight : (layer != nullptr && layer->Description.RepeatY));
	}
	bool LevelScriptLoader::set_layerTileHeight(std::uint8_t id, bool value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return false;
		}
		layer->Description.RepeatY = value;
		if (proxy != nullptr) {
			proxy->tileHeight = value;
		}
		return value;
	}
	bool LevelScriptLoader::get_layerTileWidth(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (proxy != nullptr ? proxy->tileWidth : (layer != nullptr && layer->Description.RepeatX));
	}
	bool LevelScriptLoader::set_layerTileWidth(std::uint8_t id, bool value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return false;
		}
		layer->Description.RepeatX = value;
		if (proxy != nullptr) {
			proxy->tileWidth = value;
		}
		return value;
	}
	bool LevelScriptLoader::get_layerLimitVisibleRegion(std::uint8_t id) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		return (layer != nullptr && layer->Description.UseInherentOffset);
	}
	bool LevelScriptLoader::set_layerLimitVisibleRegion(std::uint8_t id, bool value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		Legacy::jjLAYER* proxy;
		auto* layer = _this->ResolveLayerNumber(id, &proxy);
		if (layer == nullptr) {
			return false;
		}
		layer->Description.UseInherentOffset = value;
		return value;
	}

	void LevelScriptLoader::setLayerXSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		if (newSpeedIsAnAutoSpeed) {
			set_layerXAutoSpeed(id, newspeed);
		} else {
			set_layerXSpeed(id, newspeed);
		}
	}
	void LevelScriptLoader::setLayerYSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		if (newSpeedIsAnAutoSpeed) {
			set_layerYAutoSpeed(id, newspeed);
		} else {
			set_layerYSpeed(id, newspeed);
		}
	}

	// TODO

	// Draws in the level what a script draws by the functions of jjDraw*, see Legacy::ScriptWorldCanvas. The layer the
	// coordinates follow (`layerXY`) and the player that sees it (`playerID`) aren't taken into account, the coordinates
	// are those of the level and everyone sees it.
	static void DrawWorldSprite(LevelScriptLoader* owner, GenericGraphicResource* graphics, std::int32_t cell, float x, float y,
		std::int8_t direction, float scaleX, float scaleY, std::int32_t angle, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ)
	{
		if (graphics == nullptr || ContentResolver::Get().IsHeadless()) {
			return;
		}
		Legacy::ScriptWorldCanvas::SpriteDraw draw = { graphics, cell, x, y, direction, scaleX, scaleY, angle, mode, param, owner->GetLayerDrawZ(layerZ) };
		owner->GetWorldCanvas().AddSprite(draw);
	}

	static void DrawWorldSolid(LevelScriptLoader* owner, float x, float y, float width, float height, std::uint8_t color,
		std::uint32_t mode, std::uint8_t param, std::int8_t layerZ)
	{
		if (ContentResolver::Get().IsHeadless()) {
			return;
		}
		Legacy::ScriptWorldCanvas::SolidDraw draw = { x, y, width, height, color, mode, param, owner->GetLayerDrawZ(layerZ) };
		owner->GetWorldCanvas().AddSolid(draw);
	}

	void LevelScriptLoader::jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		DrawWorldSolid(owner, xPixel, yPixel, 1.0f, 1.0f, color, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		DrawWorldSolid(owner, xPixel, yPixel, float(width), float(height), color, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, direction, 1.0f, 1.0f, 0, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, direction, 1.0f, 1.0f, 0, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, 0, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, 0, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, angle, mode, param, layerZ);
	}

	void LevelScriptLoader::jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, angle, mode, param, layerZ);
	}

	Legacy::ScriptWorldCanvas& LevelScriptLoader::GetWorldCanvas()
	{
		if (_worldCanvas == nullptr) {
			_worldCanvas = std::make_unique<Legacy::ScriptWorldCanvas>();
			_worldCanvas->setParent(_levelHandler->_rootNode.get());
		}
		return *_worldCanvas;
	}

	std::uint16_t LevelScriptLoader::GetLayerDrawZ(std::int32_t layerNumber) const
	{
		// The sprite layer is where the objects are, the others are just in front of their tiles
		Tiles::TileMap* tileMap = _levelHandler->_tileMap.get();
		std::int32_t index = (layerNumber != 4 ? GetLayerIndex(layerNumber) : -1);
		if (tileMap == nullptr || index < 0 || index >= std::int32_t(tileMap->_layers.size())) {
			return std::uint16_t(ILevelHandler::SpritePlaneZ);
		}
		return std::uint16_t(tileMap->_layers[index].Description.Depth + 1);
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
		// The fonts live in ContentResolver (the HUD just borrows them), so this works even when called during script
		// initialization / onLevelLoad, before the HUD has been created
		ScriptTextStyle textStyle = GetScriptTextStyle(size);
		if (textStyle.Font == nullptr) {
			return 0;
		}
		auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
		return (std::int32_t)textStyle.Font->MeasureString(recodedText, textStyle.Scale, textStyle.CharSpacing, textStyle.LineSpacing).X;
	}

	void jjSetDarknessColor(jjPALCOLOR color) {
		noop();
	}
	// The textured background of the level is layer 8, its jjLAYER keeps how scripts set it (see jjLAYER::ApplyTexture()), the
	// global properties of JJ2+ are those of it
	namespace
	{
		struct TexturedBackgroundLayer
		{
			jjLAYER* Layer;

			TexturedBackgroundLayer() {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				Layer = owner->GetLayerProxy(owner->GetLayerIndex(8));
			}
			~TexturedBackgroundLayer() {
				if (Layer != nullptr) {
					Layer->Release();
				}
			}

			// The fade position is of the style the layer has now
			jjLAYER::TextureStyleParams& GetStyleParams() const {
				switch (Layer->GetTextureMode()) {
					case tbgModeTUNNEL: return Layer->tunnel;
					case tbgModeWAVE: return Layer->wave;
					case tbgModeCYLINDER: return Layer->cylinder;
					case tbgModeREFLECTION: return Layer->reflection;
					default: return Layer->warpHorizon;
				}
			}
		};
	}

	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
		// Without a textured background it's kept for when there's one, the alpha of the color shows the stars and stays
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			jjPALCOLOR color = {};
			color.red = red;
			color.green = green;
			color.blue = blue;
			jjLAYER::SetFadeColor(&background.Layer->warpHorizon, color);
		}
	}
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID) {
		// The palette in effect, the same as `jjPalette` once it's applied
		std::uint32_t color = ContentResolver::Get().GetPalettes()[paletteColorID];
		jjSetFadeColors(std::uint8_t(color & 0xFF), std::uint8_t((color >> 8) & 0xFF), std::uint8_t((color >> 16) & 0xFF));
	}
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color) {
		jjSetFadeColors(color.red, color.green, color.blue);
	}
	jjPALCOLOR jjGetFadeColors() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr ? jjLAYER::GetFadeColor(&background.Layer->warpHorizon) : jjPALCOLOR{});
	}
	void jjUpdateTexturedBG() {
		// The tiles of layer 8 are drawn into the texture again
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (auto tileMap = owner->GetLevelHandler()->TileMap()) {
			tileMap->InvalidateTexturedBackground();
		}
	}
	std::int32_t get_jjTexturedBGTexture() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr ? background.Layer->GetTexture() : 0);
	}
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr && background.Layer->GetTexture() != texture) {
			background.Layer->SetTexture(texture);
		}
		return texture;
	}
	std::int32_t get_jjTexturedBGStyle() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr ? background.Layer->GetTextureMode() : 0);
	}
	std::int32_t set_jjTexturedBGStyle(std::int32_t style) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			background.Layer->SetTextureMode(style);
		}
		return style;
	}
	bool get_jjTexturedBGUsed() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr && background.Layer->GetTextureSurface() != jjLAYER::Untextured);
	}
	bool set_jjTexturedBGUsed(bool used) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			std::int32_t surface = background.Layer->GetTextureSurface();
			if (used && surface == jjLAYER::Untextured) {
				background.Layer->SetTextureSurface(jjLAYER::Legacy);
			} else if (!used) {
				background.Layer->SetTextureSurface(jjLAYER::Untextured);
			}
		}
		return used;
	}
	bool get_jjTexturedBGStars() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr && jjLAYER::GetTextureStars(&background.Layer->warpHorizon));
	}
	bool set_jjTexturedBGStars(bool used) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			jjLAYER::SetTextureStars(&background.Layer->warpHorizon, used);
		}
		return used;
	}
	float get_jjTexturedBGFadePositionX() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr ? background.GetStyleParams().X : 0.5f);
	}
	float set_jjTexturedBGFadePositionX(float value) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>(&background.GetStyleParams(), value);
		}
		return value;
	}
	float get_jjTexturedBGFadePositionY() {
		TexturedBackgroundLayer background;
		return (background.Layer != nullptr ? background.GetStyleParams().Y : 0.5f);
	}
	float set_jjTexturedBGFadePositionY(float value) {
		TexturedBackgroundLayer background;
		if (background.Layer != nullptr) {
			jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>(&background.GetStyleParams(), value);
		}
		return value;
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
		for (auto& pair : _tileProxies) {
			pair.second->Release();
		}
		// The objects of the game a script still drives stop now, while the script can still be released
		ReleaseObjects();
		// What the script drew refers to the graphics of the animation tables
		_worldCanvas = nullptr;
		// Gives the graphics of the game back as they were, before anything else of the level goes
		_animTables = nullptr;
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
			// Per tick of JJ2+, as scripts take them
			layer->xAutoSpeed = Tiles::LayerDescription::ToOriginalAutoSpeed(src.Description.AutoSpeedX, src.Description.SpeedModelX);
			layer->yAutoSpeed = Tiles::LayerDescription::ToOriginalAutoSpeed(src.Description.AutoSpeedY, src.Description.SpeedModelY);
			layer->xOffset = layer->_syncedOffsetX = src.Description.OffsetX;
			layer->yOffset = layer->_syncedOffsetY = src.Description.OffsetY;
			layer->tileWidth = src.Description.RepeatX;
			layer->tileHeight = src.Description.RepeatY;
			layer->limitVisibleRegion = src.Description.UseInherentOffset;
			layer->SpeedModeX = ToScriptSpeedModel(src.Description.SpeedModelX);
			layer->SpeedModeY = ToScriptSpeedModel(src.Description.SpeedModelY);
			// A layer without a single tile is one JJ2+ keeps without its tile map, and draws nothing of it
			layer->hasTileMap = src.Layout.HasAnyTile();
			layer->hasTiles = src.Visible;
			if (src.Description.RendererType == Tiles::LayerRendererType::Tinted || src.Description.RendererType == Tiles::LayerRendererType::PaletteShift ||
				src.Description.RendererType == Tiles::LayerRendererType::SingleColor || src.Description.RendererType == Tiles::LayerRendererType::ChromaKey) {
				layer->_spriteParam = std::uint8_t(src.Description.Color.X * 255.0f + 0.5f);
			}
			layer->InitializeTexture(src);

			_layerProxies.emplace(index, layer);
			// The map keeps one reference; AddRef again below for the one we hand to the script
		}

		layer->AddRef();
		return layer;
	}

	std::int32_t LevelScriptLoader::GetLayerIndex(std::int32_t layerNumber) const
	{
		constexpr std::int32_t Count = Formats::LayerNumbersChunk::OriginalLayerCount;
		if (layerNumber < 1 || layerNumber > Count) {
			return -1;
		}

		if (!_layerIndicesResolved) {
			_layerIndicesResolved = true;
			for (std::int32_t& index : _layerIndexByNumber) {
				index = -1;
			}
			auto tileMap = _levelHandler->_tileMap.get();
			if (tileMap != nullptr) {
				// A converted level knows the number of each layer of the original, wherever it's drawn now
				bool anyNumber = false;
				for (std::int32_t i = 0; i < std::int32_t(tileMap->_layers.size()); i++) {
					std::uint8_t number = tileMap->_layers[i].OriginalNumber;
					if (number != 0) {
						anyNumber = true;
						if (_layerIndexByNumber[number] < 0) {
							_layerIndexByNumber[number] = i;
						}
					}
				}
				// Without them the numbers are counted from the sprite layer, which is always layer 4
				if (!anyNumber && tileMap->_sprLayerIndex >= 0) {
					for (std::int32_t number = 1; number <= Count; number++) {
						std::int32_t index = tileMap->_sprLayerIndex + (number - 4);
						if (index >= 0 && index < std::int32_t(tileMap->_layers.size())) {
							_layerIndexByNumber[number] = index;
						}
					}
				}
			}
		}
		return _layerIndexByNumber[layerNumber];
	}

	std::int32_t LevelScriptLoader::FindLayerByName(StringView name) const
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || name.empty()) {
			return -1;
		}
		for (std::int32_t i = 0; i < std::int32_t(tileMap->_layers.size()); i++) {
			if (tileMap->_layers[i].Name == name) {
				return i;
			}
		}
		return -1;
	}

	void LevelScriptLoader::GetLayerOrder(SmallVectorImpl<std::int32_t>& order) const
	{
		order.clear();
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr) {
			return;
		}
		for (std::int32_t i = 0; i < std::int32_t(tileMap->_layers.size()); i++) {
			if (std::size_t(i) >= _layersOutOfOrder.size() || !_layersOutOfOrder[i]) {
				order.push_back(i);
			}
		}
		// A layer with a greater depth is drawn later, so it's in front
		std::stable_sort(order.begin(), order.end(), [tileMap](std::int32_t a, std::int32_t b) {
			return (tileMap->_layers[a].Description.Depth > tileMap->_layers[b].Description.Depth);
		});
	}

	bool LevelScriptLoader::SetLayerOrder(ArrayView<const std::int32_t> order)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr) {
			return false;
		}
		const std::int32_t layerCount = std::int32_t(tileMap->_layers.size());
		std::int32_t spritePosition = -1;
		for (std::size_t i = 0; i < order.size(); i++) {
			if (order[i] < 0 || order[i] >= layerCount) {
				return false;
			}
			if (order[i] == tileMap->_sprLayerIndex && spritePosition < 0) {
				spritePosition = std::int32_t(i);
			}
		}
		if (spritePosition < 0) {
			return false;
		}

		// Every layer gets a proxy, the proxies push what's drawn into the layers every frame (see SyncLayerProperties())
		for (std::int32_t i = 0; i < layerCount; i++) {
			if (Legacy::jjLAYER* proxy = GetLayerProxy(i)) {
				proxy->Release();
			}
		}
		_layersOutOfOrder.assign(std::size_t(layerCount), true);

		// The sprite layer stays where the objects are, the others are spaced around it as the converter spaces the layers of
		// the original, the farther ones closer together (see Compatibility::JJ2Level)
		const std::int32_t spriteDepth = tileMap->_layers[tileMap->_sprLayerIndex].Description.Depth;
		for (std::size_t i = 0; i < order.size(); i++) {
			std::int32_t index = order[i];
			if (!_layersOutOfOrder[index]) {
				continue;	// Only the first of the same layer counts
			}
			_layersOutOfOrder[index] = false;
			if (index == tileMap->_sprLayerIndex) {
				continue;
			}
			std::int32_t depth = (std::int32_t(i) - spritePosition) * 100;
			if (depth < -200) {
				depth = -200 - (200 - depth) / 20;
			} else if (depth > 300) {
				depth = 300 + (depth - 300) / 20;
			}
			// The sprite layer itself is 50 below the main plane, a layer of depth 0 is at the main plane
			tileMap->_layers[index].Description.Depth = std::uint16_t(std::clamp<std::int32_t>(spriteDepth + 50 - depth, 0, INT16_MAX));
		}
		SyncLayerProperties();
		return true;
	}

	Legacy::jjLAYER* LevelScriptLoader::CreateLayer(std::int32_t width, std::int32_t height)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || tileMap->_sprLayerIndex < 0) {
			return nullptr;
		}

		// It doesn't move with the view, until the script says otherwise, as in JJ2+
		Tiles::LayerDescription description = {};
		description.Depth = tileMap->_layers[tileMap->_sprLayerIndex].Description.Depth;
		description.SpeedX = 0.0f;
		description.SpeedY = 0.0f;
		description.SpeedModelX = Tiles::LayerSpeedModel::Default;
		description.SpeedModelY = Tiles::LayerSpeedModel::Default;
		description.RendererType = Tiles::LayerRendererType::Default;
		description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);

		std::int32_t index = AddScriptLayer(Vector2i(width, height), description);
		return (index >= 0 ? MakeScriptLayerProxy(index, true) : nullptr);
	}

	Legacy::jjLAYER* LevelScriptLoader::CopyLayer(const Legacy::jjLAYER& source)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || source._layerIndex < 0 || source._layerIndex >= std::int32_t(tileMap->_layers.size())) {
			return nullptr;
		}

		// Taken first, adding a layer can move the layers in memory
		Vector2i size = tileMap->_layers[source._layerIndex].LayoutSize;
		Tiles::LayerDescription description = tileMap->_layers[source._layerIndex].Description;
		std::int32_t index = AddScriptLayer(size, description);
		if (index < 0) {
			return nullptr;
		}
		CopyLayerTiles(source._layerIndex, index, 0);

		Legacy::jjLAYER* layer = MakeScriptLayerProxy(index, source.hasTiles);
		// What the proxy pushes into the layer, which a script may have changed since it was pushed last
		*layer = source;
		// The sprite mode is the one thing the copy doesn't take, as in JJ2+
		layer->_spriteMode = Legacy::spriteType_NORMAL;
		layer->_hasSpriteMode = false;
		// The texture too, and how it's drawn - a layer of a script is made drawn as tiles (see TileMap::AddLayer())
		tileMap->SetLayerTexture(index, tileMap->GetLayerTexture(source._layerIndex));
		layer->ApplyTexture();
		return layer;
	}

	bool LevelScriptLoader::CreateLayersFromLevel(StringView fileName, ArrayView<const std::uint32_t> layerNumbers, std::int32_t tileIdAdjustment,
		SmallVectorImpl<Legacy::jjLAYER*>& layers)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || layerNumbers.empty()) {
			return false;
		}
		for (std::uint32_t number : layerNumbers) {
			if (number < 1 || number > Formats::LayerNumbersChunk::OriginalLayerCount) {
				return false;
			}
		}

		std::size_t firstLayer = layers.size();
		auto fail = [&layers, firstLayer]() {
			// The layers made so far are used again, nothing refers to them
			for (std::size_t i = firstLayer; i < layers.size(); i++) {
				layers[i]->Release();
			}
			layers.erase(layers.begin() + firstLayer, layers.end());
			return false;
		};

		if (IsCurrentLevelFile(fileName)) {
			// The layers of this level are here already, as they are now
			SmallVector<std::int32_t, Formats::LayerNumbersChunk::OriginalLayerCount> sources;
			for (std::uint32_t number : layerNumbers) {
				std::int32_t sourceIndex = GetLayerIndex(std::int32_t(number));
				if (sourceIndex < 0) {
					return false;
				}
				sources.push_back(sourceIndex);
			}
			for (std::int32_t sourceIndex : sources) {
				Vector2i size = tileMap->_layers[sourceIndex].LayoutSize;
				Tiles::LayerDescription description = tileMap->_layers[sourceIndex].Description;
				bool hasTiles = tileMap->_layers[sourceIndex].Layout.HasAnyTile();
				std::int32_t index = AddScriptLayer(size, description);
				if (index < 0) {
					return fail();
				}
				CopyLayerTiles(sourceIndex, index, tileIdAdjustment);
				layers.push_back(MakeScriptLayerProxy(index, hasTiles));
			}
			return true;
		}

		// Any other level from its original file, which isn't converted
		if (!IsScriptFileNameSafe(fileName)) {
			return false;
		}
		String path = fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), fileName));
		if (!fs::IsReadableFile(path)) {
			LOGW("Cannot find level \"{}\" to take layers of", fileName);
			return false;
		}
		Compatibility::JJ2Level level;
		if (!level.Open(path, false)) {
			return false;
		}

		const std::int32_t sourceAnimatedTilesOffset = level.GetAnimatedTilesOffset();
		const std::int32_t animatedTilesOffset = std::int32_t(tileMap->_animatedTilesOffset);
		SmallVector<Formats::TileRecord, 0> tiles;
		for (std::uint32_t number : layerNumbers) {
			Formats::LayerRecord record;
			if (!level.ConvertLayer(std::int32_t(number) - 1, record, tiles)) {
				return fail();
			}
			std::int32_t index = AddScriptLayer(Vector2i(record.Width, record.Height), Tiles::LayerDescription::FromRecord(record, ILevelHandler::MainPlaneZ));
			if (index < 0) {
				return fail();
			}

			// An animated tile is the animated tile of this level of the same index
			Tiles::LayerLayout& layout = tileMap->_layers[index].Layout;
			for (std::size_t i = 0; i < tiles.size(); i++) {
				std::int32_t tileId = tiles[i].TileId;
				if (tileId == 0) {
					continue;
				}
				tileId = (tileId < sourceAnimatedTilesOffset ? tileId + tileIdAdjustment : animatedTilesOffset + (tileId - sourceAnimatedTilesOffset));
				if (tileId > 0 && tileId <= UINT16_MAX) {
					layout.Set(i, TileMap::MakeLayerTile(tiles[i].Flags, std::uint16_t(tileId)));
				}
			}
			layers.push_back(MakeScriptLayerProxy(index, (record.Flags & Formats::LayerRecord::Visible) != 0));
		}
		return true;
	}

	bool LevelScriptLoader::AddTilesFromTileSet(StringView fileName, std::uint32_t firstTileId, std::uint32_t tileCount, const std::uint8_t* paletteRemapping)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || firstTileId > UINT16_MAX || tileCount > UINT16_MAX) {
			return false;
		}

		String name = GetTileSetName(fileName);
		if (name.empty() || !tileMap->AppendTileSet(name, std::uint16_t(firstTileId), std::uint16_t(tileCount), paletteRemapping)) {
			return false;
		}
		numberOfTiles = std::uint32_t(tileMap->GetLoadedTileCount());
		return true;
	}

	Legacy::jjWEAPON* LevelScriptLoader::GetWeapon(std::int32_t number)
	{
		constexpr std::int32_t Count = std::int32_t(WeaponType::Count);
		if (!_weaponsInitialized) {
			_weaponsInitialized = true;
			// As JJ2+ starts a level: the blaster has infinite ammo and gives way to a shield, the rest of them have the
			// default maximum (-1, 99 in single player and 50 in multiplayer), as JJ2+ documents them
			for (std::int32_t i = 0; i <= Count; i++) {
				Legacy::jjWEAPON& weapon = _weapons[i];
				weapon = {};
				weapon.infinite = (i == 1);
				weapon.replacedByShield = (i == 1);
				weapon.comesFromGunCrates = (i >= 2 && i <= 6);
				weapon.multiplier = 1;
				weapon.maximum = -1;
				weapon.defaultSample = true;
				weapon.allowed = true;
				weapon.allowedPowerup = true;
				switch (i) {
					case 3: weapon.spread = wspNORMALORDIRECTIONANDAIM; break;
					case 4: weapon.style = wsMISSILE; break;
					case 5: weapon.style = wsMISSILE; weapon.spread = wspDOUBLEORTRIPLE; break;
					case 6: weapon.style = wsPOPCORN; weapon.spread = wspREFLECTSFASTFIRE; break;
					case 7: weapon.style = wsCAPPED; break;
					case 8: weapon.spread = wspNORMALORBBGUN; break;
				}
			}
		}
		// The entry 0 of JJ2+ is no weapon, it's the scratch one too
		return &_weapons[number >= 1 && number <= Count ? number : 0];
	}

	bool LevelScriptLoader::AddTrueColorTilesFromTileSet(StringView fileName, std::uint32_t firstTileId, std::uint32_t tileCount, const Legacy::jjPAL* palette)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || firstTileId > UINT16_MAX || tileCount > UINT16_MAX) {
			return false;
		}
		String name = GetTileSetName(fileName);
		if (name.empty()) {
			return false;
		}

		// The colors of the 8-bit tiles, those of the palette or of the tile set's own, index 0 is transparent either way
		auto colors = std::make_unique<std::uint32_t[]>(ContentResolver::ColorsPerPalette);
		if (palette != nullptr) {
			for (std::int32_t i = 0; i < ContentResolver::ColorsPerPalette; i++) {
				const jjPALCOLOR& color = palette->getConstColor(std::uint8_t(i));
				colors[i] = std::uint32_t(color.red) | (std::uint32_t(color.green) << 8) | (std::uint32_t(color.blue) << 16) |
					(i == 0 ? 0u : 0xFF000000u);
			}
		} else {
			auto s = ContentResolver::Get().OpenTileSetFile(name);
			if (s == nullptr || !Formats::TileSetFile::ReadPalette(*s, colors.get())) {
				return false;
			}
		}

		if (!tileMap->AppendTileSet(name, std::uint16_t(firstTileId), std::uint16_t(tileCount), nullptr, colors.get())) {
			return false;
		}
		// What scripts read of the tiles is in the same colors (see ResolveTileSetPixels())
		std::size_t partCount = tileMap->_tileSets.size();
		if (_tileSetPixels.size() < partCount) {
			_tileSetPixels.resize(partCount);
		}
		_tileSetPixels[partCount - 1].TrueColorPalette = Death::move(colors);
		numberOfTiles = std::uint32_t(tileMap->GetLoadedTileCount());
		return true;
	}

	bool LevelScriptLoader::GetPlusTexture(std::int32_t index, std::uint8_t* pixels)
	{
		constexpr std::int32_t TextureBytes = Formats::TexturedLayerChunk::TextureSize * Formats::TexturedLayerChunk::TextureSize;
		if (!_plusTexturesLoaded) {
			_plusTexturesLoaded = true;
			// As the converter takes them for the layers of the levels (see Compatibility::AssetConverter), only the textures
			// are kept of the file
			String path = fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), "Plus.j2d"_s));
			Compatibility::JJ2Data data;
			if (fs::IsReadableFile(path) && data.Open(path, false)) {
				for (Compatibility::JJ2Data::Item& item : data.Items) {
					if (item.Filename.hasPrefix("TextureBG."_s) && item.Size == TextureBytes) {
						StringView number = item.Filename.exceptPrefix("TextureBG."_s);
						_plusTextures.push_back({ std::int32_t(stou32(number.data(), number.size())), Death::move(item.Blob) });
					}
				}
			}
		}
		for (const PlusTexture& texture : _plusTextures) {
			if (texture.Index == index) {
				std::memcpy(pixels, texture.Pixels.get(), TextureBytes);
				return true;
			}
		}
		return false;
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

	std::int32_t LevelScriptLoader::AddScriptLayer(Vector2i size, const Tiles::LayerDescription& description)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr) {
			return -1;
		}
		const std::int32_t layerCount = std::int32_t(tileMap->_layers.size());
		if (_firstScriptLayer < 0) {
			_firstScriptLayer = layerCount;
		}

		// A layer of a script that no script refers to and that isn't drawn can't be drawn ever again, so it's used again
		for (std::int32_t i = _firstScriptLayer; i < layerCount; i++) {
			auto it = _layerProxies.find(i);
			if (it == _layerProxies.end() || it->second->_refCount > 1 || std::size_t(i) >= _layersOutOfOrder.size() || !_layersOutOfOrder[i]) {
				continue;
			}
			if (tileMap->ResetLayer(i, size, description)) {
				it->second->Release();
				_layerProxies.erase(it);
				return i;
			}
		}
		return tileMap->AddLayer(size, description);
	}

	Legacy::jjLAYER* LevelScriptLoader::MakeScriptLayerProxy(std::int32_t index, bool hasTiles)
	{
		// Out of the order until a script puts it there, the layers before it stay as they are
		std::size_t layerCount = _levelHandler->_tileMap->_layers.size();
		if (_layersOutOfOrder.size() < layerCount) {
			_layersOutOfOrder.resize(layerCount, false);
		}
		_layersOutOfOrder[index] = true;

		Legacy::jjLAYER* layer = GetLayerProxy(index);
		// JJ2+ has a tile map for every layer a script makes, empty or not
		layer->hasTileMap = true;
		layer->hasTiles = hasTiles;
		return layer;
	}

	void LevelScriptLoader::CopyLayerTiles(std::int32_t sourceIndex, std::int32_t targetIndex, std::int32_t tileIdAdjustment)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		const Tiles::TileMapLayer& source = tileMap->_layers[sourceIndex];
		Tiles::TileMapLayer& target = tileMap->_layers[targetIndex];
		const std::int32_t animatedTilesOffset = std::int32_t(tileMap->_animatedTilesOffset);
		const std::size_t count = std::min(source.Layout.size(), target.Layout.size());
		for (std::size_t i = 0; i < count; i++) {
			std::int32_t tileId = source.Layout.TileIDAt(i);
			if (tileId == 0) {
				continue;
			}
			if (tileId < animatedTilesOffset) {
				tileId += tileIdAdjustment;
				if (tileId <= 0 || tileId > UINT16_MAX) {
					continue;
				}
			}
			// Only what's drawn, the tiles of a copy aren't solid, destructible or anything else
			const Tiles::LayerTile& tile = source.Layout[i];
			Tiles::LayerTile copy = TileMap::MakeLayerTile(0, std::uint16_t(tileId));
			copy.Flags = tile.Flags & (Tiles::LayerTileFlags::FlipX | Tiles::LayerTileFlags::FlipY | Tiles::LayerTileFlags::Heat);
			copy.Alpha = tile.Alpha;
			target.Layout.Set(i, copy);
		}
	}

	bool LevelScriptLoader::IsCurrentLevelFile(StringView fileName) const
	{
		String name = fs::GetFileNameWithoutExtension(fileName);
		StringUtils::lowercaseInPlace(name);
		return (name == fs::GetFileName(_levelHandler->GetLevelName()));
	}

	Tiles::LayerDescription* LevelScriptLoader::GetLayerDescription(std::int32_t layerNumber)
	{
		std::int32_t index = GetLayerIndex(layerNumber);
		return (index >= 0 ? &_levelHandler->_tileMap->_layers[index].Description : nullptr);
	}

	Tiles::TileMapLayer* LevelScriptLoader::ResolveLayerNumber(std::int32_t layerNumber, Legacy::jjLAYER** proxy)
	{
		*proxy = nullptr;
		std::int32_t index = GetLayerIndex(layerNumber);
		if (index < 0) {
			return nullptr;
		}
		auto it = _layerProxies.find(index);
		if (it != _layerProxies.end()) {
			*proxy = it->second;
		}
		return &_levelHandler->_tileMap->_layers[index];
	}

	std::int32_t LevelScriptLoader::ResolveAnimatedTile(std::uint16_t tileID) const
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || (tileID & TileMap::TileFlagAnimated) == 0) {
			return -1;
		}
		std::int32_t index = (tileID & TileMap::TileIndexMask);
		return (index < std::int32_t(tileMap->_animatedTiles.size()) ? index : -1);
	}

	Legacy::jjTILE* LevelScriptLoader::GetTileProxy(std::uint16_t tileID)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr) {
			return nullptr;
		}
		// The flips are of where the tile is placed, not of the tile
		std::uint16_t key = std::uint16_t(tileID & (TileMap::TileIndexMask | TileMap::TileFlagAnimated));
		if ((key & TileMap::TileFlagAnimated) != 0 ? ResolveAnimatedTile(key) < 0 : key >= tileMap->GetStaticTileCount()) {
			return nullptr;
		}

		Legacy::jjTILE* tile;
		auto it = _tileProxies.find(key);
		if (it != _tileProxies.end()) {
			tile = it->second;
		} else {
			void* mem = asAllocMem(sizeof(Legacy::jjTILE));
			tile = new(mem) Legacy::jjTILE(key);
			// The map keeps this reference, the script gets another one below
			_tileProxies.emplace(key, tile);
		}
		tile->AddRef();
		return tile;
	}

	void LevelScriptLoader::GetTileFrames(std::uint16_t tileID, SmallVectorImpl<std::uint16_t>& frames) const
	{
		frames.clear();
		std::int32_t index = ResolveAnimatedTile(tileID);
		if (index < 0) {
			// A static tile is its only frame
			frames.push_back(tileID);
			return;
		}

		auto tileMap = _levelHandler->_tileMap.get();
		const AnimatedTile& animTile = tileMap->_animatedTiles[index];
		const std::int32_t animatedTilesOffset = std::int32_t(tileMap->_animatedTilesOffset);
		auto toScript = [animatedTilesOffset](std::uint16_t engineId) {
			// A frame can be an animated tile itself in a level of this game
			return (engineId >= animatedTilesOffset
				? std::uint16_t(((engineId - animatedTilesOffset) & TileMap::TileIndexMask) | TileMap::TileFlagAnimated)
				: std::uint16_t(engineId & TileMap::TileIndexMask));
		};

		// One loop as TileMap::OnUpdate() plays it, a frame for each frame duration: the first frame takes the wait too,
		// and in ping-pong the animation turns at the last frame after its wait, and at the first one after the other
		const std::int32_t count = std::int32_t(animTile.Tiles.size());
		const std::int32_t delay = std::max<std::int32_t>(animTile.Delay, 0);
		if (count < 2) {
			for (const AnimatedTileFrame& frame : animTile.Tiles) {
				frames.push_back(toScript(frame.TileID));
			}
			return;
		}
		if (!animTile.IsPingPong) {
			for (std::int32_t i = 0; i < 1 + delay; i++) {
				frames.push_back(toScript(animTile.Tiles[0].TileID));
			}
			for (std::int32_t i = 1; i < count; i++) {
				frames.push_back(toScript(animTile.Tiles[i].TileID));
			}
		} else {
			for (std::int32_t i = 0; i < 2 + delay; i++) {
				frames.push_back(toScript(animTile.Tiles[0].TileID));
			}
			for (std::int32_t i = 1; i < count - 1; i++) {
				frames.push_back(toScript(animTile.Tiles[i].TileID));
			}
			for (std::int32_t i = 0; i < 2 + std::max<std::int32_t>(animTile.PingPongDelay, 0); i++) {
				frames.push_back(toScript(animTile.Tiles[count - 1].TileID));
			}
			for (std::int32_t i = count - 2; i > 0; i--) {
				frames.push_back(toScript(animTile.Tiles[i].TileID));
			}
		}
	}

	bool LevelScriptLoader::SetTileFrames(std::uint16_t tileID, ArrayView<const std::uint16_t> frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait)
	{
		std::int32_t index = ResolveAnimatedTile(tileID);
		if (index < 0 || frames.empty()) {
			return false;
		}

		auto tileMap = _levelHandler->_tileMap.get();
		const AnimatedTile& animTile = tileMap->_animatedTiles[index];
		Formats::AnimatedTileRecord record;
		// The speed stays as it was
		record.FrameDuration16 = std::uint16_t(std::clamp(animTile.FrameDuration * 16.0f, 0.0f, float(UINT16_MAX)));
		record.Delay = wait;
		record.DelayJitter = randomWait;
		record.PingPongDelay = pingPongWait;
		record.IsPingPong = (pingPong ? 1 : 0);
		// More than 64 frames are undefined in JJ2+, the file has room for 255
		const std::size_t count = std::min<std::size_t>(frames.size(), UINT8_MAX);
		for (std::size_t i = 0; i < count; i++) {
			const std::uint16_t value = frames[i];
			Formats::TileRecord& frame = record.Frames.emplace_back();
			frame.TileId = std::uint16_t((value & TileMap::TileFlagAnimated) != 0
				? tileMap->_animatedTilesOffset + (value & TileMap::TileIndexMask)
				: (value & TileMap::TileIndexMask));
			frame.Flags = std::uint8_t(((value & TileMap::TileFlagFlipX) != 0 ? Formats::TileRecord::FlipX : 0) |
				((value & TileMap::TileFlagFlipY) != 0 ? Formats::TileRecord::FlipY : 0));
		}
		tileMap->SetAnimatedTile(index, record);
		return true;
	}

	std::uint8_t LevelScriptLoader::GetTileFps(std::uint16_t tileID) const
	{
		std::int32_t index = ResolveAnimatedTile(tileID);
		if (index < 0) {
			return 0;
		}
		// The converter makes the duration of a frame 50 / fps (see Compatibility::JJ2Level)
		float duration = _levelHandler->_tileMap->_animatedTiles[index].FrameDuration;
		return (duration > 0.0f ? std::uint8_t(std::clamp(50.0f / duration + 0.5f, 0.0f, 255.0f)) : 0);
	}

	void LevelScriptLoader::SetTileFps(std::uint16_t tileID, std::uint8_t fps)
	{
		std::int32_t index = ResolveAnimatedTile(tileID);
		if (index < 0) {
			return;
		}
		AnimatedTile& animTile = _levelHandler->_tileMap->_animatedTiles[index];
		animTile.FrameDuration = (fps > 0 ? 50.0f / fps : 0.0f);
		animTile.FramesLeft = std::min(animTile.FramesLeft, animTile.FrameDuration);
	}

	// Makes the 8-bit tiles of a tile set true color tiles in the given colors, as jjTilesFromTileset32() imports them -
	// except the empty ones, which aren't 32-bit tiles in JJ2+ either
	static void MakeTrueColorTiles(Formats::TileSetData& data, const std::uint32_t* palette)
	{
		constexpr std::int32_t PixelCount = TileSet::DefaultTileSize * TileSet::DefaultTileSize;
		for (std::int32_t tile = 0; tile < data.TileCount; tile++) {
			if (data.TrueColor[tile] != 0) {
				continue;
			}
			std::uint8_t* image = &data.Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
			bool isEmpty = true;
			for (std::int32_t i = 0; i < PixelCount && isEmpty; i++) {
				isEmpty = (image[i] == 0);
			}
			if (isEmpty) {
				continue;
			}
			// In place from the end, the indices are at the start of the room of the tile and a color takes 4 bytes
			for (std::int32_t i = PixelCount - 1; i >= 0; i--) {
				std::uint8_t index = image[i];
				std::uint32_t color = (index != 0 ? (palette[index] | 0xFF000000u) : 0u);
				image[i * 4] = std::uint8_t(color);
				image[i * 4 + 1] = std::uint8_t(color >> 8);
				image[i * 4 + 2] = std::uint8_t(color >> 16);
				image[i * 4 + 3] = std::uint8_t(color >> 24);
			}
			data.TrueColor[tile] = 1;
		}
	}

	Formats::TileSetData* LevelScriptLoader::ResolveTileSetPixels(std::int32_t tileId, std::int32_t& fileTileId)
	{
		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || tileId < 0 || tileId >= tileMap->GetStaticTileCount()) {
			return nullptr;
		}

		// The same walk as TileMap::ResolveTileSet()
		std::int32_t partCount = std::int32_t(tileMap->_tileSets.size());
		for (std::int32_t i = 0; i < partCount; i++) {
			auto& part = tileMap->_tileSets[i];
			if (tileId >= part.Count) {
				tileId -= part.Count;
				continue;
			}

			if (std::int32_t(_tileSetPixels.size()) < partCount) {
				_tileSetPixels.resize(partCount);
			}
			auto& pixels = _tileSetPixels[i];
			if (!pixels.Loaded) {
				pixels.Loaded = true;
				// A tile set stored in the level has only a name, a file of the same name would be the wrong one
				if (part.Data != nullptr && !part.Embedded && !part.Data->FilePath.empty()) {
					auto s = ContentResolver::Get().OpenTileSetFile(part.Data->FilePath);
					if (s != nullptr) {
						auto data = std::make_unique<Formats::TileSetData>();
						if (Formats::TileSetFile::Read(*s, *data)) {
							if (pixels.TrueColorPalette != nullptr) {
								MakeTrueColorTiles(*data, pixels.TrueColorPalette.get());
							}
							pixels.Data = Death::move(data);
						} else {
							LOGW("Cannot read pixels of tile set \"{}\" for the script", part.Data->FilePath);
						}
					}
				}
			}

			fileTileId = tileId + part.Offset;
			if (pixels.Data == nullptr || fileTileId < 0 || fileTileId >= pixels.Data->TileCount) {
				return nullptr;
			}
			return pixels.Data.get();
		}
		return nullptr;
	}

	bool LevelScriptLoader::GetTilePixels(std::int32_t tileId, std::uint8_t* pixels, const Legacy::jjPAL* palette)
	{
		constexpr std::int32_t PixelCount = TileSet::DefaultTileSize * TileSet::DefaultTileSize;

		std::int32_t fileTileId;
		Formats::TileSetData* data = ResolveTileSetPixels(tileId, fileTileId);
		if (data == nullptr) {
			return false;
		}

		const std::uint8_t* image = &data->Images[std::size_t(fileTileId) * Formats::TileSetData::ImageBytesPerTile];
		bool trueColor = (std::size_t(fileTileId) < data->TrueColor.size() && data->TrueColor[fileTileId] != 0);
		if (!trueColor) {
			std::memcpy(pixels, image, PixelCount);
			return true;
		}

		// A 32-bit pixel of JJ2+ that isn't transparent becomes the nearest color of the palette
		Legacy::jjPAL& targetPalette = const_cast<Legacy::jjPAL&>(palette != nullptr ? *palette : jjPalette);
		for (std::int32_t i = 0; i < PixelCount; i++) {
			const std::uint8_t* rgba = &image[i * 4];
			if (rgba[3] == 0) {
				pixels[i] = 0;
			} else {
				jjPALCOLOR color;
				color.red = rgba[0];
				color.green = rgba[1];
				color.blue = rgba[2];
				pixels[i] = targetPalette.findNearestColor(color);
			}
		}
		return true;
	}

	bool LevelScriptLoader::SetTilePixels(std::int32_t tileId, const std::uint8_t* pixels)
	{
		constexpr std::int32_t PixelCount = TileSet::DefaultTileSize * TileSet::DefaultTileSize;

		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || tileId < 0 || tileId >= tileMap->GetStaticTileCount()) {
			return false;
		}

		// Remembered, so a pixel map made of the tile later gets what was saved into it
		std::int32_t fileTileId;
		Formats::TileSetData* data = ResolveTileSetPixels(tileId, fileTileId);
		if (data != nullptr) {
			std::memcpy(&data->Images[std::size_t(fileTileId) * Formats::TileSetData::ImageBytesPerTile], pixels, PixelCount);
			if (std::size_t(fileTileId) < data->TrueColor.size()) {
				data->TrueColor[fileTileId] = 0;
			}
		}

		ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, tileId, pixels);
		return true;
	}

	bool LevelScriptLoader::GetTileColors(std::int32_t tileId, Legacy::jjPALCOLOR* colors, const Legacy::jjPAL& palette)
	{
		constexpr std::int32_t PixelCount = TileSet::DefaultTileSize * TileSet::DefaultTileSize;

		std::int32_t fileTileId;
		Formats::TileSetData* data = ResolveTileSetPixels(tileId, fileTileId);
		if (data == nullptr) {
			return false;
		}

		const std::uint8_t* image = &data->Images[std::size_t(fileTileId) * Formats::TileSetData::ImageBytesPerTile];
		bool trueColor = (std::size_t(fileTileId) < data->TrueColor.size() && data->TrueColor[fileTileId] != 0);
		for (std::int32_t i = 0; i < PixelCount; i++) {
			if (trueColor) {
				const std::uint8_t* rgba = &image[i * 4];
				colors[i] = jjPALCOLOR{rgba[0], rgba[1], rgba[2], rgba[3]};
			} else if (image[i] != 0) {
				colors[i] = palette.getConstColor(image[i]);
				colors[i].alpha = 255;
			} else {
				colors[i] = jjPALCOLOR{};
			}
		}
		return true;
	}

	bool LevelScriptLoader::SetTileColors(std::int32_t tileId, const Legacy::jjPALCOLOR* colors)
	{
		constexpr std::int32_t PixelCount = TileSet::DefaultTileSize * TileSet::DefaultTileSize;

		auto tileMap = _levelHandler->_tileMap.get();
		if (tileMap == nullptr || tileId < 0 || tileId >= tileMap->GetStaticTileCount()) {
			return false;
		}

		if (tileMap->IsTileSetIndexed(tileId)) {
			// JJ2+ draws every tile in true color from then on, a tile set drawn indexed here can't have such a tile
			if (!_warnedIndexedTileColors) {
				_warnedIndexedTileColors = true;
				LOGW("Tile {} is drawn palette-indexed, a 32-bit image saved into it gets the nearest colors of the palette", tileId);
			}
			std::uint8_t indices[PixelCount];
			for (std::int32_t i = 0; i < PixelCount; i++) {
				indices[i] = (colors[i].alpha != 0 ? jjPalette.FindNearestOpaqueColor(colors[i]) : 0);
			}
			return SetTilePixels(tileId, indices);
		}

		std::uint8_t rgba[PixelCount * 4];
		for (std::int32_t i = 0; i < PixelCount; i++) {
			rgba[i * 4] = colors[i].red;
			rgba[i * 4 + 1] = colors[i].green;
			rgba[i * 4 + 2] = colors[i].blue;
			rgba[i * 4 + 3] = colors[i].alpha;
		}
		if (!ContentResolver::Get().ApplyTileDiffuseOverrideRgba(*tileMap, tileId, rgba)) {
			return false;
		}

		// Remembered, so a color map made of the tile later gets what was saved into it
		std::int32_t fileTileId;
		Formats::TileSetData* data = ResolveTileSetPixels(tileId, fileTileId);
		if (data != nullptr) {
			std::memcpy(&data->Images[std::size_t(fileTileId) * Formats::TileSetData::ImageBytesPerTile], rgba, sizeof(rgba));
			if (std::size_t(fileTileId) < data->TrueColor.size()) {
				data->TrueColor[fileTileId] = 1;
			}
		}
		return true;
	}

	bool LevelScriptLoader::IsTileTrueColor(std::int32_t tileId)
	{
		std::int32_t fileTileId;
		Formats::TileSetData* data = ResolveTileSetPixels(tileId, fileTileId);
		return (data != nullptr && std::size_t(fileTileId) < data->TrueColor.size() && data->TrueColor[fileTileId] != 0);
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
			desc.SpeedModelX = FromScriptSpeedModel(layer->SpeedModeX);
			desc.SpeedModelY = FromScriptSpeedModel(layer->SpeedModeY);
			desc.SpeedX = layer->xSpeed;
			desc.SpeedY = layer->ySpeed;
			// Per tick of JJ2+, the layer moves by its auto speed each frame (see Tiles::LayerDescription::FromOriginalAutoSpeed())
			desc.AutoSpeedX = Tiles::LayerDescription::FromOriginalAutoSpeed(layer->xAutoSpeed, desc.SpeedModelX);
			desc.AutoSpeedY = Tiles::LayerDescription::FromOriginalAutoSpeed(layer->yAutoSpeed, desc.SpeedModelY);
			desc.RepeatX = layer->tileWidth;
			desc.RepeatY = layer->tileHeight;
			desc.UseInherentOffset = layer->limitVisibleRegion;
			desc.TextureInnerSpeed = Vector2f(layer->xInnerSpeed, layer->yInnerSpeed);
			desc.TextureInnerAutoSpeed = Vector2f(layer->xInnerAutoSpeed, layer->yInnerAutoSpeed) * Tiles::LayerDescription::OriginalAutoSpeedScale;
			// Drawn if the script says so and it's in the order of the layers (see SetLayerOrder())
			bool outOfOrder = (std::size_t(index) < _layersOutOfOrder.size() && _layersOutOfOrder[index]);
			tileMap->_layers[index].Visible = (layer->hasTiles && !outOfOrder);

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
				buffer = std::make_unique<AudioBuffer>(Death::move(stream), path);
			}
		}

		AudioBuffer* result = buffer.get();
		_scriptSamples.emplace(sampleId, Death::move(buffer));
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

		auto player = _levelHandler->PlaySfx(nullptr, {}, buffer, Vector3f(pos.X, pos.Y, 0.0f), sourceRelative, gain, pitch, SfxFlags::None);
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
		auto buffer = std::make_unique<AudioBuffer>(Death::move(stream), path);

		auto it = _scriptSamples.find(sampleId);
		if (it != _scriptSamples.end()) {
			it->second = Death::move(buffer);
		} else {
			_scriptSamples.emplace(sampleId, Death::move(buffer));
		}
		return true;
#else
		return false;
#endif
	}

	Legacy::AnimationTables& LevelScriptLoader::GetAnimationTables()
	{
		if (_animTables == nullptr) {
			_animTables = std::make_unique<Legacy::AnimationTables>(versionTSF);
		}
		return *_animTables;
	}

	std::int32_t LevelScriptLoader::AddObjectFromEvent(std::uint8_t eventId, float xPixel, float yPixel)
	{
		// The converters that read the level get an empty one, see Legacy::EventTranslator
		auto result = GetEventTranslator().FromOriginal(eventId, 0);
		if (result.Type == EventType::Empty) {
			return 0;
		}

		auto actor = _levelHandler->EventSpawner()->SpawnEvent(result.Type, result.Params, Actors::ActorState::None,
			Vector3i((std::int32_t)xPixel, (std::int32_t)yPixel, ILevelHandler::MainPlaneZ));
		if (actor == nullptr) {
			return 0;
		}

		// The object gets its object ID as every object of an event, see OnActorAdded()
		_levelHandler->AddActor(actor);
		return _lastObjectId;
	}

	Actors::ActorBase* LevelScriptLoader::GetScriptObject(std::int32_t objectId)
	{
		if (objectId <= 0 || objectId >= std::int32_t(_objectSlots.size())) {
			return nullptr;
		}
		// Returns nullptr if the actor has since been destroyed (the registry holds only weak references)
		return _objectSlots[objectId].Actor.lock().get();
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

		// The object ID keeps a reference of its own
		obj->AddRef();
		std::int32_t id = RegisterObject(wrapper, obj, eventId);
		if (id == 0) {
			obj->Release();
		}
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
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetWeapon(index);
	}

	jjCHARACTER* get_jjCHARACTER(int index) {
		static jjCHARACTER dummy;
		return &dummy;
	}

	// Returns the bits of an event word a parameter of JJ2+ means, by its offset from the first bit of the parameters and
	// its length, negative for a signed one, as JCS.ini writes them; `false` if they aren't in the word
	static bool GetEventParamBits(std::int32_t offset, std::int32_t length, std::int32_t& shift, std::uint32_t& mask) {
		std::int32_t bits = (length < 0 ? -length : length);
		shift = Legacy::EventTranslator::ParamsShift + offset;
		if (bits <= 0 || shift < 0 || shift + bits > 32) {
			return false;
		}
		mask = (bits >= 32 ? UINT32_MAX : (1u << bits) - 1);
		return true;
	}

	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return std::int32_t(owner->GetOriginalEvent(tx, ty) & 0xFF);
	}
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length) {
		std::int32_t shift; std::uint32_t mask;
		if (!GetEventParamBits(offset, length, shift, mask)) {
			return 0;
		}

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t value = (owner->GetOriginalEvent(tx, ty) >> shift) & mask;
		if (length < 0 && mask != UINT32_MAX && (value & ((mask >> 1) + 1)) != 0) {
			// A signed parameter, two's complement in its own length
			return std::int32_t(std::int64_t(value) - std::int64_t(mask) - 1);
		}
		return std::int32_t(value);
	}
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId) {
		// Only the event changes, the parameters stay as they are
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t word = owner->GetOriginalEvent(tx, ty);
		owner->SetOriginalEvent(tx, ty, (word & ~0xFFu) | newEventId);
	}
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, std::int8_t offset, std::int8_t length, std::int32_t newValue) {
		std::int32_t shift; std::uint32_t mask;
		if (!GetEventParamBits(offset, length, shift, mask)) {
			return;
		}

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t word = owner->GetOriginalEvent(tx, ty);
		word = (word & ~(mask << shift)) | ((std::uint32_t(newValue) & mask) << shift);
		owner->SetOriginalEvent(tx, ty, word);
	}
	std::int8_t GetTileType(std::uint16_t tile) {
		noop(); return 0;
	}
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value) {
		noop(); return 0;
	}
	bool GetTileIs32Bit(std::uint16_t tile) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->IsTileTrueColor(tile);
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
		return (tileMap != nullptr ? tileMap->GetTile(owner->GetLayerIndex(layer), xTile, yTile) : 0);
	}
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap != nullptr && tileMap->SetTile(owner->GetLayerIndex(layer), xTile, yTile, newTile)) {
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
		return Legacy::AnimationTables::CustomFirstSetID + index;
	}
}

#endif
