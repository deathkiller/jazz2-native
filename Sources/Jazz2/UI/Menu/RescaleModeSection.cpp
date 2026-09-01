#include "RescaleModeSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/RHI/RhiFwd.h"	// RHI_CAP_POSTPROCESSING (a header macro, not a build define)

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
#if defined(RHI_CAP_POSTPROCESSING) && !defined(DISABLE_RESCALE_SHADERS)
		// Neither the direct rendering tier nor a build with `DISABLE_RESCALE_SHADERS` has the rescale shader
		// passes (see UpscaleRenderPass), only the default pixel-perfect mode works there - the section itself
		// is already hidden in GraphicsOptionsSection, this is just defense in depth
		// CleanEdge, SABR and Monochrome are left out on a backend whose offline shader profile rejected them
		// (see RHI_CAP_HEAVY_RESCALE_SHADERS). Omitting them here is all that is needed: a value stored by
		// another backend needs no fallback, because UpscaleRenderPass already falls back to the plain sprite
		// shader whenever the mode resolves to no program, which is exactly the pixel-perfect mode.
#	if defined(RHI_CAP_HEAVY_RESCALE_SHADERS)
		add(RescaleMode::CleanEdge, "CleanEdge"_s);
#	endif
		add(RescaleMode::HQ2x, "HQ2×"_s);
		add(RescaleMode::_3xBrz, "3×BRZ"_s);
#	if defined(RHI_CAP_HEAVY_RESCALE_SHADERS)
		add(RescaleMode::Sabr, "SABR"_s);
#	endif
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtScanlines, _("CRT Scanlines"));
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtShadowMask, _("CRT Shadow Mask"));
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::CrtApertureGrille, _("CRT Aperture Grille"));
#	if defined(RHI_CAP_HEAVY_RESCALE_SHADERS)
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		add(RescaleMode::Monochrome, _("Monochrome"));
#	endif
#endif

		list->SetSelectedIndex(selectedIndex);
		SetContent(std::move(list));
	}
}
