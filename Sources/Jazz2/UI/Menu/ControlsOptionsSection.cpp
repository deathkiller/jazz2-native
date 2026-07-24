#include "ControlsOptionsSection.h"
#include "RemapControlsSection.h"
#include "TouchControlsOptionsSection.h"
#include "InputDiagnosticsSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#include <Utf8.h>

namespace Jazz2::UI::Menu
{
	ControlsOptionsSection::~ControlsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void ControlsOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Controls"));

		// Row heights matching the original section (ItemHeight = 40, integer math)
		constexpr float RemapHeight = 40 * 4 / 5;	// 32
		constexpr float ActionHeight = 40 * 8 / 7;	// 45

		auto list = std::make_unique<ScrollView>();

#if defined(WITH_MULTIPLAYER)
		constexpr std::int32_t MaxSupportedPlayers = ControlScheme::MaxSupportedPlayers;
#else
		constexpr std::int32_t MaxSupportedPlayers = 1;
#endif
		if (MaxSupportedPlayers > 1) {
			for (std::int32_t i = 0; i < MaxSupportedPlayers; i++) {
				// TRANSLATORS: Menu item in Options > Controls section
				list->Add<ListItem>(_f("Remap Controls for Player {}", i + 1),
					[root, i]() { root->SwitchToSection<RemapControlsSection>(i); }, RemapHeight);
			}
		} else {
			// TRANSLATORS: Menu item in Options > Controls section
			list->Add<ListItem>(_("Remap Controls"),
				[root]() { root->SwitchToSection<RemapControlsSection>(0); }, RemapHeight);
		}

		// TRANSLATORS: Menu item in Options > Controls section
		list->Add<ListItem>(_("Touch Controls"),
			[root]() { root->SwitchToSection<TouchControlsOptionsSection>(); }, ActionHeight);

		// TRANSLATORS: Menu item in Options > Controls section
		list->Add<ChoiceItem>(_("Toggle Run"),
			[]() -> StringView { return (PreferencesCache::ToggleRunAction ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::ToggleRunAction = !PreferencesCache::ToggleRunAction; _isDirty = true; });

		list->Add<ChoiceItem>(_("Gamepad Button Labels"),
			[]() -> StringView {
				switch (PreferencesCache::GamepadButtonLabels) {
					default:
					case GamepadType::Xbox: return "Xbox"_s;
					case GamepadType::PlayStation: return "PlayStation™"_s;
					case GamepadType::Steam: return "Steam Deck"_s;
					case GamepadType::Switch: return "Switch"_s;
				}
			},
			[this](std::int32_t direction) {
				// 4 contiguous values; Left/Right step backward/forward with wraparound
				PreferencesCache::GamepadButtonLabels = (GamepadType)(((std::int32_t)PreferencesCache::GamepadButtonLabels + direction + 4) % 4);
				_isDirty = true;
			});

#if defined(NCINE_HAS_GAMEPAD_RUMBLE)
		list->Add<ChoiceItem>(_("Gamepad Rumble"),
			[]() -> StringView {
				return (PreferencesCache::GamepadRumble == 2
					// TRANSLATORS: Option for Gamepad Rumble in Options > Controls section
					? _("Strong")
					: (PreferencesCache::GamepadRumble == 1
						// TRANSLATORS: Option for Gamepad Rumble in Options > Controls section
						? _("Weak")
						: _("Disabled")));
			},
			[this](std::int32_t direction) {
				// 3 contiguous values (Disabled/Weak/Strong); Left/Right step backward/forward with wraparound
				PreferencesCache::GamepadRumble = (PreferencesCache::GamepadRumble + direction + 3) % 3;
				_isDirty = true;
			});
#endif
#if (defined(WITH_SDL2) || defined(WITH_SDL3))
		list->Add<ChoiceItem>(_("Extended PlayStation™ Support"),
			[]() -> StringView { return (PreferencesCache::PlayStationExtendedSupport ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::PlayStationExtendedSupport = !PreferencesCache::PlayStationExtendedSupport;
				theApplication().EnablePlayStationExtendedSupport(PreferencesCache::PlayStationExtendedSupport);
				_isDirty = true;
			});
#endif
#if defined(NCINE_HAS_NATIVE_BACK_BUTTON)
		// TRANSLATORS: Menu item in Options > Controls section (Android only)
		list->Add<ChoiceItem>(_("Native Back Button"),
			[]() -> StringView { return (PreferencesCache::UseNativeBackButton ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::UseNativeBackButton = !PreferencesCache::UseNativeBackButton; _isDirty = true; });
#endif

		list->Add<ListItem>(_("Input Diagnostics"),
			[root]() { root->SwitchToSection<InputDiagnosticsSection>(); }, ActionHeight);
		list->Add<ListItem>(_("Reset To Default"),
			[this]() { ControlScheme::Reset(); _isDirty = true; }, ActionHeight);

		SetContent(std::move(list));
	}
}
