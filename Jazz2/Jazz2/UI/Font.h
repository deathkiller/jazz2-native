#pragma once

#include "Canvas.h"
#include "../../nCine/Primitives/Colorf.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Base/HashMap.h"
#include "../../nCine/Graphics/Texture.h"

using namespace nCine;

namespace Jazz2::UI
{
	class Font
	{
	public:
		Font(const StringView& path);

		void DrawString(Canvas* canvas, const StringView& text, int& charOffset, float x, float y, uint16_t z, Alignment align, Colorf color, float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f, float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f);

	private:
		Rectf _asciiChars[128];
		HashMap<uint32_t, Rectf> _unicodeChars;
		int _baseSpacing, _charHeight;
		std::unique_ptr<Texture> _texture;
	};
}