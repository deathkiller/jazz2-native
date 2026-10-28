#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PixelMaps.h"
#include "Player.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief A tilemap layer of the level
		 *
		 * Reference-counted proxy for one of the level's parallax tile layers, mirroring the original JJ2+ `jjLAYER`.
		 * Exposes the layer's size, scrolling speeds and offsets, parallax/auto-speed and texture-background modes, and
		 * per-tile get/set access. The level loader keeps one proxy per engine layer and pushes writable fields back
		 * each frame (see @ref LevelScriptLoader::GetLayerProxy); reached from script via `jjLayers`.
		 */
		class jjLAYER
		{
		public:
			/** @brief Creates a new instance */
			jjLAYER();
			~jjLAYER();

			/** @brief Returns a new layer of the given size */
			static jjLAYER* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new layer copied from another */
			static jjLAYER* CreateCopy(jjLAYER* other);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjLAYER& operator=(const jjLAYER& o);

			/** @brief Returns the level layer at the specified index */
			static jjLAYER* get_jjLayers(int32_t index);

			/** @brief Layer width in tiles */
			std::int32_t width = 0;
			/** @brief Actual (untruncated) layer width in tiles */
			std::int32_t widthReal = 0;
			/** @brief Layer width rounded up for tiling */
			std::int32_t widthRounded = 0;
			/** @brief Layer height in tiles */
			std::int32_t height = 0;
			/** @brief Horizontal parallax scrolling speed */
			float xSpeed = 0;
			/** @brief Vertical parallax scrolling speed */
			float ySpeed = 0;
			/** @brief Horizontal automatic scrolling speed */
			float xAutoSpeed = 0;
			/** @brief Vertical automatic scrolling speed */
			float yAutoSpeed = 0;
			/** @brief Horizontal scrolling offset */
			float xOffset = 0;
			/** @brief Vertical scrolling offset */
			float yOffset = 0;
			/** @brief Horizontal inner parallax speed */
			float xInnerSpeed = 0;
			/** @brief Vertical inner parallax speed */
			float yInnerSpeed = 0;
			/** @brief Horizontal inner automatic speed */
			float xInnerAutoSpeed = 0;
			/** @brief Vertical inner automatic speed */
			float yInnerAutoSpeed = 0;

			/** @brief Returns the sprite blend mode used by the layer */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets the sprite blend mode used by the layer */
			std::uint32_t set_spriteMode(std::uint32_t value) const;
			/** @brief Returns the parameter for the layer's sprite blend mode */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter for the layer's sprite blend mode */
			std::uint8_t set_spriteParam(std::uint8_t value) const;

			/** @brief Sets the horizontal scrolling speed, optionally as an auto-speed */
			void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/** @brief Sets the vertical scrolling speed, optionally as an auto-speed */
			void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/** @brief Returns the layer's horizontal position for the given player's camera */
			float getXPosition(const jjPLAYER* play) const;
			/** @brief Returns the layer's vertical position for the given player's camera */
			float getYPosition(const jjPLAYER* play) const;

			/** @brief Where a textured layer is drawn, `SURFACE::Surface` of JJ2+ */
			enum TextureSurfaceStyles : std::int32_t {
				Untextured,		/**< Drawn as tiles */
				Legacy,			/**< Over the whole view, as layer 8 of the original game */
				FullScreen,		/**< Over the whole view */
				InnerWindow,	/**< Only where the layer is, as a window into one over the whole view */
				InnerLayer		/**< Only where the layer is, the horizon and the vanishing point a fraction of the layer */
			};

			/**
			 * @brief Parameters of one texture style of a layer, `jjLAYERWARPHORIZON` and the others of JJ2+
			 *
			 * What they mean depends on the style - the fade position of a warp horizon, a tunnel and a cylinder, the pivot of
			 * a menu, the amplitudes of a wave, the vanishing point and the top of a reflection - but as in JJ2+, they're
			 * the same values for all the styles, a value set for one is the value of the others too (see @ref SetStyleValue()),
			 * only the tint color is a reflection's own. The fade color and the stars, which each style takes in its own way
			 * too, are of the layer as well (see @ref ApplyTexture()).
			 */
			struct TextureStyleParams {
				/** @brief Layer the parameters are of */
				jjLAYER* Owner = nullptr;
				/** @brief Horizontal value, as a fraction of the view (of the layer for @ref InnerLayer), or the horizontal amplitude */
				float X = 0.5f;
				/** @brief Vertical value, as a fraction of the view (of the layer for @ref InnerLayer), or the vertical amplitude */
				float Y = 0.5f;
				/** @brief Whether it fades into the fade color */
				bool Fade = true;
				/** @brief Palette entry a reflection is tinted with */
				std::uint8_t TintColor = 192;
			};

			/** @brief Parameters of `TEXTURE::WARPHORIZON` */
			TextureStyleParams warpHorizon;
			/** @brief Parameters of `TEXTURE::TUNNEL` */
			TextureStyleParams tunnel;
			/** @brief Parameters of `TEXTURE::MENU` */
			TextureStyleParams menu;
			/** @brief Parameters of `TEXTURE::TILEMENU` */
			TextureStyleParams tileMenu;
			/** @brief Parameters of `TEXTURE::WAVE` */
			TextureStyleParams wave;
			/** @brief Parameters of `TEXTURE::CYLINDER` */
			TextureStyleParams cylinder;
			/** @brief Parameters of `TEXTURE::REFLECTION` */
			TextureStyleParams reflection;

			/** @brief Returns the textured-background mode of the layer */
			std::int32_t GetTextureMode() const;
			/** @brief Sets the textured-background mode of the layer */
			void SetTextureMode(std::int32_t value);
			/** @brief Returns the texture used by the layer */
			std::int32_t GetTexture() const;
			/** @brief Sets the texture used by the layer */
			void SetTexture(std::int32_t value);
			/** @brief Returns where the layer is drawn textured, see @ref TextureSurfaceStyles */
			std::int32_t GetTextureSurface() const;
			/** @brief Sets where the layer is drawn textured, see @ref TextureSurfaceStyles */
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
			/** @brief Sets a value of the parameters of a texture style, of all of them as in JJ2+ */
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
			/** @brief Sets whether a texture style fades, all of them as in JJ2+ */
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
			/** @brief Returns the fade color */
			static jjPALCOLOR GetFadeColor(const TextureStyleParams* params);
			/** @brief Sets the fade color */
			static void SetFadeColor(TextureStyleParams* params, jjPALCOLOR color);

			/** @brief Rotation angle of the layer */
			std::int32_t rotationAngle = 0;
			/** @brief Radius multiplier applied when rotating the layer */
			std::int32_t rotationRadiusMultiplier = 0;
			/** @brief Whether the layer tiles vertically */
			bool tileHeight = false;
			/** @brief Whether the layer tiles horizontally */
			bool tileWidth = false;
			/** @brief Whether the layer's visible region is limited */
			bool limitVisibleRegion = false;
			/** @brief Whether the layer has a tile map */
			bool hasTileMap = false;
			/** @brief Whether the layer contains any tiles */
			bool hasTiles = false;

			/** @brief Returns the draw order of all layers */
			static CScriptArray* jjLayerOrderGet();
			/** @brief Sets the draw order of all layers */
			static bool jjLayerOrderSet(const CScriptArray& order);
			/**
			 * @brief Returns an offset of a layer as JJ2+ keeps it, in 16.16 fixed point (measured)
			 *
			 * An offset out of its range is 0 and 32768 itself wraps to -32768 - layers offset by `jjRandom()`,
			 * which leaves them all where they'd be without an offset.
			 */
			static float ToStoredOffset(float value);
			/** @brief Loads the given layers from another level file */
			static CScriptArray* jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor);
			/** @brief Imports tiles from another tileset into the current one */
			static bool jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping);
			/** @brief Imports tiles from another tileset as 32-bit tiles, in the colors of a palette or of the tileset's own */
			static bool jjTilesFromTileset32(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const jjPAL* palette);

			/** @brief Returns the tile at the given tile coordinates */
			std::uint16_t tileGet(int xTile, int yTile);
			/** @brief Sets the tile at the given tile coordinates */
			std::uint16_t tileSet(int xTile, int yTile, std::uint16_t newTile);
			/** @brief Marks a rectangular tile area as runtime-settable */
			void generateSettableTileArea(int xTile, int yTile, int width, int height);
			/** @brief Marks the whole layer as runtime-settable */
			void generateSettableTileAreaAll();

			/** @brief Horizontal speed mode of the layer */
			std::int32_t SpeedModeX = 0;
			/** @brief Vertical speed mode of the layer */
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

			friend class LevelScriptLoader;
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
			/** @brief Creates a new instance for a tile ID, with @ref TILE::ANIMATED for an animated tile and without flips */
			explicit jjTILE(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns the tile for a tile ID of `jjTiles`, or `nullptr` if the level has no such tile */
			static jjTILE* get_jjTiles(std::uint16_t tileID);
			/** @brief Returns the animated tile of the given index, or `nullptr` if the level has no such animated tile */
			static jjTILE* get_jjAnimatedTiles(std::uint16_t tileID);

			/** @brief Returns the frames of the tile as it plays them, with the wait and ping-pong frames */
			CScriptArray* getFrames() const;
			/** @brief Replaces the frames of an animated tile, returns `false` for a static one or no frames */
			bool setFrames(const CScriptArray& frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait);
			/** @brief Returns the frames per second of an animated tile, 0 for a static one */
			std::uint8_t get_fps() const;
			/** @brief Sets the frames per second of an animated tile */
			std::uint8_t set_fps(std::uint8_t value);
			/** @brief Returns the tile ID, as `jjTiles` takes it */
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

	/** @brief Runs MLLE (level editor) level setup */
	bool mlleSetup();
	/** @brief Spawns MLLE off-grid objects */
	void mlleSpawnOffgrids();
	/** @brief Spawns MLLE off-grid objects for the local player */
	void mlleSpawnOffgridsLocal();
	/** @brief Returns the layer of the given name, as MLLE named it, or `nullptr` */
	Legacy::jjLAYER* mlleGetLayer(const String& name);

	// TODO

	void jjSetDarknessColor(jjPALCOLOR color);
	/** @brief Sets the fade color from red, green and blue components */
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
	/** @brief Sets the fade color from a palette index */
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID);
	/** @brief Sets the fade color from a palette color */
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color);
	/** @brief Returns the current fade color */
	jjPALCOLOR jjGetFadeColors();
	/** @brief Recomputes the textured background */
	void jjUpdateTexturedBG();
	/** @brief Returns the texture used by the textured background */
	std::int32_t get_jjTexturedBGTexture();
	/** @brief Sets the texture used by the textured background */
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture);
	/** @brief Returns the textured background style */
	std::int32_t get_jjTexturedBGStyle();
	/** @brief Sets the textured background style */
	std::int32_t set_jjTexturedBGStyle(std::int32_t style);
	/** @brief Returns whether the textured background is used */
	bool get_jjTexturedBGUsed();
	/** @brief Sets whether the textured background is used */
	bool set_jjTexturedBGUsed(bool used);
	/** @brief Returns whether stars are drawn in the textured background */
	bool get_jjTexturedBGStars();
	/** @brief Sets whether stars are drawn in the textured background */
	bool set_jjTexturedBGStars(bool used);
	/** @brief Returns the horizontal fade position of the textured background */
	float get_jjTexturedBGFadePositionX();
	/** @brief Sets the horizontal fade position of the textured background */
	float set_jjTexturedBGFadePositionX(float value);
	/** @brief Returns the vertical fade position of the textured background */
	float get_jjTexturedBGFadePositionY();
	/** @brief Sets the vertical fade position of the textured background */
	float set_jjTexturedBGFadePositionY(float value);

	/** @brief Returns whether layer 8 uses its own scrolling speeds */
	bool getUseLayer8Speeds();
	/** @brief Sets whether layer 8 uses its own scrolling speeds */
	bool setUseLayer8Speeds(bool value);

	// TODO

	std::uint16_t jjGetStaticTile(std::uint16_t tileID);
	/** @brief Returns the tile at the given coordinates on the given layer */
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile);
	/** @brief Sets the tile at the given coordinates on the given layer */
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile);
	/** @brief Marks a rectangular tile area on the given layer as runtime-settable */
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height);

	// TODO

	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel);
	/** @brief Returns whether the given pixel is masked (solid) on the given layer */
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer);
	/** @brief Returns whether any pixel along the horizontal line is masked (solid) */
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel);
	/** @brief Returns whether any pixel along the horizontal line is masked (solid) on the given layer */
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer);
	/** @brief Returns whether any pixel along the vertical line is masked (solid) */
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/** @brief Returns whether any pixel along the vertical line is masked (solid) on the given layer */
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
	/** @brief Returns the position of the topmost masked (solid) pixel along the vertical line counted from 1, or `lineLength + 1` if there's none */
	std::int32_t jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/** @brief Returns the position of the topmost masked (solid) pixel along the vertical line on the given layer counted from 1, or `lineLength + 1` if there's none */
	std::int32_t jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
}

#endif
