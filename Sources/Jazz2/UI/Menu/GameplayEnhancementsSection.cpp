#include "GameplayEnhancementsSection.h"
#include "InGameMenu.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <Utf8.h>

namespace Jazz2::UI::Menu
{
	// Standard divider line insets (matching WidgetSection)
	static constexpr std::int32_t TopLine = 31;
	static constexpr std::int32_t BottomLine = 42;
	// Intro panel slides the frame down by this much and reveals the header text
	static constexpr float IntroOffset = 28.0f;
	// Extra arrow spacing so the `<` `>` arrows clear the wider values (e.g., "Enabled With Ammo Count")
	static constexpr float WideArrowSpacing = 40.0f;

	GameplayEnhancementsSection::~GameplayEnhancementsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	Recti GameplayEnhancementsSection::GetClipRectangle(const Recti& contentBounds)
	{
		std::int32_t topLine = TopLine + (std::int32_t)IntroOffset;
		return Recti(contentBounds.X, contentBounds.Y + topLine - 1, contentBounds.W, contentBounds.H - topLine - BottomLine + 2);
	}

	void GameplayEnhancementsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		bool reforgedGameplayReadOnly = false;
		bool ledgeClimbReadOnly = false;
		if (auto* inGameMenu = runtime_cast<InGameMenu>(root)) {
			reforgedGameplayReadOnly = true;
			ledgeClimbReadOnly = !inGameMenu->IsLocalSession();
		}

		auto list = std::make_unique<ScrollView>();

		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		list->Add<ChoiceItem>(_("Reforged Gameplay"),
			[]() -> StringView { return (PreferencesCache::EnableReforgedGameplay ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::EnableReforgedGameplay = !PreferencesCache::EnableReforgedGameplay; _isDirty = true; },
			reforgedGameplayReadOnly)->ArrowSpacing = WideArrowSpacing;
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		list->Add<ChoiceItem>(_("Reforged HUD"),
			[]() -> StringView { return (PreferencesCache::EnableReforgedHUD ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::EnableReforgedHUD = !PreferencesCache::EnableReforgedHUD; _isDirty = true; })->ArrowSpacing = WideArrowSpacing;
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		list->Add<ChoiceItem>(_("Reforged Main Menu"),
			[]() -> StringView { return (PreferencesCache::EnableReforgedMainMenu ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::EnableReforgedMainMenu = !PreferencesCache::EnableReforgedMainMenu;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
				_isDirty = true;
			})->ArrowSpacing = WideArrowSpacing;
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		list->Add<ChoiceItem>(_("Ledge Climbing"),
			[]() -> StringView { return (PreferencesCache::EnableLedgeClimb ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) { PreferencesCache::EnableLedgeClimb = !PreferencesCache::EnableLedgeClimb; _isDirty = true; },
			ledgeClimbReadOnly)->ArrowSpacing = WideArrowSpacing;
		// TRANSLATORS: Menu item in Options > Gameplay > Enhancements section
		list->Add<ChoiceItem>(_("Weapon Wheel"),
			[]() -> StringView {
				return (PreferencesCache::WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount
					// TRANSLATORS: Option for Weapon Wheel item in Options > Gameplay > Enhancements section
					? _("Enabled With Ammo Count")
					: (PreferencesCache::WeaponWheel == WeaponWheelStyle::Enabled ? _("Enabled") : _("Disabled")));
			},
			[this](std::int32_t direction) {
				// 3 contiguous values (Disabled/Enabled/EnabledWithAmmoCount); Left/Right step backward/forward with wraparound
				PreferencesCache::WeaponWheel = (WeaponWheelStyle)(((std::int32_t)PreferencesCache::WeaponWheel + direction + 3) % 3);
				_isDirty = true;
			})->ArrowSpacing = WideArrowSpacing;

		SetContent(std::move(list));
	}

	void GameplayEnhancementsSection::OnUpdate(float timeMult)
	{
		// Advance the intro animation first; the base update may leave the section (back button), after which no
		// member must be touched
		if (_transition < 1.0f) {
			_transition = std::min(_transition + timeMult * 0.14f, 1.0f);
		}

		WidgetSection::OnUpdate(timeMult);
	}

	void GameplayEnhancementsSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine + IntroOffset * Easing::OutCubic(_transition);
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		_root->DrawMenuTitle(charOffset, _("Enhancements"), centerX, contentBounds.Y + TopLine);

		// TRANSLATORS: Header in Options > Gameplay > Enhancements section
		_root->DrawStringShadow(_("You can enable enhancements that were added to this remake."), charOffset, centerX, topLine - 15.0f - 4.0f, IMenuContainer::FontLayer - 2,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.2f + 0.3f * _transition), 0.76f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}
}
