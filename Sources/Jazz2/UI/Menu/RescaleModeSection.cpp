#include "RescaleModeSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

namespace Jazz2::UI::Menu
{
	static void ApplyRescaleMode(IMenuContainer* root, RescaleMode newMode)
	{
		if ((PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask) != newMode) {
			PreferencesCache::ActiveRescaleMode = newMode | (PreferencesCache::ActiveRescaleMode & ~RescaleMode::TypeMask);
			if (newMode == RescaleMode::CrtScanlines || newMode == RescaleMode::CrtShadowMask || newMode == RescaleMode::CrtApertureGrille) {
				// Turn off Antialiasing when using CRT modes
				PreferencesCache::ActiveRescaleMode &= ~RescaleMode::UseAntialiasing;
			}
			PreferencesCache::Save();
			root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
		}

		root->LeaveSection();
	}

	void RescaleModeSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Select Rescale Mode"));

		auto list = std::make_unique<ScrollView>();
		RescaleMode currentMode = (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);
		std::int32_t selectedIndex = 0;
		std::int32_t index = 0;

		auto add = [&](RescaleMode mode, StringView name) {
			if (mode == currentMode) {
				selectedIndex = index;
			}
			list->Add<ListItem>(name, [root, mode]() { ApplyRescaleMode(root, mode); });
			index++;
		};

		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::None, _("None / Pixel-perfect"));
		add(RescaleMode::CleanEdge, "CleanEdge"_s);
		add(RescaleMode::HQ2x, "HQ2×"_s);
		add(RescaleMode::_3xBrz, "3×BRZ"_s);
		add(RescaleMode::Sabr, "SABR"_s);
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtScanlines, _("CRT Scanlines"));
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtShadowMask, _("CRT Shadow Mask"));
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtApertureGrille, _("CRT Aperture Grille"));
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::Monochrome, _("Monochrome"));

		list->SetSelectedIndex(selectedIndex);
		SetContent(std::move(list));
	}
}
