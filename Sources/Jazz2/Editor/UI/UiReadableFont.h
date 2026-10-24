#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../UI/Font.h"

#include <Containers/SmallVector.h>

#include <memory>

namespace Jazz2::Editor
{
	/**
		@brief Readable font of the level editor, drawn from an outline font at the size text has

		The plainer font the interface switches to from the pixel font of the game, see @ref UiTheme::ReadableFont. Its
		glyphs are drawn from `Editor/Fonts/Roboto-Regular.ttf` of the content the first time text is shown at a scale,
		at exactly the size it has there, so the text is crisp at any scale --- the pixel font is crisp only at whole
		multiples of its size. It's measured in the units of the pixel font and its lines are as high, so the interface
		keeps its layout when the font is switched. The glyphs are gray with a dark rim, as those of the pixel font are,
		so the colors of text tint them the same way.

		The surface of the interface owns it, see @ref UiContext::GetFont().
	*/
	class UiReadableFont
	{
	public:
		UiReadableFont();
		~UiReadableFont();

		UiReadableFont(const UiReadableFont&) = delete;
		UiReadableFont& operator=(const UiReadableFont&) = delete;

		/**
		 * @brief Returns the font for text drawn at a scale, or `nullptr` if the font file can't be read
		 *
		 * The scale is that of text, see @ref UiTheme::GetTextScale(). The glyphs are drawn for it if they weren't yet,
		 * which takes a moment. The fonts of a couple of scales are kept, but none that was used in the current frame is
		 * dropped, text drawn with it is still to be rendered.
		 */
		UI::Font* Get(float textScale);

	private:
		struct Face;

		struct Entry
		{
			float TextScale;
			std::uint32_t LastFrame;
			std::unique_ptr<UI::Font> Font;
		};

		std::unique_ptr<Face> _face;
		Death::Containers::SmallVector<Entry, 2> _fonts;
		bool _failed;

		bool LoadFace();
		std::unique_ptr<UI::Font> Build(float textScale);
	};
}

#endif
