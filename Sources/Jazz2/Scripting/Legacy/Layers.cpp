#if defined(WITH_ANGELSCRIPT)

#include "Layers.h"
#include "AnimationTables.h"
#include "Enums.h"
#include "Internal.h"
#include "Stream.h"

#include "../../Actors/ActorBase.h"
#include "../../Actors/Player.h"
#include "../../Compatibility/JJ2Data.h"
#include "../../Compatibility/JJ2Level.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <IO/FileSystem.h>

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
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

	namespace Legacy
	{
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
				constexpr std::size_t Size = std::size_t(Formats::LayerPropertiesChunk::TextureSize) * Formats::LayerPropertiesChunk::TextureSize;
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
			if (Tiles::LayerDescription::IsTexturedBackground(description->RendererType) && description->TextureMapping > 0) {
				return (description->Blend == Tiles::LayerBlend::Translucent ? spriteType_TRANSLUCENTMAPPING : spriteType_MAPPING);
			}
			switch (description->Blend) {
				case Tiles::LayerBlend::Screen: return spriteType_BLENDSCREEN;
				case Tiles::LayerBlend::Multiply: return spriteType_BLENDMULTIPLY;
				case Tiles::LayerBlend::ChromaKey: return spriteType_CHROMAKEY;
				case Tiles::LayerBlend::Darken: return spriteType_BLENDDARKEN;
				case Tiles::LayerBlend::Lighten: return spriteType_BLENDLIGHTEN;
				case Tiles::LayerBlend::Hue: return spriteType_BLENDHUE;
				case Tiles::LayerBlend::Saturation: return spriteType_BLENDSATURATION;
				case Tiles::LayerBlend::Color: return spriteType_BLENDCOLOR;
				case Tiles::LayerBlend::Luminosity: return spriteType_BLENDLUMINANCE;
				case Tiles::LayerBlend::Dissolve: return spriteType_BLENDDISSOLVE;
				case Tiles::LayerBlend::Overlay: return spriteType_BLENDOVERLAY;
				case Tiles::LayerBlend::HardLight: return spriteType_BLENDHARDLIGHT;
				case Tiles::LayerBlend::SoftLight: return spriteType_BLENDSOFTLIGHT;
				case Tiles::LayerBlend::Difference: return spriteType_BLENDDIFFERENCE;
				case Tiles::LayerBlend::Dodge: return spriteType_BLENDDODGE;
				case Tiles::LayerBlend::Burn: return spriteType_BLENDBURN;
				case Tiles::LayerBlend::Exclusion: return spriteType_BLENDEXCLUSION;
				default: break;
			}
			const bool translucent = (description->Color.W < 1.0f);
			switch (description->RendererType) {
				// SPRITE::INVISIBLE is converted to an ordinary layer of no alpha
				case Tiles::LayerRendererType::Default: return (description->Color.W <= 0.0f ? spriteType_INVISIBLE : (translucent ? spriteType_TRANSLUCENT : spriteType_NORMAL));
				case Tiles::LayerRendererType::Tinted: return spriteType_TINTED;
				case Tiles::LayerRendererType::PaletteShift: return (translucent ? spriteType_TRANSLUCENTPALSHIFT : spriteType_PALSHIFT);
				case Tiles::LayerRendererType::SingleColor: return (translucent ? spriteType_TRANSLUCENTCOLOR : spriteType_SINGLECOLOR);
				case Tiles::LayerRendererType::AlphaMap: return spriteType_ALPHAMAP;
				case Tiles::LayerRendererType::PaletteMapping: return (translucent ? spriteType_TRANSLUCENTMAPPING : spriteType_MAPPING);
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
				if (!SetBlend(description)) {
					LOGW("Sprite mode {} isn't supported for textured layers, layer {} is drawn as it is", value, _layerIndex);
				}
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				return value;
			}

			// Only a layer drawn as tiles takes a mode. The others (x-ray, blur behind, reflection) are drawn by renderers of
			// their own that read the color their own way, so a mode would turn them into plain tiles.
			if (current->RendererType != Tiles::LayerRendererType::Default && current->RendererType != Tiles::LayerRendererType::Tinted &&
				current->RendererType != Tiles::LayerRendererType::PaletteShift && current->RendererType != Tiles::LayerRendererType::SingleColor &&
				current->RendererType != Tiles::LayerRendererType::AlphaMap &&
				current->RendererType != Tiles::LayerRendererType::PaletteMapping) {
				LOGW("Sprite mode {} isn't supported for layer {}, it isn't drawn as tiles", value, _layerIndex);
				return value;
			}

			// The modes a layer can be drawn with, the rest of them aren't supported for layers
			Tiles::LayerDescription description = *current;
			const float param = _spriteParam / 255.0f;
			description.Blend = Tiles::LayerBlend::Normal;
			description.BlendParam = 255;
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
				case spriteType_CHROMAKEY:
					description.RendererType = Tiles::LayerRendererType::Default;
					description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
					description.Blend = Tiles::LayerBlend::ChromaKey;
					description.BlendParam = _spriteParam;
					break;
				case spriteType_ALPHAMAP: description.RendererType = Tiles::LayerRendererType::AlphaMap; description.Color = Vector4f(param, 0.0f, 0.0f, 1.0f); break;
				// The parameter is the mapping, which are the additional palettes of the level, as MLLE sets them up
				case spriteType_MAPPING: description.RendererType = Tiles::LayerRendererType::PaletteMapping; description.Color = Vector4f(param, 0.0f, 0.0f, 1.0f); break;
				case spriteType_TRANSLUCENTMAPPING: description.RendererType = Tiles::LayerRendererType::PaletteMapping; description.Color = Vector4f(param, 0.0f, 0.0f, 0.5f); break;
				// The parameter is how far the result is mixed over what is behind it
				case spriteType_BLENDDARKEN:
				case spriteType_BLENDLIGHTEN:
				case spriteType_BLENDHUE:
				case spriteType_BLENDSATURATION:
				case spriteType_BLENDCOLOR:
				case spriteType_BLENDLUMINANCE:
				case spriteType_BLENDMULTIPLY:
				case spriteType_BLENDSCREEN:
				case spriteType_BLENDDISSOLVE:
				case spriteType_BLENDOVERLAY:
				case spriteType_BLENDHARDLIGHT:
				case spriteType_BLENDSOFTLIGHT:
				case spriteType_BLENDDIFFERENCE:
				case spriteType_BLENDDODGE:
				case spriteType_BLENDBURN:
				case spriteType_BLENDEXCLUSION:
					description.RendererType = Tiles::LayerRendererType::Default;
					description.Color = Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
					description.Blend = Tiles::LayerDescription::FromSpriteMode(value);
					description.BlendParam = _spriteParam;
					break;
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
					SetBlend(description);
					tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				} else if (current->TextureMapping > 0 && current->TextureMapping != std::uint8_t(value + 1)) {
					// SPRITE::MAPPING the level gave it, the parameter is the palette (a script may change it while playing)
					Tiles::LayerDescription description = *current;
					description.TextureMapping = std::uint8_t(value + 1);
					tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
				}
			} else if (current != nullptr && (current->Blend == Tiles::LayerBlend::Screen || current->Blend == Tiles::LayerBlend::Multiply ||
				current->Blend == Tiles::LayerBlend::ChromaKey || Tiles::LayerDescription::IsBehindBlend(current->Blend))) {
				Tiles::LayerDescription description = *current;
				description.BlendParam = value;
				tileMap->SetLayerDescription(_layerIndex, description, tileMap->IsLayerVisible(_layerIndex));
			} else if (current != nullptr && (current->RendererType == Tiles::LayerRendererType::Tinted || current->RendererType == Tiles::LayerRendererType::PaletteShift ||
				current->RendererType == Tiles::LayerRendererType::SingleColor ||
				current->RendererType == Tiles::LayerRendererType::AlphaMap || current->RendererType == Tiles::LayerRendererType::PaletteMapping)) {
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

		bool jjLAYER::SetBlend(Tiles::LayerDescription& description) const {
			// The modes a textured layer can be drawn with over what is behind it, others are drawn as it is
			std::uint32_t mode = (_hasSpriteMode ? _spriteMode : std::uint32_t(spriteType_NORMAL));
			description.BlendParam = 255;
			description.TextureMapping = 0;
			switch (mode) {
				// The parameter is the additional palette the texture is drawn through, half translucent too (as a layer of tiles is)
				case spriteType_MAPPING: description.Blend = Tiles::LayerBlend::Normal; description.TextureMapping = std::uint8_t(_spriteParam + 1); return true;
				case spriteType_TRANSLUCENTMAPPING:
					description.Blend = Tiles::LayerBlend::Translucent;
					description.BlendParam = 128;
					description.TextureMapping = std::uint8_t(_spriteParam + 1);
					return true;
				case spriteType_NORMAL: description.Blend = Tiles::LayerBlend::Normal; return true;
				// Half way between the texture and what is behind it, not as translucent tiles are, measured in JJ2+
				case spriteType_TRANSLUCENT:
				case spriteType_TRANSLUCENTTILE:
					description.Blend = Tiles::LayerBlend::Translucent;
					description.BlendParam = 128;
					return true;
				// The parameter is how opaque the layer is
				case spriteType_BLENDNORMAL: description.Blend = Tiles::LayerBlend::Translucent; description.BlendParam = _spriteParam; return true;
				case spriteType_INVISIBLE: description.Blend = Tiles::LayerBlend::Translucent; description.BlendParam = 0; return true;
				// The parameter is the entry of the palette of the key
				case spriteType_CHROMAKEY: description.Blend = Tiles::LayerBlend::ChromaKey; description.BlendParam = _spriteParam; return true;
				// The parameter is how far the result is mixed over what is behind it, measured in JJ2+
				case spriteType_BLENDSCREEN: description.Blend = Tiles::LayerBlend::Screen; description.BlendParam = _spriteParam; return true;
				case spriteType_BLENDMULTIPLY: description.Blend = Tiles::LayerBlend::Multiply; description.BlendParam = _spriteParam; return true;
				default:
					// The blend modes that take what is behind it, the parameter is how far the result is mixed over it
					description.Blend = Tiles::LayerDescription::FromSpriteMode(mode);
					if (!Tiles::LayerDescription::IsBehindBlend(description.Blend)) {
						description.Blend = Tiles::LayerBlend::Normal;
						return false;
					}
					description.BlendParam = _spriteParam;
					return true;
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
			constexpr std::size_t Size = std::size_t(Formats::LayerPropertiesChunk::TextureSize) * Formats::LayerPropertiesChunk::TextureSize;
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
			constexpr std::int32_t Size = Formats::LayerPropertiesChunk::TextureSize;
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
					// Of a layer other than the sky layer of the original game, as the level has it (see JJ2Level::MakeLayerRecord())
					flags |= (description.TextureFlags & Formats::TexturedLayerParams::FloorOnly);
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
			SetBlend(description);
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
		float jjLAYER::ToStoredOffset(float value) {
			constexpr double FixedOne = 65536.0;
			constexpr double FixedLimit = 2147483648.0;
			double fixed = double(value) * FixedOne;
			if (fixed > FixedLimit || fixed < -FixedLimit) {
				return 0.0f;
			}
			std::int32_t stored = std::int32_t(std::uint32_t(std::int64_t(fixed)));
			return float(double(stored) / FixedOne);
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
	}

	// The converter does what MLLE::Setup() and MLLE::SpawnOffgrids() do, when the level is converted - the layers, their
	// properties and their order, the palette, the edited tiles and the off-grid events are all in the level already
	bool mlleSetup() {
		// Except the sprites it recolors, which the frames of the animations have to show
		ScriptLoader::FromActiveContext<LevelScriptLoader>()->ApplySpriteRecolors();
		return true;
	}

	void LevelScriptLoader::ApplySpriteRecolors()
	{
		// The sprites MLLE can recolor, by their slot (see Formats::ScriptDataChunk) - the set, the animation of the first
		// frame and the number of frames from it
		static constexpr struct {
			Legacy::anim Set;
			std::uint8_t Animation;
			std::uint8_t FrameCount;
		} RecolorableSprites[] = {
			{ Legacy::mPINBALL, 0, 4 }, { Legacy::mPINBALL, 2, 4 }, { Legacy::mCARROTPOLE, 0, 1 }, { Legacy::mDIAMPOLE, 0, 1 },
			{ Legacy::mPINBALL, 4, 8 }, { Legacy::mJUNGLEPOLE, 0, 1 }, { Legacy::mZZSCENERY, 0, 17 }, { Legacy::mPSYCHPOLE, 0, 1 },
			{ Legacy::mSMALTREE, 0, 1 }, { Legacy::mSNOW, 0, 8 }, { Legacy::mCOMMON, 2, 18 }, { Legacy::mBOLLPLAT, 0, 2 },
			{ Legacy::mFRUITPLAT, 0, 2 }, { Legacy::mGRASSPLAT, 0, 2 }, { Legacy::mPINKPLAT, 0, 2 }, { Legacy::mSONICPLAT, 0, 2 },
			{ Legacy::mSPIKEPLAT, 0, 2 }, { Legacy::mSPIKEBOLL, 0, 2 }, { Legacy::mSPIKEBOLL3D, 0, 2 }, { Legacy::mVINE, 1, 1 }
		};

		ArrayView<const std::uint8_t> recolors = _levelHandler->_spriteRecolors;
		auto& tables = GetAnimationTables();
		for (std::size_t i = 0; i + Formats::ScriptDataChunk::RecolorRecordSize <= recolors.size(); i += Formats::ScriptDataChunk::RecolorRecordSize) {
			std::uint8_t slot = recolors[i];
			if (slot >= std::size(RecolorableSprites)) {
				continue;
			}
			const auto& sprite = RecolorableSprites[slot];
			std::uint32_t firstAnim = tables.GetFirstAnim(Legacy::AnimationTables::GetSetID(sprite.Set, versionTSF));
			std::uint32_t frameCount, firstFrame;
			if (firstAnim == 0 || !tables.GetAnimationFrames(firstAnim + sprite.Animation, frameCount, firstFrame)) {
				continue;
			}
			// As jjPIXELMAP(frame).recolor(colors).save(frame) of the include
			const std::uint8_t* colors = &recolors[i + 1];
			for (std::uint32_t j = 0; j < sprite.FrameCount; j++) {
				Legacy::jjANIMFRAME* frame = tables.GetFrame(firstFrame + j);
				if (frame == nullptr) {
					break;
				}
				tables.RecolorFrame(*frame, colors);
				frame->Release();
			}
		}
	}

	static const WeaponDefinition* GetScriptWeapon(std::uint32_t slot) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (slot < 1 || slot > std::uint32_t(WeaponType::Count)) {
			return nullptr;
		}
		const WeaponDefinition& definition = owner->GetLevelHandler()->GetWeaponDefinition(WeaponType(slot - 1));
		return (definition.Kind == WeaponKind::Script ? &definition : nullptr);
	}

	bool mlleGetWeaponParams(std::uint32_t slot, Legacy::jjSTREAM* data) {
		// The parameters of a weapon the script sets up, see Formats::WeaponsChunk
		const WeaponDefinition* definition = GetScriptWeapon(slot);
		bool isScriptWeapon = (data != nullptr && definition != nullptr);
		if (isScriptWeapon && !definition->ScriptParams.empty()) {
			data->write(String(reinterpret_cast<const char*>(definition->ScriptParams.data()), definition->ScriptParams.size()));
		}
		// A handle passed to a native function is the function's to release
		if (data != nullptr) {
			data->Release();
		}
		return isScriptWeapon;
	}

	std::uint8_t mlleGetWeaponAmmoCrate(std::uint32_t slot) {
		const WeaponDefinition* definition = GetScriptWeapon(slot);
		return (definition != nullptr ? definition->AmmoCrateEvent : 0);
	}

	std::int16_t mlleGetWeaponHookArgument() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetLevelHandler()->GetWeapons().ScriptHookArgument;
	}

	void mlleSetWeaponSprite(std::uint32_t slot, bool powered, std::uint32_t firstFrame, std::uint32_t frameCount) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->OnWeaponSprite(slot, powered, firstFrame, frameCount);
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
		value = Legacy::jjLAYER::ToStoredOffset(value);
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
		value = Legacy::jjLAYER::ToStoredOffset(value);
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

	void jjSetDarknessColor(jjPALCOLOR color) {
		noop();
	}
	// The textured background of the level is layer 8, its jjLAYER keeps how scripts set it (see jjLAYER::ApplyTexture()), the
	// global properties of JJ2+ are those of it. JJ2+ draws the other textured layers of a level without the MLLE stream in
	// them (see JJ2Level::MakeLayerRecord()), so without a textured layer 8 they're those of the first of them.
	namespace
	{
		struct TexturedBackgroundLayer
		{
			jjLAYER* Layer;

			explicit TexturedBackgroundLayer(bool skyLayerOnly = false) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				Layer = owner->GetLayerProxy(owner->GetLayerIndex(8));
				if (skyLayerOnly || (Layer != nullptr && Layer->GetTextureSurface() != jjLAYER::Untextured)) {
					return;
				}
				for (std::int32_t number = 7; number >= 1; number--) {
					jjLAYER* other = owner->GetLayerProxy(owner->GetLayerIndex(number));
					if (other == nullptr) {
						continue;
					}
					if (other->GetTextureSurface() != jjLAYER::Untextured) {
						if (Layer != nullptr) {
							Layer->Release();
						}
						Layer = other;
						return;
					}
					other->Release();
				}
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
		TexturedBackgroundLayer background(true);
		return (background.Layer != nullptr && background.Layer->GetTextureSurface() != jjLAYER::Untextured);
	}
	bool set_jjTexturedBGUsed(bool used) {
		TexturedBackgroundLayer background(true);
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
			if (Tiles::LayerDescription::IsTexturedBackground(src.Description.RendererType) && src.Description.TextureMapping > 0) {
				layer->_spriteParam = std::uint8_t(src.Description.TextureMapping - 1);
			} else if (src.Description.Blend != Tiles::LayerBlend::Normal) {
				layer->_spriteParam = src.Description.BlendParam;
			} else if (src.Description.RendererType == Tiles::LayerRendererType::Tinted || src.Description.RendererType == Tiles::LayerRendererType::PaletteShift ||
				src.Description.RendererType == Tiles::LayerRendererType::SingleColor ||
				src.Description.RendererType == Tiles::LayerRendererType::AlphaMap || src.Description.RendererType == Tiles::LayerRendererType::PaletteMapping) {
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
		constexpr std::int32_t Count = Formats::LayerPropertiesChunk::OriginalLayerCount;
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
		InvalidateLayerDrawZ();
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
			if (number < 1 || number > Formats::LayerPropertiesChunk::OriginalLayerCount) {
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
			SmallVector<std::int32_t, Formats::LayerPropertiesChunk::OriginalLayerCount> sources;
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
		constexpr std::int32_t TextureBytes = Formats::LayerPropertiesChunk::TextureSize * Formats::LayerPropertiesChunk::TextureSize;
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
		InvalidateLayerDrawZ();

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
		std::int32_t partFirst = 0;
		for (std::int32_t i = 0; i < partCount; i++) {
			auto& part = tileMap->_tileSets[i];
			if (tileId >= part.Count) {
				tileId -= part.Count;
				partFirst += part.Count;
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
							if (part.PaletteRemapping != nullptr) {
								// In the colors of the level, as they're drawn (chains drawn from a tile of an extra tile set)
								for (std::int32_t tile = 0; tile < data->TileCount; tile++) {
									if (std::size_t(tile) < data->TrueColor.size() && data->TrueColor[tile] != 0) {
										continue;
									}
									std::uint8_t* image = &data->Images[std::size_t(tile) * Formats::TileSetData::ImageBytesPerTile];
									for (std::int32_t i = 0; i < TileSet::DefaultTileSize * TileSet::DefaultTileSize; i++) {
										image[i] = (image[i] != 0 ? part.PaletteRemapping[image[i]] : 0);
									}
								}
							}
							if (pixels.TrueColorPalette != nullptr) {
								MakeTrueColorTiles(*data, pixels.TrueColorPalette.get());
							}
							// The images the level replaces (MLLE edited tiles) are what JJ2+ reads of the tiles
							for (const Formats::TileDiffuseOverride& tile : _levelHandler->_tileDiffuseOverrides) {
								std::int32_t fileTile = std::int32_t(tile.TileId) - partFirst + part.Offset;
								if (tile.TileId >= partFirst && tile.TileId < partFirst + part.Count && fileTile < data->TileCount) {
									std::memcpy(&data->Images[std::size_t(fileTile) * Formats::TileSetData::ImageBytesPerTile], tile.Indices, sizeof(tile.Indices));
									if (std::size_t(fileTile) < data->TrueColor.size()) {
										data->TrueColor[fileTile] = 0;
									}
								}
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
		// JJ2+ has room for tiles after those of the tile sets, scripts save images into them (SwitchTestV2)
		if (tileMap == nullptr || tileId < 0 || !tileMap->AppendBlankTiles(tileId)) {
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
		if (tileMap == nullptr || tileId < 0 || !tileMap->AppendBlankTiles(tileId)) {
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
				layer->xOffset = Legacy::jjLAYER::ToStoredOffset(layer->xOffset);
				desc.OffsetX = layer->xOffset;
			} else {
				layer->xOffset = desc.OffsetX;
			}
			layer->_syncedOffsetX = layer->xOffset;
			if (layer->yOffset != layer->_syncedOffsetY) {
				layer->yOffset = Legacy::jjLAYER::ToStoredOffset(layer->yOffset);
				desc.OffsetY = layer->yOffset;
			} else {
				layer->yOffset = desc.OffsetY;
			}
			layer->_syncedOffsetY = layer->yOffset;
		}
		// The speed models of layer 8 were pushed above as they're without it
		if (_layer8Fixed >= 0) {
			ApplyUseLayer8Speeds();
		}
	}

	bool getUseLayer8Speeds() {
		return ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetUseLayer8Speeds();
	}
	bool setUseLayer8Speeds(bool value) {
		ScriptLoader::FromActiveContext<LevelScriptLoader>()->SetUseLayer8Speeds(value);
		return value;
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
		// Every tile of a layer can be set already
	}

	// TODO

	// The event at the masked pixel the functions below found last, see jjEventAtLastMaskedPixel
	static std::uint8_t _eventAtLastMaskedPixel = 0;

	static bool IsPixelMasked(std::int32_t xPixel, std::int32_t yPixel) {
		return IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)xPixel, (float)yPixel));
	}
	static void FoundMaskedPixel(std::int32_t xPixel, std::int32_t yPixel) {
		if (xPixel >= 0 && yPixel >= 0) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			_eventAtLastMaskedPixel = std::uint8_t(owner->GetOriginalEvent(std::uint16_t(xPixel / 32), std::uint16_t(yPixel / 32)));
		}
	}
	// The first masked pixel of a line, by steps of one pixel, false if there's none
	static bool FindMaskedPixel(std::int32_t xPixel, std::int32_t yPixel, std::int32_t stepX, std::int32_t stepY, std::int32_t lineLength, std::int32_t& found) {
		for (std::int32_t i = 0; i < lineLength; i++) {
			if (IsPixelMasked(xPixel + i * stepX, yPixel + i * stepY)) {
				found = i;
				FoundMaskedPixel(xPixel + i * stepX, yPixel + i * stepY);
				return true;
			}
		}
		return false;
	}

	std::uint8_t get_jjEventAtLastMaskedPixel() {
		return _eventAtLastMaskedPixel;
	}
	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel) {
		if (!IsPixelMasked(xPixel, yPixel)) {
			return false;
		}
		FoundMaskedPixel(xPixel, yPixel);
		return true;
	}
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer) {
		// Only the sprite layer has collision masks in this engine, so the layer argument is ignored
		return jjMaskedPixel(xPixel, yPixel);
	}
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel) {
		if (lineLength <= 0 || !IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)(xPixel + lineLength - 1), (float)yPixel))) {
			return false;
		}
		std::int32_t found;
		FindMaskedPixel(xPixel, yPixel, 1, 0, lineLength, found);
		return true;
	}
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer) {
		return jjMaskedHLine(xPixel, lineLength, yPixel);
	}
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		if (lineLength <= 0 || !IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)xPixel, (float)(yPixel + lineLength - 1)))) {
			return false;
		}
		std::int32_t found;
		FindMaskedPixel(xPixel, yPixel, 0, 1, lineLength, found);
		return true;
	}
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		return jjMaskedVLine(xPixel, yPixel, lineLength);
	}
	std::int32_t jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		// As measured in JJ2+, the topmost masked pixel of the line counted from 1, `lineLength + 1` if there's none
		std::int32_t found;
		if (lineLength <= 0 || !IsRegionMasked(AABBf((float)xPixel, (float)yPixel, (float)xPixel, (float)(yPixel + lineLength - 1))) ||
			!FindMaskedPixel(xPixel, yPixel, 0, 1, lineLength, found)) {
			return std::max(lineLength, 0) + 1;
		}
		return found + 1;
	}
	std::int32_t jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		return jjMaskedTopVLine(xPixel, yPixel, lineLength);
	}
	// The same of a layer, which is the sprite layer here too
	static bool LayerMaskedPixel(const jjLAYER* self, std::int32_t xPixel, std::int32_t yPixel) {
		return jjMaskedPixel(xPixel, yPixel);
	}
	static bool LayerMaskedHLine(const jjLAYER* self, std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel) {
		return jjMaskedHLine(xPixel, lineLength, yPixel);
	}
	static bool LayerMaskedVLine(const jjLAYER* self, std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		return jjMaskedVLine(xPixel, yPixel, lineLength);
	}
	static std::int32_t LayerMaskedTopVLine(const jjLAYER* self, std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		return jjMaskedTopVLine(xPixel, yPixel, lineLength);
	}

	void LevelScriptLoader::RegisterLegacyLayerGlobals(asIScriptEngine* engine)
	{
		engine->RegisterGlobalFunction("float get_jjLayerXOffset(uint8)", asFUNCTION(get_layerXOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerXOffset(uint8, float)", asFUNCTION(set_layerXOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYOffset(uint8)", asFUNCTION(get_layerYOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerYOffset(uint8, float)", asFUNCTION(set_layerYOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidth(uint8)", asFUNCTION(get_layerWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidthReal(uint8)", asFUNCTION(get_layerRealWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidthRounded(uint8)", asFUNCTION(get_layerRoundedWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerHeight(uint8)", asFUNCTION(get_layerHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerXSpeed(uint8)", asFUNCTION(get_layerXSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerXSpeed(uint8, float)", asFUNCTION(set_layerXSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYSpeed(uint8)", asFUNCTION(get_layerYSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerYSpeed(uint8, float)", asFUNCTION(set_layerYSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerXAutoSpeed(uint8)", asFUNCTION(get_layerXAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerXAutoSpeed(uint8, float)", asFUNCTION(set_layerXAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYAutoSpeed(uint8)", asFUNCTION(get_layerYAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjLayerYAutoSpeed(uint8, float)", asFUNCTION(set_layerYAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerHasTiles(uint8)", asFUNCTION(get_layerHasTiles), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjLayerHasTiles(uint8, bool)", asFUNCTION(set_layerHasTiles), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerTileHeight(uint8)", asFUNCTION(get_layerTileHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjLayerTileHeight(uint8, bool)", asFUNCTION(set_layerTileHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerTileWidth(uint8)", asFUNCTION(get_layerTileWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjLayerTileWidth(uint8, bool)", asFUNCTION(set_layerTileWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerLimitVisibleRegion(uint8)", asFUNCTION(get_layerLimitVisibleRegion), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjLayerLimitVisibleRegion(uint8, bool)", asFUNCTION(set_layerLimitVisibleRegion), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjSetLayerXSpeed(uint8 layerID, float newspeed, bool newSpeedIsAnAutoSpeed)", asFUNCTION(setLayerXSpeedSeamlessly), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetLayerYSpeed(uint8 layerID, float newspeed, bool newSpeedIsAnAutoSpeed)", asFUNCTION(setLayerYSpeedSeamlessly), asCALL_CDECL);
	}

	void LevelScriptLoader::RegisterLegacyTexturedBackground(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("TEXTURE");
		engine->RegisterEnum("Texture");
		engine->RegisterEnumValue("Texture", "FROMTILES", 0);
		engine->RegisterEnumValue("Texture", "LAYER8", 0);
		engine->RegisterEnumValue("Texture", "NORMAL", 1);
		engine->RegisterEnumValue("Texture", "PSYCH", 2);
		engine->RegisterEnumValue("Texture", "MEDIVO", 3);
		engine->RegisterEnumValue("Texture", "DIAMONDUSBETA", 4);
		engine->RegisterEnumValue("Texture", "WISETYNESS", 5);
		engine->RegisterEnumValue("Texture", "BLADE", 6);
		engine->RegisterEnumValue("Texture", "MEZ02", 7);
		engine->RegisterEnumValue("Texture", "WINDSTORMFORTRESS", 8);
		engine->RegisterEnumValue("Texture", "RANEFORUSV", 9);
		engine->RegisterEnumValue("Texture", "CORRUPTEDSANCTUARY", 10);
		engine->RegisterEnumValue("Texture", "XARGON", 11);
		engine->RegisterEnumValue("Texture", "ICTUBELECTRIC", 12);
		engine->RegisterEnumValue("Texture", "WTF", 13);
		engine->RegisterEnumValue("Texture", "MUCKAMOKNIGHT", 14);
		engine->RegisterEnumValue("Texture", "DESOLATION", 15);
		engine->RegisterEnumValue("Texture", "CUSTOM", ~0);
		engine->RegisterEnum("Style");
		engine->RegisterEnumValue("Style", "WARPHORIZON", tbgModeWARPHORIZON);
		engine->RegisterEnumValue("Style", "TUNNEL", tbgModeTUNNEL);
		engine->RegisterEnumValue("Style", "MENU", tbgModeMENU);
		engine->RegisterEnumValue("Style", "TILEMENU", tbgModeTILEMENU);
		engine->RegisterEnumValue("Style", "WAVE", tbgModeWAVE);
		engine->RegisterEnumValue("Style", "CYLINDER", tbgModeCYLINDER);
		engine->RegisterEnumValue("Style", "REFLECTION", tbgModeREFLECTION);
		engine->SetDefaultNamespace("");

		engine->RegisterGlobalFunction("void jjSetDarknessColor(jjPALCOLOR color = jjPALCOLOR())", asFUNCTION(jjSetDarknessColor), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(uint8 red, uint8 green, uint8 blue)", asFUNCTION(jjSetFadeColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(uint8 paletteColorID = 207)", asFUNCTION(jjSetFadeColorsFromPalette), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(jjPALCOLOR color)", asFUNCTION(jjSetFadeColorsFromPalcolor), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPALCOLOR jjGetFadeColors()", asFUNCTION(jjGetFadeColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjUpdateTexturedBG()", asFUNCTION(jjUpdateTexturedBG), asCALL_CDECL); // Deprecated
		engine->RegisterGlobalFunction("TEXTURE::Texture get_jjTexturedBGTexture()", asFUNCTION(get_jjTexturedBGTexture), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Texture set_jjTexturedBGTexture(TEXTURE::Texture)", asFUNCTION(set_jjTexturedBGTexture), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Style get_jjTexturedBGStyle()", asFUNCTION(get_jjTexturedBGStyle), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Style set_jjTexturedBGStyle(TEXTURE::Style)", asFUNCTION(set_jjTexturedBGStyle), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjTexturedBGUsed()", asFUNCTION(get_jjTexturedBGUsed), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjTexturedBGUsed(bool)", asFUNCTION(set_jjTexturedBGUsed), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjTexturedBGStars()", asFUNCTION(get_jjTexturedBGStars), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjTexturedBGStars(bool)", asFUNCTION(set_jjTexturedBGStars), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjTexturedBGFadePositionX()", asFUNCTION(get_jjTexturedBGFadePositionX), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjTexturedBGFadePositionX(float value)", asFUNCTION(set_jjTexturedBGFadePositionX), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjTexturedBGFadePositionY()", asFUNCTION(get_jjTexturedBGFadePositionY), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjTexturedBGFadePositionY(float value)", asFUNCTION(set_jjTexturedBGFadePositionY), asCALL_CDECL);
	}

	void LevelScriptLoader::RegisterLegacyLayers(asIScriptEngine* engine)
	{
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_FACTORY, "jjLAYER@ f(uint layerWidth, uint layerHeight)", asFUNCTION(jjLAYER::CreateFromSize), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_FACTORY, "jjLAYER@ f(const jjLAYER &in layer)", asFUNCTION(jjLAYER::CreateCopy), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_ADDREF, "void f()", asMETHOD(jjLAYER, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_RELEASE, "void f()", asMETHOD(jjLAYER, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjLAYER @get_jjLayers(int)", asFUNCTION(jjLAYER::get_jjLayers), asCALL_CDECL);
		engine->RegisterObjectProperty("jjLAYER", "const int width", asOFFSET(jjLAYER, width));
		engine->RegisterObjectProperty("jjLAYER", "const int widthReal", asOFFSET(jjLAYER, widthReal));
		engine->RegisterObjectProperty("jjLAYER", "const int widthRounded", asOFFSET(jjLAYER, widthRounded));
		engine->RegisterObjectProperty("jjLAYER", "const int height", asOFFSET(jjLAYER, height));
		engine->RegisterObjectProperty("jjLAYER", "float xSpeed", asOFFSET(jjLAYER, xSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float ySpeed", asOFFSET(jjLAYER, ySpeed));
		engine->RegisterObjectProperty("jjLAYER", "float xAutoSpeed", asOFFSET(jjLAYER, xAutoSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float yAutoSpeed", asOFFSET(jjLAYER, yAutoSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float xOffset", asOFFSET(jjLAYER, xOffset));
		engine->RegisterObjectProperty("jjLAYER", "float yOffset", asOFFSET(jjLAYER, yOffset));
		engine->RegisterObjectProperty("jjLAYER", "float xInnerSpeed", asOFFSET(jjLAYER, xInnerSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float yInnerSpeed", asOFFSET(jjLAYER, yInnerSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float xInnerAutoSpeed", asOFFSET(jjLAYER, xInnerAutoSpeed));
		engine->RegisterObjectProperty("jjLAYER", "float yInnerAutoSpeed", asOFFSET(jjLAYER, yInnerAutoSpeed));
		engine->RegisterObjectMethod("jjLAYER", "SPRITE::Mode get_spriteMode() const", asMETHOD(jjLAYER, get_spriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "SPRITE::Mode set_spriteMode(SPRITE::Mode)", asMETHOD(jjLAYER, set_spriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "uint8 get_spriteParam() const", asMETHOD(jjLAYER, get_spriteParam), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "uint8 set_spriteParam(uint8)", asMETHOD(jjLAYER, set_spriteParam), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjLAYER", "void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed)", asMETHOD(jjLAYER, setXSpeed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed)", asMETHOD(jjLAYER, setYSpeed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "float getXPosition(const jjPLAYER &in play) const", asMETHOD(jjLAYER, getXPosition), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "float getYPosition(const jjPLAYER &in play) const", asMETHOD(jjLAYER, getYPosition), asCALL_THISCALL);

		// The parameters of the texture styles, objects of the layer itself (see jjLAYER::TextureStyleParams) - the fade color
		// and the stars are of the layer, each style takes them as one of its own parameters
		engine->RegisterObjectType("jjLAYERWARPHORIZON", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "float get_fadePositionX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "float set_fadePositionX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "float get_fadePositionY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "float set_fadePositionY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool get_fade() const", asFUNCTION(jjLAYER::GetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool set_fade(bool)", asFUNCTION(jjLAYER::SetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool get_stars() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool set_stars(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "jjPALCOLOR getFadeColor() const", asFUNCTION(jjLAYER::GetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "void setFadeColor(jjPALCOLOR)", asFUNCTION(jjLAYER::SetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERWARPHORIZON warpHorizon", asOFFSET(jjLAYER, warpHorizon));

		engine->RegisterObjectType("jjLAYERTUNNEL", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "float get_fadePositionX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "float set_fadePositionX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "float get_fadePositionY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "float set_fadePositionY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool get_fade() const", asFUNCTION(jjLAYER::GetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool set_fade(bool)", asFUNCTION(jjLAYER::SetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool get_spiral() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool set_spiral(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "jjPALCOLOR getFadeColor() const", asFUNCTION(jjLAYER::GetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "void setFadeColor(jjPALCOLOR)", asFUNCTION(jjLAYER::SetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERTUNNEL tunnel", asOFFSET(jjLAYER, tunnel));

		engine->RegisterObjectType("jjLAYERMENU", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERMENU", "float get_pivotX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "float set_pivotX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "float get_pivotY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "float set_pivotY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow16() const", asFUNCTION(jjLAYER::GetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow16(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow32() const", asFUNCTION(jjLAYER::GetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow32(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow256() const", asFUNCTION(jjLAYER::GetTextureComponent<2>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow256(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<2>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "bool get_lightToDark() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERMENU", "bool set_lightToDark(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERMENU menu", asOFFSET(jjLAYER, menu));

		engine->RegisterObjectType("jjLAYERTILEMENU", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "float get_pivotX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "float set_pivotX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "float get_pivotY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "float set_pivotY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "bool get_fullSize() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "bool set_fullSize(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERTILEMENU tileMenu", asOFFSET(jjLAYER, tileMenu));

		engine->RegisterObjectType("jjLAYERWAVE", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERWAVE", "float get_amplitudeX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "float set_amplitudeX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "float get_amplitudeY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "float set_amplitudeY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 get_wavelengthX() const", asFUNCTION(jjLAYER::GetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 set_wavelengthX(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 get_wavelengthY() const", asFUNCTION(jjLAYER::GetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 set_wavelengthY(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "int8 get_waveSpeed() const", asFUNCTION(jjLAYER::GetWaveSpeed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "int8 set_waveSpeed(int8)", asFUNCTION(jjLAYER::SetWaveSpeed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "bool get_distortionAngle() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERWAVE", "bool set_distortionAngle(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERWAVE wave", asOFFSET(jjLAYER, wave));

		engine->RegisterObjectType("jjLAYERCYLINDER", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "float get_fadePositionX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "float set_fadePositionX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "float get_fadePositionY() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "float set_fadePositionY(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool get_fade() const", asFUNCTION(jjLAYER::GetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool set_fade(bool)", asFUNCTION(jjLAYER::SetStyleFade), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool get_halfSize() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool set_halfSize(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "jjPALCOLOR getFadeColor() const", asFUNCTION(jjLAYER::GetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "void setFadeColor(jjPALCOLOR)", asFUNCTION(jjLAYER::SetFadeColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERCYLINDER cylinder", asOFFSET(jjLAYER, cylinder));

		engine->RegisterObjectType("jjLAYERREFLECTION", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "float get_fadePositionX() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "float set_fadePositionX(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::X>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "float get_top() const", asFUNCTION(jjLAYER::GetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "float set_top(float)", asFUNCTION(jjLAYER::SetStyleValue<&jjLAYER::TextureStyleParams::Y>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_tintOpacity() const", asFUNCTION(jjLAYER::GetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_tintOpacity(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<0>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_distance() const", asFUNCTION(jjLAYER::GetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_distance(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<1>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_distortion() const", asFUNCTION(jjLAYER::GetTextureComponent<2>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_distortion(uint8)", asFUNCTION(jjLAYER::SetTextureComponent<2>), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "bool get_blur() const", asFUNCTION(jjLAYER::GetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "bool set_blur(bool)", asFUNCTION(jjLAYER::SetTextureStars), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_tintColor() const", asFUNCTION(jjLAYER::GetStyleTintColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_tintColor(uint8)", asFUNCTION(jjLAYER::SetStyleTintColor), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERREFLECTION reflection", asOFFSET(jjLAYER, reflection));

		engine->RegisterObjectMethod("jjLAYER", "TEXTURE::Style get_textureStyle() const", asMETHOD(jjLAYER, GetTextureMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_textureStyle(TEXTURE::Style)", asMETHOD(jjLAYER, SetTextureMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "TEXTURE::Texture get_texture() const", asMETHOD(jjLAYER, GetTexture), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_texture(TEXTURE::Texture)", asMETHOD(jjLAYER, SetTexture), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjLAYER", "int rotationAngle", asOFFSET(jjLAYER, rotationAngle));
		engine->RegisterObjectProperty("jjLAYER", "int rotationRadiusMultiplier", asOFFSET(jjLAYER, rotationRadiusMultiplier));
		engine->RegisterObjectProperty("jjLAYER", "bool tileHeight", asOFFSET(jjLAYER, tileHeight));
		engine->RegisterObjectProperty("jjLAYER", "bool tileWidth", asOFFSET(jjLAYER, tileWidth));
		engine->RegisterObjectProperty("jjLAYER", "bool limitVisibleRegion", asOFFSET(jjLAYER, limitVisibleRegion));
		engine->RegisterObjectProperty("jjLAYER", "const bool hasTileMap", asOFFSET(jjLAYER, hasTileMap));
		engine->RegisterObjectProperty("jjLAYER", "bool hasTiles", asOFFSET(jjLAYER, hasTiles));
		engine->RegisterGlobalFunction("array<jjLAYER@>@ jjLayerOrderGet()", asFUNCTION(jjLAYER::jjLayerOrderGet), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjLayerOrderSet(const array<jjLAYER@> &in order)", asFUNCTION(jjLAYER::jjLayerOrderSet), asCALL_CDECL);
		engine->RegisterGlobalFunction("array<jjLAYER@>@ jjLayersFromLevel(const string &in filename, const array<uint> &in layerIDs, int tileIDAdjustmentFactor = 0)", asFUNCTION(jjLAYER::jjLayersFromLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjTilesFromTileset(const string &in filename, uint firstTileID, uint tileCount, const array<uint8>@ paletteColorMapping = null)", asFUNCTION(jjLAYER::jjTilesFromTileset), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjTilesFromTileset32(const string &in filename, uint firstTileID, uint tileCount, const jjPAL@ palette = null)", asFUNCTION(jjLAYER::jjTilesFromTileset32), asCALL_CDECL);

		engine->SetDefaultNamespace("LAYERSPEEDMODEL");
		engine->RegisterEnum("LayerSpeedModel");
		engine->RegisterEnumValue("LayerSpeedModel", "NORMAL", 0);
		engine->RegisterEnumValue("LayerSpeedModel", "LAYER8", 1);
		engine->RegisterEnumValue("LayerSpeedModel", "BOTHSPEEDS", 2);
		engine->RegisterEnumValue("LayerSpeedModel", "FROMSTART", 3);
		engine->RegisterEnumValue("LayerSpeedModel", "FITLEVEL", 4);
		engine->RegisterEnumValue("LayerSpeedModel", "SPEEDMULTIPLIERS", 5);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectProperty("jjLAYER", "LAYERSPEEDMODEL::LayerSpeedModel xSpeedModel", asOFFSET(jjLAYER, SpeedModeX));
		engine->RegisterObjectProperty("jjLAYER", "LAYERSPEEDMODEL::LayerSpeedModel ySpeedModel", asOFFSET(jjLAYER, SpeedModeY));

		engine->SetDefaultNamespace("SURFACE");
		engine->RegisterEnum("Surface");
		engine->RegisterEnumValue("Surface", "UNTEXTURED", jjLAYER::TextureSurfaceStyles::Untextured);
		engine->RegisterEnumValue("Surface", "LEGACY", jjLAYER::TextureSurfaceStyles::Legacy);
		engine->RegisterEnumValue("Surface", "FULLSCREEN", jjLAYER::TextureSurfaceStyles::FullScreen);
		engine->RegisterEnumValue("Surface", "INNERWINDOW", jjLAYER::TextureSurfaceStyles::InnerWindow);
		engine->RegisterEnumValue("Surface", "INNERLAYER", jjLAYER::TextureSurfaceStyles::InnerLayer);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjLAYER", "SURFACE::Surface get_textureSurface() const", asMETHOD(jjLAYER, GetTextureSurface), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_textureSurface(SURFACE::Surface)", asMETHOD(jjLAYER, SetTextureSurface), asCALL_THISCALL);

		engine->RegisterObjectType("jjTILE", 0, asOBJ_REF);
		engine->RegisterObjectBehaviour("jjTILE", asBEHAVE_ADDREF, "void f()", asMETHOD(jjTILE, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjTILE", asBEHAVE_RELEASE, "void f()", asMETHOD(jjTILE, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "array<uint16>@ getFrames() const", asMETHOD(jjTILE, getFrames), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "bool setFrames(const array<uint16> &in frames, bool pingPong = false, uint16 wait = 0, uint16 randomWait = 0, uint16 pingPongWait = 0)", asMETHOD(jjTILE, setFrames), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "uint8 get_fps() const", asMETHOD(jjTILE, get_fps), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "uint8 set_fps(uint8)", asMETHOD(jjTILE, set_fps), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "uint16 get_tileID() const", asMETHOD(jjTILE, get_tileID), asCALL_THISCALL);

		engine->RegisterGlobalFunction("const jjTILE@ get_jjTiles(uint16)", asFUNCTION(jjTILE::get_jjTiles), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjTILE@ get_jjAnimatedTiles(uint16)", asFUNCTION(jjTILE::get_jjAnimatedTiles), asCALL_CDECL);

		engine->RegisterGlobalFunction("uint16 jjGetStaticTile(uint16 tileID)", asFUNCTION(jjGetStaticTile), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint16 jjTileGet(uint8 layer, int xTile, int yTile)", asFUNCTION(jjTileGet), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint16 jjTileSet(uint8 layer, int xTile, int yTile, uint16 newTile)", asFUNCTION(jjTileSet), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjGenerateSettableTileArea(uint8 layer, int xTile, int yTile, int width, int height)", asFUNCTION(jjGenerateSettableTileArea), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "uint16 tileGet(int xTile, int yTile) const", asMETHOD(jjLAYER, tileGet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "uint16 tileSet(int xTile, int yTile, uint16 newTile)", asMETHOD(jjLAYER, tileSet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void generateSettableTileArea(int xTile, int yTile, int width, int height)", asMETHOD(jjLAYER, generateSettableTileArea), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void generateSettableTileArea()", asMETHOD(jjLAYER, generateSettableTileAreaAll), asCALL_THISCALL);

		engine->RegisterGlobalFunction("bool jjMaskedPixel(int xPixel, int yPixel)", asFUNCTION(jjMaskedPixel), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedPixel(int xPixel, int yPixel, uint8 layer)", asFUNCTION(jjMaskedPixelLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedPixel(int xPixel, int yPixel) const", asFUNCTION(LayerMaskedPixel), asCALL_CDECL_OBJFIRST);
		engine->RegisterGlobalFunction("bool jjMaskedHLine(int xPixel, int lineLength, int yPixel)", asFUNCTION(jjMaskedHLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedHLine(int xPixel, int lineLength, int yPixel, uint8 layer)", asFUNCTION(jjMaskedHLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedHLine(int xPixel, int lineLength, int yPixel) const", asFUNCTION(LayerMaskedHLine), asCALL_CDECL_OBJFIRST);
		engine->RegisterGlobalFunction("bool jjMaskedVLine(int xPixel, int yPixel, int lineLength)", asFUNCTION(jjMaskedVLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedVLine(int xPixel, int yPixel, int lineLength,uint8 layer)", asFUNCTION(jjMaskedVLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedVLine(int xPixel, int yPixel, int lineLength) const", asFUNCTION(LayerMaskedVLine), asCALL_CDECL_OBJFIRST);
		engine->RegisterGlobalFunction("int jjMaskedTopVLine(int xPixel, int yPixel, int lineLength)", asFUNCTION(jjMaskedTopVLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjMaskedTopVLine(int xPixel, int yPixel, int lineLength,uint8 layer)", asFUNCTION(jjMaskedTopVLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "int maskedTopVLine(int xPixel, int yPixel, int lineLength) const", asFUNCTION(LayerMaskedTopVLine), asCALL_CDECL_OBJFIRST);
		engine->RegisterGlobalFunction("uint8 get_jjEventAtLastMaskedPixel()", asFUNCTION(get_jjEventAtLastMaskedPixel), asCALL_CDECL);
	}

	void LevelScriptLoader::RegisterLegacyMlle(asIScriptEngine* engine)
	{
		// Create fake MLLE namespace, because "MLLE-Include-xxx.asc" includes are blocked - the converter does what they do
		// when the level is loaded, so only what scripts use of it afterwards is here
		engine->SetDefaultNamespace("MLLE");
		engine->RegisterGlobalFunction("bool Setup()", asFUNCTION(mlleSetup), asCALL_CDECL);
		// For the include of weapons, see LevelScriptLoader::OnProcessInclude()
		engine->RegisterGlobalFunction("bool _GetWeaponParams(uint slot, jjSTREAM@ data)", asFUNCTION(mlleGetWeaponParams), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint8 _GetWeaponAmmoCrate(uint slot)", asFUNCTION(mlleGetWeaponAmmoCrate), asCALL_CDECL);
		engine->RegisterGlobalFunction("int16 _GetWeaponHookArgument()", asFUNCTION(mlleGetWeaponHookArgument), asCALL_CDECL);
		engine->RegisterGlobalFunction("void _SetWeaponSprite(uint slot, bool powerup, uint firstFrame, uint frameCount)", asFUNCTION(mlleSetWeaponSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void ReapplyPalette()", asMETHOD(jjPAL, apply), asCALL_THISCALL_ASGLOBAL, &_mllePalette);
		engine->RegisterGlobalFunction("void SpawnOffgrids()", asFUNCTION(mlleSpawnOffgrids), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SpawnOffgridsLocal()", asFUNCTION(mlleSpawnOffgridsLocal), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjLAYER@ GetLayer(const ::string &in name)", asFUNCTION(mlleGetLayer), asCALL_CDECL);
		// A handle in MLLE, it's never reassigned by its scripts, only changed and applied
		engine->RegisterGlobalProperty("jjPAL Palette", &_mllePalette);
	}
}

#endif
