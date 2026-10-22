#pragma once

#include "../../Main.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Primitives/Vector4.h"

namespace Jazz2::Formats
{
	struct LayerRecord;
	struct TexturedLayerParams;
}

namespace Jazz2::Tiles
{
	/**
		@brief Describes how a layer's parallax scrolling speed is interpreted

		Selected per axis in @ref LayerDescription to control how a layer follows the camera --- the default parallax
		model, the same counted from the top/left of the level, locking to the top/left of the screen, fitting the whole
		layer into view, or treating the speed values as multipliers of the camera size rather than its position.

		They are the speed models of JJ2+ (`LAYERSPEEDMODEL`) under other values, and a layer is placed as JJ2+ places
		it, measured with `Tests/Level/LayerProbe.j2as` (see @ref LayerDescription::GetViewCoordinates()). `NORMAL`
		has no model of its own, it's @ref Default without the speed for a layer that has an auto speed.
	*/
	enum class LayerSpeedModel {
		/** @brief Default model, as `LAYERSPEEDMODEL::BOTHSPEEDS` --- moves by the speed and the auto speed, with the origin depending on the size of the view */
		Default,
		/**
			Ignores all speed and offset settings to be tied to the top/left side of the screen, as `LAYERSPEEDMODEL::LAYER8`.
			A textured layer in the perspective of JJ2+ (@ref Formats::TexturedLayerParams::Fullscreen) is placed where
			layer 5 of the original game is drawn instead, or at the screen without one, and moved by its own auto speed,
			as there. Since 4.0.0.
		*/
		AlwaysOnTop,
		/** @brief Ignores the speed and auto speed properties, and instead ensures that the full extent of this layer will be visible and no blank space outside of it will be shown */
		FitLevel,
		/** @brief Treats the layer's speed and auto speed properties on this axis as multipliers of the current camera size, rather than camera position */
		SpeedMultipliers,
		/**
			As @ref Default, but with the origin at the top/left side of the level whatever the size of the view, and
			without the inherent offset, as `LAYERSPEEDMODEL::FROMSTART`. Since 4.0.0, earlier versions place such a
			layer as one of @ref Default.
		*/
		FromStart
	};

	/**
		@brief Specifies how a layer is rendered
		
		Stored in @ref LayerDescription to choose the rendering path for a layer --- ordinary textured tiles, a solid
		or color-tinted fill, tiles with their colors moved through the palette, one of the two layers that show
		something of what is behind them (x-ray and blur), one of the special textured background effects (sky,
		tunnel, wave or cylinder) used for distant parallax backdrops, or a reflection of what is above its horizon.

		The x-ray, blur and reflection layers need a backend that can sample a render target in a shader. Elsewhere an
		x-ray layer is drawn as a default one showing its animated tiles as they are outside the objects, and a blur
		or a reflection layer isn't drawn at all, so a level looks as it would without the effects.

		The textured background effects take the values the textured layers of JJ2+ have (`TEXTURE::Style`, the type
		of the textured background of a layer of the original game). Any number of layers can be textured or reflections,
		each is drawn with a texture of its own, made of its tiles.
	*/
	enum class LayerRendererType {
		Default,				/**< Default rendering */
		Solid,					/**< Solid color rendering */
		Tinted,					/**< Color-tinted rendering */
		/**
			X-ray --- the last frame of an animated tile of the layer is its mask frame, shown only where an object is
			behind it, in the object's silhouette. Elsewhere the tile animates through the frames before it as usual,
			and a tile of a single frame shows nothing at all. Any frame may be the empty tile, which cuts the object
			out of the layer or shows the tile only over the object. Static tiles are drawn as usual.
		*/
		XRay,
		/**
			Blur behind --- the layer isn't drawn, it shows what is below it instead, per pixel by the channels of its
			tiles: moved horizontally by red and vertically by green (128 leaves it in place, every step away from it
			moves it by a quarter of the layer's pixel, so up to 32 pixels either way), mirrored vertically by blue,
			and blurred by alpha - the reflection too, so a gradient fades a reflection out into a blur. A pixel of
			no alpha does nothing at all, whatever its colour, so alpha 1 is one that moves or mirrors without a
			blur. The alpha of the layer's color sets how strong all of it is. The mirror line of a reflection is the
			first visible row of the topmost tile of the uninterrupted column of reflective tiles it belongs to, so a
			taller column reflects more of what is above it. The blur follows the blur effects option, the rest is
			always shown.
		*/
		BlurBehind,
		/**
			Palette shift, as `SPRITE::PALSHIFT` of JJ2+ --- every pixel of the tiles takes the color the given number
			of entries further in the palette, wrapping around after its end. The shift is the red of the layer's
			color (0 to 255, as a byte), its alpha how opaque the layer is. The transparent pixels (index 0) stay
			transparent, a pixel moved onto index 0 shows its color. It follows the palette as it changes. Tiles
			stored in true color aren't changed. Readers before 4.0.0 draw such a layer as an ordinary one.
		*/
		PaletteShift,
		/**
			Single color, as `SPRITE::SINGLECOLOR` of JJ2+ --- every visible pixel of the tiles takes the same color, the
			entry of the palette the red of the layer's color is (0 to 255, as a byte), so only the shapes of the tiles
			are seen, e.g. a shadow of another layer. It follows the palette as it changes, but only the color of the
			entry is taken, not its alpha. The alpha is how opaque the layer is. Readers before 4.0.0 draw such a layer as
			an ordinary one.
		*/
		SingleColor,
		/**
			Chroma key, as `SPRITE::CHROMAKEY` of JJ2+ --- the tiles are drawn as they are, but only over the pixels of
			what is behind the layer that have the color of the entry of the palette the red of the layer's color is (0
			to 255, as a byte), so a part of the scene painted in that color is a window the layer shows through. The
			alpha is how opaque the layer is. Where the scene behind the layers can't be read, the layer isn't drawn.
			Readers before 4.0.0 draw such a layer as an ordinary one.
		*/
		ChromaKey,

