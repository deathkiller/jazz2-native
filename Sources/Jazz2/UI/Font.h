#pragma once

#include "Canvas.h"
#include "../../nCine/Primitives/Colorf.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Base/HashMap.h"
#include "../../nCine/Graphics/Texture.h"

using namespace nCine;

namespace Jazz2::UI
{
	/**
		@brief Bitmap font renderer

		The renderer loads a bitmap font from a custom file format with full Unicode support and renders it to a canvas.
		It can also measure the size of a string without rendering it.

		@section Jazz2-UI-Font-format Text formatting

		The renderer supports inline text formatting using the @cpp "\f[…]" @ce notation. Unknown attributes are ignored.
		Following attributes are supported:
		-   @cpp "\f[c:#RRGGBB]" @ce --- Sets the font color that can be specified in a hex value in RGB format
		    - The game usually renders text using a colorization shader that uses a different color space to be able
			  to change the text color more precisely, so the specified color must be adjusted accordingly
		-   @cpp "\f[/c]" @ce --- Resets the font color
		-   @cpp "\f[w:XX]" @ce --- Sets the character spacing as a percentage, recommended range is 80% to 120%
		-   @cpp "\f[/w]" @ce --- Resets the character spacing

	*/
	class Font
	{
	public:
		/** @{ @name Constants */

		/** @brief Default (yellow) font color */
		static constexpr Colorf DefaultColor = Colorf(333.0f, 333.0f, 333.0f, 1.0f);
		/** @brief Default (yellow) font color with 60% transparency */
		static constexpr Colorf TransparentDefaultColor = Colorf(333.0f, 333.0f, 333.0f, 0.6f);
		/** @brief Random (rainbow) font color */
		static constexpr Colorf RandomColor = Colorf(444.0f, 444.0f, 444.0f, 0.5f);
		/** @brief Random (rainbow) font color with 60% transparency */
		static constexpr Colorf TransparentRandomColor = Colorf(444.0f, 444.0f, 444.0f, 0.36f);

		/** @} */

		Font(StringView path, const std::uint32_t* palette);

		/** @brief Returns font size in pixels */
		std::int32_t GetSizeInPixels() const;
		/** @brief Returns font ascent in pixels */
		std::int32_t GetAscentInPixels() const;
		/** @brief Returns size of a single character */
		Vector2f MeasureChar(char32_t c) const;
		/** @brief Returns size of a string */
		Vector2f MeasureString(StringView text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f);
		/** @brief Returns approximate size of a string */
		Vector2f MeasureStringApprox(StringView text, float scale = 1.0f, float charSpacing = 1.0f);
		/** @brief Returns size of a string and its cumulative widths */
		Vector2f MeasureStringEx(StringView text, float scale, float charSpacing, float maxWidth, std::int32_t* charFit, float* charFitWidths);
		/** @brief Draws a string */
		void DrawString(Canvas* canvas, StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, Colorf color, float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f, float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f);

		/** @brief Strips formatting from the specified text */
		static String StripFormatting(StringView text);

	private:
		static constexpr Colorf RandomColors[] = {
			Colorf(0.4f, 0.55f, 0.85f, 0.5f),
			Colorf(0.7f, 0.45f, 0.42f, 0.5f),
			Colorf(0.58f, 0.48f, 0.38f, 0.5f),
			Colorf(0.25f, 0.45f, 0.3f, 0.5f),
			Colorf(0.7f, 0.42f, 0.7f, 0.5f),
			Colorf(0.44f, 0.44f, 0.8f, 0.5f),
			Colorf(0.54f, 0.54f, 0.54f, 0.5f),
			Colorf(0.62f, 0.44f, 0.34f, 0.5f),
			Colorf(0.56f, 0.50f, 0.42f, 0.5f),
		};

		Rectf _asciiChars[128];
		HashMap<std::uint32_t, Rectf> _unicodeChars;
		Vector2i _charSize;
		std::int32_t _baseSpacing;
		std::unique_ptr<Texture> _texture;
	};
}