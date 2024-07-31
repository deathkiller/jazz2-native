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
		if (ControlScheme::MaxSupportedPlayers > 1) {
			for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
				// TRANSLATORS: Menu item in Options > Controls section
				_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::RemapControls, _f("Remap Controls for Player %i", i + 1), false, i });
			}
		} else {
			// TRANSLATORS: Menu item in Options > Controls section
			_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::RemapControls, _("Remap Controls") });
		}
		// TRANSLATORS: Menu item in Options > Controls section
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::TouchControls, _("Touch Controls") });
		// TRANSLATORS: Menu item in Options > Controls section
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::ToggleRunAction, _("Toggle Run"), true });
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::EnableAltGamepad, _("Gamepad Button Labels"), true });
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::EnableGamepadRumble, _("Gamepad Rumble"), true });
#endif
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
		// TRANSLATORS: Menu item in Options > Controls section (Android only)
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::UseNativeBackButton, _("Native Back Button"), true });
#endif
		_items.emplace_back(ControlsOptionsItem { ControlsOptionsItemType::InputDiagnostics, _("Input Diagnostics") });
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
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Controls"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void ControlsOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = (item.Item.HasBooleanValue ? 52 : (item.Item.Type == ControlsOptionsItemType::RemapControls ? (ItemHeight * 4 / 5) : (ItemHeight * 8 / 7)));
	}

	void ControlsOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

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
			StringView customText;
			bool enabled;
			switch (item.Item.Type) {
				case ControlsOptionsItemType::ToggleRunAction: enabled = PreferencesCache::ToggleRunAction; break;
				case ControlsOptionsItemType::EnableAltGamepad: enabled = PreferencesCache::GamepadButtonLabels != GamepadType::Xbox; customText = (enabled ? "PlayStation™"_s : "Xbox"_s); break;
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
				case ControlsOptionsItemType::EnableGamepadRumble:
					customText = (PreferencesCache::GamepadRumble == 2
						// TRANSLATORS: Option for Gamepad Rumble in Options > Controls section
						? _("Strong")
						: (PreferencesCache::GamepadRumble == 1
							// TRANSLATORS: Option for Gamepad Rumble in Options > Controls section
							? _("Weak")
							: _("Disabled")));
					break;
#endif
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
				case ControlsOptionsItemType::UseNativeBackButton: enabled = PreferencesCache::UseNativeBackButton; break;
#endif
				default: enabled = false; break;
			}

			_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
		}
	}

	void ControlsOptionsSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_items[_selectedIndex].Item.Type) {
			case ControlsOptionsItemType::RemapControls: _root->SwitchToSection<RemapControlsSection>(_items[_selectedIndex].Item.PlayerIndex); break;
			case ControlsOptionsItemType::TouchControls: _root->SwitchToSection<TouchControlsOptionsSection>(); break;
			case ControlsOptionsItemType::ToggleRunAction:
				PreferencesCache::ToggleRunAction = !PreferencesCache::ToggleRunAction;
				_isDirty = true;
				_animation = 0.0f;
				break;
			case ControlsOptionsItemType::EnableAltGamepad:
				if (PreferencesCache::GamepadButtonLabels != GamepadType::Xbox) {
					PreferencesCache::GamepadButtonLabels = GamepadType::Xbox;
				} else {
					PreferencesCache::GamepadButtonLabels = GamepadType::PlayStation;
				}
				_isDirty = true;
				_animation = 0.0f;
				break;
#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
			case ControlsOptionsItemType::EnableGamepadRumble:
				PreferencesCache::GamepadRumble = (PreferencesCache::GamepadRumble == 1
					? 2
					: (PreferencesCache::GamepadRumble == 2 ? 0 : 1));
				_isDirty = true;
				_animation = 0.0f;
				break;
#endif
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
			case ControlsOptionsItemType::UseNativeBackButton:
				PreferencesCache::UseNativeBackButton = !PreferencesCache::UseNativeBackButton;
				_isDirty = true;
				_animation = 0.0f;
				break;
#endif
			case ControlsOptionsItemType::InputDiagnostics: _root->SwitchToSection<InputDiagnosticsSection>(); break;
		}
	}
}