		/**
			Textured background --- sky, as `TEXTURE::WARPHORIZON` of JJ2+: the tiles of a layer of 8×8 tiles as two planes
			stretching to the horizon, fading into the red, green and blue of the layer's color there; non-zero alpha
			shows stars over the horizon. Where the vanishing point and the horizon are (@ref LayerDescription::TextureFadePosition)
			and whether it fades out (@ref LayerDescription::TextureFade) are kept apart from the color (see
			@ref Formats::TexturedLayerParams), readers before 4.0.0 draw them in the middle, fading. A layer of another
			size is drawn as ordinary tiles, unless it has a texture of its own (see
			@ref Formats::TexturedLayerChunk::Entry::Texture), which any textured background takes instead of its tiles.

			Any textured background can also be drawn only where the layer is placed, as an ordinary layer would be, as
			`SURFACE::INNERWINDOW` and `SURFACE::INNERLAYER` of JJ2+ (@ref LayerDescription::TextureFlags), but a wave.
			Over the whole view, a sky, a tunnel and a cylinder are drawn as the sky layers always were here, or in the
			perspective of JJ2+ with the texture placed by the speeds of the layer as `SURFACE::FULLSCREEN` (see
			@ref Formats::TexturedLayerParams::Fullscreen), which the inner surfaces are always drawn in.
		*/
		Sky = 10,
		/**
			Textured background --- tunnel, as `TEXTURE::TUNNEL` of JJ2+: the tiles of a layer of 8×8 tiles as circles
			getting smaller into the distance, fading into the red, green and blue of the layer's color in the center;
			non-zero alpha shows stars over it. Where the center is (@ref LayerDescription::TextureFadePosition), whether it fades
			out (@ref LayerDescription::TextureFade) and whether the circles spiral (@ref LayerDescription::TextureFlags)
			are kept apart from the color (see @ref Formats::TexturedLayerParams). A layer of another size is drawn as
			ordinary tiles, unless it has a texture of its own (see @ref Sky).
		*/
		Tunnel,

		// 12 is `TEXTURE::MENU` of JJ2+ (Sky + its style), which isn't drawn yet, a layer of it is drawn as ordinary tiles

		/**
			Textured background --- tile menu, as `TEXTURE::TILEMENU` of JJ2+: the rotating background of the main menu of
			the original, three layers repeated over the view, each turning and zooming in and out at its own pace. The
			frontmost is the texture of the layer (its 8×8 tiles at the top left, or its texture of its own, see @ref Sky),
			the middle one the 2×2 tiles at the top left of the sprite layer and the back one its tile (0, 2). They're
			drawn at half their size, at their own size with non-zero alpha of the layer's color. The point all of them
			turn around is a fraction of the view (@ref LayerDescription::TextureFadePosition), they turn and zoom at the
			pace of JJ2+, whatever the speeds of the layer are. Readers before 4.0.0 draw such a layer as ordinary tiles.
		*/
		TileMenu = 13,

