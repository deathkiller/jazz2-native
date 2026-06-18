#include "GameplayOptionsSection.h"
#include "InGameMenu.h"
#include "GameplayEnhancementsSection.h"
#include "LanguageSelectSection.h"
#include "RefreshCacheSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../Input/RgbLights.h"
#include "../../PreferencesCache.h"
#include "../../ContentResolver.h"

#include "../../../nCine/I18n.h"

#include <cmath>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GameplayOptionsSection::~GameplayOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GameplayOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		bool isInGame = (runtime_cast<InGameMenu>(root) != nullptr);

		SetTitle(_("Gameplay"));

		auto list = std::make_unique<ScrollView>();

		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ListItem>(_("Enhancements"), [root]() { root->SwitchToSection<GameplayEnhancementsSection>(); });
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ListItem>(_("Language"), [root]() { root->SwitchToSection<LanguageSelectSection>(); });

#if defined(WITH_ANGELSCRIPT)
		// TRANSLATORS: Menu item in Options > Gameplay section
		// Uses a custom value drawer to show the UAC icon next to "Enabled" (it allows running unsigned scripts)
		auto* scripting = list->Add<CustomValueItem>(_("Scripting"));
		scripting->ReadOnly = isInGame;
		scripting->OnChange = [this](std::int32_t) { PreferencesCache::AllowUnsignedScripts = !PreferencesCache::AllowUnsignedScripts; _isDirty = true; };
		scripting->DrawValue = [](IMenuContainer* r, float centerX, float y, std::int32_t& charOffset, bool selected, bool readOnly) {
			Colorf valueColor = (selected ? Colorf(0.46f, 0.46f, 0.46f, readOnly ? 0.36f : 0.5f) : (readOnly ? Font::TransparentDefaultColor : Font::DefaultColor));
			if (PreferencesCache::AllowUnsignedScripts) {
				r->DrawStringShadow(_("Enabled"), charOffset, centerX + 12.0f, y, IMenuContainer::FontLayer - 10, Alignment::Center, valueColor, 0.8f);
				Vector2f textSize = r->MeasureString(_("Enabled"), 0.8f);
				r->DrawElement(Uac, 0, std::ceil(centerX - textSize.X * 0.5f - 6.0f), y - 1.0f, IMenuContainer::MainLayer + 10,
					Alignment::Center, (readOnly ? Colorf(1.0f, 1.0f, 1.0f, 0.5f) : Colorf::White));
			} else {
				r->DrawStringShadow(_("Disabled"), charOffset, centerX, y, IMenuContainer::FontLayer - 10, Alignment::Center, valueColor, 0.8f);
			}
		};
#endif
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Continuous Jump"),
			[]() -> StringView { return (PreferencesCache::EnableContinuousJump ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::EnableContinuousJump = !PreferencesCache::EnableContinuousJump; _isDirty = true; });
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Switch To New Weapon"),
			[]() -> StringView { return (PreferencesCache::SwitchToNewWeapon ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::SwitchToNewWeapon = !PreferencesCache::SwitchToNewWeapon; _isDirty = true; });
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Show Minimap"),
			[]() -> StringView { return (PreferencesCache::ShowMinimap ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::ShowMinimap = !PreferencesCache::ShowMinimap; _isDirty = true; });
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Allow Cheats"),
			[]() -> StringView { return (PreferencesCache::AllowCheats ? _("Yes") : _("No")); },
			[this](std::int32_t) { PreferencesCache::AllowCheats = !PreferencesCache::AllowCheats; _isDirty = true; },
			isInGame);
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Overwrite Episode Completion"),
			[]() -> StringView {
				return (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::NoCheatsOnly
					? _("No Cheats Only")
					: (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::HigherScoreOnly
						? _("Higher Score Only")
						: _("Always")));
			},
			[this](std::int32_t direction) {
				// 3 contiguous values; Left/Right step backward/forward with wraparound
				PreferencesCache::OverwriteEpisodeEnd = (EpisodeEndOverwriteMode)(((std::int32_t)PreferencesCache::OverwriteEpisodeEnd + direction + 3) % 3);
				_isDirty = true;
			},
			isInGame);

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ChoiceItem>(_("Razer Chroma™"),
			[]() -> StringView { return (PreferencesCache::EnableRgbLights ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::EnableRgbLights = !PreferencesCache::EnableRgbLights;
				if (!PreferencesCache::EnableRgbLights) {
					RgbLights::Get().Clear();
				}
				_isDirty = true;
			});
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > Gameplay section
		list->Add<ListItem>(_("Browse \"Source\" Directory"), [root]() {
			auto& resolver = ContentResolver::Get();
			String sourcePath = fs::GetAbsolutePath(resolver.GetSourcePath());
			if (sourcePath.empty()) {
				sourcePath = resolver.GetSourcePath();
			}
			fs::CreateDirectories(sourcePath);
			fs::LaunchDirectoryAsync(sourcePath);
		});
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!isInGame) {
			// TRANSLATORS: Menu item in Options > Gameplay section
			list->Add<ListItem>(_("Refresh Cache"), [root]() { root->SwitchToSection<RefreshCacheSection>(); });
		}
#endif

		SetContent(std::move(list));
	}
}
