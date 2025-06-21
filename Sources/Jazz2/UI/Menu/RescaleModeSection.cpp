#include "RescaleModeSection.h"
#include "MenuResources.h"

#include "../../../nCine/I18n.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	RescaleModeSection::RescaleModeSection()
	{
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		_items.emplace_back(RescaleModeItem { RescaleMode::None, _("None / Pixel-perfect") });

		_items.emplace_back(RescaleModeItem { RescaleMode::CleanEdge, "CleanEdge"_s });
		_items.emplace_back(RescaleModeItem { RescaleMode::HQ2x, "HQ2×"_s });
		_items.emplace_back(RescaleModeItem { RescaleMode::_3xBrz, "3×BRZ"_s });
		_items.emplace_back(RescaleModeItem { RescaleMode::Sabr, "SABR"_s });

		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		_items.emplace_back(RescaleModeItem { RescaleMode::CrtScanlines, _("CRT Scanlines") });
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		_items.emplace_back(RescaleModeItem { RescaleMode::CrtShadowMask, _("CRT Shadow Mask") });
		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		_items.emplace_back(RescaleModeItem { RescaleMode::CrtApertureGrille, _("CRT Aperture Grille") });

		// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
		_items.emplace_back(RescaleModeItem { RescaleMode::Monochrome, _("Monochrome") });

		RescaleMode currentMode = (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);

		for (std::size_t i = 0; i < _items.size(); i++) {
			if (_items[i].Item.Mode == currentMode) {
				_selectedIndex = static_cast<std::int32_t>(i);
				break;
			}
		}
	}

	void RescaleModeSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Select Rescale Mode"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void RescaleModeSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}
	}

	void RescaleModeSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		RescaleMode newMode = _items[_selectedIndex].Item.Mode;
		if ((PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask) != newMode) {
			PreferencesCache::ActiveRescaleMode = newMode | (PreferencesCache::ActiveRescaleMode & ~RescaleMode::TypeMask);
			if (newMode == RescaleMode::CrtScanlines || newMode == RescaleMode::CrtShadowMask || newMode == RescaleMode::CrtApertureGrille) {
				// Turn off Antialiasing when using CRT modes
				PreferencesCache::ActiveRescaleMode &= ~RescaleMode::UseAntialiasing;
			}
			PreferencesCache::Save();
			_root->ApplyPreferencesChanges(ChangedPreferencesType::Graphics);
		}
		
		_root->LeaveSection();
	}
}