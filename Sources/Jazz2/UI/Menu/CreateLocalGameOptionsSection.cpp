#include "CreateLocalGameOptionsSection.h"

#if defined(WITH_MULTIPLAYER)

#include "MainMenu.h"
#include "MenuResources.h"
#include "MultiplayerGameModeSelectSection.h"
#include "../Font.h"
#include "../../PreferencesCache.h"
#include "../../LevelInitialization.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Jazz2::Multiplayer;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	namespace
	{
		// One focusable character row per player: shows "Player N" and the Jazz/Spaz/Lori options in a single row,
		// highlighting the selected one (like StartGameOptionsSection) with animated arrows. Left/Right changes this
		// player's character. Operates on the section's state through pointers, so the portrait and the launched game
		// reflect the selection. Rows are shown/hidden by the player count, so the character area resizes accordingly.
		class PlayerCharacterWidget : public Widget
		{
		public:
			std::int32_t PlayerIndex = 0;
			std::int32_t* PlayerType = nullptr;		// &section._playerTypes[PlayerIndex]
			std::int32_t* FocusedPlayer = nullptr;	// &section._currentPlayer (updated when this row is selected)
			std::int32_t AvailableCharacters = 3;
			AnimatedValue Animation;
			float RowHeight = 32.0f;

			PlayerCharacterWidget() {
				Focusable = true;
			}

			float GetHeight() const override {
				return RowHeight;
			}

			void OnUpdate(float timeMult) override {
				if (Selected) {
					Animation.Update(timeMult);
				}
			}

			void OnSelected() override {
				Animation.Restart(0.0f);
				if (FocusedPlayer != nullptr) {
					*FocusedPlayer = PlayerIndex;
				}
			}

			// On-screen x of character option `j` within the current row bounds; shared by Draw and touch hit-testing
			// so the tappable areas always match what's drawn
			float GetCharacterX(std::int32_t j) const {
				float centerX = Bounds.X + Bounds.W * 0.5f;
				float offset, spacing;
				if (AvailableCharacters <= 1) {
					offset = 0.0f;
					spacing = 0.0f;
				} else if (AvailableCharacters == 2) {
					offset = 50.0f;
					spacing = 100.0f;
				} else {
					offset = 100.0f;
					spacing = 300.0f / AvailableCharacters;
				}
				return centerX - offset + j * spacing;
			}

			void Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset) override {
				Bounds = bounds;
				float optionsY = bounds.Y + bounds.H * 0.5f;

				if (Selected && FocusedPlayer != nullptr) {
					*FocusedPlayer = PlayerIndex;
				}

				static const StringView playerTypes[] = { "Jazz"_s, "Spaz"_s, "Lori"_s };
				static const Colorf playerColors[] = {
					Colorf(0.2f, 0.45f, 0.2f, 0.5f),
					Colorf(0.45f, 0.27f, 0.22f, 0.5f),
					Colorf(0.5f, 0.45f, 0.22f, 0.5f)
				};

				std::int32_t selectedType = (PlayerType != nullptr ? *PlayerType : 0);
				for (std::int32_t j = 0; j < AvailableCharacters; j++) {
					float x = GetCharacterX(j);
					if (selectedType == j) {
						// Selected character: pops with the elastic selection animation when the row is focused
						float size = (Selected ? 0.5f + Easing::OutElastic(Animation.Raw()) * 0.6f : 1.0f);
						root->DrawStringGlow(playerTypes[j], charOffset, x, optionsY, IMenuContainer::FontLayer,
							Alignment::Center, playerColors[j], size, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
					} else {
						root->DrawStringShadow(playerTypes[j], charOffset, x, optionsY, IMenuContainer::FontLayer,
							Alignment::Center, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
					}
				}

				if (Selected) {
					root->DrawMenuArrows(charOffset, Bounds.X + Bounds.W * 0.5f, optionsY, Animation.Raw(), 50.0f);
				}
			}

			bool OnNavigate(const WidgetInput& input, IMenuContainer* root) override {
				if (input.Left || input.Right) {
					if (PlayerType != nullptr && AvailableCharacters > 0) {
						std::int32_t dir = (input.Left ? -1 : 1);
						*PlayerType = (*PlayerType + dir + AvailableCharacters) % AvailableCharacters;
						root->PlaySfx("MenuSelect"_s, 0.5f);
						Animation.Restart(0.0f);
					}
					return true;
				}
				return false;
			}

			bool OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root) override {
				// A tap selects the character nearest the touched position (so tapping a name picks that character
				// directly), but only once the row is selected; the first tap just selects the row (return false so
				// the container handles selection), matching ChoiceItem/CustomValueItem and the keyboard flow.
				if (!Selected || PlayerType == nullptr || AvailableCharacters <= 0 || event.type != TouchEventType::Up) {
					return false;
				}
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex == -1) {
					return false;
				}
				float x = event.pointers[pointerIndex].x * viewSize.X;
				std::int32_t nearest = 0;
				float nearestDist = std::abs(x - GetCharacterX(0));
				for (std::int32_t j = 1; j < AvailableCharacters; j++) {
					float dist = std::abs(x - GetCharacterX(j));
					if (dist < nearestDist) {
						nearestDist = dist;
						nearest = j;
					}
				}
				if (nearest != *PlayerType) {
					*PlayerType = nearest;
					root->PlaySfx("MenuSelect"_s, 0.5f);
					Animation.Restart(0.0f);
				}
				return true;
			}
		};

		StringView GetDifficultyName(std::int32_t difficulty)
		{
			switch (difficulty) {
				default:
				case 0: return _("Easy");
				case 1: return _("Medium");
				case 2: return _("Hard");
			}
		}

		StringView GetGameModeName(MpGameMode mode)
		{
			switch (mode) {
				case MpGameMode::Battle: return _("Battle");
				case MpGameMode::TeamBattle: return _("Team Battle");
				case MpGameMode::Race: return _("Race");
				case MpGameMode::TeamRace: return _("Team Race");
				case MpGameMode::TreasureHunt: return _("Treasure Hunt");
				case MpGameMode::TeamTreasureHunt: return _("Team Treasure Hunt");
				case MpGameMode::CaptureTheFlag: return _("Capture The Flag");
				case MpGameMode::Cooperation: return _("Cooperation");
				default: return _("Battle");
			}
		}
	}

	CreateLocalGameOptionsSection::CreateLocalGameOptionsSection(StringView levelName, StringView previousEpisodeName, StringView resumeEpisodeName)
		: _levelName(levelName), _previousEpisodeName(previousEpisodeName), _resumeEpisodeName(resumeEpisodeName), _availableCharacters(3),
			_playerCount(2), _playerTypes{}, _currentPlayer(0), _difficulty(1), _gameMode(MpGameMode::Cooperation),
			_characterRows{}, _shouldStart(false), _transitionTime(0.0f)
	{
	}

	void CreateLocalGameOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		if (auto* mainMenu = runtime_cast<MainMenu>(_root)) {
			auto* res = mainMenu->_metadata->FindAnimation(LoriExistsCheck);
			_availableCharacters = (res != nullptr ? 3 : 2);
		}

		for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
			_playerTypes[i] = i % _availableCharacters;
		}

		// When resuming a saved episode, restore the difficulty and the first player's character from the saved
		// progress (continue); the remaining players keep their defaults and can still be configured below
		if (!_resumeEpisodeName.empty()) {
			if (auto* episodeContinue = PreferencesCache::GetEpisodeContinue(_resumeEpisodeName)) {
				_difficulty = std::clamp((std::int32_t)(episodeContinue->State.DifficultyAndPlayerType & 0x0f) - (std::int32_t)GameDifficulty::Easy, 0, 2);
				std::int32_t savedType = (std::int32_t)((episodeContinue->State.DifficultyAndPlayerType >> 4) & 0x0f) - (std::int32_t)PlayerType::Jazz;
				if (savedType >= 0 && savedType < _availableCharacters) {
					_playerTypes[0] = savedType;
				}
			}
		}

		SetTitle(_("Create Local Splitscreen Game"));

		auto list = std::make_unique<ScrollView>();

		// TRANSLATORS: Menu item to select the number of local splitscreen players
		list->Add<ChoiceItem>(_("Number of Players"),
			[this]() -> StringView {
				static const StringView labels[] = { "1"_s, "2"_s, "3"_s, "4"_s };
				std::int32_t index = _playerCount - 1;
				return (index >= 0 && index < (std::int32_t)arraySize(labels) ? labels[index] : "?"_s);
			},
			[this](std::int32_t direction) { ChangePlayerCount(direction); });

		// A single "Character" header (so the label isn't repeated) followed by one focusable character row per player.
		// The rows are shown/hidden by the player count, so the character area grows and shrinks with it.
		auto* characterHeader = list->Add<CanvasWidget>(28.0f, /*focusable*/false);
		characterHeader->OnDrawContent = [](IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation) {
			root->DrawStringShadow(_("Character"), charOffset, bounds.X + bounds.W * 0.5f, bounds.Y + bounds.H * 0.5f,
				IMenuContainer::FontLayer, Alignment::Center, Font::DefaultColor, 0.9f);
		};

		for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
			auto* row = list->Add<PlayerCharacterWidget>();
			row->PlayerIndex = i;
			row->PlayerType = &_playerTypes[i];
			row->FocusedPlayer = &_currentPlayer;
			row->AvailableCharacters = _availableCharacters;
			row->Visible = (i < _playerCount);
			_characterRows[i] = row;
		}

		// TRANSLATORS: Menu item to select difficulty
		list->Add<ChoiceItem>(_("Difficulty"),
			[this]() -> StringView { return GetDifficultyName(_difficulty); },
			[this](std::int32_t direction) {
				_difficulty = std::clamp(_difficulty + direction, 0, 2);
			});

		// Game mode is selected through the shared MultiplayerGameModeSelectSection (same as the create server screens)
		auto* gameModeItem = list->Add<CustomValueItem>(_("Game Mode"));
		gameModeItem->DrawValue = [this](IMenuContainer* root, float centerX, float y, std::int32_t& charOffset, bool selected, bool readOnly) {
			root->DrawMenuValue(charOffset, GetGameModeName(_gameMode), centerX, y, selected, readOnly, false, 0.0f);
		};
		gameModeItem->OnActivate = [root]() { root->SwitchToSection<MultiplayerGameModeSelectSection>(); };

		// Episode continue only applies to cooperation, so lock the game mode when resuming a saved episode
		if (!_resumeEpisodeName.empty()) {
			_gameMode = MpGameMode::Cooperation;
			gameModeItem->ReadOnly = true;
		}

		// TRANSLATORS: Menu item to start the local game with the selected settings
		list->Add<ListItem>(_("Start"), [this]() {
			// Begin the fade-out transition; the level is loaded once it finishes (see OnUpdate)
			_shouldStart = true;
			_transitionTime = 1.0f;
		});

		SetContent(std::move(list));
	}

	void CreateLocalGameOptionsSection::OnUpdate(float timeMult)
	{
		if (_shouldStart) {
			// Run the start-game fade-out transition, then load the level (matches StartGameOptionsSection)
			_transitionTime -= 0.025f * timeMult;
			if (_transitionTime <= 0.0f) {
				StartGame();
			}
			return;
		}

		WidgetSection::OnUpdate(timeMult);
	}

	void CreateLocalGameOptionsSection::OnDraw(Canvas* canvas)
	{
		// Draw the character portrait of the player currently being configured on the left side (like
		// StartGameOptionsSection), behind the scrollable list which the base class draws afterwards
		std::int32_t focusedPlayer = (_currentPlayer >= 0 && _currentPlayer < _playerCount ? _currentPlayer : 0);

		AnimState characterImage;
		switch (_playerTypes[focusedPlayer]) {
			default:
			case 0: characterImage = MenuDifficultyJazz; break;
			case 1: characterImage = MenuDifficultySpaz; break;
			case 2: characterImage = MenuDifficultyLori; break;
		}

		Recti contentBounds = _root->GetContentBounds();
		float imageX = contentBounds.X + contentBounds.W * 0.2f;
		float imageY = contentBounds.Y + contentBounds.H * 0.5f;
		_root->DrawElement(MenuDim, 0, imageX, imageY, IMenuContainer::ShadowLayer - 2, Alignment::Center, Colorf::White, 24.0f, 36.0f);
		_root->DrawElement(characterImage, _difficulty, imageX, imageY, IMenuContainer::ShadowLayer, Alignment::Center, Colorf::White, 0.88f, 0.88f);

		WidgetSection::OnDraw(canvas);
	}

	void CreateLocalGameOptionsSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_shouldStart) {
			auto command = canvas->RentRenderCommand();
			if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(static_cast<float>(canvas->ViewSize.X), static_cast<float>(canvas->ViewSize.Y)).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->SetTransformation(Matrix4x4f::Identity);
			command->SetLayer(999);

			canvas->DrawRenderCommand(command);
		}
	}

	void CreateLocalGameOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		// Ignore touch input once the start transition is running
		if (_shouldStart) {
			return;
		}

		WidgetSection::OnTouchEvent(event, viewSize);
	}

	void CreateLocalGameOptionsSection::ChangePlayerCount(std::int32_t direction)
	{
		_playerCount = std::clamp(_playerCount + direction, 2, ControlScheme::MaxSupportedPlayers);

		// Show only the character rows for the active players; the scroll view skips the hidden ones, so the
		// character area resizes with the player count
		for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
			if (_characterRows[i] != nullptr) {
				_characterRows[i]->Visible = (i < _playerCount);
			}
		}

		if (_currentPlayer >= _playerCount) {
			_currentPlayer = _playerCount - 1;
		}
	}

	void CreateLocalGameOptionsSection::StartGame()
	{
		SmallVector<PlayerType, ControlScheme::MaxSupportedPlayers> players(_playerCount);
		for (std::int32_t i = 0; i < _playerCount; i++) {
			players[i] = (PlayerType)((std::int32_t)PlayerType::Jazz + _playerTypes[i]);
		}

		LevelInitialization levelInit(_levelName, (GameDifficulty)((std::int32_t)GameDifficulty::Easy + _difficulty),
			PreferencesCache::EnableReforgedGameplay, false, players);

		// All local splitscreen games (including cooperation) run through the local MpLevelHandler, so every game
		// mode shares the same authoritative code path; a non-Unknown game mode is what selects that handler
		levelInit.LocalMultiplayerGameMode = (std::uint8_t)_gameMode;

		if (!_resumeEpisodeName.empty()) {
			// Resuming a saved episode: restore the first player's progress (lives, score, ammo, ...) from the
			// continue state, matching the single-player continue path in EpisodeSelectSection
			if (auto* episodeContinue = PreferencesCache::GetEpisodeContinue(_resumeEpisodeName)) {
				if ((episodeContinue->State.Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
					levelInit.CheatsUsed = true;
				}
				levelInit.ElapsedMilliseconds = episodeContinue->State.ElapsedMilliseconds;

				auto& firstPlayer = levelInit.PlayerCarryOvers[0];
				firstPlayer.Lives = episodeContinue->State.Lives;
				firstPlayer.Score = episodeContinue->State.Score;
				std::memcpy(firstPlayer.Gems, episodeContinue->State.Gems, sizeof(firstPlayer.Gems));
				std::memcpy(firstPlayer.Ammo, episodeContinue->State.Ammo, sizeof(firstPlayer.Ammo));
				std::memcpy(firstPlayer.WeaponUpgrades, episodeContinue->State.WeaponUpgrades, sizeof(firstPlayer.WeaponUpgrades));
			}
		}
		// Cooperation carries over player stats from the previous episode (and is resumable), like classic co-op
		else if (_gameMode == MpGameMode::Cooperation && !_previousEpisodeName.empty()) {
			auto previousEpisodeEnd = PreferencesCache::GetEpisodeEnd(_previousEpisodeName);
			if (previousEpisodeEnd != nullptr) {
				if ((previousEpisodeEnd->Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed ||
					(previousEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) != EpisodeContinuationFlags::IsCompleted) {
					levelInit.CheatsUsed = true;
				}
				levelInit.ElapsedMilliseconds = previousEpisodeEnd->ElapsedMilliseconds;

				auto& firstPlayer = levelInit.PlayerCarryOvers[0];
				if (previousEpisodeEnd->Lives > 0) {
					firstPlayer.Lives = previousEpisodeEnd->Lives;
				}
				firstPlayer.Score = previousEpisodeEnd->Score;
				std::memcpy(firstPlayer.Gems, previousEpisodeEnd->Gems, sizeof(firstPlayer.Gems));
				std::memcpy(firstPlayer.Ammo, previousEpisodeEnd->Ammo, sizeof(firstPlayer.Ammo));
				std::memcpy(firstPlayer.WeaponUpgrades, previousEpisodeEnd->WeaponUpgrades, sizeof(firstPlayer.WeaponUpgrades));
			} else {
				levelInit.CheatsUsed = true;
			}
		}

		// Competitive modes use infinite respawns (like online multiplayer), not the lives/game-over system which
		// only applies to cooperation. Without this a local competitive player would be removed after running out of
		// the default lives. (Online players bypass lives entirely because the session isn't local.)
		if (_gameMode != MpGameMode::Cooperation) {
			for (std::int32_t i = 0; i < _playerCount; i++) {
				levelInit.PlayerCarryOvers[i].Lives = UINT8_MAX;
			}
		}

		if (PreferencesCache::AllowCheatsLives) {
			for (std::int32_t i = 0; i < _playerCount; i++) {
				levelInit.PlayerCarryOvers[i].Lives = UINT8_MAX;
			}
			levelInit.CheatsUsed = true;
		}

		_root->ChangeLevel(std::move(levelInit));
	}
}

#endif
