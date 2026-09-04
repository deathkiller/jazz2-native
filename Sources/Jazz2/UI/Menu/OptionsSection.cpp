#include "OptionsSection.h"
#include "GameplayOptionsSection.h"
#include "GraphicsOptionsSection.h"
#include "SoundsOptionsSection.h"
#include "ControlsOptionsSection.h"
#include "UserProfileOptionsSection.h"

#include "../../../nCine/I18n.h"

namespace Jazz2::UI::Menu
{
	void OptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		// Build the content only on the first show, so the selection is preserved when returning from a sub-section
		// (a fresh instance is created on language change, which rebuilds with the new strings)
		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Options"));

		// A ScrollView (rather than a plain centered StackLayout) so the list keeps edge padding and scrolls instead of
		// cropping the first/last item when the window is short enough that the items only just fit
		auto list = std::make_unique<ScrollView>();
		const float itemHeight = GetItemHeight(root);
		_items.clear();
		// TRANSLATORS: Menu item in Options section
		_items.push_back(list->Add<ListItem>(_("Gameplay"), [root]() { root->SwitchToSection<GameplayOptionsSection>(); }, itemHeight));
		// TRANSLATORS: Menu item in Options section
		_items.push_back(list->Add<ListItem>(_("Graphics"), [root]() { root->SwitchToSection<GraphicsOptionsSection>(); }, itemHeight));
#if defined(WITH_AUDIO)
		// TRANSLATORS: Menu item in Options section
		_items.push_back(list->Add<ListItem>(_("Sounds"), [root]() { root->SwitchToSection<SoundsOptionsSection>(); }, itemHeight));
#endif
		// TRANSLATORS: Menu item in Options section
		_items.push_back(list->Add<ListItem>(_("Controls"), [root]() { root->SwitchToSection<ControlsOptionsSection>(); }, itemHeight));
		// TRANSLATORS: Menu item in Options section
		_items.push_back(list->Add<ListItem>(_("User Profile"), [root]() { root->SwitchToSection<UserProfileOptionsSection>(); }, itemHeight));
		SetContent(std::move(list));
	}

	void OptionsSection::OnLayoutChanged(IMenuContainer* root)
	{
		// The row height was decided for the view the section was built in (see GetItemHeight()); the rows
		// themselves stay, with their selection
		const float itemHeight = GetItemHeight(root);
		for (ListItem* item : _items) {
			item->Height = itemHeight;
		}
	}

	float OptionsSection::GetItemHeight(IMenuContainer* root)
	{
		// Five plain rows at the default spacing fill a handheld's view with air, so the rows tighten up on a
		// compact view (see MenuLayout) and keep the default height on a full one
		return MenuLayout::Blend(32.0f, 40.0f, root->GetViewSize());
	}
}
