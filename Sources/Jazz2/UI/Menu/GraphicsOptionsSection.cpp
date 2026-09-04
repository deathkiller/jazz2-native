#include "GraphicsOptionsSection.h"
#include "RescaleModeSection.h"
#include "../Font.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/RHI/RhiFwd.h"	// RHI_CAP_POSTPROCESSING (a header macro, not a build define)

#include <algorithm>
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

#if defined(RHI_CAP_POSTPROCESSING) && !defined(DISABLE_RESCALE_SHADERS)
		// The direct rendering tier has no rescale/antialiasing shader passes (the scene is rendered at the
		// logical resolution directly into the screen framebuffer, see UpscaleRenderPass), so the option is
		// hidden there. `DISABLE_RESCALE_SHADERS` takes the same passes out of a build that does have the
		// tier - the PS Vita's, where the section would list modes that then resolve to no program at all.
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ListItem>(_("Rescale Mode"), [root]() { root->SwitchToSection<RescaleModeSection>(); });
#endif

		// Display-only row (no OnChange): shows the current drawable resolution without arrows
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Screen Resolution"),
			[this]() -> StringView {
				Vector2i res = theApplication().GetGfxDevice().drawableResolution();
				_resolutionValue = format("{}x{}", res.X, res.Y);
				return _resolutionValue;
			},
			nullptr);

#if defined(RHI_CAP_POSTPROCESSING)
		// The most the scene is rendered at - the logical view is bounded by the default size scaled by this
		// (see UpscaleRenderPass::CalculateViewSize), and a smaller display keeps a view of its own size. On PS
		// Vita it sizes the frame surface itself instead (see PreferencesCache::ApplyRenderingResolution), so
		// the whole frame is rendered at that fraction of the panel, the drawable follows and the view follows
		// the drawable. The direct rendering tier renders straight into the screen framebuffer at the display's
		// own (small) size, where there is nothing left to lower, so the option is hidden there.
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Rendering Resolution"),
			[this]() -> StringView {
				// The menu's view is computed by the same rule as the level's, so this is the size the scene will be
				Vector2i viewSize = _root->GetViewSize();
				_renderingResolutionValue = format("{}% ({}x{})", PreferencesCache::RenderingResolutionPercent, viewSize.X, viewSize.Y);
				return _renderingResolutionValue;
			},
			[this](std::int32_t direction) {
				// Ascending presets so Right increases and Left decreases; clamped at the ends (no wraparound)
#if defined(WITH_RHI_GXM)
				// The Vita's frame surface is the panel scaled by this, and 75% (720x408) is already the logical
				// view's own size - the full panel would only upscale that same 720x408 scene fractionally into
				// 960x544, at the cost of another full-panel pass, and looks worse for it. So it stops at 75%,
				// with 60% (576x326) as the step between it and the 480x272 default.
				static const std::int32_t presets[] = { 50, 60, 75 };
#else
				static const std::int32_t presets[] = { 50, 75, 100 };
#endif
				constexpr std::int32_t count = (std::int32_t)(sizeof(presets) / sizeof(presets[0]));
				std::int32_t index = count - 1;
				for (std::int32_t i = 0; i < count; i++) {
					if (PreferencesCache::RenderingResolutionPercent <= presets[i]) {
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
				PreferencesCache::RenderingResolutionPercent = presets[index];
				// The drawable itself changes on PS Vita, so the device goes first and the viewports are laid out
				// for the result; every section in the stack then lays itself out again for the new view size
				// (at the next update, as this widget is what is asking - see MenuContainerBase)
				PreferencesCache::ApplyRenderingResolution();
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics | ChangedPreferencesType::Layout);
				_isDirty = true;
			});
