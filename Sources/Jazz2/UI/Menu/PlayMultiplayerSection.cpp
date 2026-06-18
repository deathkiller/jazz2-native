#include "PlayMultiplayerSection.h"

#if defined(WITH_MULTIPLAYER)

#include "EpisodeSelectSection.h"
#include "ServerSelectSection.h"

#include "../../../nCine/I18n.h"

namespace Jazz2::UI::Menu
{
	void PlayMultiplayerSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Play Online"));

		auto list = std::make_unique<ScrollView>();
		// TRANSLATORS: Menu item in Play Multiplayer section
		list->Add<ListItem>(_("Connect To Server"), [root]() { root->SwitchToSection<ServerSelectSection>(); });
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// TRANSLATORS: Menu item in Play Multiplayer section
		list->Add<ListItem>(_("Create Public Server"), [root]() { root->SwitchToSection<EpisodeSelectSection>(true, false); });
		// TRANSLATORS: Menu item in Play Multiplayer section
		list->Add<ListItem>(_("Create Private Server"), [root]() { root->SwitchToSection<EpisodeSelectSection>(true, true); });
#endif
		SetContent(std::move(list));
	}
}

#endif
