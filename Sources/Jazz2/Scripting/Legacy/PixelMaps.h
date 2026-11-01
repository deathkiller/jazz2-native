#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Animations.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		class jjLAYER;
		class jjCOLORMAP;

		/**
		 * @brief Editable 8-bit image
		 *
		 * A rectangular array of palette indices, where 0 is transparent. A pixel map can be created from a tile, an
		 * animation frame, a texture, an area of a layer, an image file, a color map or just a size, edited pixel by
		 * pixel or by its transforming methods, and then saved into a tile or an animation frame, or used as the texture
		 * of a layer. Only the size of the image matters when saving, not where it came from.
		 *
		 * Where a source has 32-bit colors, such as a true color tile or an image file, the optional palette of the
		 * constructor maps them to its nearest entries. It defaults to @ref jjPalette.
		 */
		class jjPIXELMAP
		{
		public:
			/** @brief Creates a new instance */
			jjPIXELMAP();
			~jjPIXELMAP();

			/**
			 * @brief Creates a 32×32 pixel map from the image of a tile
			 *
			 * @ref TILE::HFLIPPED and @ref TILE::VFLIPPED in `tileID` flip the image, an animated tile gives the frame it shows
			 * now. A tile the level doesn't have gives a transparent map.
			 */
			static jjPIXELMAP* CreateFromTile(std::uint16_t tileID, const jjPAL* palette);
			/**
			 * @brief Creates a transparent pixel map of the given size in pixels
			 *
			 * A map of more than 8192×8192 pixels in total can't be created and raises a script exception.
			 */
			static jjPIXELMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Creates a copy of another pixel map */
			static jjPIXELMAP* CreateFromOther(const jjPIXELMAP& other);
			/** @brief Creates a pixel map with the size and the image of an animation frame */
			static jjPIXELMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL* palette);
			/**
			 * @brief Creates a pixel map from an area of one of the original layers
			 *
			 * `left`, `top`, `width` and `height` are in pixels, `layer` is a number from 1 to 8, as in @ref jjLayers. Makes
			 * it easy to compose an image from several tiles placed next to each other. The part out of the layer is
			 * transparent.
			 */
			static jjPIXELMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL* palette);
			/**
			 * @brief Creates a pixel map from an area of a layer
			 *
			 * The same as the constructor taking a layer number. A layer that isn't part of the level gives a transparent
			 * map.
			 */
			static jjPIXELMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer, const jjPAL* palette);
			/**
			 * @brief Creates a 256×256 pixel map from a texture
			 *
			 * @ref TEXTURE::FROMTILES takes the 8×8 tiles at the top left corner of `jjLayers[8]`. The other textures are read
			 * from a data file in the game's `Source` directory, if it's missing, the map stays transparent.
			 */
			static jjPIXELMAP* CreateFromTexture(std::uint32_t texture, const jjPAL* palette);
			/**
			 * @brief Creates a pixel map from an image file
			 *
			 * The palette indices of an image with a palette are taken as they are, regardless of its colors, so the image
			 * should use a palette that matches the level's. The colors of any other image are mapped to the nearest
			 * entries of `palette`, a pixel with alpha lower than `threshold` (or no alpha at all) stays transparent.
			 * A file that can't be read gives a 1×1 map.
			 */
			static jjPIXELMAP* CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold);
			/**
			 * @brief Creates a pixel map from a color map
			 *
			 * Each color becomes the nearest entry of `palette`, a pixel with no alpha becomes transparent and any other
			 * pixel is opaque.
			 */
			static jjPIXELMAP* CreateFromColorMap(const jjCOLORMAP& other, const jjPAL* palette);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjPIXELMAP& operator=(const jjPIXELMAP& o);

			/**
			 * @brief Returns the pixel at the given position
			 *
			 * A palette index, 0 is transparent. Pixels are stored row by row, so an `x` past `width` continues on the
			 * following rows. A position past the end of the map reads as 0, and what's written there is lost.
			 */
			std::uint8_t* GetPixel(std::uint32_t x, std::uint32_t y);

			/**
			 * @brief Width of the image in pixels
			 *
			 * Read-only, changed by `addBorders()`, `crop()`, `resize()`, `rotate()` and `trim()`.
			 */
			std::uint32_t width = 0;
			/**
			 * @brief Height of the image in pixels
			 *
			 * Read-only, changed by `addBorders()`, `crop()`, `resize()`, `rotate()` and `trim()`.
			 */
			std::uint32_t height = 0;

			/**
			 * @brief Saves the image into a tile
			 *
			 * The map has to be exactly 32×32 pixels. @ref TILE::HFLIPPED and @ref TILE::VFLIPPED in `tileID` flip the
			 * image, so the tile shows the map when it's drawn flipped that way. Returns `false` if the size is wrong
			 * or `tileID` is an animated tile.
			 * @partialsupport The flipped versions of a tile always change along with it, so `hFlip` has no effect.
			 */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/**
			 * @brief Replaces the image of an animation frame
			 *
			 * The frame takes the size of the map, its hotspot and other spots stay as they were. Returns `false` if
			 * `frame` is null or the map is larger than 65535 pixels on either axis.
			 */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/**
			 * @brief Saves the image to a PNG file
			 *
			 * @partialsupport Currently not supported, always returns `false`.
			 */
			bool saveToFile(const String& filename, const jjPAL& palette) const;
			/**
			 * @brief Makes the image the texture of a layer
			 *
			 * The map has to be exactly 256×256 pixels. Without a layer, `jjLayers[8]` is used. The layer's `texture`
			 * then reads as @ref TEXTURE::CUSTOM. Returns `false` if the size is wrong or the layer isn't part of the level.
			 */
			bool makeTexture(jjLAYER* layer) const;

			/**
			 * @brief Adds or removes a border on each edge
			 *
			 * A positive number of pixels extends the image on that edge, filling the new space with `borderColor`, a
			 * negative one crops it. Nothing changes if the result would be empty or more would be cropped than the image
			 * has. Returns the map itself.
			 */
			jjPIXELMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::uint8_t borderColor);
			/**
			 * @brief Crops the image to a rectangle
			 *
			 * Nothing changes if the rectangle is empty or reaches out of the image. Returns the map itself.
			 */
			jjPIXELMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/**
			 * @brief Flips the image across one or both axes
			 *
			 * Takes @ref SPRITE::FLIPH, @ref SPRITE::FLIPV or @ref SPRITE::FLIPHV. Returns the map itself.
			 */
			jjPIXELMAP& flip(std::int32_t directions);
			/**
			 * @brief Recolors the image through a color mapping
			 *
			 * Every pixel is replaced by the entry of `colorMapping` at its palette index, e.g. pixels of index 10 become
			 * `colorMapping[10]`. Nothing changes if the mapping has fewer than 256 entries. Returns the map itself.
			 */
			jjPIXELMAP& recolor(const CScriptArray& colorMapping);
			/**
			 * @brief Scales the image to a new size by nearest-neighbor sampling
			 *
			 * Nothing changes if the old or the new size is 0 on either axis. Returns the map itself.
			 */
			jjPIXELMAP& resize(std::uint32_t width, std::uint32_t height);
			/**
			 * @brief Rotates the image by 90 degrees clockwise
			 *
			 * The width and the height swap. Returns the map itself.
			 */
			jjPIXELMAP& rotate();
			/**
			 * @brief Removes the rows and columns of a single color from the edges, telling how many were removed
			 *
			 * Every outer row and column made only of `trimColor` (by default transparent) is removed. The output
			 * arguments receive how many were removed from each edge. If the whole image is `trimColor`, it's cropped
			 * to 1×1, and `left` and `right` receive the former width, `top` and `bottom` the former height. Returns
			 * the map itself.
			 */
			jjPIXELMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimColor);
			/**
			 * @brief Removes the rows and columns of a single color from the edges
			 *
			 * See the overload with output arguments. Returns the map itself.
			 */
			jjPIXELMAP& trimBasic(std::uint8_t trimColor);

		private:
			std::int32_t _refCount;
			std::unique_ptr<std::uint8_t[]> _pixels;
			// The pixel GetPixel() gives for a position out of the map
			std::uint8_t _outOfRange = 0;

			// Returns a new transparent map, or nullptr with a script exception if it would be too large
			static jjPIXELMAP* Create(std::uint32_t width, std::uint32_t height);
			// Returns a new map of a rectangle of the tiles of a level layer, by its index in the tile map
			static jjPIXELMAP* CreateFromEngineLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::int32_t layerIndex, const jjPAL* palette);
			// Replaces the image by the given rectangle of it, the part out of the image is filled with `fill`
			void Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, std::uint8_t fill);
		};

		/**
		 * @brief Editable 32-bit image with alpha
		 *
		 * The true color counterpart of @ref jjPIXELMAP, each pixel is a @ref jjPALCOLOR with its own alpha. An 8-bit source is
		 * converted through the palette given to the constructor, by default @ref jjPalette: index 0 becomes fully
		 * transparent, any other index the color of that palette entry, fully opaque. A 32-bit source keeps its colors.
		 *
		 * Saved into a tile of a palette-indexed tile set, the colors are reduced to the nearest entries of @ref jjPalette.
		 */
		class jjCOLORMAP
		{
			friend class jjPIXELMAP;

		public:
			/** @brief Creates a new instance */
			jjCOLORMAP();
			~jjCOLORMAP();

			/**
			 * @brief Creates a fully transparent color map of the given size in pixels
			 *
			 * A map of more than 8192×8192 pixels in total can't be created and raises a script exception.
			 */
			static jjCOLORMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Creates a color map from a pixel map in the colors of a palette */
			static jjCOLORMAP* CreateFromPixelMap(const jjPIXELMAP& other, const jjPAL& palette);
			/** @brief Creates a copy of another color map */
			static jjCOLORMAP* CreateFromOther(const jjCOLORMAP& other);
			/**
			 * @brief Creates a color map with the size and the image of an animation frame
			 *
			 * A 32-bit frame keeps its own colors.
			 */
			static jjCOLORMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL& palette);
			/**
			 * @brief Creates a 256×256 color map from a texture
			 *
			 * @ref TEXTURE::FROMTILES takes the 8×8 tiles at the top left corner of `jjLayers[8]`, see the same constructor of
			 * @ref jjPIXELMAP.
			 */
			static jjCOLORMAP* CreateFromTexture(std::uint32_t texture, const jjPAL& palette);
			/**
			 * @brief Creates a 32×32 color map from the image of a tile
			 *
			 * A true color tile keeps its own colors. Flips and animated tiles are handled as by the same constructor of
			 * @ref jjPIXELMAP.
			 */
			static jjCOLORMAP* CreateFromTile(std::uint16_t tileID, const jjPAL& palette);
			/**
			 * @brief Creates a color map from an area of one of the original layers
			 *
			 * `left`, `top`, `width` and `height` are in pixels, `layer` is a number from 1 to 8, as in @ref jjLayers. True
			 * color tiles are reduced to the nearest colors of `palette` first.
			 */
			static jjCOLORMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL& palette);
			/**
			 * @brief Creates a color map from an area of a layer
			 *
			 * The same as the constructor taking a layer number. A layer that isn't part of the level gives a transparent
			 * map.
			 */
			static jjCOLORMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER& layer, const jjPAL& palette);
			/**
			 * @brief Creates a color map from an image file
			 *
			 * Takes the actual colors and alpha of the image, whatever its format is. A file that can't be read gives a
			 * 1×1 map.
			 */
			static jjCOLORMAP* CreateFromFilename(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjCOLORMAP& operator=(const jjCOLORMAP& o);

			/**
			 * @brief Returns the color at the given position
			 *
			 * Pixels are stored row by row, so an `x` past `width` continues on the following rows. A position past the
			 * end of the map reads as transparent black, and what's written there is lost.
			 */
			jjPALCOLOR* GetPixel(std::uint32_t x, std::uint32_t y);

			/**
			 * @brief Width of the image in pixels
			 *
			 * Read-only, changed by `addBorders()`, `crop()`, `resize()`, `rotate()` and `trim()`.
			 */
			std::uint32_t width = 0;
			/**
			 * @brief Height of the image in pixels
			 *
			 * Read-only, changed by `addBorders()`, `crop()`, `resize()`, `rotate()` and `trim()`.
			 */
			std::uint32_t height = 0;

			/**
			 * @brief Saves the image into a tile
			 *
			 * If the tile set is drawn palette-indexed, the colors become the nearest entries of @ref jjPalette and any
			 * pixel with non-zero alpha is opaque, so the tile follows later palette changes.
			 * @partialsupport The rules of @ref jjPIXELMAP::save() for tiles apply: the map has to be 32×32, flips in
			 * `tileID` flip the image, `hFlip` has no effect and animated tiles fail.
			 */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/**
			 * @brief Replaces the image of an animation frame
			 *
			 * The frame becomes a 32-bit one with the size of the map, its hotspot and other spots stay as they were.
			 * Returns `false` if `frame` is null or the map is larger than 65535 pixels on either axis.
			 */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/**
			 * @brief Saves the image to a PNG file
			 *
			 * @partialsupport Currently not supported, always returns `false`.
			 */
			bool saveToFile(const String& filename, bool allowCompression) const;

			/**
			 * @brief Adds or removes a border on each edge
			 *
			 * A positive number of pixels extends the image on that edge, filling the new space with `borderColor`
			 * (transparent by default), a negative one crops it. Nothing changes if the result would be empty or more
			 * would be cropped than the image has. Returns the map itself.
			 */
			jjCOLORMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, jjPALCOLOR borderColor);
			/**
			 * @brief Crops the image to a rectangle
			 *
			 * Nothing changes if the rectangle is empty or reaches out of the image. Returns the map itself.
			 */
			jjCOLORMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/**
			 * @brief Flips the image across one or both axes
			 *
			 * Takes @ref SPRITE::FLIPH, @ref SPRITE::FLIPV or @ref SPRITE::FLIPHV. Returns the map itself.
			 */
			jjCOLORMAP& flip(std::int32_t directions);
			/**
			 * @brief Scales the image to a new size by nearest-neighbor sampling
			 *
			 * Nothing changes if the old or the new size is 0 on either axis. Returns the map itself.
			 */
			jjCOLORMAP& resize(std::uint32_t width, std::uint32_t height);
			/**
			 * @brief Rotates the image by 90 degrees clockwise
			 *
			 * The width and the height swap. Returns the map itself.
			 */
			jjCOLORMAP& rotate();
			/**
			 * @brief Rearranges the components of every color
			 *
			 * Each argument names the component the new one is taken from, as @ref jjPALCOLOR::swizzle() does for a single
			 * color. Returns the map itself.
			 */
			jjCOLORMAP& swizzle(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha);
			/**
			 * @brief Removes the nearly transparent rows and columns from the edges, telling how many were removed
			 *
			 * Every outer row and column with no alpha greater than `trimAlpha` (by default fully transparent) is removed.
			 * The output arguments receive how many were removed from each edge. If nothing in the image is more opaque,
			 * it's cropped to 1×1, and `left` and `right` receive the former width, `top` and `bottom` the former height.
			 * Returns the map itself.
			 */
			jjCOLORMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimAlpha);
			/**
			 * @brief Removes the nearly transparent rows and columns from the edges
			 *
			 * See the overload with output arguments. Returns the map itself.
			 */
			jjCOLORMAP& trimBasic(std::uint8_t trimAlpha);

			/** @brief Returns the palette indices of the image, the nearest colors of a palette, 0 where there's no alpha */
			std::unique_ptr<std::uint8_t[]> ToIndices(const jjPAL& palette) const;

		private:
			std::int32_t _refCount;
			std::unique_ptr<jjPALCOLOR[]> _pixels;
			// The pixel GetPixel() gives for a position out of the map
			jjPALCOLOR _outOfRange = {};

			// Returns a new transparent map, or nullptr with a script exception if it would be too large
			static jjCOLORMAP* Create(std::uint32_t width, std::uint32_t height);
			// Returns a new map of palette indices in the colors of a palette, index 0 transparent
			static jjCOLORMAP* CreateFromIndices(const std::uint8_t* indices, std::uint32_t width, std::uint32_t height, const jjPAL& palette);
			// Replaces the image by the given rectangle of it, the part out of the image is filled with `fill`
			void Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, jjPALCOLOR fill);
		};

		/**
		 * @brief Editable collision mask of a tile
		 *
		 * A 32×32 array of pixels, `true` for a solid pixel and `false` for an empty one. It can be read from a tile and
		 * saved into a tile to change where objects collide with it.
		 */
		class jjMASKMAP
		{
		public:
			/** @brief Size of the mask, which is always the one of a tile */
			static constexpr std::uint32_t Size = 32;

			/** @brief Creates a new instance */
			jjMASKMAP();
			~jjMASKMAP();

			/** @brief Creates a mask with every pixel solid or every pixel empty */
			static jjMASKMAP* CreateFromBool(bool filled);
			/**
			 * @brief Creates a mask from the collision mask of a tile
			 *
			 * Flips and animated tiles are handled as by the same constructor of @ref jjPIXELMAP.
			 */
			static jjMASKMAP* CreateFromTile(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjMASKMAP& operator=(const jjMASKMAP& o);

			/**
			 * @brief Returns the pixel at the given position, `true` if it's solid
			 *
			 * Pixels are stored row by row, so an `x` past 31 continues on the following rows. A position past the end
			 * of the mask reads as `false`, and what's written there is lost.
			 */
			bool* GetPixel(std::uint32_t x, std::uint32_t y);

			/**
			 * @brief Replaces the collision mask of a tile
			 *
			 * @partialsupport The rules of @ref jjPIXELMAP::save() for tiles apply, except that the size is always
			 * right: flips in `tileID` flip the mask, `hFlip` has no effect and animated tiles fail.
			 */
			bool save(std::uint16_t tileID, bool hFlip) const;

		private:
			std::int32_t _refCount;
			bool _mask[Size * Size];
			// The pixel GetPixel() gives for a position out of the map
			bool _outOfRange = false;
		};
	}
}

#endif
