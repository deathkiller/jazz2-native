#include "MultiplayerGameModeSelectSection.h"

#if defined(WITH_MULTIPLAYER)

#include "CreateServerOptionsSection.h"
#include "CreateLocalGameOptionsSection.h"
#include "../../Multiplayer/MpGameMode.h"

#include "../../../nCine/I18n.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::UI::Menu
{
	void MultiplayerGameModeSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Select Game Mode"));

		MpGameMode currentMode = MpGameMode::Battle;
		MenuSection* underlying = root->GetUnderlyingSection();
		if (auto* serverSection = dynamic_cast<CreateServerOptionsSection*>(underlying)) {
			currentMode = serverSection->GetGameMode();
		} else if (auto* localSection = dynamic_cast<CreateLocalGameOptionsSection*>(underlying)) {
			currentMode = localSection->GetGameMode();
		}

		auto list = std::make_unique<ScrollView>();
		std::int32_t selectedIndex = 0;
		std::int32_t index = 0;

		auto add = [&](MpGameMode mode, StringView name) {
			if (mode == currentMode) {
				selectedIndex = index;
			}
			list->Add<ListItem>(name, [root, mode]() {
				MenuSection* underlying = root->GetUnderlyingSection();
				if (auto* serverSection = dynamic_cast<CreateServerOptionsSection*>(underlying)) {
					serverSection->SetGameMode(mode);
				} else if (auto* localSection = dynamic_cast<CreateLocalGameOptionsSection*>(underlying)) {
					localSection->SetGameMode(mode);
				}
				root->LeaveSection();
			});
			index++;
		};

		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::Battle, _("Battle"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::TeamBattle, _("Team Battle"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::Race, _("Race"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::TeamRace, _("Team Race"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::TreasureHunt, _("Treasure Hunt"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::TeamTreasureHunt, _("Team Treasure Hunt"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::CaptureTheFlag, _("Capture The Flag"));
		// TRANSLATORS: Menu item in Select Game Mode section
		add(MpGameMode::Cooperation, _("Cooperation"));

		list->SetSelectedIndex(selectedIndex);
		SetContent(std::move(list));
	}
}

#endif
