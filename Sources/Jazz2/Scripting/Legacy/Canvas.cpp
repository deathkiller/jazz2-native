#if defined(WITH_ANGELSCRIPT)

#include "Canvas.h"
#include "AnimationTables.h"
#include "WorldCanvas.h"
#include "Internal.h"

#include "../../Compatibility/JJ2Strings.h"
#include "../../UI/HUD.h"

#include "../../../nCine/Base/Algorithms.h"

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

	// The text of scripts is drawn a quarter smaller than in JJ2+, as large as JJ2+ draws it, it looks too big in this game
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
		style.Scale *= ScriptTextScale;
		style.OffsetY *= ScriptTextScale;
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
					std::uint32_t mode, std::uint8_t param, std::int32_t spritePaletteOffset)
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

				float angleRad = angle * fTwoPi / 1024.0f;
				canvas->DrawTexture(*base->TextureDiffuse.get(), pos, z, size, texCoords, color,
					mode == spriteType_BLENDLIGHTEN, angleRad, paletteOffset, shader);
			}
		}

		void jjTEXTAPPEARANCE::constructor(void* self)
		{
			new(self) jjTEXTAPPEARANCE{};
		}
		void jjTEXTAPPEARANCE::constructorMode(std::uint32_t mode, void* self)
		{
			new(self) jjTEXTAPPEARANCE{};
		}

		jjTEXTAPPEARANCE& jjTEXTAPPEARANCE::operator=(std::uint32_t other)
		{
			noop(); return *this;
		}

		bool ScriptWorldCanvas::OnDraw(RenderQueue& renderQueue)
		{
			Canvas::OnDraw(renderQueue);

			for (const SolidDraw& draw : _solids) {
				Colorf color = SpriteModeToSolidColor(draw.Color, draw.Mode, draw.Param);
				DrawSolid(Vector2f(draw.X, draw.Y), draw.Z, Vector2f(draw.Width, draw.Height), color, draw.Mode == spriteType_BLENDLIGHTEN);
			}
			for (const SpriteDraw& draw : _sprites) {
				DrawSpriteFrame(this, draw.Z, draw.Graphics, draw.Cell, draw.X, draw.Y, draw.Direction, draw.ScaleX, draw.ScaleY,
					draw.Angle, draw.Mode, draw.Param, draw.PaletteOffset);
			}
			return true;
		}

		jjCANVAS::jjCANVAS(UI::HUD* hud, const Rectf& view, std::uint16_t* drawCount)
			: Hud(hud), View(view), DrawCount(drawCount)
		{
		}

		std::uint16_t jjCANVAS::NextLayer()
		{
			// Commands of the same layer are drawn in no particular order, so every draw gets a layer of its own - from the
			// main layer of the HUD up to just below its font layer, the draws after that stay on the topmost one
			constexpr std::uint16_t MaxDraws = UI::HUD::FontLayer - UI::HUD::MainLayer - 1;
			std::uint16_t index = (DrawCount != nullptr ? std::min<std::uint16_t>((*DrawCount)++, MaxDraws) : 0);
			return std::uint16_t(UI::HUD::MainLayer + index);
		}

		void jjCANVAS::DrawPixel(std::int32_t xPixel, int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param)
		{
			Colorf c = SpriteModeToSolidColor(color, mode, param);
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), NextLayer(), Vector2f(1.0f, 1.0f), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawRectangle(std::int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param)
		{
			Colorf c = SpriteModeToSolidColor(color, mode, param);
			Hud->DrawSolid(Vector2f((float)xPixel, (float)yPixel), NextLayer(), Vector2f((float)width, (float)height), c,
				mode == spriteType_BLENDLIGHTEN);
		}
		void jjCANVAS::DrawSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, direction, 1.0f, 1.0f, 0, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawResizedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawResizedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, 0, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawTransformedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveSprite(setID, animation, frame, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawTransformedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t cell = 0;
			auto* base = owner->GetAnimationTables().ResolveFrame(sprite, cell);
			DrawSpriteFrame(Hud, NextLayer(), base, cell,
				(float)xPixel, (float)yPixel, 1, xScale, yScale, angle, mode, param, owner->GetSpritePaletteOffset(mode, param));
		}
		void jjCANVAS::DrawSwingingVine(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t length, int32_t curvature, std::uint32_t mode, std::uint8_t param)
		{
			noop();
		}

		void jjCANVAS::ExternalDrawTile(std::int32_t xPixel, int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant)
		{
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
			auto recodedText = Compatibility::JJ2Strings::RecodeScriptString(text);
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
			auto recodedText = Compatibility::JJ2Strings::RecodeScriptString(text);
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
		Legacy::ScriptWorldCanvas::SpriteDraw draw = { graphics, cell, x, y, direction, scaleX, scaleY, angle, mode, param, owner->GetLayerDrawZ(layerZ),
			owner->GetSpritePaletteOffset(mode, param) };
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

	std::int32_t LevelScriptLoader::GetSpritePaletteOffset(std::uint32_t mode, std::uint8_t param)
	{
		std::uint16_t kind;
		switch (mode) {
			case Legacy::spriteType_PALSHIFT:
			case Legacy::spriteType_TRANSLUCENTPALSHIFT: kind = 0; break;
			case Legacy::spriteType_SINGLEHUE:
			case Legacy::spriteType_TRANSLUCENTSINGLEHUE: kind = 1; break;
			case Legacy::spriteType_GEM: kind = 2; break;
			default: return 0;
		}

		std::uint16_t key = std::uint16_t((kind << 8) | param);
		auto it = _spritePaletteOffsets.find(key);
		if (it == _spritePaletteOffsets.end()) {
			auto& resolver = ContentResolver::Get();
			std::int32_t offset = (kind == 0 ? resolver.AcquireShiftedPaletteOffset(param)
				: (kind == 1 ? resolver.AcquireSingleHuePaletteOffset(param) : resolver.AcquireGemPaletteOffset(param)));
			it = _spritePaletteOffsets.emplace(key, offset).first;
		}
		// Without a free row it's drawn in the sprite palette
		return std::max<std::int32_t>(it->second, 0);
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
