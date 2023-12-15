#include "PlayCustomSection.h"

#if defined(WITH_MULTIPLAYER)

#include "MenuResources.h"
#include "CustomLevelSelectSection.h"
#include "EpisodeSelectSection.h"
#include "ServerSelectSection.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	PlayCustomSection::PlayCustomSection()
	{
		// TRANSLATORS: Menu item in Play Custom Game section
		_items.emplace_back(PlayCustomItem { PlayCustomItemType::PlayCustomLevels, _("Play Custom Levels") });
		// TRANSLATORS: Menu item in Play Custom Game section
		_items.emplace_back(PlayCustomItem { PlayCustomItemType::PlayStoryInCoop, _("Play Story in Cooperation") });
		// TRANSLATORS: Menu item in Play Custom Game section
		_items.emplace_back(PlayCustomItem { PlayCustomItemType::CreateCustomServer, _("Create Custom Server") });
		// TRANSLATORS: Menu item in Play Custom Game section
		_items.emplace_back(PlayCustomItem { PlayCustomItemType::ConnectToServer, _("Connect To Server") });
	}

	void PlayCustomSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement(MenuDim, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		//int32_t charOffset = 0;
		//_root->DrawStringShadow(_("Play Custom Game"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
		//	Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void PlayCustomSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = ItemHeight * 8 / 7;
	}

	void PlayCustomSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}
	}

	void PlayCustomSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_items[_selectedIndex].Item.Type) {
			case PlayCustomItemType::PlayCustomLevels: _root->SwitchToSection<CustomLevelSelectSection>(); break;
			case PlayCustomItemType::PlayStoryInCoop: _root->SwitchToSection<EpisodeSelectSection>(true); break;
			case PlayCustomItemType::CreateCustomServer: _root->SwitchToSection<CustomLevelSelectSection>(true); break;
			case PlayCustomItemType::ConnectToServer: _root->SwitchToSection<ServerSelectSection>(); break;
		}
	}
}

#endif