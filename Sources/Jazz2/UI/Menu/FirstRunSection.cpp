#include "FirstRunSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	// Standard divider line insets, with the frame pushed down to make room for the welcome header
	static constexpr std::int32_t TopLine = 31;
	static constexpr std::int32_t BottomLine = 42;
	static constexpr float HeaderOffset = 66.0f;

	Recti FirstRunSection::GetClipRectangle(const Recti& contentBounds)
	{
		std::int32_t topLine = TopLine + (std::int32_t)HeaderOffset;
		return Recti(contentBounds.X, contentBounds.Y + topLine - 1, contentBounds.W, contentBounds.H - topLine - BottomLine + 2);
	}

	void FirstRunSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		auto list = std::make_unique<ScrollView>();

		struct Preset {
			bool Reforged;
			StringView Name;
			StringView Description;
		};
		Preset presets[] = {
			// TRANSLATORS: Menu item in First Run section
			{ false, _("Legacy"), _("I want to play the game the way it used to be.") },
			// TRANSLATORS: Menu item in First Run section
			{ true, _("Reforged"), _("I want to play the game with something new.") }
		};

		for (const auto& preset : presets) {
			bool reforged = preset.Reforged;
			StringView name = preset.Name;
			StringView description = preset.Description;

			auto* item = list->Add<CanvasWidget>(68.0f);
			item->OnDrawContent = [reforged, name, description](IMenuContainer* r, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation) {
				float centerX = bounds.X + bounds.W * 0.5f;
				float y = bounds.Y + bounds.H * 0.5f;

				if (selected) {
					float size = 0.7f + Easing::OutElastic(animation) * 0.6f;
					r->DrawElement(MenuGlow, 0, centerX, y + 10.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.2f), 22.0f, 12.0f, true, true);
					r->DrawStringShadow(name, charOffset, centerX, y, IMenuContainer::FontLayer + 10,
						Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					r->DrawStringShadow(name, charOffset, centerX, y, IMenuContainer::FontLayer,
						Alignment::Center, reforged ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor, 1.0f);
				}

				r->DrawStringShadow(description, charOffset, centerX, y + 22.0f, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.94f);
			};
			item->OnSelectedChanged = [this, reforged]() {
				// Live-preview the main menu style of the highlighted preset
				bool wasReforged = PreferencesCache::EnableReforgedMainMenu;
				PreferencesCache::EnableReforgedMainMenu = reforged;
				if (reforged != wasReforged) {
					_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
				}
			};
			item->OnActivate = [this, reforged]() {
				PreferencesCache::EnableReforgedGameplay = reforged;
				PreferencesCache::EnableReforgedHUD = reforged;
				PreferencesCache::EnableLedgeClimb = reforged;
				PreferencesCache::Save();
				_root->LeaveSection();
			};
		}

		list->SetSelectedIndex(PreferencesCache::EnableReforgedGameplay ? 1 : 0);

		SetContent(std::move(list));
	}

	void FirstRunSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine + HeaderOffset;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		// TRANSLATORS: Header in First Run section
		_root->DrawStringShadow(_("Welcome to \f[c:#9e7056]Jazz Jackrabbit 2\f[/c] reimplementation!"), charOffset, centerX, topLine - HeaderOffset - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Subheader in First Run section
		_root->DrawStringShadow(_f("You can choose your preferred play style.\nThis option can be changed at any time in \f[c:#707070]{}\f[/c] > \f[c:#707070]{}\f[/c] > \f[c:#707070]{}\f[/c].\nFor more information, visit {} and  Discord!", _("Options"), _("Gameplay"), _("Enhancements"), "\f[c:#707070]https://deat.tk/jazz2/\f[/c]"_s),
			charOffset, centerX, topLine - 40.0f, IMenuContainer::FontLayer - 2, Alignment::Center, Font::DefaultColor,
			0.86f, 0.7f, 0.0f, 0.0f, 0.4f, 0.9f);
	}

	void FirstRunSection::OnBackPressed()
	{
		// Can't go back from here
	}
}
