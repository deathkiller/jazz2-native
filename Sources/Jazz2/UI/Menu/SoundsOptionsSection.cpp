#include "SoundsOptionsSection.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	SoundsOptionsSection::SoundsOptionsSection()
		: _selectedIndex(0), _animation(0.0f), _isDirty(false), _pressedCooldown(0.0f), _pressedCount(0)
	{
		// TRANSLATORS: Menu item in Options > Sounds section
		_items[(int32_t)Item::MasterVolume].Name = _("Master Volume");
		// TRANSLATORS: Menu item in Options > Sounds section
		_items[(int32_t)Item::SfxVolume].Name = _("SFX Volume");
		// TRANSLATORS: Menu item in Options > Sounds section
		_items[(int32_t)Item::MusicVolume].Name = _("Music Volume");
	}

	SoundsOptionsSection::~SoundsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void SoundsOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void SoundsOptionsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_pressedCooldown < 1.0f) {
			_pressedCooldown = std::min(_pressedCooldown + timeMult * 0.008f, 1.0f);
		}

		if (_root->ActionHit(PlayerActions::Menu)) {
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
		} else if (_root->ActionPressed(PlayerActions::Left) || _root->ActionPressed(PlayerActions::Right)) {
			if (_pressedCooldown >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right)) {
				float* value;
				switch (_selectedIndex) {
					default:
					case (int32_t)Item::MasterVolume: value = &PreferencesCache::MasterVolume; break;
					case (int32_t)Item::SfxVolume: value = &PreferencesCache::SfxVolume; break;
					case (int32_t)Item::MusicVolume: value = &PreferencesCache::MusicVolume; break;
				}

				*value = std::clamp(*value + (_root->ActionPressed(PlayerActions::Left) ? -0.03f : 0.03f), 0.0f, 1.0f);

				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_isDirty = true;
				_pressedCooldown = 0.0f;
				_pressedCount = std::min(_pressedCount + 6, 10);
			}
		} else {
			_pressedCount = 0;
		}
	}

	void SoundsOptionsSection::OnDraw(Canvas* canvas)
	{
		constexpr int BlockCount = 33;

		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + contentBounds.H * 0.5f);
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 42.0f;

		_root->DrawElement(MenuDim, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.35f / (int32_t)Item::Count;
		int charOffset = 0;

		_root->DrawStringShadow(_("Sounds"), charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		char stringBuffer[34];

		for (int i = 0; i < (int32_t)Item::Count; i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawElement(MenuGlow, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(_items[i].Name) + 3) * 0.5f * size, 4.0f * size, true, true);

				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				_root->DrawStringShadow("<"_s, charOffset, center.X - 70.0f - 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, center.X + 80.0f + 30.0f * size, center.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			int currentBlockCount;
			switch (i) {
				default:
				case (int32_t)Item::MasterVolume: currentBlockCount = (int32_t)std::round(PreferencesCache::MasterVolume * BlockCount); break;
				case (int32_t)Item::SfxVolume: currentBlockCount = (int32_t)std::round(PreferencesCache::SfxVolume * BlockCount); break;
				case (int32_t)Item::MusicVolume: currentBlockCount = (int32_t)std::round(PreferencesCache::MusicVolume * BlockCount); break;
			}

			for (int i = 0; i < BlockCount; i++) {
				stringBuffer[i] = '|';
			}
			stringBuffer[BlockCount] = '\0';

			_root->DrawStringShadow(stringBuffer, charOffset, center.X - 66.0f, center.Y + 24.0f, IMenuContainer::FontShadowLayer + 2,
				Alignment::Left, Colorf(0.38f, 0.37f, 0.34f, 0.34f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

			for (int i = 0; i < currentBlockCount; i++) {
				stringBuffer[i] = '|';
			}
			stringBuffer[currentBlockCount] = '\0';

			_root->DrawStringShadow(stringBuffer, charOffset, center.X - 66.0f, center.Y + 24.0f, IMenuContainer::FontLayer - 10,
				Alignment::Left, (_selectedIndex == i ? Font::RandomColor : Font::DefaultColor), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);


			center.Y += (bottomLine - topLine) * 0.9f / (int32_t)Item::Count;
		}
	}

	void SoundsOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (int i = 0; i < (int32_t)Item::Count; i++) {
					if (std::abs(x - 0.5f) < 0.22f && std::abs(y - _items[i].TouchY) < 30.0f) {
						if (_selectedIndex == i) {
							float* value;
							switch (_selectedIndex) {
								default:
								case (int32_t)Item::MasterVolume: value = &PreferencesCache::MasterVolume; break;
								case (int32_t)Item::SfxVolume: value = &PreferencesCache::SfxVolume; break;
								case (int32_t)Item::MusicVolume: value = &PreferencesCache::MusicVolume; break;
							}

							*value = std::clamp(*value + (x < 0.5f ? -0.03f : 0.03f), 0.0f, 1.0f);

							_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
							_root->PlaySfx("MenuSelect"_s, 0.6f);
							_isDirty = true;
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
}