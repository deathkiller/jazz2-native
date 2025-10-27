﻿#include "GraphicsOptionsSection.h"
#include "MenuResources.h"
#include "RescaleModeSection.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#include <Environment.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GraphicsOptionsSection::GraphicsOptionsSection()
		: _isDirty(false)
	{
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::RescaleMode, _("Rescale Mode") });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::Resolution, _("Resolution") });
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
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::Antialiasing, _("Antialiasing"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::BackgroundDithering, _("Background Dithering"), true });
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
		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
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
		item.Height = (item.Item.HasBooleanValue || item.Item.Type == GraphicsOptionsItemType::Resolution ? 52 : ItemHeight);
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

		if (item.Item.Type == GraphicsOptionsItemType::Resolution) {
			Vector2i res = theApplication().GetGfxDevice().drawableResolution();
			char customText[64];
			std::size_t length = formatInto(customText, "{}x{}", res.X, res.Y);
			_root->DrawStringShadow({ customText, length }, charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
		} else if (item.Item.HasBooleanValue) {
			StringView customText;
			bool enabled = false;
			switch (item.Item.Type) {
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
				case GraphicsOptionsItemType::Fullscreen: enabled = PreferencesCache::EnableFullscreen; break;
#endif
				case GraphicsOptionsItemType::Antialiasing: enabled = (PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing; break;
				case GraphicsOptionsItemType::BackgroundDithering: enabled = PreferencesCache::BackgroundDithering; break;
				case GraphicsOptionsItemType::LowWaterQuality: enabled = PreferencesCache::LowWaterQuality; customText = (enabled ? _("Low") : _("High")); break;
				case GraphicsOptionsItemType::ShowPlayerTrails: enabled = PreferencesCache::ShowPlayerTrails; break;
				case GraphicsOptionsItemType::PreferVerticalSplitscreen: enabled = PreferencesCache::PreferVerticalSplitscreen; customText = (enabled ? _("Vertical") : _("Horizontal"));  break;
				case GraphicsOptionsItemType::PreferZoomOut: enabled = PreferencesCache::PreferZoomOut; break;
				case GraphicsOptionsItemType::KeepAspectRatioInCinematics: enabled = PreferencesCache::KeepAspectRatioInCinematics; break;
				case GraphicsOptionsItemType::UnalignedViewport: enabled = PreferencesCache::UnalignedViewport; customText = (enabled ? _("Enabled \f[c:#d0705d](Experimental)\f[/c]") : _("Disabled")); break;
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
			case GraphicsOptionsItemType::RescaleMode:
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->SwitchToSection<RescaleModeSection>();
				break;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
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
			case GraphicsOptionsItemType::BackgroundDithering:
				PreferencesCache::BackgroundDithering = !PreferencesCache::BackgroundDithering;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::LowWaterQuality:
				PreferencesCache::LowWaterQuality = !PreferencesCache::LowWaterQuality;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
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