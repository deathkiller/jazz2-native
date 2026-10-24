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
		 * @brief A rectangular buffer of palette-indexed pixels
		 *
		 * Reference-counted, editable image of 8-bit palette indices that scripts read and write per pixel. It can be
		 * built from a tile, an animation frame, a texture, a region of a tile layer, an image file or a blank size,
		 * and saved back into a tile, an animation frame or an image file. The bridge for procedural graphics editing.
		 */
		class jjPIXELMAP
		{
		public:
			/** @brief Creates a new instance */
			jjPIXELMAP();
			~jjPIXELMAP();

			/** @brief Returns a new instance built from a single tile */
			static jjPIXELMAP* CreateFromTile(std::uint16_t tileID, const jjPAL* palette);
			/** @brief Returns a new instance of the given size */
			static jjPIXELMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new instance with the same size and image as another one */
			static jjPIXELMAP* CreateFromOther(const jjPIXELMAP& other);
			/** @brief Returns a new instance built from an animation frame */
			static jjPIXELMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL* palette);
			/** @brief Returns a new instance built from a region of a tile layer */
			static jjPIXELMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL* palette);
			/** @brief Returns a new instance built from a region of the given layer */
			static jjPIXELMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer, const jjPAL* palette);
			/** @brief Returns a new instance built from a texture */
			static jjPIXELMAP* CreateFromTexture(std::uint32_t texture, const jjPAL* palette);
			/** @brief Returns a new instance built from an image file, mapped onto the given palette */
			static jjPIXELMAP* CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold);
			/** @brief Returns a new instance of a color map in the nearest colors of a palette, a pixel of no alpha is transparent */
			static jjPIXELMAP* CreateFromColorMap(const jjCOLORMAP& other, const jjPAL* palette);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjPIXELMAP& operator=(const jjPIXELMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the map */
			std::uint8_t* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Width in pixels */
			std::uint32_t width = 0;
			/** @brief Height in pixels */
			std::uint32_t height = 0;

			/** @brief Saves the pixel map into a tile */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/** @brief Saves the pixel map into an animation frame */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/** @brief Saves the pixel map to an image file using the given palette */
			bool saveToFile(const String& filename, const jjPAL& palette) const;
			/** @brief Makes the pixel map, 256 × 256 pixels, the texture of a layer, of `jjLayers[8]` without one */
			bool makeTexture(jjLAYER* layer) const;

			/** @brief Extends (positive) or crops (negative) each edge by a number of pixels */
			jjPIXELMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::uint8_t borderColor);
			/** @brief Crops the map to the given rectangle */
			jjPIXELMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/** @brief Flips the image across one or both axes (`SPRITE::Direction`) */
			jjPIXELMAP& flip(std::int32_t directions);
			/** @brief Replaces every pixel by the value at its index of a mapping of at least 256 entries */
			jjPIXELMAP& recolor(const CScriptArray& colorMapping);
			/** @brief Resizes the image by nearest-neighbor sampling */
			jjPIXELMAP& resize(std::uint32_t width, std::uint32_t height);
			/** @brief Rotates the image by 90 degrees clockwise */
			jjPIXELMAP& rotate();
			/** @brief Trims empty rows and columns of the given color from all edges, returning how many of them */
			jjPIXELMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimColor);
			/** @brief Trims empty rows and columns of the given color from all edges */
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
		 * @brief A rectangular image of colors with alpha, `jjCOLORMAP` of JJ2+
		 *
		 * The true color counterpart of @ref jjPIXELMAP. A palette index of an image of the game becomes the color the
		 * palette has for it, index 0 is transparent. Saved into a tile or a frame, its colors become the nearest colors
		 * of the palette, the game draws them indexed (see @ref saveToTile()).
		 */
		class jjCOLORMAP
		{
			friend class jjPIXELMAP;

		public:
			/** @brief Creates a new instance */
			jjCOLORMAP();
			~jjCOLORMAP();

			/** @brief Returns a new transparent instance of the given size */
			static jjCOLORMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new instance of a pixel map in the colors of a palette */
			static jjCOLORMAP* CreateFromPixelMap(const jjPIXELMAP& other, const jjPAL& palette);
			/** @brief Returns a new instance with the same image as another one */
			static jjCOLORMAP* CreateFromOther(const jjCOLORMAP& other);
			/** @brief Returns a new instance of an animation frame in the colors of a palette */
			static jjCOLORMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL& palette);
			/** @brief Returns a new instance of a texture of a textured background in the colors of a palette */
			static jjCOLORMAP* CreateFromTexture(std::uint32_t texture, const jjPAL& palette);
			/** @brief Returns a new instance of a tile in the colors of a palette, a tile in true color in its own */
			static jjCOLORMAP* CreateFromTile(std::uint16_t tileID, const jjPAL& palette);
			/** @brief Returns a new instance of a region of a level layer by its number in the colors of a palette */
			static jjCOLORMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL& palette);
			/** @brief Returns a new instance of a region of a layer in the colors of a palette */
			static jjCOLORMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER& layer, const jjPAL& palette);
			/** @brief Returns a new instance of an image file */
			static jjCOLORMAP* CreateFromFilename(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjCOLORMAP& operator=(const jjCOLORMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the map */
			jjPALCOLOR* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Width in pixels */
			std::uint32_t width = 0;
			/** @brief Height in pixels */
			std::uint32_t height = 0;

			/**
			 * @brief Saves the color map into a tile
			 *
			 * The colors become the nearest ones of `jjPalette`, a pixel of any alpha but none is opaque - the game draws the
			 * tiles of a level indexed, so they follow the palette as it changes.
			 */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/** @brief Saves the color map into an animation frame, its colors become the nearest ones of `jjPalette`, as @ref saveToTile() */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/** @brief Saves the color map to an image file, not supported */
			bool saveToFile(const String& filename, bool allowCompression) const;

			/** @brief Extends (positive) or crops (negative) each edge by a number of pixels */
			jjCOLORMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, jjPALCOLOR borderColor);
			/** @brief Crops the map to the given rectangle */
			jjCOLORMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/** @brief Flips the image across one or both axes (`SPRITE::Direction`) */
			jjCOLORMAP& flip(std::int32_t directions);
			/** @brief Resizes the image by nearest-neighbor sampling */
			jjCOLORMAP& resize(std::uint32_t width, std::uint32_t height);
			/** @brief Rotates the image by 90 degrees clockwise */
			jjCOLORMAP& rotate();
			/** @brief Swaps the components of every color, see `jjPALCOLOR::swizzle()` */
			jjCOLORMAP& swizzle(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha);
			/** @brief Trims the rows and columns of no more alpha than given from all edges, returning how many of them */
			jjCOLORMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimAlpha);
			/** @brief Trims the rows and columns of no more alpha than given from all edges */
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

		/** @brief A rectangular collision mask of solid/empty pixels */
		class jjMASKMAP
		{
		public:
			/** @brief Size of the mask, which is always the one of a tile */
			static constexpr std::uint32_t Size = 32;

			/** @brief Creates a new instance */
			jjMASKMAP();
			~jjMASKMAP();

			/** @brief Returns a new instance filled entirely solid or empty */
			static jjMASKMAP* CreateFromBool(bool filled);
			/** @brief Returns a new instance built from a tile's collision mask */
			static jjMASKMAP* CreateFromTile(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjMASKMAP& operator=(const jjMASKMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the mask */
			bool* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Saves the mask into a tile's collision mask */
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
