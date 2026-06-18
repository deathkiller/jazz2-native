#include "GraphicsOptionsSection.h"
#include "RescaleModeSection.h"
#include "../Font.h"
#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/I18n.h"

#include <Environment.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#endif

namespace Jazz2::UI::Menu
{
	GraphicsOptionsSection::~GraphicsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GraphicsOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Graphics"));

		auto list = std::make_unique<ScrollView>();

		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ListItem>(_("Rescale Mode"), [root]() { root->SwitchToSection<RescaleModeSection>(); });

		// Display-only row (no OnChange): shows the current drawable resolution without arrows
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Resolution"),
			[this]() -> StringView {
				Vector2i res = theApplication().GetGfxDevice().drawableResolution();
				_resolutionValue = format("{}x{}", res.X, res.Y);
				return _resolutionValue;
			},
			nullptr);

#if defined(NCINE_HAS_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		// Xbox is always fullscreen
		if (Environment::CurrentDeviceType != DeviceType::Xbox)
#	endif
		{
			// TRANSLATORS: Menu item in Options > Graphics section
			list->Add<ChoiceItem>(_("Fullscreen"),
				[]() -> StringView { return (PreferencesCache::EnableFullscreen ? _("Enabled") : _("Disabled")); },
				[this](std::int32_t) {
					PreferencesCache::EnableFullscreen = !PreferencesCache::EnableFullscreen;
					if (PreferencesCache::EnableFullscreen) {
						theApplication().GetGfxDevice().setResolution(true);
						theApplication().GetInputManager().setCursor(IInputManager::Cursor::Hidden);
					} else {
						theApplication().GetGfxDevice().setResolution(false);
						theApplication().GetInputManager().setCursor(IInputManager::Cursor::Arrow);
					}
					_isDirty = true;
				});
		}
#endif
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Antialiasing"),
			[]() -> StringView { return ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				RescaleMode newMode = (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);
				if ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) != RescaleMode::UseAntialiasing) {
					newMode |= RescaleMode::UseAntialiasing;
				}
				PreferencesCache::ActiveRescaleMode = newMode;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Background Dithering"),
			[]() -> StringView { return (PreferencesCache::BackgroundDithering ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::BackgroundDithering = !PreferencesCache::BackgroundDithering;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Blur Effects"),
			[]() -> StringView { return (PreferencesCache::BlurEffects ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::BlurEffects = !PreferencesCache::BlurEffects;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Lighting Resolution"),
			[this]() -> StringView {
				_lightingResolutionValue = format("{}% ({}x{})", PreferencesCache::LightingResolutionPercent,
					LevelHandler::DefaultWidth * PreferencesCache::LightingResolutionPercent / 100,
					LevelHandler::DefaultHeight * PreferencesCache::LightingResolutionPercent / 100);
				return _lightingResolutionValue;
			},
			[this](std::int32_t direction) {
				// Ascending presets so Right increases and Left decreases; clamped at the ends (no wraparound)
				static const std::int32_t presets[] = { 12, 25, 50, 75, 100 };
				constexpr std::int32_t count = (std::int32_t)(sizeof(presets) / sizeof(presets[0]));
				std::int32_t index = count - 1;
				for (std::int32_t i = 0; i < count; i++) {
					if (PreferencesCache::LightingResolutionPercent <= presets[i]) {
						index = i;
						break;
					}
				}
				index += direction;
				if (index < 0) {
					index = 0;
				} else if (index >= count) {
					index = count - 1;
				}
				PreferencesCache::LightingResolutionPercent = presets[index];
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Water Quality"),
			[]() -> StringView { return (PreferencesCache::LowWaterQuality ? _("Low") : _("High")); },
			[this](std::int32_t) {
				PreferencesCache::LowWaterQuality = !PreferencesCache::LowWaterQuality;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Show Player Trails"),
			[]() -> StringView { return (PreferencesCache::ShowPlayerTrails ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::ShowPlayerTrails = !PreferencesCache::ShowPlayerTrails;
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Preferred Splitscreen"),
			[]() -> StringView { return (PreferencesCache::PreferVerticalSplitscreen ? _("Vertical") : _("Horizontal")); },
			[this](std::int32_t) {
				PreferencesCache::PreferVerticalSplitscreen = !PreferencesCache::PreferVerticalSplitscreen;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Prefer Zoom Out"),
			[]() -> StringView { return (PreferencesCache::PreferZoomOut ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::PreferZoomOut = !PreferencesCache::PreferZoomOut;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Keep Aspect Ratio In Cinematics"),
			[]() -> StringView { return (PreferencesCache::KeepAspectRatioInCinematics ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::KeepAspectRatioInCinematics = !PreferencesCache::KeepAspectRatioInCinematics;
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Unaligned Viewport"),
			[]() -> StringView { return (PreferencesCache::UnalignedViewport ? _("Enabled \f[c:#d0705d](Experimental)\f[/c]") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::UnalignedViewport = !PreferencesCache::UnalignedViewport;
				_isDirty = true;
			});
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Performance Metrics"),
			[]() -> StringView { return (PreferencesCache::ShowPerformanceMetrics ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::ShowPerformanceMetrics = !PreferencesCache::ShowPerformanceMetrics;
				_isDirty = true;
			});

		SetContent(std::move(list));
	}

	void GraphicsOptionsSection::OnDraw(Canvas* canvas)
	{
		WidgetSection::OnDraw(canvas);

		// Performance Metrics (FPS counter overlay drawn outside the framed content area)
		if (PreferencesCache::ShowPerformanceMetrics) {
			Vector2i view = canvas->ViewSize;
			std::int32_t charOffset = 0;
			char stringBuffer[32];
			i32tos((std::int32_t)std::round(theApplication().GetFrameTimer().GetAverageFps()), stringBuffer);
#if defined(DEATH_TARGET_ANDROID)
			if (static_cast<AndroidApplication&>(theApplication()).IsScreenRound()) {
				_root->DrawStringShadow(stringBuffer, charOffset, view.X / 2 + 40.0f, 6.0f, IMenuContainer::FontLayer,
					Alignment::TopRight, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
			} else
#endif
				_root->DrawStringShadow(stringBuffer, charOffset, view.X - 4.0f, 1.0f, IMenuContainer::FontLayer,
					Alignment::TopRight, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f);
		}
	}
}
