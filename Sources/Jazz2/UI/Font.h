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
		static constexpr Colorf DefaultColor = Colorf(333.0f, 333.0f, 333.0f, 1.0f);
		static constexpr Colorf RandomColor = Colorf(444.0f, 444.0f, 444.0f, 0.5f);
		static constexpr Colorf TransparentRandomColor = Colorf(444.0f, 444.0f, 444.0f, 0.36f);

		Font(const StringView path, const uint32_t* palette);

		Vector2f MeasureString(const StringView text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f);
		void DrawString(Canvas* canvas, const StringView text, int32_t& charOffset, float x, float y, uint16_t z, Alignment align, Colorf color, float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f, float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f);

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
		HashMap<uint32_t, Rectf> _unicodeChars;
		Vector2i _charSize;
		int32_t _baseSpacing;
		std::unique_ptr<Texture> _texture;
	};
}