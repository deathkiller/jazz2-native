#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PixelMaps.h"
#include "Player.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief Layer of tiles
		 *
		 * A level normally has eight layers, numbered 1 to 8 from front to back: layers 1 and 2 are drawn in front of
		 * everything, layer 3 just in front of the sprites, layer 4 is the sprite layer where the gameplay happens, and
		 * layers 5 to 8 form the background, layer 8 being the farthest one. They're reached through @ref jjLayers, which
		 * keeps this numbering even after @ref jjLayerOrderSet() changes the order they're drawn in. More layers can be
		 * created by the constructors or taken from another level by @ref jjLayersFromLevel().
		 *
		 * Properties a script changes are applied to the level once per frame.
		 */
		class jjLAYER
		{
		public:
			/** @brief Creates a new instance */
			jjLAYER();
			~jjLAYER();

			/**
			 * @brief Creates a new empty layer of the given size in tiles
			 *
			 * @partialsupport The new layer has no tiles yet, doesn't move with the camera (both speeds are 0) and
			 * isn't drawn until it's included in @ref jjLayerOrderSet().
			 */
			static jjLAYER* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/**
			 * @brief Creates a new layer as a copy of another one
			 *
			 * The copy takes the tiles, the texture and all other properties of the layer except its sprite mode, which
			 * starts as @ref SPRITE::NORMAL.
			 * @partialsupport It isn't drawn until it's included in @ref jjLayerOrderSet().
			 */
			static jjLAYER* CreateCopy(jjLAYER* other);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjLAYER& operator=(const jjLAYER& o);

			/**
			 * @brief Returns the layers of the level by their original number, from 1 to 8
			 *
			 * `jjLayers[4]` is the sprite layer and `jjLayers[8]` the farthest background layer. The numbers always refer
			 * to the layers the level was made with, regardless of @ref jjLayerOrderSet(). An index out of range gives an
			 * empty layer that isn't part of the level.
			 */
			static jjLAYER* get_jjLayers(int32_t index);

			/** @brief Width of the layer in tiles */
			std::int32_t width = 0;
			/**
			 * @brief Width of the layer in tiles, including any widening needed for tiling
			 *
			 * Always equal to `width`, layers are never stored widened.
			 */
			std::int32_t widthReal = 0;
			/** @brief Width of the layer in groups of four tiles, `(widthReal + 3) / 4` */
			std::int32_t widthRounded = 0;
			/** @brief Height of the layer in tiles */
			std::int32_t height = 0;
			/**
			 * @brief Horizontal parallax speed of the layer
			 *
			 * How fast the layer moves relative to the camera: 1 moves it together with the sprite layer, 0 keeps it fixed
			 * on screen. How it combines with @ref xAutoSpeed depends on @ref xSpeedModel.
			 */
			float xSpeed = 0;
			/** @brief Vertical parallax speed of the layer, see `xSpeed` */
			float ySpeed = 0;
			/**
			 * @brief Horizontal automatic scrolling speed of the layer
			 *
			 * Scrolls the layer constantly, regardless of the camera. How it combines with `xSpeed` depends on
			 * @ref xSpeedModel.
			 */
			float xAutoSpeed = 0;
			/** @brief Vertical automatic scrolling speed of the layer, see @ref xAutoSpeed */
			float yAutoSpeed = 0;
			/**
			 * @brief Horizontal offset of the layer in pixels
			 *
			 * Added to the position of the layer regardless of its speeds. On a layer with an automatic speed, the value
			 * also advances as the layer scrolls. Stored in 16.16 fixed point, so a value out of the range of roughly
			 * -32768 to 32767 becomes 0.
			 */
			float xOffset = 0;
			/** @brief Vertical offset of the layer in pixels, see `xOffset` */
			float yOffset = 0;
			/**
			 * @brief Horizontal speed of the texture inside the layer
			 *
			 * Used only when @ref textureSurface is @ref SURFACE::INNERLAYER or @ref SURFACE::INNERWINDOW: the regular speeds then
			 * place the layer in the level, while the inner speeds move the texture within it. Defaults to 0.
			 */
			float xInnerSpeed = 0;
			/** @brief Vertical speed of the texture inside the layer, see @ref xInnerSpeed */
			float yInnerSpeed = 0;
			/** @brief Horizontal automatic scrolling speed of the texture inside the layer, see @ref xInnerSpeed */
			float xInnerAutoSpeed = 0;
			/** @brief Vertical automatic scrolling speed of the texture inside the layer, see @ref xInnerSpeed */
			float yInnerAutoSpeed = 0;

			/**
			 * @brief Returns the sprite mode the layer is drawn with
			 *
			 * Defaults to @ref SPRITE::NORMAL. A layer drawn as tiles supports `NORMAL`, @ref TRANSLUCENT, @ref TRANSLUCENTTILE,
			 * @ref INVISIBLE, @ref TINTED, `PALSHIFT`, @ref TRANSLUCENTPALSHIFT, @ref SINGLECOLOR, @ref TRANSLUCENTCOLOR, @ref BRIGHTNESS,
			 * @ref CHROMAKEY, @ref ALPHAMAP, @ref MAPPING, @ref TRANSLUCENTMAPPING and all `BLEND_*` modes, any other mode is ignored.
			 * A textured layer supports `NORMAL`, @ref TRANSLUCENT, @ref TRANSLUCENTTILE, @ref INVISIBLE, @ref CHROMAKEY, @ref MAPPING,
			 * @ref TRANSLUCENTMAPPING and the `BLEND_*` modes, with any other mode it's drawn normally. Layers drawn by
			 * special effects, such as a reflection, ignore the mode.
			 */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets the sprite mode the layer is drawn with */
			std::uint32_t set_spriteMode(std::uint32_t value) const;
			/**
			 * @brief Returns the parameter of the layer's sprite mode
			 *
			 * Its meaning depends on `spriteMode`, for example the opacity for @ref SPRITE::BLEND_NORMAL, the palette entry
			 * for @ref SPRITE::SINGLECOLOR and @ref SPRITE::CHROMAKEY, or the brightness for @ref SPRITE::BRIGHTNESS (128 leaves it
			 * unchanged). It's kept even while the current mode doesn't use it. Defaults to 0.
			 */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter of the layer's sprite mode */
			std::uint8_t set_spriteParam(std::uint8_t value) const;

			/**
			 * @brief Changes the horizontal speed or automatic speed of the layer
			 *
			 * Sets @ref xAutoSpeed if `newSpeedIsAnAutoSpeed` is `true`, otherwise `xSpeed`.
			 *
			 * @note Currently the same as assigning the property, the layer isn't shifted to stay where it was before
			 *		the change.
			 */
			void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/**
			 * @brief Changes the vertical speed or automatic speed of the layer
			 *
			 * Sets @ref yAutoSpeed if `newSpeedIsAnAutoSpeed` is `true`, otherwise `ySpeed`.
			 *
			 * @note Currently the same as assigning the property, the layer isn't shifted to stay where it was before
			 *		the change.
			 */
			void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/**
			 * @brief Returns the horizontal position of the layer as last drawn for a player, in pixels
			 *
			 * Accounts for everything that moves the layer, such as its speeds, offsets and the player's camera, so it can
			 * be used to keep something aligned to a layer. The player should be a local one. For a layer that isn't part
			 * of the level, the position is estimated from the camera, `xSpeed` and `xOffset`.
			 */
			float getXPosition(const jjPLAYER* play) const;
			/**
			 * @brief Returns the vertical position of the layer as last drawn for a player, in pixels
			 *
			 * See @ref getXPosition().
			 */
			float getYPosition(const jjPLAYER* play) const;

			/** @brief Where a textured layer is drawn, values of @ref SURFACE::Surface */
			enum TextureSurfaceStyles : std::int32_t {
				Untextured,		/**< Drawn as tiles */
				Legacy,			/**< Over the whole view, as layer 8 of the original game */
				FullScreen,		/**< Over the whole view */
				InnerWindow,	/**< Only where the layer is, as a window into one over the whole view */
				InnerLayer		/**< Only where the layer is, the horizon and the vanishing point a fraction of the layer */
			};

			/**
			 * @brief Parameters of one texture style of a layer, the object behind `jjLAYERWARPHORIZON` and the others
			 *
			 * What they mean depends on the style - the fade position of a warp horizon, a tunnel and a cylinder, the pivot of
			 * a menu, the amplitudes of a wave, the vanishing point and the top of a reflection - but they're the same values
			 * for all the styles, a value set for one is the value of the others too (see `SetStyleValue()`), only the tint
			 * color is a reflection's own. The fade color and the stars, which each style takes in its own way too, are of
			 * the layer as well (see `ApplyTexture()`).
			 */
			struct TextureStyleParams {
				/** @brief Layer the parameters are of */
				jjLAYER* Owner = nullptr;
				/** @brief Horizontal value, as a fraction of the view (of the layer for `InnerLayer`), or the horizontal amplitude */
				float X = 0.5f;
				/** @brief Vertical value, as a fraction of the view (of the layer for `InnerLayer`), or the vertical amplitude */
				float Y = 0.5f;
				/** @brief Whether it fades into the fade color */
				bool Fade = true;
				/** @brief Palette entry a reflection is tinted with */
				std::uint8_t TintColor = 192;
			};

			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::WARPHORIZON
			 *
			 * The texture is drawn as a floor and a ceiling stretching to a horizon in the distance. The horizon can fade
			 * into the fade color and show stars.
			 *
			 * @note The fade position and `fade` are shared by all texture styles of the layer, and so are the fade color
			 *		and @ref stars, which the other styles use as their own parameters.
			 */
			TextureStyleParams warpHorizon;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::TUNNEL
			 *
			 * The texture is drawn as rings shrinking towards a center in the distance, which can fade into the fade
			 * color. @ref spiral makes the rings twist inwards.
			 *
			 * @note Shares its values with the other texture styles of the layer, see @ref warpHorizon.
			 */
			TextureStyleParams tunnel;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::MENU
			 *
			 * Meant for the rotating background of the main menu, built from three 16-color palette rows.
			 *
			 * @partialsupport This style isn't drawn yet, a layer with it is drawn as @ref TEXTURE::WARPHORIZON
			 * instead.
			 */
			TextureStyleParams menu;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::TILEMENU
			 *
			 * The rotating background of the main menu: three parts repeated over the view, each turning and zooming at its
			 * own pace around a pivot. The front part is the layer's texture, the middle one the 2×2 tiles at the top left
			 * corner of layer 4, and the back one the tile at 0,2 of layer 4.
			 *
			 * @note Shares its values with the other texture styles of the layer, see @ref warpHorizon.
			 */
			TextureStyleParams tileMenu;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::WAVE
			 *
			 * The texture is drawn flat and repeated, with its rows and columns shifted by sine waves.
			 *
			 * @note Shares its values with the other texture styles of the layer, see @ref warpHorizon. A wave is always drawn
			 *		over the whole view, the inner surfaces aren't supported for it.
			 */
			TextureStyleParams wave;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::CYLINDER
			 *
			 * The texture is drawn as the inside of a horizontal tube receding a short way into the distance, which can
			 * fade into the fade color near its horizon.
			 *
			 * @note Shares its values with the other texture styles of the layer, see @ref warpHorizon.
			 */
			TextureStyleParams cylinder;
			/**
			 * @brief Properties used when @ref textureStyle is @ref TEXTURE::REFLECTION
			 *
			 * The part of the view below the horizon is drawn as a plane stretching into the distance that mirrors
			 * everything drawn above the horizon before this layer, rippled by the texture. Nothing is drawn above the
			 * horizon. The layer ignores its sprite mode.
			 *
			 * @note Shares its values with the other texture styles of the layer, see @ref warpHorizon, except @ref tintColor,
			 *		which is its own.
			 */
			TextureStyleParams reflection;

			/**
			 * @brief Returns the effect a textured layer is drawn with
			 *
			 * Used only when @ref textureSurface isn't @ref SURFACE::UNTEXTURED. Each style has its own properties, for example
			 * @ref warpHorizon for @ref TEXTURE::WARPHORIZON. @ref TEXTURE::MENU is drawn as a warp horizon.
			 */
			std::int32_t GetTextureMode() const;
			/** @brief Sets the effect a textured layer is drawn with */
			void SetTextureMode(std::int32_t value);
			/**
			 * @brief Returns the 256×256 pixel image a textured layer is drawn with
			 *
			 * @ref TEXTURE::FROMTILES (the default) takes the 8×8 tiles at the top left corner of the layer. Reads as
			 * @ref TEXTURE::CUSTOM after @ref jjPIXELMAP::makeTexture() gave the layer an image. The other textures are read
			 * from a data file in the game's `Source` directory, if it's missing, the image of the layer doesn't change.
			 */
			std::int32_t GetTexture() const;
			/** @brief Sets the 256×256 pixel image a textured layer is drawn with */
			void SetTexture(std::int32_t value);
			/**
			 * @brief Returns where and whether the layer is drawn textured
			 *
			 * @ref SURFACE::UNTEXTURED draws the layer as tiles. Otherwise it's drawn with the effect of @ref textureStyle, see
			 * @ref SURFACE::Surface for the options.
			 */
			std::int32_t GetTextureSurface() const;
			/** @brief Sets where and whether the layer is drawn textured */
			void SetTextureSurface(std::int32_t value);
			/**
			 * @brief Makes the layer of the level drawn as its texture properties say
			 *
			 * A style the game can't draw (`TEXTURE::MENU`) is drawn as a warp horizon, a wave is drawn over the whole view
			 * whatever its surface.
			 */
			void ApplyTexture();
			/**
			 * @brief Sets how a textured layer is drawn over what is behind it by the sprite mode a script set
			 *
			 * Returns `false` if the mode isn't one a textured layer can be drawn with, it's drawn as it is then.
			 */
			bool SetBlend(Tiles::LayerDescription& description) const;

			/** @brief Returns a value of the parameters of a texture style */
			template<float TextureStyleParams::*Member>
			static float GetStyleValue(const TextureStyleParams* params) {
				return params->*Member;
			}
			/** @brief Sets a value of the parameters of a texture style, shared by all styles of the layer */
			template<float TextureStyleParams::*Member>
			static float SetStyleValue(TextureStyleParams* params, float value) {
				if (params->*Member != value) {
					jjLAYER* owner = params->Owner;
					for (TextureStyleParams* style : { &owner->warpHorizon, &owner->tunnel, &owner->menu, &owner->tileMenu, &owner->wave, &owner->cylinder, &owner->reflection }) {
						style->*Member = value;
					}
					owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns whether a texture style fades */
			static bool GetStyleFade(const TextureStyleParams* params) {
				return params->Fade;
			}
			/** @brief Sets whether a texture style fades, shared by all styles of the layer */
			static bool SetStyleFade(TextureStyleParams* params, bool value) {
				if (params->Fade != value) {
					jjLAYER* owner = params->Owner;
					for (TextureStyleParams* style : { &owner->warpHorizon, &owner->tunnel, &owner->menu, &owner->tileMenu, &owner->wave, &owner->cylinder, &owner->reflection }) {
						style->Fade = value;
					}
					owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns the palette entry a reflection is tinted with */
			static std::uint8_t GetStyleTintColor(const TextureStyleParams* params) {
				return params->TintColor;
			}
			/** @brief Sets the palette entry a reflection is tinted with */
			static std::uint8_t SetStyleTintColor(TextureStyleParams* params, std::uint8_t value) {
				if (params->TintColor != value) {
					params->TintColor = value;
					params->Owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns a component of the fade color, or what the style takes it as */
			template<std::int32_t Index>
			static std::uint8_t GetTextureComponent(const TextureStyleParams* params) {
				return params->Owner->_textureColor[Index];
			}
			/** @brief Sets a component of the fade color, or what the style takes it as */
			template<std::int32_t Index>
			static std::uint8_t SetTextureComponent(TextureStyleParams* params, std::uint8_t value) {
				params->Owner->SetTextureColor(Index, value);
				return value;
			}
			/** @brief Returns the speed of a wave, the blue of the fade color as a signed byte */
			static std::int8_t GetWaveSpeed(const TextureStyleParams* params) {
				return std::int8_t(params->Owner->_textureColor[2]);
			}
			/** @brief Sets the speed of a wave, the blue of the fade color as a signed byte */
			static std::int8_t SetWaveSpeed(TextureStyleParams* params, std::int8_t value) {
				params->Owner->SetTextureColor(2, std::uint8_t(value));
				return value;
			}
			/** @brief Returns the stars of a warp horizon, or what the style takes them as */
			static bool GetTextureStars(const TextureStyleParams* params) {
				return params->Owner->_textureStars;
			}
			/** @brief Sets the stars of a warp horizon, or what the style takes them as */
			static bool SetTextureStars(TextureStyleParams* params, bool value) {
				if (params->Owner->_textureStars != value) {
					params->Owner->_textureStars = value;
					params->Owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns the fade color of the layer's texture styles */
			static jjPALCOLOR GetFadeColor(const TextureStyleParams* params);
			/** @brief Sets the fade color of the layer's texture styles */
			static void SetFadeColor(TextureStyleParams* params, jjPALCOLOR color);

			/**
			 * @brief Angle offset of the layer's swaying while a player is stoned
			 *
			 * Uses the range of 0 to 1023 of @ref jjSin() and @ref jjCos().
			 *
			 * @partialsupport Currently has no effect, and it starts as 0 on every layer.
			 */
			std::int32_t rotationAngle = 0;
			/**
			 * @brief How far the layer sways while a player is stoned
			 *
			 * 0 keeps the layer still.
			 *
			 * @partialsupport Currently has no effect, and it starts as 0 on every layer.
			 */
			std::int32_t rotationRadiusMultiplier = 0;
			/** @brief Whether the layer repeats vertically */
			bool tileHeight = false;
			/** @brief Whether the layer repeats horizontally */
			bool tileWidth = false;
			/**
			 * @brief Whether the layer is shifted down by an amount that depends on the resolution
			 *
			 * Applies to a layer that doesn't repeat vertically and uses @ref LAYERSPEEDMODEL::NORMAL or
			 * @ref LAYERSPEEDMODEL::BOTHSPEEDS vertically.
			 */
			bool limitVisibleRegion = false;
			/**
			 * @brief Whether the layer has a tile map
			 *
			 * `false` for a layer of the level that contains no tiles at all, nothing can ever be drawn of it. Always
			 * `true` for a layer created from a size or by @ref jjLayersFromLevel().
			 */
			bool hasTileMap = false;
			/**
			 * @brief Whether the layer is drawn
			 *
			 * Setting it to `false` hides the layer. A layer left out by @ref jjLayerOrderSet() isn't drawn either way.
			 */
			bool hasTiles = false;

			/**
			 * @brief Returns the layers in the order they're drawn, from the front to the back
			 *
			 * Includes every layer currently drawn, the ones a script added by @ref jjLayerOrderSet() too.
			 */
			static CScriptArray* jjLayerOrderGet();
			/**
			 * @brief Changes which layers are drawn and in what order
			 *
			 * The first layer of the array is drawn in front, the last one at the back. Layers left out aren't drawn.
			 * The array must contain `jjLayers[4]` and no null handles, otherwise nothing changes and `false` is returned.
			 * A layer that appears more than once is placed by its first appearance.
			 *
			 * @ref jjLayers and the global functions that take a layer number keep referring to the original eight layers
			 * after the order changes.
			 */
			static bool jjLayerOrderSet(const CScriptArray& order);
			/**
			 * @brief Returns a layer offset as it's stored, in 16.16 fixed point
			 *
			 * An offset out of the range is 0, and 32768 itself wraps to -32768.
			 */
			static float ToStoredOffset(float value);
			/**
			 * @brief Creates new layers from the layers of another level
			 *
			 * `layerIDs` are the numbers of the layers to take, from 1 to 8. The level is read from the `Source`
			 * directory, or the layers are copied as they are now if it's the current level. `tileIDAdjustmentFactor` is
			 * added to every non-empty, non-animated tile, which helps when the tiles were added by @ref jjTilesFromTileset();
			 * animated tiles refer to the animated tiles of this level. The new layers aren't drawn until they're included
			 * in @ref jjLayerOrderSet().
			 *
			 * Returns an empty array if the level can't be read or any of the numbers is invalid.
			 */
			static CScriptArray* jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor);
			/**
			 * @brief Appends tiles of another tile set to the end of the level's tile set
			 *
			 * Takes `tileCount` tiles starting at `firstTileID` and increases @ref jjTileCount by that many. If
			 * `paletteColorMapping` has at least 256 entries, every pixel of an 8-bit tile is replaced by the entry at its
			 * palette index, to fit the colors into the current palette. Returns `false` on failure.
			 */
			static bool jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping);
			/**
			 * @brief Appends tiles of another tile set as 32-bit tiles
			 *
			 * The same as @ref jjTilesFromTileset(), but 8-bit tiles are converted to true color, either in the colors of
			 * `palette` or, if it's null, in the tile set's own palette, so they keep their colors whatever the level's
			 * palette is.
			 */
			static bool jjTilesFromTileset32(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const jjPAL* palette);

			/**
			 * @brief Returns the tile at the given position in tiles
			 *
			 * An animated tile is returned as its animated tile ID, not its current frame. Returns 0 for a position outside
			 * the layer or a layer that isn't part of the level.
			 */
			std::uint16_t tileGet(int xTile, int yTile);
			/**
			 * @brief Changes the tile at the given position in tiles
			 *
			 * Only that tile changes, other places using the same tiles aren't affected. Returns `newTile`, or 0 if the
			 * layer isn't part of the level.
			 */
			std::uint16_t tileSet(int xTile, int yTile, std::uint16_t newTile);
			/**
			 * @brief Prepares a rectangular area of the layer for @ref tileSet()
			 *
			 * @partialsupport Currently has no effect, every tile can already be changed on its own.
			 */
			void generateSettableTileArea(int xTile, int yTile, int width, int height);
			/**
			 * @brief Prepares the whole layer for @ref tileSet()
			 *
			 * @partialsupport Currently has no effect, every tile can already be changed on its own.
			 */
			void generateSettableTileAreaAll();

			/**
			 * @brief Horizontal speed model of the layer
			 *
			 * Decides how the speed properties position the layer horizontally, see @ref LAYERSPEEDMODEL::LayerSpeedModel.
			 */
			std::int32_t SpeedModeX = 0;
			/**
			 * @brief Vertical speed model of the layer
			 *
			 * Decides how the speed properties position the layer vertically, see @ref LAYERSPEEDMODEL::LayerSpeedModel.
			 */
			std::int32_t SpeedModeY = 0;

		private:
			std::int32_t _refCount;
			// The engine tile-map layer this proxy is bound to (-1 = a standalone layer not part of the level). The loader
			// caches one proxy per level layer and pushes the writable fields into the engine each frame (see SyncLayerProperties).
			std::int32_t _layerIndex = -1;
			// Last-synced offsets, used to tell a script write apart from the engine's auto-scroll (which advances OffsetX/Y
			// itself): if the proxy value still equals this, the engine's value is authoritative; otherwise the script changed it.
			float _syncedOffsetX = 0.0f;
			float _syncedOffsetY = 0.0f;
			// The sprite mode of a standalone layer, and the parameter of any layer - it's kept even while the mode doesn't use it,
			// as in JJ2+. The mode of a level layer is its renderer type, see get_spriteMode(), unless a script set one, some
			// modes are only like the renderer type they're drawn with.
			std::uint32_t _spriteMode = 0;
			std::uint8_t _spriteParam = 0;
			bool _hasSpriteMode = false;
			// The texture of the layer as scripts see it, the layer of the level is drawn as it says (see ApplyTexture()) - a
			// style the game can't draw is still kept as the script set it. The fade color and the stars are shared by the
			// styles, as in JJ2+, each takes them in its own way.
			std::int32_t _textureStyle = 0;
			std::int32_t _texture = 0;
			std::int32_t _textureSurface = Untextured;
			std::uint8_t _textureColor[3] = {};
			bool _textureStars = false;
			// The image jjPIXELMAP::makeTexture() gave the layer (TEXTURE::CUSTOM) as the script made it, see SetCustomTexture()
			std::unique_ptr<std::uint8_t[]> _customTexture;

			// Sets a component of the fade color and makes the layer drawn with it
			void SetTextureColor(std::int32_t index, std::uint8_t value);
			// Gives the layer of the level the image the script made, or the one it has again after the style changed - a sky, a
			// tunnel or a cylinder of JJ2+ is drawn from the upper half of the palette, index | 128 (measured), a wave and the
			// other styles as it is. Returns `false` if the layer isn't one of the level.
			bool SetCustomTexture(const std::uint8_t* pixels);
			// Takes the texture properties of the layer of the level as it's drawn now, when the proxy is made
			void InitializeTexture(const Tiles::TileMapLayer& layer);
			// Points the parameters of the texture styles to this layer
			void BindTextureParams();

			friend class Scripting::LevelScriptLoader;
			friend class jjPIXELMAP;	// A pixel map can be made of the tiles of a layer
			friend class jjCOLORMAP;
		};

		/**
		 * @brief A tile of the level, static or animated
		 *
		 * There's one instance for every tile ID, `jjTiles[id]`, which a script reads the frames and the speed of an
		 * animated tile through, and changes them through `jjAnimatedTiles[id]`. A static tile is its only frame.
		 */
		class jjTILE
		{
		public:
			/** @brief Creates a new instance for a tile ID, with `TILE::ANIMATED` for an animated tile and without flips */
			explicit jjTILE(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/**
			 * @brief Returns the tiles of the level by tile ID, read-only
			 *
			 * Covers static tiles as well as animated ones, which are indexed with @ref TILE::ANIMATED set. Flip bits of the
			 * ID are ignored. Returns null if the level has no such tile. To change an animated tile, use
			 * @ref jjAnimatedTiles.
			 */
			static jjTILE* get_jjTiles(std::uint16_t tileID);
			/**
			 * @brief Returns the animated tiles of the level by their index
			 *
			 * `jjAnimatedTiles[i]` is the same object as `jjTiles[i | TILE::ANIMATED]`, but it can be changed. Returns
			 * null if the level has no such animated tile.
			 */
			static jjTILE* get_jjAnimatedTiles(std::uint16_t tileID);

			/**
			 * @brief Returns the tile IDs of the frames in the order the animation plays them
			 *
			 * Frames shown longer because of the wait or the ping-pong setting appear repeatedly, so the array can be
			 * longer than the list of frames of the animated tile. A frame can itself be an animated tile. For a static
			 * tile, the array contains only its own ID.
			 */
			CScriptArray* getFrames() const;
			/**
			 * @brief Replaces the frames of an animated tile
			 *
			 * @param frames		Tile IDs of the new frames, only the first 255 are used
			 * @param pingPong		Whether the animation plays back and forth
			 * @param wait			Frames to wait between two cycles of the animation
			 * @param randomWait	Random amount added to `wait`
			 * @param pingPongWait	Frames to wait before the animation plays backwards
			 * @return `false` if this isn't an animated tile or `frames` is empty
			 *
			 * The speed stays as it was. The number of animated tiles of a level can't be increased.
			 */
			bool setFrames(const CScriptArray& frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait);
			/**
			 * @brief Returns the animation speed in frames per second
			 *
			 * Always 0 for a static tile, which can't be changed.
			 */
			std::uint8_t get_fps() const;
			/** @brief Sets the animation speed in frames per second */
			std::uint8_t set_fps(std::uint8_t value);
			/**
			 * @brief Returns the tile ID of the tile
			 *
			 * Includes @ref TILE::ANIMATED for an animated tile, so `jjTiles[tile.tileID]` is always the same tile.
			 */
			std::uint16_t get_tileID() const;

		private:
			std::int32_t _refCount;
			std::uint16_t _tileID;
		};
	}
}

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/**
	 * @brief Finishes setting up a level made in an extended level editor
	 *
	 * The layers, the palette, the edited tiles and the like are already part of the level when it's converted, so
	 * this only recolors the sprites the level asks for. Always returns `true`.
	 */
	bool mlleSetup();
	/**
	 * @brief Spawns the objects placed off the tile grid
	 *
	 * @partialsupport Currently has no effect, these objects are already placed when the level is converted.
	 */
	void mlleSpawnOffgrids();
	/**
	 * @brief Spawns the objects placed off the tile grid, for local players only
	 *
	 * @partialsupport Currently has no effect, these objects are already placed when the level is converted.
	 */
	void mlleSpawnOffgridsLocal();
	/** @brief Returns the layer of the given name, or null if the level has no such layer */
	Legacy::jjLAYER* mlleGetLayer(const String& name);

	// TODO

	/**
	 * @brief Sets the color of darkness used by ambient lighting
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjSetDarknessColor(jjPALCOLOR color);
	/**
	 * @brief Sets the fade color of the textured background
	 *
	 * The same as calling `setFadeColor()` on @ref warpHorizon of the textured background layer, which is `jjLayers[8]`,
	 * or if that layer isn't textured but another one is, the farthest textured layer.
	 */
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
	/**
	 * @brief Sets the fade color of the textured background to the color of a palette entry
	 *
	 * The default, 207, is the last color of the gradient most textures use.
	 */
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID);
	/** @brief Sets the fade color of the textured background */
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color);
	/** @brief Returns the fade color of the textured background, see @ref jjSetFadeColors() */
	jjPALCOLOR jjGetFadeColors();
	/**
	 * @brief Redraws the textured background from the tiles of its layer
	 *
	 * Changes to textured layers are applied automatically, so this is never needed.
	 */
	void jjUpdateTexturedBG();
	/**
	 * @brief Returns the texture of the textured background
	 *
	 * A shortcut for @ref jjLAYER::texture of the textured background layer, see @ref jjSetFadeColors() for which layer that is.
	 */
	std::int32_t get_jjTexturedBGTexture();
	/** @brief Sets the texture of the textured background */
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture);
	/**
	 * @brief Returns the effect the textured background is drawn with
	 *
	 * A shortcut for @ref jjLAYER::textureStyle of the textured background layer, see @ref jjSetFadeColors() for which layer
	 * that is.
	 */
	std::int32_t get_jjTexturedBGStyle();
	/** @brief Sets the effect the textured background is drawn with */
	std::int32_t set_jjTexturedBGStyle(std::int32_t style);
	/**
	 * @brief Whether `jjLayers[8]` is drawn textured
	 *
	 * `true` if its @ref jjLAYER::textureSurface is anything but @ref SURFACE::UNTEXTURED. Setting `true` on an untextured
	 * layer changes it to @ref SURFACE::LEGACY, setting `false` to @ref SURFACE::UNTEXTURED.
	 */
	bool get_jjTexturedBGUsed();
	/** @brief Sets whether `jjLayers[8]` is drawn textured */
	bool set_jjTexturedBGUsed(bool used);
	/**
	 * @brief Whether the textured background shows stars
	 *
	 * A shortcut for `warpHorizon.stars` of the textured background layer, see @ref jjSetFadeColors() for which layer
	 * that is. The other styles use this value as their own parameter, see @ref jjLAYER::warpHorizon.
	 */
	bool get_jjTexturedBGStars();
	/** @brief Sets whether the textured background shows stars */
	bool set_jjTexturedBGStars(bool used);
	/**
	 * @brief Returns the horizontal fade position of the textured background
	 *
	 * A fraction of the view, 0.5 being the middle. A shortcut for the fade position of the textured background layer,
	 * see @ref jjSetFadeColors() for which layer that is.
	 */
	float get_jjTexturedBGFadePositionX();
	/** @brief Sets the horizontal fade position of the textured background */
	float set_jjTexturedBGFadePositionX(float value);
	/**
	 * @brief Returns the vertical fade position of the textured background
	 *
	 * See @ref jjTexturedBGFadePositionX.
	 */
	float get_jjTexturedBGFadePositionY();
	/** @brief Sets the vertical fade position of the textured background */
	float set_jjTexturedBGFadePositionY(float value);

	/**
	 * @brief Whether layer 8 moves by its own speeds
	 *
	 * When `false`, layer 8 stays fixed to the screen as with @ref LAYERSPEEDMODEL::LAYER8. Only has an effect if layer 8
	 * uses @ref LAYERSPEEDMODEL::LAYER8 on both axes when the level starts.
	 */
	bool getUseLayer8Speeds();
	/** @brief Sets whether layer 8 moves by its own speeds */
	bool setUseLayer8Speeds(bool value);

	// TODO

	/**
	 * @brief Returns the frame an animated tile currently shows
	 *
	 * @partialsupport Currently always returns 0.
	 */
	std::uint16_t jjGetStaticTile(std::uint16_t tileID);
	/**
	 * @brief Returns the tile at a position in tiles in one of the original layers
	 *
	 * A shortcut for `jjLayers[layer].tileGet()`.
	 */
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile);
	/**
	 * @brief Changes the tile at a position in tiles in one of the original layers
	 *
	 * A shortcut for `jjLayers[layer].tileSet()`. Returns `newTile`, or 0 if the layer or the position is invalid.
	 */
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile);
	/**
	 * @brief Prepares a rectangular area of one of the original layers for @ref jjTileSet()
	 *
	 * @partialsupport Currently has no effect, every tile can already be changed on its own.
	 */
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height);

	// TODO

	/**
	 * @brief Returns whether a pixel of the sprite layer is solid
	 *
	 * Coordinates are in pixels. When a solid pixel is found, @ref jjEventAtLastMaskedPixel is set to the event of its tile.
	 */
	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel);
	/**
	 * @brief Returns whether a pixel is solid, see @ref jjMaskedPixel()
	 *
	 * @partialsupport Only the sprite layer has collision masks, so `layer` is ignored.
	 */
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer);
	/**
	 * @brief Returns whether any pixel of a horizontal line in the sprite layer is solid
	 *
	 * The line starts at `xPixel`,`yPixel` and is `lineLength` pixels long to the right. Returns `false` if
	 * `lineLength` isn't positive. Sets @ref jjEventAtLastMaskedPixel like @ref jjMaskedPixel().
	 */
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel);
	/**
	 * @brief Returns whether any pixel of a horizontal line is solid, see @ref jjMaskedHLine()
	 *
	 * @partialsupport Only the sprite layer has collision masks, so `layer` is ignored.
	 */
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer);
	/**
	 * @brief Returns whether any pixel of a vertical line in the sprite layer is solid
	 *
	 * The line starts at `xPixel`,`yPixel` and is `lineLength` pixels long downwards. Returns `false` if
	 * `lineLength` isn't positive. Sets @ref jjEventAtLastMaskedPixel like @ref jjMaskedPixel().
	 */
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/**
	 * @brief Returns whether any pixel of a vertical line is solid, see @ref jjMaskedVLine()
	 *
	 * @partialsupport Only the sprite layer has collision masks, so `layer` is ignored.
	 */
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
	/**
	 * @brief Returns where the topmost solid pixel of a vertical line in the sprite layer is
	 *
	 * The line starts at `xPixel`,`yPixel` and is `lineLength` pixels long downwards. The result counts from 1, so
	 * it's 1 if `yPixel` itself is solid, and `lineLength + 1` if no pixel of the line is. Useful for finding slopes
	 * under an object. Sets @ref jjEventAtLastMaskedPixel like @ref jjMaskedPixel().
	 */
	std::int32_t jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/**
	 * @brief Returns where the topmost solid pixel of a vertical line is, see @ref jjMaskedTopVLine()
	 *
	 * @partialsupport Only the sprite layer has collision masks, so `layer` is ignored.
	 */
	std::int32_t jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
}

#endif
