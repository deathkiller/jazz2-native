#include "GameplayEnhancementsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GameplayEnhancementsSection::GameplayEnhancementsSection()
		: _selectedIndex(0), _animation(0.0f), _transition(0.0f), _isDirty(false), _isInGame(false)
	{
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items[(int32_t)Item::Reforged].Name = _("Reforged Mode");
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items[(int32_t)Item::LedgeClimb].Name = _("Ledge Climbing");
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		_items[(int32_t)Item::WeaponWheel].Name = _("Weapon Wheel");
	}

	GameplayEnhancementsSection::~GameplayEnhancementsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GameplayEnhancementsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
		_isInGame = (dynamic_cast<InGameMenu*>(_root) != nullptr);
	}

	void GameplayEnhancementsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_transition < 1.0f) {
			_transition = std::min(_transition + timeMult * 0.2f, 1.0f);
		}

		if (_root->ActionHit(PlayerActions::Fire) || _root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right)) {

			ExecuteSelected();
		} else if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (int32_t)Item::Count - 1;
			}
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (int32_t)Item::Count - 1) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
		}
	}

	void GameplayEnhancementsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		float topLine = 131.0f + 34.0f * IMenuContainer::EaseOutCubic(_transition);
		float bottomLine = viewSize.Y - 42.0f;
		_root->DrawElement(MenuDim, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.35f / (int32_t)Item::Count;
		int32_t charOffset = 0;

		_root->DrawStringShadow(_("Enhancements"), charOffset, center.X, 131.0f - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Header in Options > Gameplay > Enhancements section
		_root->DrawStringShadow(_("You can enable enhancements that were added to this remake."), charOffset, center.X, topLine - 21.0f - 4.0f, IMenuContainer::FontLayer - 2,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.2f + 0.3f * _transition), 0.76f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		for (int32_t i = 0; i < (int32_t)Item::Count; i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawElement(MenuGlow, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(_items[i].Name) + 3) * 0.5f * size, 4.0f * size, true);

				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, (_selectedIndex == 0 && _isInGame ? Font::TransparentRandomColor : Font::RandomColor), size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				_root->DrawStringShadow("<"_s, charOffset, center.X - 110.0f - 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, center.X + 120.0f + 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			StringView customText;
			bool enabled = false;
			switch (i) {
				default:
				case (int32_t)Item::Reforged: enabled = PreferencesCache::EnableReforged; break;
				case (int32_t)Item::LedgeClimb: enabled = PreferencesCache::EnableLedgeClimb; break;
				case (int32_t)Item::WeaponWheel:
					customText = (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount
						// TRANSLATORS: Option for Weapon Wheel item in Options > Gameplay > Enhancements section
						? _("Enabled With Ammo Count")
						: (PreferencesCache::WeaponWheel == WeaponWheelStyle::Enabled ? _("Enabled") : _("Disabled")));
					break;
			}

			_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, center.X, center.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (_selectedIndex == i ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);

			center.Y += (bottomLine - topLine) * 0.9f / (int32_t)Item::Count;
		}
	}

	void GameplayEnhancementsSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (int32_t i = 0; i < (int32_t)Item::Count; i++) {
					if (std::abs(x - 0.5f) < 0.22f && std::abs(y - _items[i].TouchY) < 22.0f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
						}
						break;
					}
				}
			}
		}
	}

	void GameplayEnhancementsSection::ExecuteSelected()
	{
		switch (_selectedIndex) {
			case (int32_t)Item::Reforged:
				if (_isInGame) {
					return;
				}
				PreferencesCache::EnableReforged = !PreferencesCache::EnableReforged;
				break;

			case (int32_t)Item::LedgeClimb: PreferencesCache::EnableLedgeClimb = !PreferencesCache::EnableLedgeClimb; break;
			case (int32_t)Item::WeaponWheel:
				PreferencesCache::WeaponWheel = (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount
						? WeaponWheelStyle::Disabled
						: (PreferencesCache::WeaponWheel == WeaponWheelStyle::Enabled ? WeaponWheelStyle::EnabledWithAmmoCount : WeaponWheelStyle::Enabled));
				break;
		}

		_isDirty = true;
		_animation = 0.0f;
		_root->PlaySfx("MenuSelect"_s, 0.6f);
	}
}