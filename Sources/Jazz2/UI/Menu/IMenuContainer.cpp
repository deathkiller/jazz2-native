#include "IMenuContainer.h"
#include "MenuResources.h"
#include "Tweening.h"
#include "../Font.h"

#include <algorithm>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	void IMenuContainer::DrawMenuFrame(float centerX, float topLine, float bottomLine)
	{
		DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, BackgroundLayer, Alignment::Center,
			Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		DrawElement(MenuLine, 0, centerX, topLine, MainLayer, Alignment::Center, Colorf::White, 1.6f);
		DrawElement(MenuLine, 1, centerX, bottomLine, MainLayer, Alignment::Center, Colorf::White, 1.6f);
	}

	void IMenuContainer::DrawMenuTitle(std::int32_t& charOffset, StringView text, float centerX, float topLine)
	{
		DrawStringShadow(text, charOffset, centerX, topLine - 21.0f, FontLayer, Alignment::Center,
			Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void IMenuContainer::DrawMenuListItem(std::int32_t& charOffset, StringView text, float centerX, float y, bool selected, float animation, bool transparent)
	{
		if (selected) {
			float size = 0.5f + Easing::OutElastic(animation) * 0.6f;
			DrawStringGlow(text, charOffset, centerX, y, FontLayer + 10, Alignment::Center,
				transparent ? Font::TransparentRandomColor : Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			DrawStringShadow(text, charOffset, centerX, y, FontLayer, Alignment::Center,
				transparent ? Font::TransparentDefaultColor : Font::DefaultColor, 0.9f);
		}
	}

	void IMenuContainer::DrawMenuArrows(std::int32_t& charOffset, float centerX, float y, float animation, float arrowSpacing)
	{
		float size = 0.5f + Easing::OutElastic(animation) * 0.6f;
		Colorf arrowColor = Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + animation));
		DrawStringShadow("<"_s, charOffset, centerX - 70.0f - arrowSpacing - 30.0f * size, y, FontLayer + 20,
			Alignment::Right, arrowColor, 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
		DrawStringShadow(">"_s, charOffset, centerX + 80.0f + arrowSpacing + 30.0f * size, y, FontLayer + 20,
			Alignment::Right, arrowColor, 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
	}

	void IMenuContainer::DrawMenuValue(std::int32_t& charOffset, StringView value, float centerX, float y, bool selected, bool readOnly, bool showArrows, float animation, float arrowSpacing)
	{
		if (showArrows) {
			DrawMenuArrows(charOffset, centerX, y, animation, arrowSpacing);
		}

		Colorf valueColor = (selected
			? Colorf(0.46f, 0.46f, 0.46f, readOnly ? 0.36f : 0.5f)
			: (readOnly ? Font::TransparentDefaultColor : Font::DefaultColor));
		DrawStringShadow(value, charOffset, centerX, y, FontLayer - 10, Alignment::Center, valueColor, 0.8f);
	}

	void IMenuContainer::DrawMenuEdgeGlow(float centerX, float y)
	{
		DrawElement(MenuGlow, 0, centerX, y, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
	}
}
