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

		SetTitle(_("Play Multiplayer"));

		auto list = std::make_unique<ScrollView>();
#if defined(WITH_ONLINE_MULTIPLAYER)
		// TRANSLATORS: Menu item in Multiplayer section (join an existing online server)
		list->Add<ListItem>(_("Join Server"), [root]() { root->SwitchToSection<ServerSelectSection>(); });
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// TRANSLATORS: Menu item in Multiplayer section (host a publicly listed online server)
		list->Add<ListItem>(_("Create Public Server"), [root]() { root->SwitchToSection<EpisodeSelectSection>(true, false, false); });
		// TRANSLATORS: Menu item in Multiplayer section (host a private online server)
		list->Add<ListItem>(_("Create Private Server"), [root]() { root->SwitchToSection<EpisodeSelectSection>(true, true, false); });
#	endif
#endif
		// TRANSLATORS: Menu item in Multiplayer section (local splitscreen multiplayer)
		list->Add<ListItem>(_("Create Local Splitscreen Game"), [root]() { root->SwitchToSection<EpisodeSelectSection>(true, false, true); });
		SetContent(std::move(list));
	}
}

#endif
