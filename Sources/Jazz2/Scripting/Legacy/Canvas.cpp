#if defined(WITH_ANGELSCRIPT)

#include "Canvas.h"
#include "AnimationTables.h"
#include "WorldCanvas.h"
#include "Internal.h"

#include "../../Actors/Player.h"
#include "../../Compatibility/JJ2Strings.h"
#include "../../Tiles/TileMap.h"
#include "../../UI/HUD.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Graphics/Texture.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
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

	// The medium and large text of scripts is drawn a quarter smaller than in JJ2+, it looks too big in this game - the small
	// font is the one of the game, so it stays as large as JJ2+ draws it
	constexpr float ScriptTextScale = 0.75f;

	static ScriptTextStyle GetScriptTextStyle(std::uint32_t size)
	{
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
		if (size != 1) {
			style.Scale *= ScriptTextScale;
			style.OffsetY *= ScriptTextScale;
		}
		return style;
	}

	namespace Legacy
	{
		namespace
		{
			// Converts a sprite-palette index (as used by the canvas drawing functions) to an RGBA color with the given alpha
			static Colorf PaletteIndexToColor(std::uint8_t index, float alpha)
			{
				std::uint32_t c = ContentResolver::Get().GetPalettes()[index];
				return Colorf((c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, alpha);
			}

			// Maps a JJ2 sprite blend mode + parameter to an alpha value for solid/canvas drawing (best effort: opaque
			// unless a translucent/blend mode supplies an explicit alpha)
			static float SpriteModeToAlpha(std::uint32_t mode, std::uint8_t param)
			{
				switch (mode) {
					case spriteType_BLENDNORMAL:
					// Random pixels as many as the parameter says, it looks like this from afar
					case spriteType_BLENDDISSOLVE:
						return param / 255.0f;
					case spriteType_TRANSLUCENT:
					case spriteType_TRANSLUCENTCOLOR:
					case spriteType_TRANSLUCENTPALSHIFT:
					case spriteType_TRANSLUCENTSINGLEHUE:
						return 0.5f;
					case spriteType_GEM:
						// As measured in JJ2+, 3/4 of the gem and 1/4 of what's behind
						return 0.75f;
					case spriteType_INVISIBLE:
						return 0.0f;
					default:
						return 1.0f;
				}
			}

			// The color a rectangle of a color of the palette is drawn with in a sprite mode, a shadow darkens what's behind
			// it to a half whatever the color is (see DrawSpriteFrame())
			static Colorf SpriteModeToSolidColor(std::uint8_t color, std::uint32_t mode, std::uint8_t param)
			{
				if (mode == spriteType_GEM) {
					// The color of the gradient of the gem, a gem without one isn't drawn (see ContentResolver::GetGemColor())
					std::uint32_t c = ContentResolver::Get().GetGemColor(param, color);
					return Colorf((c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f,
						c != 0 ? SpriteModeToAlpha(mode, param) : 0.0f);
				}
				return (mode == spriteType_SHADOW ? Colorf(0.0f, 0.0f, 0.0f, 0.5f) : PaletteIndexToColor(color, SpriteModeToAlpha(mode, param)));
			}

			// Draws a single animation frame of a resolved graphic onto a HUD canvas, following the HUD::DrawElement
			// pattern. The sprite is anchored at the frame's hotspot, optionally flipped (by `SPRITE::Direction` or a
			// negative scale), scaled and rotated (angle in JJ2 units where 1024 = a full turn). Indexed graphics draw
			// through the palette-remap shader against the live sprite palette (so they recolor with jjPalette), or the
			// row of the shared palette texture of the sprite mode (see LevelScriptLoader::GetSpritePaletteOffset()).
			static void DrawSpriteFrame(UI::Canvas* canvas, std::uint16_t z, GenericGraphicResource* base, std::int32_t frame,
					float x, float y, std::int8_t direction, float scaleX, float scaleY, std::int32_t angle,
					std::uint32_t mode, std::uint8_t param, std::int32_t spritePaletteOffset, bool translucentFrame = false)
			{
				if (base == nullptr || base->TextureDiffuse == nullptr || base->FrameCount <= 0 || base->FrameConfiguration.X <= 0 ||
					mode == spriteType_INVISIBLE) {
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

				// Indexed sprites recolor via the shared palette texture; pre-baked RGBA sprites use -1
				bool indexed = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed);
				std::int32_t paletteOffset = (indexed ? spritePaletteOffset : -1);

				Shader* shader = nullptr;
				Colorf color;
				switch (mode) {
					case spriteType_SHADOW:
						// Darkens what's behind the shape of the sprite to a half (as measured in JJ2+), whatever its colors are
						color = Colorf(0.0f, 0.0f, 0.0f, 0.5f);
						break;
					case spriteType_SINGLECOLOR:
					case spriteType_TRANSLUCENTCOLOR:
						// The shape of the sprite in a color of the palette (as measured in JJ2+)
						shader = ContentResolver::Get().GetShader(indexed ? PrecompiledShader::SingleColorPalette : PrecompiledShader::SingleColor);
						color = PaletteIndexToColor(param, SpriteModeToAlpha(mode, param));
						break;
					case spriteType_FROZEN:
						// As the game draws a frozen object (see Actors::ActorRendererType::FrozenMask), JJ2+ draws an ice
						// block of its own - the color is the size of a texel and how frozen it is
						shader = ContentResolver::Get().GetShader(indexed ? PrecompiledShader::FrozenMaskPalette : PrecompiledShader::FrozenMask);
						color = Colorf(1.0f / texSize.X, 1.0f / texSize.Y, 1.0f, 1.0f);
						break;
					default:
						break;
				}
				if (shader == nullptr && mode != spriteType_SHADOW) {
					// Also where the backend has no such shader
					color = Colorf(1.0f, 1.0f, 1.0f, SpriteModeToAlpha(mode, param));
				}
				if (translucentFrame && mode != spriteType_NORMAL && mode != spriteType_SHADOW) {
					// A `transparent` frame is translucent only in SPRITE::NORMAL in JJ2+ (bullets drawn in
					// SINGLEHUE stay opaque), its pixels have less alpha in the graphics
					color.A *= 255.0f / float(AnimationTables::TranslucentFrameAlpha);
				}

				// JJ2 turns a sprite counter-clockwise about its hotspot (a puppet hung on strings turns so, from the
				// hotspot at the end of the string), the canvas turns the other way about the middle of the quad
				float angleRad = -angle * fTwoPi / 1024.0f;
				if (angle != 0) {
					Vector2f fromMiddle = Vector2f(frameAnchor.X * scaleX, frameAnchor.Y * scaleY) - size * 0.5f;
					float c = std::cos(angleRad), s = std::sin(angleRad);
					pos += fromMiddle - Vector2f(fromMiddle.X * c - fromMiddle.Y * s, fromMiddle.X * s + fromMiddle.Y * c);
				}
				// NEONGLOW adds the brightness of its row of the palette to what's behind (see GetSpritePaletteOffset())
				canvas->DrawTexture(*base->TextureDiffuse.get(), pos, z, size, texCoords, color,
					mode == spriteType_BLENDLIGHTEN || mode == spriteType_NEONGLOW, angleRad, paletteOffset, shader);
			}

			// Draws a tile of a value as JJ2+ has it (with TILE::HFLIPPED, VFLIPPED and ANIMATED) with its top left corner at
			// the position, only a quarter of it for a `TILE::Quadrant` other than ALLQUADRANTS (4), at its place in the tile
			static void DrawTileQuad(UI::Canvas* canvas, std::uint16_t z, Tiles::TileMap* tileMap, std::uint16_t tile,
				std::uint32_t quadrant, float x, float y)
			{
				if (tileMap == nullptr) {
					return;
				}
				std::int32_t tileId = (tile & Tiles::TileMap::TileIndexMask);
				if ((tile & Tiles::TileMap::TileFlagAnimated) != 0) {
					tileId += tileMap->GetStaticTileCount();
				}
				if (tileId == 0) {
					return;
				}
				Tiles::LayerTileFlags flags = Tiles::LayerTileFlags::None;
				if ((tile & Tiles::TileMap::TileFlagFlipX) != 0) {
					flags |= Tiles::LayerTileFlags::FlipX;
				}
				if ((tile & Tiles::TileMap::TileFlagFlipY) != 0) {
					flags |= Tiles::LayerTileFlags::FlipY;
				}

				Texture* texture;
				Vector4f texCoords;
				bool indexed;
				if (!tileMap->GetTileQuad(tileId, flags, texture, texCoords, indexed)) {
					return;
				}

				constexpr float TileSize = float(Tiles::TileSet::DefaultTileSize);
				Vector2f size(TileSize, TileSize);
				if (quadrant < 4) {
					float qx = float(quadrant & 1), qy = float(quadrant >> 1);
					texCoords.Y += texCoords.X * 0.5f * qx;
					texCoords.X *= 0.5f;
					texCoords.W += texCoords.Z * 0.5f * qy;
					texCoords.Z *= 0.5f;
					x += TileSize * 0.5f * qx;
					y += TileSize * 0.5f * qy;
					size = Vector2f(TileSize * 0.5f, TileSize * 0.5f);
				}
				canvas->DrawTexture(*texture, Vector2f(x, y), z, size, texCoords, Colorf::White, false, 0.0f, indexed ? 0 : -1);
			}
		}

		void jjTEXTAPPEARANCE::constructor(void* self)
		{
			new(self) jjTEXTAPPEARANCE{};
		}
		// As JJ2+ makes an appearance of STRING::Mode - BOUNCE moves the letters up and down, SPIN around, RIGHTALIGN aligns it
		static void ApplyStringMode(jjTEXTAPPEARANCE& appearance, std::uint32_t mode)
		{
			appearance.xAmp = (mode == 4 ? 1 : 0);
			appearance.yAmp = (mode == 3 || mode == 4 ? 1 : 0);
			appearance.align = (mode == 2 ? jjTEXTAPPEARANCE::align_RIGHT : jjTEXTAPPEARANCE::align_DEFAULT);
		}

		void jjTEXTAPPEARANCE::constructorMode(std::uint32_t mode, void* self)
		{
			auto* appearance = new(self) jjTEXTAPPEARANCE{};
			ApplyStringMode(*appearance, mode);
		}

		jjTEXTAPPEARANCE& jjTEXTAPPEARANCE::operator=(std::uint32_t other)
		{
			ApplyStringMode(*this, other);
			return *this;
		}

		bool ScriptWorldCanvas::OnDraw(RenderQueue& renderQueue)
		{
			Canvas::OnDraw(renderQueue);

			for (const SolidDraw& draw : _solids) {
				Colorf color = SpriteModeToSolidColor(draw.Color, draw.Mode, draw.Param);
				DrawSolid(Vector2f(draw.X, draw.Y), draw.Z, Vector2f(draw.Width, draw.Height), color, draw.Mode == spriteType_BLENDLIGHTEN, draw.Angle);
			}
			for (const TileDraw& draw : _tiles) {
				DrawTileQuad(this, draw.Z, draw.TileMap, draw.Tile, draw.Quadrant, draw.X, draw.Y);
			}
			for (const SpriteDraw& draw : _sprites) {
				// Following the object in every frame, not only in the ticks of the script (a player drawn by `onPlayerDraw()`)
				Vector2f anchor = (draw.Anchor != nullptr ? draw.Anchor->GetPos() : Vector2f::Zero);
				DrawSpriteFrame(this, draw.Z, draw.Graphics, draw.Cell, anchor.X + draw.X, anchor.Y + draw.Y, draw.Direction, draw.ScaleX,
					draw.ScaleY, draw.Angle, draw.Mode, draw.Param, draw.PaletteOffset, draw.TranslucentFrame);
			}
			for (const TextDraw& draw : _texts) {
				// Where the HUD puts text of the size against the position (see jjCANVAS::DrawScriptText())
				ScriptTextStyle style = GetScriptTextStyle(draw.Size);
				if (style.Font != nullptr) {
					std::int32_t charOffset = 0;
					style.Font->DrawString(this, draw.Text, charOffset, draw.X, draw.Y + style.OffsetY, draw.Z, UI::Alignment::TopLeft,
						draw.Color, style.Scale, 0.0f, 0.0f, 0.0f, 0.0f, style.CharSpacing, style.LineSpacing);
				}
			}
			return true;
		}

		bool ScriptLayerCanvas::OnDraw(RenderQueue& renderQueue)
		{
			Canvas::OnDraw(renderQueue);
			_owner->OnDrawLayers(this);
			return true;
		}

		jjCANVAS::jjCANVAS(UI::Canvas* target, Vector2f offset, float width, std::uint16_t* drawCount, std::uint16_t firstLayer, std::uint16_t lastLayer)
			: Target(target), Offset(offset), Width(width), DrawCount(drawCount), FirstLayer(firstLayer), LastLayer(lastLayer)
		{
		}

		float jjCANVAS::GetTextX(std::int32_t xPixel, float textWidth) const
		{
			float x;
			if (xPixel >= 0xC000) {
				x = Width - float(xPixel - 0x10000) - textWidth;
			} else if (xPixel >= 0x4000) {
				x = std::round((Width - textWidth) * 0.5f) + float(xPixel - 0x8000);
			} else {
				x = float(xPixel);
			}
			return x + Offset.X;
		}

		std::uint16_t jjCANVAS::NextLayer()
		{
			// Commands of the same layer are drawn in no particular order, so every draw gets a layer of its own, the draws
			// past the last layer stay on it
			std::uint16_t index = (DrawCount != nullptr ? std::min<std::uint16_t>((*DrawCount)++, std::uint16_t(LastLayer - FirstLayer)) : 0);
			return std::uint16_t(FirstLayer + index);
		}

		void jjCANVAS::DrawPixel(std::int32_t xPixel, int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param)
		{
			Colorf c = SpriteModeToSolidColor(color, mode, param);
			Target->DrawSolid(Vector2f((float)xPixel, (float)yPixel) + Offset, NextLayer(), Vector2f(1.0f, 1.0f), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawRectangle(std::int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param)
		{
			if (mode == spriteType_BLENDDISSOLVE) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				std::int32_t paletteOffset;
				if (Texture* noise = owner->GetDissolveNoise(color, param, paletteOffset)) {
					// Other pixels in every tick, as JJ2+ picks them at random every time
					Vector2i noiseSize = noise->GetSize();
					std::uint32_t tick = std::uint32_t(owner->GetGameTick());
					Vector4f texCoords(float(width) / noiseSize.X, float((tick * 37u) % 64u) / 64.0f,
						float(height) / noiseSize.Y, float((tick * 23u) % 64u) / 64.0f);
					Target->DrawTexture(*noise, Vector2f((float)xPixel, (float)yPixel) + Offset, NextLayer(),
						Vector2f((float)width, (float)height), texCoords, Colorf::White, false, 0.0f, paletteOffset);
					return;
				}
			}
			Colorf c = SpriteModeToSolidColor(color, mode, param);
			Target->DrawSolid(Vector2f((float)xPixel, (float)yPixel) + Offset, NextLayer(), Vector2f((float)width, (float)height), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (DrawMissingFontCharacter(xPixel, yPixel, setID, animation, frame, mode, param)) {
				return;
			}
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, direction, 1.0f, 1.0f, 0, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, direction, 1.0f, 1.0f, 0, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawResizedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, 1, xScale, yScale, 0, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawResizedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, 1, xScale, yScale, 0, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawTransformedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, 1, xScale, yScale, angle, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawTransformedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			bool translucent;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
			DrawSpriteFrame(Target, NextLayer(), base, cell,
				xPixel + Offset.X, yPixel + Offset.Y, 1, xScale, yScale, angle, mode, param, owner->GetSpritePaletteOffset(mode, param), translucent);
		}
		void jjCANVAS::DrawSwingingVine(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t length, int32_t curvature, std::uint32_t mode, std::uint8_t param)
		{
			noop();
		}

		void jjCANVAS::ExternalDrawTile(std::int32_t xPixel, int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			DrawTileQuad(Target, NextLayer(), owner->GetLevelHandler()->TileMap(), tile, tileQuadrant, xPixel + Offset.X, yPixel + Offset.Y);
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
				case spriteType_SINGLEHUE:
					// The font tinted by the middle of the gradient of 8 entries
					return PaletteIndexToColor(std::uint8_t(param + 4), 1.0f);
				case spriteType_SHADOW:
					return Colorf(0.0f, 0.0f, 0.0f, 0.5f);
				case spriteType_BRIGHTNESS: {
					// The font times param/128, through the colorizing shader of the font: gray (1.5x the glyph) times
					// 1 + (color - 0.5) * 4, alpha 0.5 keeps that of the glyph
					float level = 0.5f + ((param / 128.0f) / 1.5f - 1.0f) * 0.25f;
					return Colorf(level, level, level, 0.5f);
				}
				default:
					return UI::Font::DefaultColor;
			}
		}

		bool jjCANVAS::DrawMissingFontCharacter(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame,
			std::uint32_t mode, std::uint8_t param) {
			// A character of the font of the original without an image, such as ∞ (127) that only the font of JJ2+ has (see
			// AnimationTables::ResolveSet()), is drawn in the font of the game, in the size of the animation (as STRING::Size)
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (setID != AnimationTables::GetSetID(mFONT, owner->IsTSF()) || animation > 2 || frame == 0 || frame > 127 - 32) {
				return false;
			}
			auto& tables = owner->GetAnimationTables();
			std::uint32_t frameCount, firstFrame;
			Recti frameRect, pixelBounds;
			if (!tables.GetAnimationFrames(tables.GetFirstAnim(setID) + animation, frameCount, firstFrame) || frame >= frameCount ||
				(tables.GetFrameBounds(firstFrame + frame, frameRect, pixelBounds) && pixelBounds.W > 0)) {
				return false;
			}
			ScriptTextStyle style = GetScriptTextStyle(animation);
			if (style.Font == nullptr) {
				return false;
			}
			char ch = char(frame + 32);
			StringView text = (ch == 127 ? "∞"_s : StringView(&ch, 1));
			std::int32_t charOffset = 0;
			// A character of a string is drawn 10 pixels higher than the frame drawn as a sprite (see drawString())
			style.Font->DrawString(Target, text, charOffset, xPixel + Offset.X, yPixel + Offset.Y + 10.0f + style.OffsetY, NextLayer(),
				UI::Alignment::TopLeft, GetTextColor(mode, param), style.Scale, 0.0f, 0.0f, 0.0f, 0.0f, style.CharSpacing, style.LineSpacing);
			return true;
		}

		void jjCANVAS::DrawTextBasicSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param) {
			// The mode is STRING::Mode here, PALSHIFT shifts the colors of the font by the parameter
			DrawScriptText(xPixel, yPixel, text, size, mode, mode == 5 ? std::uint32_t(spriteType_PALSHIFT) : std::uint32_t(spriteType_NORMAL), param);
		}
		void jjCANVAS::DrawTextExtSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param) {
			// Only how the letters move and the alignment are taken of the appearance (see jjTEXTAPPEARANCE::constructorMode())
			std::uint32_t stringMode = (appearance.xAmp != 0 && appearance.yAmp != 0 ? 4 : (appearance.yAmp != 0 ? 3
				: (appearance.align == jjTEXTAPPEARANCE::align_RIGHT ? 2 : 0)));
			DrawScriptText(xPixel, yPixel, text, size, stringMode, mode, param);
		}
		void jjCANVAS::DrawScriptText(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t stringMode,
			std::uint32_t spriteMode, std::uint8_t param) {
			ScriptTextStyle style = GetScriptTextStyle(size);
			if (style.Font == nullptr) {
				return;
			}
			Colorf color = GetTextColor(spriteMode, param);
			// As the menus of the game move the letters of a selected item
			float angleOffset = 0.0f, varianceX = 0.0f, varianceY = 0.0f, speed = 0.0f;
			switch (stringMode) {
				case 1: // DARK
					color = Colorf(0.54f, 0.44f, 0.34f, 0.5f);
					break;
				case 3: // BOUNCE
					angleOffset = 0.7f; varianceY = 1.1f; speed = 0.4f;
					break;
				case 4: // SPIN
					angleOffset = 0.7f; varianceX = 1.1f; varianceY = 1.1f; speed = 0.4f;
					break;
			}
			auto recode = [stringMode](StringView part) {
				return (stringMode == 4 ? Compatibility::JJ2Strings::RecodeString(part) : Compatibility::JJ2Strings::RecodeScriptString(part));
			};

			// A character whose frame of the font of the original a script replaced is drawn as that frame, as JJ2+ draws all
			// of them so (SwitchTestV2 makes `$` to `(` the stars of its par times), the rest in the font of the game; the
			// colors start again after each such character
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto& tables = owner->GetAnimationTables();
			std::int32_t fontSetID = AnimationTables::GetSetID(mFONT, owner->IsTSF());
			// STRING::Size is the animation of the font itself, MEDIUM 0, SMALL 1, LARGE 2
			std::uint32_t fontAnim = (size <= 2 ? size : 0);
			struct Piece {
				String Text;
				std::int32_t Frame;
				float Width;
			};
			SmallVector<Piece, 1> pieces;
			// `^` is ∞ (`x^` is infinite ammo), JJ2+ draws it as the character 127 of its font - the one of the medium font of
			// the game is a small wave
			std::int32_t infinityFrame = -1;
			std::uint32_t frameCount, firstFrame;
			if (tables.GetAnimationFrames(tables.GetFirstAnim(fontSetID) + fontAnim, frameCount, firstFrame) && std::uint32_t(127 - 32) < frameCount) {
				infinityFrame = std::int32_t(firstFrame + (127 - 32));
			}
			std::size_t start = 0;
			for (std::size_t i = 0; i <= text.size(); i++) {
				std::int32_t frame = (i < text.size() && std::uint8_t(text[i]) >= 32 ? tables.FindChangedFrame(fontSetID, fontAnim, std::uint8_t(text[i]) - 32) : -1);
				if (frame < 0 && i < text.size() && text[i] == '^') {
					frame = infinityFrame;
				}
				if (frame < 0 && i < text.size()) {
					continue;
				}
				if (i > start) {
					String part = recode(text.slice(start, i));
					float width = style.Font->MeasureString(part, style.Scale, style.CharSpacing, style.LineSpacing).X;
					pieces.push_back(Piece{ Death::move(part), -1, width });
				}
				if (frame >= 0) {
					Recti frameRect, pixelBounds;
					tables.GetFrameBounds(std::uint32_t(frame), frameRect, pixelBounds);
					pieces.push_back(Piece{ {}, frame, float(frameRect.W) });
				}
				start = i + 1;
			}

			float x;
			if (stringMode == 2 || xPixel >= 0x4000) {
				float width = 0.0f;
				for (const Piece& piece : pieces) {
					width += piece.Width;
				}
				x = (stringMode == 2 ? xPixel + Offset.X - width : GetTextX(xPixel, width));
			} else {
				x = xPixel + Offset.X;
			}
			std::uint16_t z = NextLayer();
			std::int32_t charOffset = 0;
			for (const Piece& piece : pieces) {
				if (piece.Frame < 0) {
					style.Font->DrawString(Target, piece.Text, charOffset, x, yPixel + Offset.Y + style.OffsetY, z,
						UI::Alignment::TopLeft, color, style.Scale, angleOffset, varianceX, varianceY, speed, style.CharSpacing, style.LineSpacing);
				} else {
					// 10 pixels higher than the frame drawn as a sprite, as the characters of JJ2+ (see drawString())
					std::int32_t cell = 0;
					auto* base = tables.ResolveFrame(std::uint32_t(piece.Frame), cell);
					DrawSpriteFrame(Target, z, base, cell, x, yPixel + Offset.Y - 10.0f, 0, 1.0f, 1.0f, 0, spriteMode, param,
						owner->GetSpritePaletteOffset(spriteMode, param));
				}
				x += piece.Width;
			}
		}

		void jjCANVAS::drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param) {
			// The frames of the animation are the characters from the space on, each as wide as its frame; the characters
			// of formatting are left out and `@` breaks the line, as in the text of JJ2
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto& tables = owner->GetAnimationTables();
			std::int32_t paletteOffset = owner->GetSpritePaletteOffset(mode, param);
			std::uint16_t z = NextLayer();
			std::uint32_t firstFrame = const_cast<jjANIMATION&>(animation).get_firstFrame();
			// The frame of a character and its width, `nullptr` for one that isn't drawn
			auto resolve = [&](std::uint8_t ch, std::int32_t& cell, Recti& frameRect) -> GenericGraphicResource* {
				if (ch == '|' || ch == '#' || ch == '~' || ch == '^' || ch == 0xA7 || ch < 32 || ch - 32 >= animation.frameCount) {
					return nullptr;
				}
				auto* base = tables.ResolveFrame(firstFrame + (ch - 32), cell);
				if (base == nullptr || base->FrameCount <= 0) {
					return nullptr;
				}
				frameRect = base->GetFrameRect(std::clamp<std::int32_t>(cell, 0, base->FrameCount - 1));
				return base;
			};
			auto getLineWidth = [&](std::size_t from) {
				float width = 0.0f;
				for (std::size_t i = from; i < text.size() && text[i] != '@' && text[i] != '\n'; i++) {
					std::int32_t cell = 0;
					Recti frameRect;
					if (resolve(std::uint8_t(text[i]), cell, frameRect) != nullptr) {
						width += float(frameRect.W);
					}
				}
				return width;
			};

			// The characters are 10 pixels higher than the frames drawn as sprites at the same position, measured in JJ2+
			constexpr float CharOffsetY = -10.0f;
			float x = GetTextX(xPixel, xPixel >= 0x4000 ? getLineWidth(0) : 0.0f), y = yPixel + Offset.Y + CharOffsetY;
			float lineHeight = 0.0f;
			for (std::size_t i = 0; i < text.size(); i++) {
				std::uint8_t ch = std::uint8_t(text[i]);
				if (ch == '@' || ch == '\n') {
					x = GetTextX(xPixel, xPixel >= 0x4000 ? getLineWidth(i + 1) : 0.0f);
					y += lineHeight;
					continue;
				}
				std::int32_t cell = 0;
				Recti frameRect;
				if (auto* base = resolve(ch, cell, frameRect)) {
					DrawSpriteFrame(Target, z, base, cell, x, y, 0, 1.0f, 1.0f, 0, mode, param, paletteOffset);
					x += float(frameRect.W);
					lineHeight = std::max(lineHeight, float(frameRect.H));
				}
			}
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
	}

	// TODO

	// Draws in the level what a script draws by the functions of jjDraw*, see Legacy::ScriptWorldCanvas. The layer the
	// coordinates follow (`layerXY`) and the player that sees it (`playerID`) aren't taken into account, the coordinates
	// are those of the level and everyone sees it.
	static void DrawWorldSprite(LevelScriptLoader* owner, GenericGraphicResource* graphics, std::int32_t cell, float x, float y,
		std::int8_t direction, float scaleX, float scaleY, std::int32_t angle, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, bool translucent)
	{
		if (graphics == nullptr || ContentResolver::Get().IsHeadless()) {
			return;
		}
		Legacy::ScriptWorldCanvas::SpriteDraw draw = { graphics, cell, x, y, direction, scaleX, scaleY, angle, mode, param, owner->GetLayerDrawZ(layerZ),
			owner->GetSpritePaletteOffset(mode, param) };
		draw.TranslucentFrame = translucent;
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
		bool translucent;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, direction, 1.0f, 1.0f, 0, mode, param, layerZ, translucent);
	}

	void LevelScriptLoader::jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		bool translucent;
		owner->OnFrameDrawn(xPixel, yPixel, sprite, mode);
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, direction, 1.0f, 1.0f, 0, mode, param, layerZ, translucent);
	}

	void LevelScriptLoader::jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		bool translucent;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, 0, mode, param, layerZ, translucent);
	}

	void LevelScriptLoader::jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		bool translucent;
		owner->OnFrameDrawn(xPixel, yPixel, sprite, mode);
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, 0, mode, param, layerZ, translucent);
	}

	void LevelScriptLoader::jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		bool translucent;
		auto* graphics = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, angle, mode, param, layerZ, translucent);
	}

	void LevelScriptLoader::jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::int32_t cell = 0;
		bool translucent;
		owner->OnFrameDrawn(xPixel, yPixel, sprite, mode);
		auto* graphics = owner->GetAnimationTables().ResolveFrame(sprite, cell, &translucent);
		DrawWorldSprite(owner, graphics, cell, xPixel, yPixel, 0, xScale, yScale, angle, mode, param, layerZ, translucent);
	}

	Legacy::ScriptWorldCanvas& LevelScriptLoader::GetWorldCanvas()
	{
		if (_worldCanvas == nullptr) {
			_worldCanvas = std::make_unique<Legacy::ScriptWorldCanvas>();
			// In the order the script draws, as in JJ2+ (a bomb drawn behind a pumpkin)
			_worldCanvas->OrderedDraws = true;
			_worldCanvas->setParent(_levelHandler->_rootNode.get());
		}
		return *_worldCanvas;
	}

	std::int32_t LevelScriptLoader::GetSpritePaletteOffset(std::uint32_t mode, std::uint8_t param)
	{
		std::uint16_t kind;
		switch (mode) {
			case Legacy::spriteType_PALSHIFT:
			case Legacy::spriteType_TRANSLUCENTPALSHIFT: kind = 0; break;
			case Legacy::spriteType_SINGLEHUE:
			case Legacy::spriteType_TRANSLUCENTSINGLEHUE: kind = 1; break;
			case Legacy::spriteType_GEM: kind = 2; break;
			case Legacy::spriteType_ALPHAMAP: kind = 3; break;
			case Legacy::spriteType_NEONGLOW: kind = 4; param = std::min<std::uint8_t>(param, 6); break;
			case Legacy::spriteType_PLAYER: {
				// In the colors of the fur of the player of the parameter
				for (Actors::Player* player : _levelHandler->_players) {
					if (player->GetPlayerIndex() == param) {
						return std::max<std::int32_t>(player->GetPaletteOffset(), 0);
					}
				}
				return 0;
			}
			default: return 0;
		}

		std::uint16_t key = std::uint16_t((kind << 8) | param);
		auto it = _spritePaletteOffsets.find(key);
		if (it == _spritePaletteOffsets.end()) {
			auto& resolver = ContentResolver::Get();
			std::int32_t offset;
			switch (kind) {
				case 0: offset = resolver.AcquireShiftedPaletteOffset(param); break;
				case 1: offset = resolver.AcquireSingleHuePaletteOffset(param); break;
				case 2: offset = resolver.AcquireGemPaletteOffset(param); break;
				case 3: offset = resolver.AcquireAlphaMapPaletteOffset(param); break;
				default: offset = resolver.AcquireNeonGlowPaletteOffset(param); break;
			}
			it = _spritePaletteOffsets.emplace(key, offset).first;
		}
		// Without a free row it's drawn in the sprite palette
		return std::max<std::int32_t>(it->second, 0);
	}

	Texture* LevelScriptLoader::GetDissolveNoise(std::uint8_t color, std::uint8_t density, std::int32_t& paletteOffset)
	{
		constexpr std::int32_t NoiseSize = 64;
		if (ContentResolver::Get().IsHeadless()) {
			return nullptr;
		}
		std::uint32_t key = 0x10000u | (std::uint32_t(color) << 8) | density;
		auto it = _spritePaletteOffsets.find(key);
		if (it == _spritePaletteOffsets.end()) {
			it = _spritePaletteOffsets.emplace(key, ContentResolver::Get().AcquireDissolvePaletteOffset(color, density)).first;
		}
		paletteOffset = it->second;
		if (paletteOffset < 0) {
			return nullptr;
		}
		if (_noiseTexture == nullptr) {
			std::uint8_t pixels[NoiseSize * NoiseSize];
			auto& random = Random();
			for (std::uint8_t& pixel : pixels) {
				pixel = std::uint8_t(random.Next() & 0xFF);
			}
			_noiseTexture = std::make_unique<Texture>("ScriptNoise", Texture::Format::R8, NoiseSize, NoiseSize);
			_noiseTexture->SetMinFiltering(SamplerFilter::Nearest);
			_noiseTexture->SetMagFiltering(SamplerFilter::Nearest);
			_noiseTexture->SetWrap(SamplerWrapping::Repeat);
			_noiseTexture->LoadFromTexels(pixels, 0, 0, NoiseSize, NoiseSize);
		}
		return _noiseTexture.get();
	}

	std::uint16_t LevelScriptLoader::GetLayerDrawZ(std::int32_t layerNumber) const
	{
		// Every object drawn asks for it, the order of the layers is sorted only once a frame
		const std::uint32_t frame = theApplication().GetFrameCount();
		if (_layerDrawZFrame != frame) {
			_layerDrawZFrame = frame;
			std::memset(_layerDrawZCache, 0, sizeof(_layerDrawZCache));
		}
		std::uint16_t& cached = _layerDrawZCache[std::uint8_t(std::clamp<std::int32_t>(layerNumber, -128, 127) + 128)];
		if (cached == 0) {
			cached = ResolveLayerDrawZ(layerNumber);
		}
		return cached;
	}

	std::uint16_t LevelScriptLoader::ResolveLayerDrawZ(std::int32_t layerNumber) const
	{
		// The sprite layer is where the objects are, on the others just behind the next layer, in front of what the hook of
		// the layer draws (see OnDrawLayers()), as JJ2+ draws the sprites of a layer over it
		Tiles::TileMap* tileMap = _levelHandler->_tileMap.get();
		std::int32_t index;
		if (!_layersOutOfOrder.empty()) {
			// With an order of the layers of a script, the layer is as far from the sprite layer in it as the number is from
			// 4, as measured in JJ2+ (shadows drawn 7 and 8 layers behind it, cars
			// in the sprite layer, behind the layer 3 in front of it)
			SmallVector<std::int32_t, 16> order;
			GetLayerOrder(order);
			std::int32_t spritePos = -1;
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				if (tileMap != nullptr && order[i] == tileMap->_sprLayerIndex) {
					spritePos = i;
					break;
				}
			}
			if (spritePos < 0 || order.empty()) {
				index = -1;
			} else {
				std::int32_t pos = std::clamp(spritePos + (layerNumber - 4), 0, std::int32_t(order.size()) - 1);
				index = (pos != spritePos ? order[pos] : -1);
			}
		} else {
			index = (layerNumber != 4 ? GetLayerIndex(layerNumber) : -1);
		}
		if (tileMap == nullptr || index < 0 || index >= std::int32_t(tileMap->_layers.size())) {
			return std::uint16_t(ILevelHandler::SpritePlaneZ);
		}
		std::int32_t first = tileMap->_layers[index].Description.Depth + 1;
		std::int32_t last = first + 8;
		for (const auto& other : tileMap->_layers) {
			if (other.Description.Depth >= first && other.Description.Depth <= last) {
				last = std::max<std::int32_t>(other.Description.Depth - 1, first);
			}
		}
		return std::uint16_t(last);
	}

	void LevelScriptLoader::jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (ContentResolver::Get().IsHeadless()) {
			return;
		}
		Legacy::ScriptWorldCanvas::TileDraw draw = { owner->_levelHandler->TileMap(), xPixel, yPixel, tile,
			std::uint8_t(std::min<std::uint32_t>(tileQuadrant, 4)), owner->GetLayerDrawZ(layerZ) };
		owner->GetWorldCanvas().AddTile(draw);
	}

	void LevelScriptLoader::jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		// STRING::Mode, PALSHIFT shifts the colors of the font by the parameter (as on the HUD, see jjCANVAS::DrawTextBasicSize())
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->AddWorldText(xPixel, yPixel, text, size, Legacy::GetTextColor(mode == 5 ? std::uint32_t(spriteType_PALSHIFT) : std::uint32_t(spriteType_NORMAL), param), layerZ);
	}

	void LevelScriptLoader::jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		// How the letters move isn't drawn in the level
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->AddWorldText(xPixel, yPixel, text, size, Legacy::GetTextColor(std::uint32_t(spriteMode), param2), layerZ);
	}

	void LevelScriptLoader::AddWorldText(float xPixel, float yPixel, StringView text, std::uint32_t size, const Colorf& color, std::int8_t layerZ)
	{
		if (ContentResolver::Get().IsHeadless() || text.empty()) {
			return;
		}
		GetWorldCanvas().AddText({ Compatibility::JJ2Strings::RecodeScriptString(text), xPixel, yPixel, size, color, GetLayerDrawZ(layerZ) });
	}

	std::int32_t LevelScriptLoader::jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style) {
		// The fonts live in ContentResolver (the HUD just borrows them), so this works even when called during script
		// initialization / onLevelLoad, before the HUD has been created
		ScriptTextStyle textStyle = GetScriptTextStyle(size);
		if (textStyle.Font == nullptr) {
			return 0;
		}
		auto recodedText = Compatibility::JJ2Strings::RecodeScriptString(text);
		return (std::int32_t)textStyle.Font->MeasureString(recodedText, textStyle.Scale, textStyle.CharSpacing, textStyle.LineSpacing).X;
	}

	void LevelScriptLoader::RegisterLegacyCanvas(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjTEXTAPPEARANCE", sizeof(jjTEXTAPPEARANCE), asOBJ_VALUE | asOBJ_POD);
		engine->RegisterObjectBehaviour("jjTEXTAPPEARANCE", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(jjTEXTAPPEARANCE::constructor), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjTEXTAPPEARANCE", asBEHAVE_CONSTRUCT, "void f(STRING::Mode mode)", asFUNCTION(jjTEXTAPPEARANCE::constructorMode), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjTEXTAPPEARANCE", "jjTEXTAPPEARANCE& opAssign(STRING::Mode)", asMETHODPR(jjTEXTAPPEARANCE, operator=, (uint32_t), jjTEXTAPPEARANCE&), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int xAmp", asOFFSET(jjTEXTAPPEARANCE, xAmp));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int yAmp", asOFFSET(jjTEXTAPPEARANCE, yAmp));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int spacing", asOFFSET(jjTEXTAPPEARANCE, spacing));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "bool monospace", asOFFSET(jjTEXTAPPEARANCE, monospace));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "bool skipInitialHash", asOFFSET(jjTEXTAPPEARANCE, skipInitialHash));
		engine->SetDefaultNamespace("STRING");
		engine->RegisterEnum("SignTreatment");
		engine->RegisterEnumValue("SignTreatment", "HIDESIGN", jjTEXTAPPEARANCE::ch_HIDE);
		engine->RegisterEnumValue("SignTreatment", "DISPLAYSIGN", jjTEXTAPPEARANCE::ch_DISPLAY);
		engine->RegisterEnumValue("SignTreatment", "SPECIALSIGN", jjTEXTAPPEARANCE::ch_SPECIAL);
		engine->RegisterEnum("Alignment");
		engine->RegisterEnumValue("Alignment", "DEFAULT", jjTEXTAPPEARANCE::align_DEFAULT);
		engine->RegisterEnumValue("Alignment", "LEFT", jjTEXTAPPEARANCE::align_LEFT);
		engine->RegisterEnumValue("Alignment", "CENTER", jjTEXTAPPEARANCE::align_CENTER);
		engine->RegisterEnumValue("Alignment", "RIGHT", jjTEXTAPPEARANCE::align_RIGHT);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment at", asOFFSET(jjTEXTAPPEARANCE, at));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment caret", asOFFSET(jjTEXTAPPEARANCE, caret));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment hash", asOFFSET(jjTEXTAPPEARANCE, hash));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment newline", asOFFSET(jjTEXTAPPEARANCE, newline));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment pipe", asOFFSET(jjTEXTAPPEARANCE, pipe));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment section", asOFFSET(jjTEXTAPPEARANCE, section));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment tilde", asOFFSET(jjTEXTAPPEARANCE, tilde));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::Alignment align", asOFFSET(jjTEXTAPPEARANCE, align));

		engine->RegisterObjectType("jjCANVAS", sizeof(jjCANVAS), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterObjectMethod("jjCANVAS", "void drawPixel(int xPixel, int yPixel, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRectangle(int xPixel, int yPixel, int width, int height, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawRectangle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawResizedSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawResizedSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawResizedSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawResizedCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRotatedSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawTransformedSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRotatedSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawTransformedCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSwingingVineSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int length, int curvature, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawSwingingVine), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjCANVAS", "void drawTile(int xPixel, int yPixel, uint16 tile, TILE::Quadrant tileQuadrant = TILE::ALLQUADRANTS)", asMETHOD(jjCANVAS, ExternalDrawTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, STRING::Size size = STRING::SMALL, STRING::Mode mode = STRING::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, DrawTextBasicSize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0)", asMETHOD(jjCANVAS, DrawTextExtSize), asCALL_THISCALL);

		engine->RegisterGlobalFunction("void jjDrawPixel(float xPixel, float yPixel, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawPixel), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRectangle(float xPixel, float yPixel, int width, int height, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawRectangle), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawSpriteFromCurFrame), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawResizedSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawResizedSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawResizedSpriteFromCurFrame), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRotatedSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawRotatedSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawRotatedSpriteFromCurFrame), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int length, int curvature, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawSwingingVineSpriteFromCurFrame), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawTile(float xPixel, float yPixel, uint16 tile, TILE::Quadrant tileQuadrant = TILE::ALLQUADRANTS, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawTile), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, STRING::Size size = STRING::SMALL, STRING::Mode mode = STRING::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawString), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjDrawStringEx), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetStringWidth(const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in style)", asFUNCTION(jjGetStringWidth), asCALL_CDECL);
	}
}

#endif
