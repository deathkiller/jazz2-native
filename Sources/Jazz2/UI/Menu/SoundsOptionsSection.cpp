#include "SoundsOptionsSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <Utf8.h>

namespace Jazz2::UI::Menu
{
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

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Sounds"));

		auto list = std::make_unique<StackLayout>();
		list->Spread = true;

		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("Master Volume"),
			[]() -> float { return PreferencesCache::MasterVolume; },
			[this](float delta) {
				PreferencesCache::MasterVolume = std::clamp(PreferencesCache::MasterVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("SFX Volume"),
			[]() -> float { return PreferencesCache::SfxVolume; },
			[this](float delta) {
				PreferencesCache::SfxVolume = std::clamp(PreferencesCache::SfxVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Sounds section
		list->Add<Slider>(_("Music Volume"),
			[]() -> float { return PreferencesCache::MusicVolume; },
			[this](float delta) {
				PreferencesCache::MusicVolume = std::clamp(PreferencesCache::MusicVolume + delta, 0.0f, 1.0f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Audio);
				_isDirty = true;
			});

		SetContent(std::move(list));
	}
}
