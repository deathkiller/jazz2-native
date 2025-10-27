﻿#include "GameplayEnhancementsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GameplayEnhancementsSection::GameplayEnhancementsSection()
		: _transition(0.0f), _isDirty(false), _isInGame(false)
	{
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items.emplace_back(GameplayEnhancementsItem { GameplayEnhancementsItemType::ReforgedGameplay, _("Reforged Gameplay") });
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items.emplace_back(GameplayEnhancementsItem { GameplayEnhancementsItemType::ReforgedHUD, _("Reforged HUD") });
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items.emplace_back(GameplayEnhancementsItem { GameplayEnhancementsItemType::ReforgedMainMenu, _("Reforged Main Menu") });
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items.emplace_back(GameplayEnhancementsItem { GameplayEnhancementsItemType::LedgeClimb, _("Ledge Climbing") });
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items.emplace_back(GameplayEnhancementsItem { GameplayEnhancementsItemType::WeaponWheel, _("Weapon Wheel") });
	}

	GameplayEnhancementsSection::~GameplayEnhancementsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	Recti GameplayEnhancementsSection::GetClipRectangle(const Recti& contentBounds)
	{
		float topLine = TopLine + 28.0f;
		return Recti(contentBounds.X, contentBounds.Y + topLine - 1, contentBounds.W, contentBounds.H - topLine - BottomLine + 2);
	}

	void GameplayEnhancementsSection::OnShow(IMenuContainer* root)
	{
		ScrollableMenuSection::OnShow(root);

		_isInGame = runtime_cast<InGameMenu*>(_root);
	}

	void GameplayEnhancementsSection::OnUpdate(float timeMult)
	{
		ScrollableMenuSection::OnUpdate(timeMult);

		if (_transition < 1.0f) {
			_transition = std::min(_transition + timeMult * 0.14f, 1.0f);
		}
	}

	void GameplayEnhancementsSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine + 28.0f * IMenuContainer::EaseOutCubic(_transition);
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Enhancements"), charOffset, centerX, contentBounds.Y + TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Header in Options > Gameplay > Enhancements section
		_root->DrawStringShadow(_("You can enable enhancements that were added to this remake."), charOffset, centerX, topLine - 15.0f - 4.0f, IMenuContainer::FontLayer - 2,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.2f + 0.3f * _transition), 0.76f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void GameplayEnhancementsSection::OnHandleInput()
	{
		if (!_items.empty() && (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}

	void GameplayEnhancementsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = 52;
	}

	void GameplayEnhancementsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;
		bool isReadOnly = IsItemReadOnly(item.Item.Type);

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawStringGlow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, isReadOnly ? Font::TransparentRandomColor : Font::RandomColor,
				size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			if (!isReadOnly) {
				_root->DrawStringShadow("<"_s, charOffset, centerX - 110.0f - 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, centerX + 120.0f + 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, isReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor, 0.9f);
		}

		StringView customText;
		bool enabled;
		switch (item.Item.Type) {
			case GameplayEnhancementsItemType::ReforgedGameplay: enabled = PreferencesCache::EnableReforgedGameplay; break;
			case GameplayEnhancementsItemType::ReforgedHUD: enabled = PreferencesCache::EnableReforgedHUD; break;
			case GameplayEnhancementsItemType::ReforgedMainMenu: enabled = PreferencesCache::EnableReforgedMainMenu; break;
			case GameplayEnhancementsItemType::LedgeClimb: enabled = PreferencesCache::EnableLedgeClimb; break;
			case GameplayEnhancementsItemType::WeaponWheel:
				customText = (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount
					// TRANSLATORS: Option for Weapon Wheel item in Options > Gameplay > Enhancements section
					? _("Enabled With Ammo Count")
					: (PreferencesCache::WeaponWheel == WeaponWheelStyle::Enabled ? _("Enabled") : _("Disabled")));
				break;
			default: enabled = false; break;
		}

		_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
			Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, isReadOnly ? 0.36f : 0.5f) : (isReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
	}

	void GameplayEnhancementsSection::OnExecuteSelected()
	{
		if (IsItemReadOnly(_items[_selectedIndex].Item.Type)) {
			return;
		}

		switch (_items[_selectedIndex].Item.Type) {
			case GameplayEnhancementsItemType::ReforgedGameplay: PreferencesCache::EnableReforgedGameplay = !PreferencesCache::EnableReforgedGameplay; break;
			case GameplayEnhancementsItemType::ReforgedHUD: PreferencesCache::EnableReforgedHUD = !PreferencesCache::EnableReforgedHUD; break;
			case GameplayEnhancementsItemType::ReforgedMainMenu:
				PreferencesCache::EnableReforgedMainMenu = !PreferencesCache::EnableReforgedMainMenu;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
				break;
			case GameplayEnhancementsItemType::LedgeClimb: PreferencesCache::EnableLedgeClimb = !PreferencesCache::EnableLedgeClimb; break;
			case GameplayEnhancementsItemType::WeaponWheel:
				PreferencesCache::WeaponWheel = (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount
						? WeaponWheelStyle::Disabled
						: (PreferencesCache::WeaponWheel == WeaponWheelStyle::Enabled ? WeaponWheelStyle::EnabledWithAmmoCount : WeaponWheelStyle::Enabled));
				break;
		}

		_isDirty = true;
		_animation = 0.0f;
		_root->PlaySfx("MenuSelect"_s, 0.6f);
	}

	bool GameplayEnhancementsSection::IsItemReadOnly(GameplayEnhancementsItemType type)
	{
		if (type == GameplayEnhancementsItemType::ReforgedGameplay && _isInGame) {
			return true;
		} else if (type == GameplayEnhancementsItemType::LedgeClimb && _isInGame) {
			if (auto* inGameMenu = runtime_cast<InGameMenu>(_root)) {
				if (!inGameMenu->IsLocalSession()) {
					return true;
				}
			}
		}

		return false;
	}
}