		/**
			Textured background --- wave, as `TEXTURE::WAVE` of JJ2+: the tiles of a layer of 8×8 tiles as a texture
			repeated where the layer's own tiles would be, with each row moved left or right and each column up or down by
			sine waves. The red of the layer's color is the wavelength of the horizontal waves, a period of one more row
			than it, green the same of the vertical waves over the columns (0 for no waves), and blue the speed the waves
			move at, as a signed byte, 1/1024 of a period each 70th of a second; alpha isn't taken. How far the waves move
			the texture (@ref LayerDescription::TextureFadePosition, in units of 128 pixels) and which way the vertical
			waves move (@ref LayerDescription::TextureFlags) are kept apart from the color (see @ref Formats::TexturedLayerParams).
			As measured in the original, the waves follow the texture, not the view, and a texel moves by the waves of
			where it is in the texture. A layer of another size is drawn as ordinary tiles, unless it has a texture of
			its own (see @ref Sky). Readers before 4.0.0 draw such a layer as ordinary tiles.
		*/
		Wave = 14,

		/**
			Textured background --- cylinder, as `TEXTURE::CYLINDER` of JJ2+, always drawn in its perspective (it has no other
			model, see @ref LayerDescription::HasPlusPerspective()): the tiles of a layer of 8×8 tiles as the inside of a
			horizontal tube. They go once from the top of the view to the bottom, narrower across towards the top and the
			bottom, a row an angle of half a turn over the view from the horizon; the horizontal scroll moves along the tube
			a quarter as fast as an ordinary layer, the vertical one turns it. Near the horizon it fades into the red, green
			and blue of the layer's color, to 65 % of it, the alpha isn't taken. Where the center of the horizon is (@ref LayerDescription::TextureFadePosition),
			whether it fades (@ref LayerDescription::TextureFade) and whether it's drawn at half size, four times across
			the view and more curved (@ref LayerDescription::TextureFlags), are kept apart from the color (see
			@ref Formats::TexturedLayerParams). A layer of another size is drawn as ordinary tiles, unless it has a texture
			of its own (see @ref Sky). Readers before 4.0.0 draw such a layer as an ordinary one modulated by its color.
		*/
		Cylinder = 15,

		/**
			Reflection, as `TEXTURE::REFLECTION` of JJ2+ --- the layer is drawn below its horizon only, where it shows
			what is drawn behind it above the horizon mirrored, as a plane of water stretching to the horizon would.
			The tiles aren't shown, the 8×8 of them at the top left of the layer --- or its texture of its own (see
			@ref Formats::TexturedLayerChunk::Entry::Texture) --- are laid on the plane at twice the size of the plane's
			own units and move what is mirrored, as ripples, up or down by how much higher the palette index of a pixel is
			than that of the pixel half of the texture further along the plane, and left or right by how much higher the
			index of the pixel a quarter of the texture back and to the left is than that of the one half of the texture
			to the right of it (the brightest channel of a tile in true color stands for its index), as JJ2+ does; a
			texture that is the same there doesn't ripple. The red of the layer's color is the opacity of the tint over the
			plane, green the distance of the plane (the higher, the smaller the texture), blue the distortion, how far the
			ripples move what is mirrored (0.6 of a pixel for each 32 palette indices times the distortion of 0 to 255,
			scaled to how strong the ripples of the built-in textures of JJ2+ are, the same on every row but the first 15
			below the horizon, where it's halved for each time the rows halve), and non-zero
			alpha blurs it horizontally. The horizon and the vanishing point (@ref LayerDescription::TextureFadePosition),
			the palette entry of the tint (@ref LayerDescription::TextureFade) and whether it's drawn only where the layer is
			placed, as an ordinary layer would be (@ref LayerDescription::TextureFlags), are kept apart from the color (see
			@ref Formats::TexturedLayerParams). Over the whole view the texture moves with the view by the speeds of the
			layer and by itself by its auto speeds (by the auto speed Y twice as far as the layer, as in JJ2+); drawn
			only where the layer is, the speeds place the layer, and the texture moves inside it by the inner speeds and
			the inner auto speeds instead (@ref LayerDescription::TextureInnerSpeed, @ref LayerDescription::TextureInnerAutoSpeed)
			--- a positive one moves it to the left and away towards the horizon, as in JJ2+. Where there are more rows below the horizon than above it, the
			top row of the view is repeated, a horizon at the top of the view reflects black. Readers before 4.0.0 draw such
			a layer as an ordinary one modulated by its color.
		*/
		Reflection = 16
	};

