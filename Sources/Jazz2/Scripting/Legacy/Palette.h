#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Enums.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/**
		 * @brief A single color of a palette or a 32-bit image
		 *
		 * Red, green and blue components, plus alpha used only by 32-bit images. Also provides conversions to and from
		 * hue, saturation and lightness.
		 */
		struct jjPALCOLOR {
			/** @brief Red component, 0 to 255 */
			std::uint8_t red;
			/** @brief Green component, 0 to 255 */
			std::uint8_t green;
			/** @brief Blue component, 0 to 255 */
			std::uint8_t blue;
			/**
			 * @brief Alpha component, 0 to 255
			 *
			 * Only colors of 32-bit images (@ref jjCOLORMAP) use it; palettes ignore it and usually leave it at 0.
			 */
			// It also makes the color 4 bytes, which the x64 calling convention of MSVC passes and returns in a register
			// as AngelScript does - a color of 3 bytes is passed by a pointer to it there, and a script's color then
			// arrives as an address.
			std::uint8_t alpha;

			/** @brief Creates a black color with all components 0 */
			static void Create(void* self);
			/** @brief Creates a color from red, green and blue components, alpha is 0 */
			static void CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self);
			/** @brief Creates a color from red, green, blue and alpha components */
			static void CreateFromRgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, void* self);

			/** @brief Returns the hue of the color, 0 to 255 for a full circle */
			std::uint8_t getHue();
			/** @brief Returns the saturation of the color, from 0 (gray) to 255 */
			std::uint8_t getSat();
			/** @brief Returns the lightness of the color, from 0 (black) to 255 (white) */
			std::uint8_t getLight();

			/**
			 * @brief Rearranges the color components
			 *
			 * Each parameter names the @ref COLOR::Component whose current value goes into red, green and blue
			 * respectively, e.g. `swizzle(COLOR::BLUE, COLOR::GREEN, COLOR::RED)` swaps red and blue. Alpha isn't
			 * changed, and @ref COLOR::ALPHA as a source leaves that component as it is.
			 */
			void swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec);
			/**
			 * @brief Sets the color from hue, saturation and lightness
			 *
			 * All three are on a 0 to 255 scale. The hue wraps around, so values outside that range are valid, e.g.
			 * 258 and -254 equal 2.
			 */
			void setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light);

			/** @brief Copies all four components of another color */
			jjPALCOLOR& operator=(const jjPALCOLOR& other);
			/** @brief Returns whether two colors have the same red, green and blue components, alpha isn't compared */
			bool operator==(const jjPALCOLOR& other);
		};

		/**
		 * @brief A palette of 256 colors
		 *
		 * Colors are edited through `color` or the fill, gradient and copy methods; nothing changes on screen until
		 * @ref apply() puts the palette into use. @ref jjPalette holds the palette in use, @ref jjBackupPalette the original
		 * one of the level. A new palette is all black.
		 */
		class jjPAL
		{
		public:
			/** @brief Creates a new instance */
			jjPAL();
			~jjPAL();

			/** @brief Creates a palette with all colors black */
			static jjPAL* Create();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			/** @brief Copies all colors of another palette */
			jjPAL& operator=(const jjPAL& o);

			/** @brief Returns whether both refer to the same palette object, the colors aren't compared */
			bool operator==(const jjPAL& o);

			/**
			 * @brief Returns the color at a palette index
			 *
			 * Changes made to the returned color edit the palette directly; they show on screen once the palette is
			 * applied with @ref apply().
			 */
			jjPALCOLOR& getColor(std::uint8_t idx);
			/** @brief Returns the color at a palette index, read-only */
			const jjPALCOLOR& getConstColor(std::uint8_t idx) const;
			/** @brief Sets the color at a palette index */
			jjPALCOLOR& setColorEntry(std::uint8_t idx, jjPALCOLOR& value);

			/** @brief Restores the original colors of the level, the same as assigning @ref jjBackupPalette */
			void reset();
			/**
			 * @brief Makes this palette the one the game draws with
			 *
			 * Also copies the colors into @ref jjPalette. Call it after every change, including changes made to
			 * @ref jjPalette itself. Index 0 is always transparent.
			 */
			void apply();
			/**
			 * @brief Loads the palette from a file and returns whether it succeeded
			 *
			 * Accepts the palette of a tile set (`.j2t`), a Windows palette file (plain or in a RIFF container), or
			 * otherwise raw red, green and blue bytes for each entry. Entries the file doesn't contain keep their
			 * colors. Returns `false` only if the file can't be opened or read.
			 */
			bool load(const String& filename);
			/**
			 * @brief Fills the whole palette with a color
			 *
			 * With `opacity` below 1.0, the color is only mixed into the existing colors, tinting them.
			 */
			void fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity);
			/**
			 * @brief Fills a range of the palette with a color
			 *
			 * Replaces `length` entries from `start`, by default all but the first and the last one. With `opacity`
			 * below 1.0, the color is only mixed into the existing colors, tinting them.
			 */
			void fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity);
			/** @brief Fills the whole palette with a color, see the overload taking components */
			void fillFromColor(jjPALCOLOR color, float opacity);
			/** @brief Fills a range of the palette with a color, see the overload taking components */
			void fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity);
			/**
			 * @brief Writes a gradient between two colors into a range of the palette
			 *
			 * Replaces `length` entries from `start`; the defaults cover the entries used by water and most textured
			 * backgrounds. When `inclusive` is `false`, the gradient stops one step short of the second color, otherwise
			 * the last entry is exactly the second color. With `opacity` below 1.0, the gradient is only mixed into the
			 * existing colors.
			 */
			void gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/** @brief Writes a gradient between two colors into a range of the palette, see the overload taking components */
			void gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/**
			 * @brief Copies a range of colors from another palette
			 *
			 * Copies `length` colors of `source` from index `start2` to this palette from index `start`. With `opacity`
			 * below 1.0, they are only mixed into the existing colors.
			 */
			void copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity);
			/** @brief Returns the index of the palette entry closest to a color, any of the 256 entries can be returned */
			std::uint8_t findNearestColor(jjPALCOLOR color);
			/** @brief Returns the palette index closest to the given color but the transparent one, index 0 */
			std::uint8_t FindNearestOpaqueColor(const jjPALCOLOR& color) const;

		private:
			std::int32_t _refCount;
			jjPALCOLOR _palette[256];
		};
	}
}

#endif
