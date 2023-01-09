#include "RescaleModeSection.h"

namespace Jazz2::UI::Menu
{
	RescaleModeSection::RescaleModeSection()
	{
		int currentMode = (int)(PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask);
		for (int i = 0; i <= (int)RescaleMode::Monochrome; i++) {
			if (currentMode == _items.size()) {
				_selectedIndex = currentMode;
			}

			auto& item = _items.emplace_back();
			item.Item.Mode = (RescaleMode)i;
			switch (item.Item.Mode) {
				case RescaleMode::None: item.Item.DisplayName = _("None / Pixel-perfect"); break;
				case RescaleMode::HQ2x: item.Item.DisplayName = "HQ2x"_s; break;
				case RescaleMode::_3xBrz: item.Item.DisplayName = "3xBRZ"_s; break;
				case RescaleMode::CrtScanlines: item.Item.DisplayName = "CRT Scanlines"_s; break;
				case RescaleMode::CrtShadowMask: item.Item.DisplayName = "CRT Shadow Mask"_s; break;
				case RescaleMode::CrtApertureGrille: item.Item.DisplayName = "CRT Aperture Grille"_s; break;
				case RescaleMode::Monochrome: item.Item.DisplayName = _("Monochrome"); break;
			}
		}
	}

	void RescaleModeSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement("MenuDim"_s, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int charOffset = 0;
		_root->DrawStringShadow(_("Select Rescale Mode"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void RescaleModeSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement("MenuGlow"_s, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (item.Item.DisplayName.size() + 3) * 0.5f * size, 4.0f * size, true);

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