#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Enums.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		/** @brief A single RGB palette color */
		struct jjPALCOLOR {
			/** @brief Red component */
			std::uint8_t red;
			/** @brief Green component */
			std::uint8_t green;
			/** @brief Blue component */
			std::uint8_t blue;
			/**
			 * @brief Alpha component, only colors of 32-bit images use it in JJ2+, left 0 otherwise
			 *
			 * It also makes the color 4 bytes, which the x64 calling convention of MSVC passes and returns in a register
			 * as AngelScript does - a color of 3 bytes is passed by a pointer to it there, and a script's color then
			 * arrives as an address.
			 */
			std::uint8_t alpha;

			/** @brief Constructs a black color in place */
			static void Create(void* self);
			/** @brief Constructs a color from red, green and blue components in place */
			static void CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self);
			/** @brief Constructs a color from red, green, blue and alpha components in place */
			static void CreateFromRgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, void* self);

			/** @brief Returns the hue component of the color */
			std::uint8_t getHue();
			/** @brief Returns the saturation component of the color */
			std::uint8_t getSat();
			/** @brief Returns the lightness component of the color */
			std::uint8_t getLight();

			/** @brief Reorders the color channels by the given source channels */
			void swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec);
			/** @brief Sets the color from hue, saturation and lightness */
			void setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light);

			jjPALCOLOR& operator=(const jjPALCOLOR& other);
			bool operator==(const jjPALCOLOR& other);
		};

		/**
		 * @brief A 256-color palette
		 *
		 * Reference-counted wrapper around 256 @ref jjPALCOLOR entries, mirroring the original JJ2+ palette object.
		 * Scripts use it to read and edit individual colors and to fill, tint, gradient or copy ranges, then `apply()`
		 * the result to the screen (or `reset()` back to the level default). Backs the global `jjPalette`.
		 */
		class jjPAL
		{
		public:
			/** @brief Creates a new instance */
			jjPAL();
			~jjPAL();

			/** @brief Returns a new instance */
			static jjPAL* Create();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			jjPAL& operator=(const jjPAL& o);

			bool operator==(const jjPAL& o);

			/** @brief Returns the color at the specified palette index */
			jjPALCOLOR& getColor(std::uint8_t idx);
			/** @brief Returns the read-only color at the specified palette index */
			const jjPALCOLOR& getConstColor(std::uint8_t idx) const;
			/** @brief Sets the color at the specified palette index */
			jjPALCOLOR& setColorEntry(std::uint8_t idx, jjPALCOLOR& value);

			/** @brief Resets the palette to the level default */
			void reset();
			/** @brief Applies the palette so it takes effect on screen */
			void apply();
			/** @brief Loads the palette from a file */
			bool load(const String& filename);
			/** @brief Fills the whole palette with the given color, blended by opacity */
			void fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity);
			/** @brief Tints a range of palette entries with the given color */
			void fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity);
			/** @brief Fills the whole palette with the given color, blended by opacity */
			void fillFromColor(jjPALCOLOR color, float opacity);
			/** @brief Tints a range of palette entries with the given color */
			void fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity);
			/** @brief Writes a color gradient into a range of palette entries */
			void gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/** @brief Writes a color gradient into a range of palette entries */
			void gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/** @brief Copies a range of entries from another palette, blended by opacity */
			void copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity);
			/** @brief Returns the palette index closest to the given color */
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
