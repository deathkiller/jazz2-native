#include "MultiplayerGameModeSelectSection.h"

#if defined(WITH_MULTIPLAYER)

#include "CreateServerOptionsSection.h"
#include "CreateLocalGameOptionsSection.h"
#include "../../Multiplayer/MpGameMode.h"

#include "../../../nCine/I18n.h"

#include <algorithm>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Multiplayer;

namespace Jazz2::UI::Menu
{
	Recti MultiplayerGameModeSelectSection::GetClipRectangle(const Recti& contentBounds)
	{
		std::int32_t bottomLine = BottomLine + (ShowsDescription() ? (std::int32_t)DescriptionHeight : 0);
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - bottomLine + 2);
	}

	void MultiplayerGameModeSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		// TRANSLATORS: Header of Select Game Mode section, opened from the Game Mode item when creating a multiplayer game
		SetTitle(_("Select Game Mode"));

		MpGameMode currentMode = MpGameMode::Battle;
		MenuSection* underlying = root->GetUnderlyingSection();
		if (auto* serverSection = runtime_cast<CreateServerOptionsSection>(underlying)) {
			currentMode = serverSection->GetGameMode();
		} else if (auto* localSection = runtime_cast<CreateLocalGameOptionsSection>(underlying)) {
			currentMode = localSection->GetGameMode();
		}

		auto list = std::make_unique<ScrollView>();
		std::int32_t selectedIndex = 0;

		auto add = [&](MpGameMode mode, StringView name) {
			if (mode == currentMode) {
				selectedIndex = (std::int32_t)_modes.size();
			}
			_modes.push_back(mode);
			list->Add<ListItem>(name, [root, mode]() {
				MenuSection* underlying = root->GetUnderlyingSection();
				if (auto* serverSection = runtime_cast<CreateServerOptionsSection>(underlying)) {
					serverSection->SetGameMode(mode);
				} else if (auto* localSection = runtime_cast<CreateLocalGameOptionsSection>(underlying)) {
					localSection->SetGameMode(mode);
				}
				root->LeaveSection();
			});
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
		// TRANSLATORS: Menu item in Select Game Mode section (only the player who is "it" can be roasted and scores)
		add(MpGameMode::RoastTag, _("Roast Tag"));
		// TRANSLATORS: Menu item in Select Game Mode section (every player has limited lives, the last one alive wins)
		add(MpGameMode::LastRabbitStanding, _("Last Rabbit Standing"));
		// TRANSLATORS: Menu item in Select Game Mode section (Last Rabbit Standing where roasts also score points)
		add(MpGameMode::ExtendedLastRabbitStanding, _("Extended Last Rabbit Standing"));
		// TRANSLATORS: Menu item in Select Game Mode section (roasted players become zombies that infect the others)
		add(MpGameMode::Pestilence, _("Pestilence"));
		// TRANSLATORS: Menu item in Select Game Mode section (every team has limited lives, the last team alive wins)
		add(MpGameMode::TeamLastRabbitStanding, _("Team Last Rabbit Standing"));
		// TRANSLATORS: Menu item in Select Game Mode section (roasted players go to jail, teammates free them)
		add(MpGameMode::Jailbreak, _("Jailbreak"));
		// TRANSLATORS: Menu item in Select Game Mode section (Capture The Flag where roasts also score points)
		add(MpGameMode::DeathCaptureTheFlag, _("Death Capture The Flag"));
		// TRANSLATORS: Menu item in Select Game Mode section (a single flag has to be carried to the enemy base)
		add(MpGameMode::FlagRun, _("Flag Run"));
		// TRANSLATORS: Menu item in Select Game Mode section (teams score by holding control points)
		add(MpGameMode::Domination, _("Domination"));
		// TRANSLATORS: Menu item in Select Game Mode section (roasted players drop gems that are brought to the own base)
		add(MpGameMode::HeadHunters, _("Head Hunters"));

		list->SetSelectedIndex(selectedIndex);
		SetContent(Death::move(list));
	}

	void MultiplayerGameModeSelectSection::OnDraw(Canvas* canvas)
	{
		if (!ShowsDescription()) {
			WidgetSection::OnDraw(canvas);
			return;
		}

		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine - DescriptionHeight;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		if (!_title.empty()) {
			_root->DrawMenuTitle(charOffset, _title, centerX, topLine);
		}

		auto* list = static_cast<ListContainer*>(GetContent());
		std::int32_t index = (list != nullptr ? list->GetSelectedIndex() : -1);
		if (index < 0 || index >= (std::int32_t)_modes.size()) {
			return;
		}

		float maxWidth = std::min((float)contentBounds.W - 60.0f, 520.0f);
		if (index != _wrappedIndex || maxWidth != _wrappedWidth) {
			_wrappedIndex = index;
			_wrappedWidth = maxWidth;
			_wrappedDescription = WrapText(GetDescription(_modes[index]), DescriptionScale, maxWidth);
		}

		_root->DrawStringShadow(_wrappedDescription, charOffset, centerX, bottomLine + 10.0f + DescriptionHeight * 0.5f,
			IMenuContainer::FontLayer, Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), DescriptionScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.9f);
	}

	bool MultiplayerGameModeSelectSection::ShowsDescription() const
	{
		// The list would be too short on smaller views
		return (_root != nullptr && _root->GetViewSize().Y >= DescriptionMinViewHeight);
	}

	StringView MultiplayerGameModeSelectSection::GetDescription(MpGameMode mode)
	{
		switch (mode) {
			// TRANSLATORS: Description of Battle game mode in Select Game Mode section
			case MpGameMode::Battle: return _("Roast the other players. Each roast is a point.");
			// TRANSLATORS: Description of Team Battle game mode in Select Game Mode section
			case MpGameMode::TeamBattle: return _("Battle in teams. The roasts of all team members add up.");
			// TRANSLATORS: Description of Race game mode in Select Game Mode section
			case MpGameMode::Race: return _("Be the first to finish all the laps.");
			// TRANSLATORS: Description of Team Race game mode in Select Game Mode section
			case MpGameMode::TeamRace: return _("Race in teams. A team wins when all its players have finished.");
			// TRANSLATORS: Description of Treasure Hunt game mode in Select Game Mode section
			case MpGameMode::TreasureHunt: return _("Collect gems, then reach the exit with enough treasure. Getting hurt costs some of it.");
			// TRANSLATORS: Description of Team Treasure Hunt game mode in Select Game Mode section
			case MpGameMode::TeamTreasureHunt: return _("Treasure Hunt in teams. The treasure of all team members adds up.");
			// TRANSLATORS: Description of Capture The Flag game mode in Select Game Mode section
			case MpGameMode::CaptureTheFlag: return _("Steal the enemy flag and bring it to your own base.");
			// TRANSLATORS: Description of Cooperation game mode in Select Game Mode section
			case MpGameMode::Cooperation: return _("Play the story levels together. Reaching the exit takes everyone to the next level.");
			// TRANSLATORS: Description of Roast Tag game mode in Select Game Mode section
			case MpGameMode::RoastTag: return _("The first roast wins the ring. Then only the ring holder and the player with the fewest points can roast or be roasted.");
			// TRANSLATORS: Description of Last Rabbit Standing game mode in Select Game Mode section
			case MpGameMode::LastRabbitStanding: return _("Everyone has a few lives. Lose them all and you're out. The last player left wins.");
			// TRANSLATORS: Description of Extended Last Rabbit Standing game mode in Select Game Mode section
			case MpGameMode::ExtendedLastRabbitStanding: return _("Last Rabbit Standing where every roast is a point. The last three players left get bonus points.");
			// TRANSLATORS: Description of Pestilence game mode in Select Game Mode section
			case MpGameMode::Pestilence: return _("Whoever dies becomes a zombie. Zombies and humans can only hurt each other. The last human gets bonus points.");
			// TRANSLATORS: Description of Team Last Rabbit Standing game mode in Select Game Mode section
			case MpGameMode::TeamLastRabbitStanding: return _("Every team shares a pool of lives. The last team with lives left wins.");
			// TRANSLATORS: Description of Jailbreak game mode in Select Game Mode section
			case MpGameMode::Jailbreak: return _("Roasted players go to jail and teammates free them. Put the whole enemy team in jail to score.");
			// TRANSLATORS: Description of Death Capture The Flag game mode in Select Game Mode section
			case MpGameMode::DeathCaptureTheFlag: return _("Capture The Flag where every roast scores too. After a capture, everybody starts over.");
			// TRANSLATORS: Description of Flag Run game mode in Select Game Mode section
			case MpGameMode::FlagRun: return _("Pick up the flag at a capture point and carry it into an enemy base before the time runs out.");
			// TRANSLATORS: Description of Domination game mode in Select Game Mode section
			case MpGameMode::Domination: return _("Touch control points to capture them. Every point your team holds keeps scoring.");
			// TRANSLATORS: Description of Head Hunters game mode in Select Game Mode section
			case MpGameMode::HeadHunters: return _("Roasted players drop their gems. Bring at least 2 gems to your base, more at once score much more.");
			default: return {};
		}
	}

	String MultiplayerGameModeSelectSection::WrapText(StringView text, float scale, float maxWidth)
	{
		// Breaks the text into lines at spaces, so every line fits the width
		String result;
		StringView line;
		StringView rest = text;
		while (!rest.empty()) {
			auto parts = rest.partition(' ');
			StringView word = parts[0];
			rest = parts[2];
			if (word.empty()) {
				continue;
			}
			if (line.empty()) {
				line = word;
				continue;
			}
			StringView extended = StringView(line.data(), (std::size_t)(word.end() - line.data()));
			if (_root->MeasureString(extended, scale).X <= maxWidth) {
				line = extended;
			} else {
				result = (result.empty() ? String(line) : result + "\n"_s + line);
				line = word;
			}
		}
		if (!line.empty()) {
			result = (result.empty() ? String(line) : result + "\n"_s + line);
		}
		return result;
	}
}

#endif
