#include "MultiplayerGameModeSelectSection.h"

#if defined(WITH_MULTIPLAYER)

#include "CreateServerOptionsSection.h"
#include "MenuResources.h"

#include <Utf8.h>

using namespace Jazz2::Multiplayer;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	MultiplayerGameModeSelectSection::MultiplayerGameModeSelectSection()
	{
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::Battle, _("Battle") });
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::TeamBattle, _("Team Battle") });
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::CaptureTheFlag, _("Capture The Flag") });
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::Race, _("Race") });
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::TreasureHunt, _("Treasure Hunt") });
		// TRANSLATORS: Menu item in Select Game Mode section
		_items.emplace_back(MultiplayerGameModeItem { MultiplayerGameMode::Cooperation, _("Cooperation") });
	}

	void MultiplayerGameModeSelectSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Select Game Mode"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void MultiplayerGameModeSelectSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
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

	void MultiplayerGameModeSelectSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		if (auto* underlyingSection = dynamic_cast<CreateServerOptionsSection*>(_root->GetUnderlyingSection())) {
			MultiplayerGameMode gameMode = _items[_selectedIndex].Item.Mode;
			underlyingSection->SetGameMode(gameMode);
		}

		_root->LeaveSection();
	}
}

#endif