#include "RescaleModeSection.h"
#include "MenuResources.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	RescaleModeSection::RescaleModeSection()
	{
		std::int32_t currentMode = (std::int32_t)(PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);
		for (std::int32_t i = 0; i <= (std::int32_t)RescaleMode::Monochrome; i++) {
			if (currentMode == _items.size()) {
				_selectedIndex = currentMode;
			}

			auto& item = _items.emplace_back();
			item.Item.Mode = (RescaleMode)i;
			switch (item.Item.Mode) {
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::None: item.Item.DisplayName = _("None / Pixel-perfect"); break;
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::HQ2x: item.Item.DisplayName = "HQ2×"_s; break;
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::_3xBrz: item.Item.DisplayName = "3×BRZ"_s; break;

				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::CrtScanlines: item.Item.DisplayName = _("CRT Scanlines"); break;
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::CrtShadowMask: item.Item.DisplayName = _("CRT Shadow Mask"); break;
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::CrtApertureGrille: item.Item.DisplayName = _("CRT Aperture Grille"); break;
				// TRANSLATORS: Menu item in Options > Graphics > Rescale Mode section
				case RescaleMode::Monochrome: item.Item.DisplayName = _("Monochrome"); break;
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