#include "FirstRunSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	FirstRunSection::FirstRunSection()
		: _committed(false)
	{
		// TRANSLATORS: Menu item in First Run section
		_items.emplace_back(FirstRunItem { FirstRunItemType::LegacyPreset, _("Legacy"), _("I want to play the game the way it used to be.") });
		// TRANSLATORS: Menu item in First Run section
		_items.emplace_back(FirstRunItem { FirstRunItemType::ReforgedPreset, _("Reforged"), _("I want to play the game with something new.") });
	}

	Recti FirstRunSection::GetClipRectangle(const Recti& contentBounds)
	{
		float topLine = TopLine + 66.0f;
		return Recti(contentBounds.X, contentBounds.Y + topLine - 1, contentBounds.W, contentBounds.H - topLine - BottomLine + 2);
	}

	void FirstRunSection::OnShow(IMenuContainer* root)
	{
		_selectedIndex = (PreferencesCache::EnableReforgedGameplay ? 1 : 0);
		EnsureVisibleSelected();

		MenuSection::OnShow(root);
	}

	void FirstRunSection::OnUpdate(float timeMult)
	{
		ScrollableMenuSection::OnUpdate(timeMult);
	}

	void FirstRunSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine + 66.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		// TRANSLATORS: Header in First Run section
		_root->DrawStringShadow(_("Welcome to \f[c:#9e7056]Jazz Jackrabbit 2\f[/c] reimplementation!"), charOffset, centerX, topLine - 66.0f - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Subheader in First Run section
		_root->DrawStringShadow(_f("You can choose your preferred play style.\nThis option can be changed at any time in \f[c:#707070]%s\f[/c] > \f[c:#707070]%s\f[/c] > \f[c:#707070]%s\f[/c].\nFor more information, visit %s and \uE000 Discord!", _("Options").data(), _("Gameplay").data(), _("Enhancements").data(), "\f[c:#707070]https://deat.tk/jazz2/\f[/c]"),
			charOffset, centerX, topLine - 40.0f, IMenuContainer::FontLayer - 2, Alignment::Center, Font::DefaultColor,
			0.86f, 0.7f, 0.0f, 0.0f, 0.4f, 0.9f);
	}

	void FirstRunSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = 68;
	}

	void FirstRunSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.7f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y + 10.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.2f), 22.0f, 12.0f, true, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, item.Item.Type == FirstRunItemType::ReforgedPreset ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor, 1.0f);
		}

		_root->DrawStringShadow(item.Item.Description, charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.94f);
	}

	void FirstRunSection::OnBackPressed()
	{
		// Can't go back from here
	}

	void FirstRunSection::OnSelectionChanged(ListViewItem& item)
	{
		bool wasReforged = PreferencesCache::EnableReforgedMainMenu;
		bool isReforged = (_items[_selectedIndex].Item.Type == FirstRunItemType::ReforgedPreset);
		PreferencesCache::EnableReforgedMainMenu = isReforged;
		if (isReforged != wasReforged) {
			_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
		}
	}

	void FirstRunSection::OnExecuteSelected()
	{
		bool isReforged = (_items[_selectedIndex].Item.Type == FirstRunItemType::ReforgedPreset);
		PreferencesCache::EnableReforgedGameplay = isReforged;
		PreferencesCache::EnableReforgedHUD = isReforged;
		PreferencesCache::EnableLedgeClimb = isReforged;
		PreferencesCache::Save();

		_committed = true;
		_animation = 0.0f;
		_root->PlaySfx("MenuSelect"_s, 0.6f);
		_root->LeaveSection();
	}
}