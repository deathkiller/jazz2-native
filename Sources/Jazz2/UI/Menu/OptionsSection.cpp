#include "OptionsSection.h"
#include "MenuResources.h"
#include "GameplayOptionsSection.h"
#include "GraphicsOptionsSection.h"
#include "SoundsOptionsSection.h"
#include "ControlsOptionsSection.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	OptionsSection::OptionsSection()
	{
		// TRANSLATORS: Menu item in Options section
		_items.emplace_back(OptionsItem { OptionsItemType::Gameplay, _("Gameplay") });
		// TRANSLATORS: Menu item in Options section
		_items.emplace_back(OptionsItem { OptionsItemType::Graphics, _("Graphics") });
		// TRANSLATORS: Menu item in Options section
		_items.emplace_back(OptionsItem { OptionsItemType::Sounds, _("Sounds") });
		// TRANSLATORS: Menu item in Options section
		_items.emplace_back(OptionsItem { OptionsItemType::Controls, _("Controls") });
	}

	void OptionsSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		
		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Options"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void OptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = ItemHeight * 8 / 7;
	}

	void OptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}
	}

	void OptionsSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_items[_selectedIndex].Item.Type) {
			case OptionsItemType::Gameplay: _root->SwitchToSection<GameplayOptionsSection>(); break;
			case OptionsItemType::Graphics: _root->SwitchToSection<GraphicsOptionsSection>(); break;
			case OptionsItemType::Sounds: _root->SwitchToSection<SoundsOptionsSection>(); break;
			case OptionsItemType::Controls: _root->SwitchToSection<ControlsOptionsSection>(); break;
		}
	}
}