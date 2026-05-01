#include "GraphicsOptionsSection.h"
#include "MenuResources.h"
#include "RescaleModeSection.h"
#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#include <Environment.h>
#include <Utf8.h>
#include <Containers/StringConcatenable.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GraphicsOptionsSection::GraphicsOptionsSection()
		: _isDirty(false)
	{
#if defined(RHI_CAP_SHADERS)
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::RescaleMode, _("Rescale Mode") });
#endif
#if defined(NCINE_HAS_WINDOWS)
#	if defined(DEATH_TARGET_WINDOWS_RT)
		// Xbox is always fullscreen
		if (Environment::CurrentDeviceType != DeviceType::Xbox)
#	endif
		{
			// TRANSLATORS: Menu item in Options > Graphics section
			_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::Fullscreen, _("Fullscreen"), true });
		}
#endif
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::ScreenResolution, _("Screen Resolution") });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::RenderingResolution, _("Rendering Resolution"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::LightingResolution, _("Lighting Resolution"), true });
#if defined(RHI_CAP_SHADERS)
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::Antialiasing, _("Antialiasing"), true });
#endif
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::BackgroundDithering, _("Background Dithering"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::BlurEffects, _("Blur Effects"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::LowWaterQuality, _("Water Quality"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::ShowPlayerTrails, _("Show Player Trails"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::PreferVerticalSplitscreen, _("Preferred Splitscreen"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::PreferZoomOut, _("Prefer Zoom Out"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::KeepAspectRatioInCinematics, _("Keep Aspect Ratio In Cinematics"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::UnalignedViewport, _("Unaligned Viewport"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::ShowPerformanceMetrics, _("Performance Metrics"), true });
	}

	GraphicsOptionsSection::~GraphicsOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GraphicsOptionsSection::OnHandleInput()
	{
		if (!_items.empty()) {
			auto& currentItem = _items[_selectedIndex].Item;
			if (currentItem.Type == GraphicsOptionsItemType::RenderingResolution ||
				currentItem.Type == GraphicsOptionsItemType::LightingResolution) {
				bool isLeft = _root->ActionHit(PlayerAction::Left);
				bool isRight = _root->ActionHit(PlayerAction::Right);
				if (isLeft || isRight) {
					bool changed = false;
					if (currentItem.Type == GraphicsOptionsItemType::RenderingResolution) {
						auto& pct = PreferencesCache::RenderingResolutionPercent;
						if (isLeft) {
							if (pct > 85) { pct = 85; changed = true; }
							else if (pct > 70) { pct = 70; changed = true; }
							else if (pct > 60) { pct = 60; changed = true; }
						} else {
							if (pct < 70) { pct = 70; changed = true; }
							else if (pct < 85) { pct = 85; changed = true; }
							else if (pct < 100) { pct = 100; changed = true; }
						}
					} else {
						auto& pct = PreferencesCache::LightingResolutionPercent;
						if (isLeft) {
							if (pct > 75) { pct = 75; changed = true; }
							else if (pct > 50) { pct = 50; changed = true; }
							else if (pct > 25) { pct = 25; changed = true; }
							else if (pct > 12) { pct = 12; changed = true; }
						} else {
							if (pct < 25) { pct = 25; changed = true; }
							else if (pct < 50) { pct = 50; changed = true; }
							else if (pct < 75) { pct = 75; changed = true; }
							else if (pct < 100) { pct = 100; changed = true; }
						}
					}
					if (changed) {
						_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
						_isDirty = true;
						_animation = 0.0f;
						_root->PlaySfx("MenuSelect"_s, 0.6f);
					}
					return;
				}
			} else if (currentItem.HasBooleanValue && (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right))) {
				OnExecuteSelected();
				return;
			}
		}
		ScrollableMenuSection::OnHandleInput();
	}

	void GraphicsOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i view = canvas->ViewSize;

		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		
		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Graphics"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// Performance Metrics
		if (PreferencesCache::ShowPerformanceMetrics) {
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

	void GraphicsOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = (item.Item.HasBooleanValue || item.Item.Type == GraphicsOptionsItemType::ScreenResolution ? 52 : ItemHeight);
	}

	void GraphicsOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawStringGlow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
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

		if (item.Item.Type == GraphicsOptionsItemType::ScreenResolution) {
			Vector2i res = theApplication().GetGfxDevice().drawableResolution();
			char customText[64];
			std::size_t length = formatInto(customText, "{}x{}", res.X, res.Y);
			_root->DrawStringShadow({ customText, length }, charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
		} else if (item.Item.HasBooleanValue) {
			StringView customText; String customTextBuffer;
			bool enabled = false;
			switch (item.Item.Type) {
#if defined(NCINE_HAS_WINDOWS)
				case GraphicsOptionsItemType::Fullscreen: enabled = PreferencesCache::EnableFullscreen; break;
#endif
				case GraphicsOptionsItemType::RenderingResolution:
					customTextBuffer = format("{}% ({}x{})", PreferencesCache::RenderingResolutionPercent,
						LevelHandler::DefaultWidth * PreferencesCache::RenderingResolutionPercent / 100,
						LevelHandler::DefaultHeight * PreferencesCache::RenderingResolutionPercent / 100);
					customText = customTextBuffer;
					break;
				case GraphicsOptionsItemType::LightingResolution:
					customTextBuffer = format("{}% ({}x{})", PreferencesCache::LightingResolutionPercent,
						LevelHandler::DefaultWidth * PreferencesCache::RenderingResolutionPercent * PreferencesCache::LightingResolutionPercent / (100 * 100),
						LevelHandler::DefaultHeight * PreferencesCache::RenderingResolutionPercent * PreferencesCache::LightingResolutionPercent / (100 * 100));
					customText = customTextBuffer;
					break;
#if defined(RHI_CAP_SHADERS)
				case GraphicsOptionsItemType::Antialiasing: enabled = (PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing; break;
#endif
				case GraphicsOptionsItemType::BackgroundDithering: enabled = PreferencesCache::BackgroundDithering; break;
				case GraphicsOptionsItemType::BlurEffects:
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
					enabled = PreferencesCache::BlurEffects;
#else
					enabled = false; customTextBuffer = format("{} \f[c:#d0705d]({})", _("Disabled"), _("Forced")); customText = customTextBuffer;
#endif		
					break;
				case GraphicsOptionsItemType::LowWaterQuality:
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
					enabled = PreferencesCache::LowWaterQuality; customText = (enabled ? _("Low") : _("High"));
#else
					enabled = true; customTextBuffer = format("{} \f[c:#d0705d]({})", _("Low"), _("Forced")); customText = customTextBuffer;
#endif
					break;
				case GraphicsOptionsItemType::ShowPlayerTrails: enabled = PreferencesCache::ShowPlayerTrails; break;
				case GraphicsOptionsItemType::PreferVerticalSplitscreen: enabled = PreferencesCache::PreferVerticalSplitscreen; customText = (enabled ? _("Vertical") : _("Horizontal"));  break;
				case GraphicsOptionsItemType::PreferZoomOut: enabled = PreferencesCache::PreferZoomOut; break;
				case GraphicsOptionsItemType::KeepAspectRatioInCinematics: enabled = PreferencesCache::KeepAspectRatioInCinematics; break;
				case GraphicsOptionsItemType::UnalignedViewport:
					enabled = PreferencesCache::UnalignedViewport; 
					if (enabled) {
						customTextBuffer = format("{} \f[c:#d0705d]({})", _("Enabled"), _("Experimental")); customText = customTextBuffer;
					}
					break;
				case GraphicsOptionsItemType::ShowPerformanceMetrics:
					enabled = PreferencesCache::ShowPerformanceMetrics;
					// TODO
					// TRANSLATORS: Reserved for later use
					auto TBD = (true ? _("Short") : _("Detailed"));
					break;
			}

			_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
		}
	}

	void GraphicsOptionsSection::OnExecuteSelected()
	{
		switch (_items[_selectedIndex].Item.Type) {
#if defined(RHI_CAP_SHADERS)
			case GraphicsOptionsItemType::RescaleMode:
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->SwitchToSection<RescaleModeSection>();
				break;
#endif
#if defined(NCINE_HAS_WINDOWS)
			case GraphicsOptionsItemType::Fullscreen:
#	if defined(DEATH_TARGET_WINDOWS_RT)
				// Xbox is always fullscreen
				if (Environment::CurrentDeviceType == DeviceType::Xbox) {
					return;
				}
#	endif
				PreferencesCache::EnableFullscreen = !PreferencesCache::EnableFullscreen;
				if (PreferencesCache::EnableFullscreen) {
					theApplication().GetGfxDevice().setResolution(true);
					theApplication().GetInputManager().setCursor(IInputManager::Cursor::Hidden);
				} else {
					theApplication().GetGfxDevice().setResolution(false);
					theApplication().GetInputManager().setCursor(IInputManager::Cursor::Arrow);
				}
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
#endif
			case GraphicsOptionsItemType::RenderingResolution:
				if (PreferencesCache::RenderingResolutionPercent >= 100) {
					PreferencesCache::RenderingResolutionPercent = 85;
				} else if (PreferencesCache::RenderingResolutionPercent >= 85) {
					PreferencesCache::RenderingResolutionPercent = 70;
				} else if (PreferencesCache::RenderingResolutionPercent >= 70) {
					PreferencesCache::RenderingResolutionPercent = 60;
				} else {
					PreferencesCache::RenderingResolutionPercent = 100;
				}
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::LightingResolution:
				if (PreferencesCache::LightingResolutionPercent >= 100) {
					PreferencesCache::LightingResolutionPercent = 75;
				} else if (PreferencesCache::LightingResolutionPercent >= 75) {
					PreferencesCache::LightingResolutionPercent = 50;
				} else if (PreferencesCache::LightingResolutionPercent >= 50) {
					PreferencesCache::LightingResolutionPercent = 25;
				} else if (PreferencesCache::LightingResolutionPercent >= 25) {
					PreferencesCache::LightingResolutionPercent = 12;
				} else {
					PreferencesCache::LightingResolutionPercent = 100;
				}
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;

#if defined(RHI_CAP_SHADERS)
			case GraphicsOptionsItemType::Antialiasing: {
				RescaleMode newMode = (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);
				if ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) != RescaleMode::UseAntialiasing) {
					newMode |= RescaleMode::UseAntialiasing;
				}
				PreferencesCache::ActiveRescaleMode = newMode;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			}
#endif
			case GraphicsOptionsItemType::BackgroundDithering:
				PreferencesCache::BackgroundDithering = !PreferencesCache::BackgroundDithering;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
			case GraphicsOptionsItemType::BlurEffects:
				PreferencesCache::BlurEffects = !PreferencesCache::BlurEffects;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
#endif
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
			case GraphicsOptionsItemType::LowWaterQuality:
				PreferencesCache::LowWaterQuality = !PreferencesCache::LowWaterQuality;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
#endif
			case GraphicsOptionsItemType::ShowPlayerTrails:
				PreferencesCache::ShowPlayerTrails = !PreferencesCache::ShowPlayerTrails;
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::PreferVerticalSplitscreen:
				PreferencesCache::PreferVerticalSplitscreen = !PreferencesCache::PreferVerticalSplitscreen;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::PreferZoomOut:
				PreferencesCache::PreferZoomOut = !PreferencesCache::PreferZoomOut;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::KeepAspectRatioInCinematics:
				PreferencesCache::KeepAspectRatioInCinematics = !PreferencesCache::KeepAspectRatioInCinematics;
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::UnalignedViewport:
				PreferencesCache::UnalignedViewport = !PreferencesCache::UnalignedViewport;
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::ShowPerformanceMetrics:
				PreferencesCache::ShowPerformanceMetrics = !PreferencesCache::ShowPerformanceMetrics;
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
		}
	}
}