	/**
		@brief How a textured background is drawn over what is behind it

		The sprite modes of JJ2+ a textured layer can be drawn with (see @ref LayerDescription::TextureBlend). Only scripts
		set it, a level file doesn't store it. @ref Translucent and @ref ChromaKey need a backend that can sample a render
		target in a shader, elsewhere such a layer is drawn opaque.
	*/
	enum class TexturedLayerBlend : std::uint8_t {
		/** @brief Opaque */
		Normal,
		/** @brief As opaque as @ref LayerDescription::TextureBlendParam is, from 0 to 255 */
		Translucent,
		/** @brief Only over the pixels behind it of the color of the palette entry @ref LayerDescription::TextureBlendParam, as `SPRITE::CHROMAKEY` */
		ChromaKey,
		/** @brief Lightens what is behind it, mixed over it as far as @ref LayerDescription::TextureBlendParam is from 0 to 255, as `SPRITE::BLEND_SCREEN` */
		Screen,
		/** @brief Darkens what is behind it, mixed over it as far as @ref LayerDescription::TextureBlendParam is from 0 to 255, as `SPRITE::BLEND_MULTIPLY` */
		Multiply
	};

	/**
		@brief Describes the configuration of a tile map layer

		Holds the parallax and rendering properties of a single layer --- its depth, per-axis scroll and auto-scroll
		speeds, scroll offsets, repeat flags, speed models and the renderer type with its color parameter. One is
		stored in each @ref TileMapLayer and drives how that layer is positioned and drawn relative to the camera.

		Where the game places a layer for a view is worked out here rather than by the tile map, so it's the same
		without one --- the preview of a level made when the level is converted (see @ref Formats::LevelPreview)
		places the layers this way too.
	*/
	struct LayerDescription {
		/** @brief Hardcoded offset for layer positioning */
		static constexpr std::int32_t HardcodedOffset = 70;
		/**
		 * @brief How much further an auto speed of the original game moves a layer each frame than the same auto speed here
		 *
		 * The original game and JJ2+ move a layer by its auto speed each of their 70 ticks a second, a layer here moves by
		 * its auto speed each of the 60 frames a second (see @ref TileMap::OnUpdate()), see @ref FromOriginalAutoSpeed().
		 */
		static constexpr float OriginalAutoSpeedScale = 70.0f / 60.0f;

		/** @brief Layer depth (Z position) */
		std::uint16_t Depth;
		/** @brief Horizontal speed */
		float SpeedX;
		/** @brief Vertical speed */
		float SpeedY;
		/** @brief Horizontal auto speed */
		float AutoSpeedX;
		/** @brief Vertical auto speed */
		float AutoSpeedY;
		/** @brief Horizontal scroll offset */
		float OffsetX;
		/** @brief Vertical scroll offset */
		float OffsetY;
		/** @brief Whether layer should repeat horizontally */
		bool RepeatX;
		/** @brief Whether layer should repeat vertically */
		bool RepeatY;
		/** @brief Whether inherent offset should be used, where it applies (see @ref GetDrawOffsets()) */
		bool UseInherentOffset;
		/** @brief Horizontal speed model */
		LayerSpeedModel SpeedModelX;
		/** @brief Vertical speed model */
		LayerSpeedModel SpeedModelY;

		/** @brief Layer renderer type */
		LayerRendererType RendererType;
		/** @brief Layer color parameter */
		nCine::Vector4f Color;
		/** @brief Vanishing point and horizon of a sky, center of a tunnel or of the horizon of a cylinder, pivot of a tile menu, or the vanishing point and the horizon of a reflection, as a fraction of the view, the amplitudes of a wave */
		nCine::Vector2f TextureFadePosition = nCine::Vector2f(0.5f, 0.5f);
		/** @brief Non-zero if a sky fades out into its horizon, a tunnel into its center or a cylinder into its horizon, the palette entry a reflection is tinted with */
		std::uint8_t TextureFade = 192;
		/** @brief Combination of the flags of @ref Formats::TexturedLayerParams, whether a tunnel spirals, whether a cylinder is drawn at half size, which way the waves of a wave move and where a textured layer is drawn */
		std::uint8_t TextureFlags = 0;
		/** @brief Speeds of the texture of a textured layer drawn only where the layer is, against the view, see @ref Formats::TexturedLayerParams::InnerSpeedX */
		nCine::Vector2f TextureInnerSpeed = nCine::Vector2f(0.0f, 0.0f);
		/** @brief Speeds the texture of a textured layer drawn only where the layer is moves at by itself, see @ref Formats::TexturedLayerParams::InnerAutoSpeedX */
		nCine::Vector2f TextureInnerAutoSpeed = nCine::Vector2f(0.0f, 0.0f);
		/** @brief How a textured background is drawn over what is behind it, never stored in a level file */
		TexturedLayerBlend TextureBlend = TexturedLayerBlend::Normal;
		/** @brief Opacity of a translucent, screened or multiplied textured background, or the palette entry of its key (see @ref TextureBlend) */
		std::uint8_t TextureBlendParam = 255;

