#include "ControlsOptionsSection.h"
#include "MenuResources.h"
#include "RemapControlsSection.h"
#include "TouchControlsOptionsSection.h"
#include "InputDiagnosticsSection.h"
#include "../../PreferencesCache.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	ControlsOptionsSection::ControlsOptionsSection()
		: _isDirty(false)
	{
		// TRANSLATORS: Menu item in Options > Controls section
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::RemapControls, _("Remap Controls") });
		// TRANSLATORS: Menu item in Options > Controls section
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::TouchControls, _("Touch Controls") });
		// TRANSLATORS: Menu item in Options > Controls section
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::InputDiagnostics, _("Input Diagnostics") });
#if defined(DEATH_TARGET_ANDROID)
		// TRANSLATORS: Menu item in Options > Controls section (Android only)
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::UseNativeBackButton, _("Native Back Button"), true });
#endif
	}

	ControlsOptionsSection::~ControlsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void ControlsOptionsSection::OnHandleInput()
	{
		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}

	void ControlsOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement(MenuDim, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Controls"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void ControlsOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = (item.Item.HasBooleanValue ? 52 : ItemHeight * 8 / 7);
	}

	void ControlsOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			if (item.Item.HasBooleanValue) {
				_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}

		if (item.Item.HasBooleanValue) {
			bool enabled;
			switch (item.Item.Type) {
#if defined(DEATH_TARGET_ANDROID)
				case ControlsOptionsItemType::UseNativeBackButton: enabled = PreferencesCache::UseNativeBackButton; break;
#endif
				default: enabled = false; break;
			}

			_root->DrawStringShadow(enabled ? _("Enabled") : _("Disabled"), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
		}
	}

	void ControlsOptionsSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_items[_selectedIndex].Item.Type) {
			case ControlsOptionsItemType::RemapControls: _root->SwitchToSection<RemapControlsSection>(); break;
			case ControlsOptionsItemType::TouchControls: _root->SwitchToSection<TouchControlsOptionsSection>(); break;
			case ControlsOptionsItemType::InputDiagnostics: _root->SwitchToSection<InputDiagnosticsSection>(); break;
#if defined(DEATH_TARGET_ANDROID)
			case ControlsOptionsItemType::UseNativeBackButton:
				PreferencesCache::UseNativeBackButton = !PreferencesCache::UseNativeBackButton;
				_isDirty = true;
				_animation = 0.0f;
				break;
#endif
		}
	}
}