#endif

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
#if defined(RHI_CAP_POSTPROCESSING) && !defined(DEATH_TARGET_VITA)
		// The antialiasing subpass is part of the rescale shader chain, which the direct tier bypasses
		// entirely, so the option is hidden there too. It is likewise hidden on PS Vita: vitaGL's runtime shader
		// compiler cannot build the antialiasing resolve shader (see ContentResolver / UpscaleRenderPass).
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
#endif
#if defined(RHI_CAP_POSTPROCESSING) && !defined(DEATH_TARGET_VITA)
		// The dithering is a second texture sample inside the warped background shader, which the direct
		// tier never runs: the fixed-function backends draw those layers as a flat repeating tilemap (see
		// TileMap's SupportsTexturedBackground), and the shader itself compiles the dither sample out for
		// the software renderer. The option would have no effect anywhere on this tier. On PS Vita it does
		// have an effect and the effect is unaffordable: the second sample is a DEPENDENT one (its
		// coordinate comes out of a sin()-based hash), which is the access pattern the SGX543 is worst at,
		// and it measured at a sixth of the frame rate over a full-screen background. The shader compiles
		// it out there (see LOW_POWER_GPU in TexturedBackground.shader) and the option goes with it.
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Background Dithering"),
			[]() -> StringView { return (PreferencesCache::BackgroundDithering ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::BackgroundDithering = !PreferencesCache::BackgroundDithering;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
#endif
#if defined(RHI_CAP_POSTPROCESSING) && !defined(DEATH_TARGET_VITA)
		// Blur effects are not supported by the direct rendering tier, and are forced off on PS Vita (see
		// PreferencesCache): the chain is five more off-screen passes over the whole view, and every one of
		// them is a scene of its own that the backend has to wait out before the next can sample it
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Blur Effects"),
			[]() -> StringView { return (PreferencesCache::BlurEffects ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::BlurEffects = !PreferencesCache::BlurEffects;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
#endif
#if defined(RHI_CAP_POSTPROCESSING)
		// Sizes the off-screen lighting buffer of the shader lighting path (see PlayerViewport), which the
		// direct tier does not create at all - it composites a CPU lightmap of its own instead
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Lighting Resolution"),
			[this]() -> StringView {
				// The pixel size is the one the buffer will actually have, so it is derived from the CURRENT
				// view size rather than from LevelHandler::DefaultWidth/Height. Those are only the upper bound
				// of the logical view: a display smaller than 720x405 (the PS Vita's 480x272, say) gets a view
				// of its own size instead, and quoting the cap there claims a lighting buffer larger than the
				// whole screen. The menu's view size is computed by the same rule from the same constants as
				// the level's, so it is the same number the level will use
				Vector2i viewSize = _root->GetViewSize();
				_lightingResolutionValue = format("{}% ({}x{})", PreferencesCache::LightingResolutionPercent,
					viewSize.X * PreferencesCache::LightingResolutionPercent / 100,
					viewSize.Y * PreferencesCache::LightingResolutionPercent / 100);
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
#endif
		// Debris and weather particles (see TileMap::CreateDebris): off, about half of every burst, or all of
		// them. Ultra is reserved for a future tier and not offered yet, so the cycle stops at High and a
		// configuration that carries Ultra reads as High.
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Particle Quality"),
			[]() -> StringView {
				switch (PreferencesCache::Particles) {
					case ParticleQuality::Off: return _("Off");
					case ParticleQuality::Low: return _("Low");
					default: return _("High");
				}
			},
			[this](std::int32_t direction) {
				std::int32_t index = std::min((std::int32_t)PreferencesCache::Particles, (std::int32_t)ParticleQuality::High) + direction;
				if (index < (std::int32_t)ParticleQuality::Off) {
					index = (std::int32_t)ParticleQuality::Off;
				} else if (index > (std::int32_t)ParticleQuality::High) {
					index = (std::int32_t)ParticleQuality::High;
				}
				PreferencesCache::Particles = (ParticleQuality)index;
				_isDirty = true;
			});
#if defined(RHI_CAP_POSTPROCESSING)
		// Selects between the two Combine shader variants (see LevelHandler), neither of which the direct
		// tier uses - its water is the per-row tint and wave applied by the device's software compositor
		// TRANSLATORS: Menu item in Options > Graphics section
		list->Add<ChoiceItem>(_("Water Quality"),
			[]() -> StringView { return (PreferencesCache::LowWaterQuality ? _("Low") : _("High")); },
			[this](std::int32_t) {
				PreferencesCache::LowWaterQuality = !PreferencesCache::LowWaterQuality;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
			});
#endif
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
