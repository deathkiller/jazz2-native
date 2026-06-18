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
		// TRANSLATORS: Menu item in Options section
		list->Add<ListItem>(_("Gameplay"), [root]() { root->SwitchToSection<GameplayOptionsSection>(); });
		// TRANSLATORS: Menu item in Options section
		list->Add<ListItem>(_("Graphics"), [root]() { root->SwitchToSection<GraphicsOptionsSection>(); });
#if defined(WITH_AUDIO)
		// TRANSLATORS: Menu item in Options section
		list->Add<ListItem>(_("Sounds"), [root]() { root->SwitchToSection<SoundsOptionsSection>(); });
#endif
		// TRANSLATORS: Menu item in Options section
		list->Add<ListItem>(_("Controls"), [root]() { root->SwitchToSection<ControlsOptionsSection>(); });
		// TRANSLATORS: Menu item in Options section
		list->Add<ListItem>(_("User Profile"), [root]() { root->SwitchToSection<UserProfileOptionsSection>(); });
		SetContent(std::move(list));
	}
}