		/**
		 * @brief Makes the description of a layer from a layer of a level file
		 *
		 * The depth of the layer is relative to @p mainPlaneDepth, the depth of the sprite layer's plane (see
		 * @ref ILevelHandler::MainPlaneZ). The sprite layer is always placed and moves with its defaults, only its
		 * renderer type and color are taken from the record, if it's one the sprite layer can be drawn with (see
		 * @ref IsSpriteLayerRenderer()), otherwise it's drawn as ordinary tiles.
		 */
		static LayerDescription FromRecord(const Formats::LayerRecord& record, std::int32_t mainPlaneDepth);

		/**
		 * @brief Takes the parameters of a tunnel, a wave, a cylinder or a reflection that the color has no room for
		 *
		 * The level file stores them apart from the record of the layer (see @ref Formats::TexturedLayerChunk).
		 */
		void SetTexturedParams(const Formats::TexturedLayerParams& params);

		/**
		 * @brief Returns `true` if the sprite layer can be drawn with a renderer type
		 *
		 * Only those that draw each tile where it is --- @ref LayerRendererType::Default, @ref LayerRendererType::Solid,
		 * @ref LayerRendererType::Tinted, @ref LayerRendererType::PaletteShift and @ref LayerRendererType::SingleColor.
		 * An x-ray layer hides its tiles away from the objects and changes its animated tiles, a blur or a reflection
		 * layer doesn't show its tiles at all and a chroma key one only partly, a sky, a tunnel or a cylinder is drawn
		 * far away from where its tiles are, and a wave moves them, while the objects of the level walk on the tiles of
		 * the sprite layer.
		 */
		static bool IsSpriteLayerRenderer(LayerRendererType type);
		/**
		 * @brief Returns `true` if a renderer type is a textured background, @ref LayerRendererType::Sky, @ref LayerRendererType::Tunnel, @ref LayerRendererType::TileMenu, @ref LayerRendererType::Wave or @ref LayerRendererType::Cylinder
		 *
		 * A @ref LayerRendererType::Reflection isn't one, though it takes the tiles of its layer as a texture too.
		 */
		static bool IsTexturedBackground(LayerRendererType type) {
			return (type == LayerRendererType::Sky || type == LayerRendererType::Tunnel || type == LayerRendererType::TileMenu ||
				type == LayerRendererType::Wave || type == LayerRendererType::Cylinder);
		}

		/**
		 * @brief Returns `true` if the texture of the layer is drawn only where the layer is placed as an ordinary one
		 *
		 * A textured background other than a wave, or a reflection, with @ref Formats::TexturedLayerParams::InnerWindow or
		 * @ref Formats::TexturedLayerParams::InnerLayer --- `SURFACE::INNERWINDOW` and `INNERLAYER` of JJ2+.
		 */
		bool HasInnerSurface() const;
		/**
		 * @brief Returns `true` if the texture of the layer is drawn in the perspective of JJ2+
		 *
		 * Over the whole view with @ref Formats::TexturedLayerParams::Fullscreen, and always only where the layer is (see
		 * @ref HasInnerSurface()). A @ref LayerRendererType::Cylinder is always drawn so, it has no other model.
		 */
		bool HasPlusPerspective() const;
		/**
		 * @brief Returns `true` if the texture of a sky, a tunnel or a cylinder over the whole view is placed where layer 5 is on an axis
		 *
		 * In the perspective of JJ2+ (see @ref HasPlusPerspective()) and with @ref LayerSpeedModel::AlwaysOnTop on the
		 * axis --- `LAYERSPEEDMODEL::LAYER8` of JJ2+, the default of the sky layer of the original game. The texture then
		 * moves there only by its own auto speed, see @ref TileMap::OnUpdate().
		 */
		bool IsTexturePlacedByLayer5(bool vertical) const;

