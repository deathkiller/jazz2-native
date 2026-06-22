#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <cstring>

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	StartGameOptionsSection::StartGameOptionsSection(StringView levelName, StringView previousEpisodeName)
		: _levelName(levelName), _previousEpisodeName(previousEpisodeName), _selectedIndex(2),
			_availableCharacters(3), _selectedPlayerType(0), _selectedDifficulty(1), _lastPlayerType(0), _lastDifficulty(0),
			_imageTransition(1.0f), _animation(0.0f), _transitionTime(0.0f), _shouldStart(false)
	{
		// TRANSLATORS: Menu item to select player character (Jazz, Spaz, Lori)
		_items[(std::int32_t)Item::Character].Name = _("Character");
		// TRANSLATORS: Menu item to select difficulty
		_items[(std::int32_t)Item::Difficulty].Name = _("Difficulty");
		// TRANSLATORS: Menu item to start selected episode/level
		_items[(std::int32_t)Item::Start].Name = _("Start");
	}

	void StartGameOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;

		if (auto* mainMenu = runtime_cast<MainMenu>(_root)) {
			auto* res = mainMenu->_metadata->FindAnimation(LoriExistsCheck);
			_availableCharacters = (res != nullptr ? 3 : 2);
		}
	}

	void StartGameOptionsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_imageTransition < 1.0f) {
			_imageTransition += timeMult * 0.1f;

			if (_imageTransition > 1.0f) {
				_imageTransition = 1.0f;
			}
		}

		if (!_shouldStart) {
			if (_root->ActionHit(PlayerAction::Fire)) {
				ExecuteSelected();
			} else if (_root->ActionHit(PlayerAction::Left)) {
				if (_selectedIndex == (std::int32_t)Item::Character) {
					StartImageTransition();
					_selectedPlayerType = (_selectedPlayerType > 0 ? _selectedPlayerType - 1 : _availableCharacters - 1);
					_root->PlaySfx("MenuSelect"_s, 0.5f);
				} else if (_selectedIndex == (std::int32_t)Item::Difficulty) {
					if (_selectedDifficulty > 0) {
						StartImageTransition();
						_selectedDifficulty--;
						_root->PlaySfx("MenuSelect"_s, 0.5f);
					}
				}
			} else if (_root->ActionHit(PlayerAction::Right)) {
				if (_selectedIndex == (std::int32_t)Item::Character) {
					StartImageTransition();
					_selectedPlayerType = (_selectedPlayerType < _availableCharacters - 1 ? _selectedPlayerType + 1 : 0);
					_root->PlaySfx("MenuSelect"_s, 0.5f);
				} else if (_selectedIndex == (std::int32_t)Item::Difficulty) {
					if (_selectedDifficulty < 3 - 1) {
						StartImageTransition();
						_selectedDifficulty++;
						_root->PlaySfx("MenuSelect"_s, 0.4f);
					}
				}
			} else if (_root->ActionHit(PlayerAction::Up)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				if (_selectedIndex > 0) {
					_selectedIndex--;
				} else {
					_selectedIndex = (std::int32_t)Item::Count - 1;
				}
			} else if (_root->ActionHit(PlayerAction::Down)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				if (_selectedIndex < (std::int32_t)Item::Count - 1) {
					_selectedIndex++;
				} else {
					_selectedIndex = 0;
				}
			} else if (_root->ActionHit(PlayerAction::Menu)) {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->LeaveSection();
			}
		} else {
			_transitionTime -= 0.025f * timeMult;
			if (_transitionTime <= 0.0f) {
				OnAfterTransition();
			}
		}
	}

	void StartGameOptionsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f * 0.86f);

		AnimState selectedDifficultyImage;
		switch (_selectedPlayerType) {
			default:
			case 0: selectedDifficultyImage = MenuDifficultyJazz; break;
			case 1: selectedDifficultyImage = MenuDifficultySpaz; break;
			case 2: selectedDifficultyImage = MenuDifficultyLori; break;
		}

		_root->DrawElement(MenuDim, 0, center.X * 0.36f, center.Y * 1.4f, IMenuContainer::ShadowLayer - 2, Alignment::Center, Colorf::White, 24.0f, 36.0f);

		_root->DrawElement(selectedDifficultyImage, _selectedDifficulty, center.X * 0.36f, center.Y * 1.4f + 3.0f, IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.2f * _imageTransition), 0.88f, 0.88f);

		if (_imageTransition < 1.0f) {
			AnimState lastDifficultyImage;
			switch (_lastPlayerType) {
				default:
				case 0: lastDifficultyImage = MenuDifficultyJazz; break;
				case 1: lastDifficultyImage = MenuDifficultySpaz; break;
				case 2: lastDifficultyImage = MenuDifficultyLori; break;
			}
			_root->DrawElement(lastDifficultyImage, _lastDifficulty, center.X * 0.36f, center.Y * 1.4f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 1.0f - _imageTransition), 0.88f, 0.88f);
		}

		_root->DrawElement(selectedDifficultyImage, _selectedDifficulty, center.X * 0.36f, center.Y * 1.4f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, _imageTransition), 0.88f, 0.88f);

		std::int32_t charOffset = 0;
		for (std::int32_t i = 0; i < (std::int32_t)Item::Count; i++) {
			if (_selectedIndex == i) {
				float size = 0.5f + Easing::OutElastic(_animation) * 0.6f;

				_root->DrawStringGlow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			if (i == (std::int32_t)Item::Character) {
				constexpr const StringView playerTypes[] = { "Jazz"_s, "Spaz"_s, "Lori"_s };
				constexpr Colorf playerColors[] = {
					Colorf(0.2f, 0.45f, 0.2f, 0.5f),
					Colorf(0.45f, 0.27f, 0.22f, 0.5f),
					Colorf(0.5f, 0.45f, 0.22f, 0.5f)
				};

				float offset, spacing;
				if (_availableCharacters == 1) {
					offset = 0.0f;
					spacing = 0.0f;
				} else if (_availableCharacters == 2) {
					offset = 50.0f;
					spacing = 100.0f;
				} else {
					offset = 100.0f;
					spacing = 300.0f / _availableCharacters;
				}

				for (std::int32_t j = 0; j < _availableCharacters; j++) {
					float x = center.X - offset + j * spacing;
					if (_selectedPlayerType == j) {
						_root->DrawStringGlow(playerTypes[j], charOffset, x, center.Y + 28.0f, IMenuContainer::FontLayer,
							Alignment::Center, playerColors[j], 1.0f, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
					} else {
						_root->DrawStringShadow(playerTypes[j], charOffset, x, center.Y + 28.0f, IMenuContainer::FontLayer,
							Alignment::Center, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
					}
				}

				if (_selectedIndex == i) {
					_root->DrawMenuArrows(charOffset, center.X, center.Y + 28.0f, _animation, 50.0f);
				}

				_items[i].TouchY = center.Y + 28.0f;
			} else if (i == (std::int32_t)Item::Difficulty) {
				const StringView difficultyTypes[] = { _("Easy"), _("Medium"), _("Hard") };
				float spacing = 100.0f;

				for (std::int32_t j = 0; j < std::int32_t(arraySize(difficultyTypes)); j++) {
					if (_selectedDifficulty == j) {
						_root->DrawStringGlow(difficultyTypes[j], charOffset, center.X + (j - 1) * spacing, center.Y + 28.0f, IMenuContainer::FontLayer,
							Alignment::Center, Colorf(0.45f, 0.45f, 0.45f, 0.5f), 1.0f, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
					} else {
						_root->DrawStringShadow(difficultyTypes[j], charOffset, center.X + (j - 1) * spacing, center.Y + 28.0f, IMenuContainer::FontLayer,
							Alignment::Center, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.9f);
					}
				}

				if (_selectedIndex == i) {
					_root->DrawMenuArrows(charOffset, center.X, center.Y + 28.0f, _animation, 50.0f);
				}

				_items[i].TouchY = center.Y + 28.0f;
				center.Y += 6.0f;
			} else {
				_items[i].TouchY = center.Y;
			}

			center.Y += 60.0f;
		}
	}

	void StartGameOptionsSection::OnDrawOverlay(Canvas* canvas)
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

	void StartGameOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (!_shouldStart && event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x * (float)viewSize.X;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				float halfWidth = viewSize.X * 0.5f;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (std::int32_t i = 0; i < (std::int32_t)Item::Count; i++) {
					if (std::abs(x - halfWidth) < 150.0f && std::abs(y - _items[i].TouchY) < 30.0f) {
						switch ((Item)i) {
							case Item::Character: {
								std::int32_t selectedSubitem;
								if (_availableCharacters == 2) {
									selectedSubitem = (x < halfWidth ? 0 : 1);
								} else {
									selectedSubitem = (x < halfWidth - 50.0f ? 0 : (x > halfWidth + 50.0f ? 2 : 1));
								}
								if (_selectedPlayerType != selectedSubitem) {
									StartImageTransition();
									_selectedPlayerType = selectedSubitem;
									_root->PlaySfx("MenuSelect"_s, 0.5f);
								}
								break;
							}
							case Item::Difficulty: {
								std::int32_t selectedSubitem = (x < halfWidth - 50.0f ? 0 : (x > halfWidth + 50.0f ? 2 : 1));
								if (_selectedDifficulty != selectedSubitem) {
									StartImageTransition();
									_selectedDifficulty = selectedSubitem;
									_root->PlaySfx("MenuSelect"_s, 0.5f);
								}
								break;
							}
							default: {
								if (_selectedIndex == i) {
									ExecuteSelected();
								} else {
									_root->PlaySfx("MenuSelect"_s, 0.5f);
									_animation = 0.0f;
									_selectedIndex = i;
								}
								break;
							}
						}
						break;
					}
				}
			}
		}
	}

	void StartGameOptionsSection::ExecuteSelected()
	{
		if (_selectedIndex == (std::int32_t)Item::Start) {
			_root->PlaySfx("MenuSelect"_s, 0.6f);

			_shouldStart = true;
			_transitionTime = 1.0f;
		}
	}

	void StartGameOptionsSection::OnAfterTransition()
	{
		bool playTutorial = (!PreferencesCache::TutorialCompleted && _levelName == "prince/01_castle1"_s);
		if (playTutorial && !ContentResolver::Get().LevelExists("prince/trainer"_s)) {
			LOGW("Tutorial level \"prince/trainer.j2l\" not found, skipping tutorial");
			playTutorial = false;
		}

		LevelInitialization levelInit((playTutorial ? "prince/trainer"_s : StringView(_levelName)), (GameDifficulty)((std::int32_t)GameDifficulty::Easy + _selectedDifficulty),
			PreferencesCache::EnableReforgedGameplay, false, (PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType));

		if (!_previousEpisodeName.empty()) {
			auto previousEpisodeEnd = PreferencesCache::GetEpisodeEnd(_previousEpisodeName);
			if (previousEpisodeEnd != nullptr) {
				// Set CheatsUsed to true if cheats were used in the previous episode or the previous episode is not completed
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
				// Set CheatsUsed to true if the previous episode is not completed
				levelInit.CheatsUsed = true;
			}
		}

		if (PreferencesCache::EnableReforgedGameplay && _levelName.hasSuffix("/01_xmas1"_s)) {
			levelInit.LastExitType = ExitType::Warp | ExitType::Frozen;
		}

		if (PreferencesCache::AllowCheatsLives) {
			levelInit.PlayerCarryOvers[0].Lives = UINT8_MAX;
			levelInit.CheatsUsed = true;
		}

		_root->ChangeLevel(std::move(levelInit));
	}

	void StartGameOptionsSection::StartImageTransition()
	{
		_lastPlayerType = _selectedPlayerType;
		_lastDifficulty = _selectedDifficulty;
		_imageTransition = 0.0f;
	}
}
