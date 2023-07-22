#include "GraphicsOptionsSection.h"
#include "RescaleModeSection.h"
#include "../../PreferencesCache.h"
#include "../../LevelHandler.h"
#include "../../../nCine/Application.h"

#include <Environment.h>
#include <Utf8.h>

namespace Jazz2::UI::Menu
{
	GraphicsOptionsSection::GraphicsOptionsSection()
		: _isDirty(false)
	{
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::RescaleMode, _("Rescale Mode") });
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
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
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::LowGraphicsQuality, _("Graphics Quality"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::ShowPlayerTrails, _("Show Player Trails"), true });
		// TRANSLATORS: Menu item in Options > Graphics section
		_items.emplace_back(GraphicsOptionsItem { GraphicsOptionsItemType::KeepAspectRatioInCinematics, _("Keep Aspect Ratio In Cinematics"), true });
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
		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}

	void GraphicsOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement("MenuDim"_s, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Graphics"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void GraphicsOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = (item.Item.HasBooleanValue ? 52 : ItemHeight);
	}

	void GraphicsOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement("MenuGlow"_s, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			if (item.Item.HasBooleanValue) {
				_root->DrawStringShadow("<"_s, charOffset, centerX - 60.0f - 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, centerX + 70.0f + 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}

		if (item.Item.HasBooleanValue) {
			StringView customText;
			bool enabled = false;
			switch (item.Item.Type) {
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
				case GraphicsOptionsItemType::Fullscreen: enabled = PreferencesCache::EnableFullscreen; break;
#endif
				case GraphicsOptionsItemType::Antialiasing: enabled = (PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing; break;
				case GraphicsOptionsItemType::ShowPerformanceMetrics: enabled = PreferencesCache::ShowPerformanceMetrics; break;
				case GraphicsOptionsItemType::KeepAspectRatioInCinematics: enabled = PreferencesCache::KeepAspectRatioInCinematics; break;
				case GraphicsOptionsItemType::ShowPlayerTrails: enabled = PreferencesCache::ShowPlayerTrails; break;
				case GraphicsOptionsItemType::LowGraphicsQuality: enabled = PreferencesCache::LowGraphicsQuality; customText = (enabled ? _("Low") : _("High")); break;
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
					theApplication().gfxDevice().setResolution(true);
					theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
				} else {
					theApplication().gfxDevice().setResolution(false);
					theApplication().inputManager().setCursor(IInputManager::Cursor::Arrow);
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
			case GraphicsOptionsItemType::ShowPerformanceMetrics:
				PreferencesCache::ShowPerformanceMetrics = !PreferencesCache::ShowPerformanceMetrics;
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
			case GraphicsOptionsItemType::ShowPlayerTrails:
				PreferencesCache::ShowPlayerTrails = !PreferencesCache::ShowPlayerTrails;
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
			case GraphicsOptionsItemType::LowGraphicsQuality:
				PreferencesCache::LowGraphicsQuality = !PreferencesCache::LowGraphicsQuality;
				_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
				_isDirty = true;
				_animation = 0.0f;
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				break;
		}
	}
}