		/**
		 * @brief Converts an auto speed of the original game or of JJ2+ to one of a layer, on an axis of a speed model
		 *
		 * Multiplied by @ref OriginalAutoSpeedScale, so the layer moves as far each second as there. The auto speed of
		 * @ref LayerSpeedModel::SpeedMultipliers isn't a speed but where the layer is at the end of the level, it's kept.
		 */
		static float FromOriginalAutoSpeed(float autoSpeed, LayerSpeedModel model) {
			return (model == LayerSpeedModel::SpeedMultipliers ? autoSpeed : autoSpeed * OriginalAutoSpeedScale);
		}
		/** @brief Converts an auto speed of a layer back to one of the original game or of JJ2+, see @ref FromOriginalAutoSpeed() */
		static float ToOriginalAutoSpeed(float autoSpeed, LayerSpeedModel model) {
			return (model == LayerSpeedModel::SpeedMultipliers ? autoSpeed : autoSpeed / OriginalAutoSpeedScale);
		}

		/**
		 * @brief Returns where the tile (0, 0) of the layer is drawn in the level, as the game places it for a view
		 *
		 * @p layoutSize is the size of the layer and @p spriteLayoutSize the size of the sprite layer, both in tiles.
		 * A position in the level then lies on the tile @cpp (position - origin) / 32 @ce of the layer, repeated if
		 * the layer repeats.
		 */
		nCine::Vector2f GetGameplayOrigin(nCine::Vector2i layoutSize, nCine::Vector2i spriteLayoutSize, const nCine::Rectf& view) const;
		/**
		 * @brief Returns the offsets the layer is drawn with in a view of a height
		 *
		 * The offsets of the layer. The vertical one is less by the inherent offset where it applies, which moves the
		 * layer down by half of how much taller the view is than 200 pixels, as `limitVisibleRegion` of JJ2+ --- only a
		 * layer of @ref LayerSpeedModel::Default that doesn't repeat vertically --- and more by a pixel, which draws every
		 * layer that moves with the level a pixel higher than JJ2+, the sprite layer too. They're what @ref GetViewCoordinates()
		 * takes.
		 */
		nCine::Vector2f GetDrawOffsets(float viewHeight) const;
		/**
		 * @brief Returns the coordinates of the layer at the top left corner of the view it's placed by, minus @ref HardcodedOffset
		 *
		 * The sizes are as in @ref GetGameplayOrigin(), @p loX and @p loY are the offsets the layer is drawn with (see
		 * @ref GetDrawOffsets()). On each axis, with the view starting at @f$ c @f$ and @f$ v @f$ long, the layer
		 * @f$ l @f$ and the level @f$ L @f$ pixels long, the speed @f$ s @f$, the auto speed @f$ a @f$ and the offset
		 * @f$ o @f$, the layer coordinate at the start of the view is, by the speed model:
		 *
		 * - @ref LayerSpeedModel::Default --- @f$ cs + o + A(s - 1) @f$, with @f$ A @f$ half of how much longer the
		 *   view is than 320 pixels horizontally and 200 vertically, so the layer meets the start of the level only
		 *   at the speed of 1 or in a view of 320×200
		 * - @ref LayerSpeedModel::FromStart --- @f$ cs + o @f$
		 * - @ref LayerSpeedModel::FitLevel --- @f$ t(l - v) + o @f$, where @f$ t = c / (L - v) @f$ is how far the
		 *   view is between the two ends of the level, clamped to 0 and 1
		 * - @ref LayerSpeedModel::SpeedMultipliers --- @f$ -v(s + t(a - s)) + o @f$
		 * - @ref LayerSpeedModel::AlwaysOnTop --- 0
		 *
		 * That's where JJ2+ v6.6h places them, measured with `Tests/Level/LayerProbe.j2as` in three resolutions (see the
		 * `TestLayerPlacement()` of the editor self-test). The auto speed of the default model and of @ref LayerSpeedModel::FromStart
		 * moves the layer by its offset, see @ref TileMap::OnUpdate().
		 */
		nCine::Vector2f GetViewCoordinates(nCine::Vector2i layoutSize, nCine::Vector2i spriteLayoutSize, const nCine::Rectf& placementRect,
			float loX, float loY) const;
		/** @brief Returns the coordinate of a layer of the default speed model at a coordinate of the view */
		static float TranslateCoordinate(float coordinate, float speed, float offset, std::int32_t viewSize, bool isY);
	};
}
