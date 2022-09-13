#include "EpisodeSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Base/Algorithms.h"

namespace Jazz2::UI::Menu
{
	EpisodeSelectSection::EpisodeSelectSection()
		:
		_selectedIndex(0),
		_animation(0.0f),
		_expandedAnimation(0.0f),
		_expanded(false)
	{
		// Search both "Content/Episodes/" and "Cache/Episodes/"
		fs::Directory dir(fs::JoinPath("Content"_s, "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dir.GetNext();
			if (item == nullptr) {
				break;
			}

			AddEpisode(item);
		}

		fs::Directory dirCache(fs::JoinPath("Cache"_s, "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dirCache.GetNext();
			if (item == nullptr) {
				break;
			}

			AddEpisode(item);
		}

		quicksort(_items.begin(), _items.end(), [](const EpisodeSelectSection::ItemData& a, const EpisodeSelectSection::ItemData& b) -> bool {
			return (a.Description.Position < b.Description.Position);
		});
	}

	void EpisodeSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_expanded && _expandedAnimation < 1.0f) {
			_expandedAnimation = std::min(_expandedAnimation + timeMult * 0.016f, 1.0f);
		}

		if (!_items.empty()) {
			if (_root->ActionHit(PlayerActions::Fire)) {
				ExecuteSelected();
			} else if (_root->ActionHit(PlayerActions::Left)) {
				if (_expanded) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_expanded = false;
					_expandedAnimation = 0.0f;
				}
			} else if (_root->ActionHit(PlayerActions::Right)) {
				if ((_items[_selectedIndex].Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_expanded = true;
				}
			} else if (_items.size() > 1) {
				if (_root->ActionHit(PlayerActions::Menu)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				} else if (_root->ActionHit(PlayerActions::Up)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					_expanded = false;
					_expandedAnimation = 0.0f;

					if (_selectedIndex > 0) {
						_selectedIndex--;
					} else {
						_selectedIndex = _items.size() - 1;
					}
				} else if (_root->ActionHit(PlayerActions::Down)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					_expanded = false;
					_expandedAnimation = 0.0f;

					if (_selectedIndex < _items.size() - 1) {
						_selectedIndex++;
					} else {
						_selectedIndex = 0;
					}
				}
			}
		}
	}

	void EpisodeSelectSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float topLine = 131.0f;
		float bottomLine = viewSize.Y - 42;
		_root->DrawElement("MenuDim"_s, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::White, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.7f / _items.size();
		int charOffset = 0;

		_root->DrawStringShadow("Play Story"_s, charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
		float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));

		for (int i = 0; i < _items.size(); i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				if ((_items[i].Flags & ItemFlags::IsAvailable) == ItemFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
					_root->DrawElement("MenuGlow"_s, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (_items[i].Description.DisplayName.size() + 3) * 0.5f * size, 4.0f * size, true);

					Colorf nameColor = Font::RandomColor;
					nameColor.SetAlpha(0.5f - expandedAnimation3 * 0.15f);
					_root->DrawStringShadow(_items[i].Description.DisplayName, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Center, nameColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

					if ((_items[i].Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue) {
						float expandX = center.X + (_items[i].Description.DisplayName.size() + 3) * 2.8f * size + 40.0f;
						float moveX = expandedAnimation3 * -12.0f;
						_root->DrawStringShadow(">"_s, charOffset, expandX + moveX, center.Y, IMenuContainer::FontLayer + 20,
							Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);

						if (_expanded) {
							float expandedAnimation4 = IMenuContainer::EaseOutElastic(_expandedAnimation) * 0.8f;

							_root->DrawStringShadow("Restart episode"_s, charOffset, expandX + 40.0f, center.Y, IMenuContainer::FontLayer + 22,
								Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f * std::min(1.0f, 0.4f + expandedAnimation3)), expandedAnimation4, 0.4f, 0.6f, 0.6f, 0.6f, 0.8f);
						}
					}
				} else {
					int prevEpisodeIndex = -1;
					if (!_items[i].Description.PreviousEpisode.empty()) {
						for (int j = 0; j < _items.size(); j++) {
							if (i != j && _items[i].Description.PreviousEpisode == _items[j].Description.Name) {
								prevEpisodeIndex = j;
								break;
							}
						}
					}

					_root->DrawElement("MenuGlow"_s, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (_items[i].Description.DisplayName.size() + 3) * 0.5f * size, 4.0f * size, true);

					_root->DrawStringShadow(_items[i].Description.DisplayName, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Center, Font::TransparentRandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

					if (prevEpisodeIndex != -1) {
						_root->DrawStringShadow("You must complete \"" + _items[prevEpisodeIndex].Description.DisplayName + "\" first!"_s, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 20,
							Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
					} else {
						_root->DrawStringShadow("Episode is locked!"_s, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 20,
							Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
					}
				}
			} else {
				_root->DrawStringShadow(_items[i].Description.DisplayName, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			if ((_items[i].Flags & (ItemFlags::IsCompleted | ItemFlags::IsAvailable)) == (ItemFlags::IsCompleted | ItemFlags::IsAvailable)) {
				float size = (_selectedIndex == i ? 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f : 0.7f);
				float expandX = center.X - (_items[i].Description.DisplayName.size() + 3) * 4.0f * (_selectedIndex == i ? size : 1.1f) + 10.0f;
				_root->DrawElement("EpisodeComplete"_s, 0, expandX, center.Y - 2.0f, IMenuContainer::MainLayer + (_selectedIndex == i ? 20 : 10), Alignment::Right,
					((_items[i].Flags & ItemFlags::CheatsUsed) == ItemFlags::CheatsUsed ? Colorf::Black : Colorf::White), size, size);
			}

			center.Y += (bottomLine - topLine) * 0.94f / _items.size();
		}
	}

	void EpisodeSelectSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (int i = 0; i < _items.size(); i++) {
					if (std::abs(x - 0.5f) < 0.3f && std::abs(y - _items[i].TouchY) < 30.0f) {
						if (_selectedIndex == i) {
							bool onExpand = (x > (0.5f + 0.2f) && (_items[i].Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue);
							if (onExpand) {
								if (_expanded) {
									ExecuteSelected();
								} else {
									_root->PlaySfx("MenuSelect"_s, 0.5f);
									_expanded = true;
								}
							} else {
								if (_expanded) {
									_root->PlaySfx("MenuSelect"_s, 0.5f);
									_expanded = false;
									_expandedAnimation = 0.0f;
								} else {
									ExecuteSelected();
								}
							}
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
							_expanded = false;
							_expandedAnimation = 0.0f;
						}
						break;
					}
				}
			}
		}
	}

	void EpisodeSelectSection::ExecuteSelected()
	{
		auto& selectedItem = _items[_selectedIndex];
		if ((selectedItem.Flags & ItemFlags::IsAvailable) == ItemFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
			_root->PlaySfx("MenuSelect"_s, 0.6f);

			if ((selectedItem.Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue) {
				if (_expanded) {
					PreferencesCache::RemoveEpisodeContinue(selectedItem.Description.Name);

					selectedItem.Flags &= ~ItemFlags::CanContinue;
					_expandedAnimation = 0.0f;
					_expanded = false;

					_root->SwitchToSectionPtr(std::make_unique<StartGameOptionsSection>(selectedItem.Description.Name, selectedItem.Description.FirstLevel, selectedItem.Description.PreviousEpisode));
				} else {
					auto episodeContinue = PreferencesCache::GetEpisodeContinue(selectedItem.Description.Name);

					PlayerType players[] = { (PlayerType)((episodeContinue->State.DifficultyAndPlayerType >> 4) & 0x0f) };
					LevelInitialization levelInit(selectedItem.Description.Name, episodeContinue->LevelName, (GameDifficulty)(episodeContinue->State.DifficultyAndPlayerType & 0x0f),
						PreferencesCache::ReduxMode, false, players, _countof(players));

					if ((episodeContinue->State.Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
						levelInit.CheatsUsed = true;
					}

					auto& firstPlayer = levelInit.PlayerCarryOvers[0];
					firstPlayer.Lives = episodeContinue->State.Lives;
					firstPlayer.Score = episodeContinue->State.Score;
					memcpy(firstPlayer.Ammo, episodeContinue->State.Ammo, sizeof(levelInit.PlayerCarryOvers[0].Ammo));
					memcpy(firstPlayer.WeaponUpgrades, episodeContinue->State.WeaponUpgrades, sizeof(levelInit.PlayerCarryOvers[0].WeaponUpgrades));

					_root->ChangeLevel(std::move(levelInit));
				}
			} else {
				_root->SwitchToSectionPtr(std::make_unique<StartGameOptionsSection>(selectedItem.Description.Name, selectedItem.Description.FirstLevel, selectedItem.Description.PreviousEpisode));
			}
		}
	}

	void EpisodeSelectSection::AddEpisode(const StringView& episodeFile)
	{
		if (!fs::HasExtension(episodeFile, "j2e"_s)) {
			return;
		}

#if defined(SHAREWARE_DEMO_ONLY)
		String fileName = fs::GetFileNameWithoutExtension(episodeFile);
		if (fileName == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) return;
		if (fileName == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) return;
		if (fileName == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) return;
		if (fileName == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) return;
		if ((fileName == "xmas98"_s || fileName == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::HolidayHare98) == UnlockableEpisodes::None) return;
		if (fileName == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None) return;
#endif

		std::optional<Episode> description = ContentResolver::Current().GetEpisodeByPath(episodeFile);
		if (description.has_value()) {
			auto& episode = _items.emplace_back();
			episode.Description = std::move(description.value());

			if (!episode.Description.PreviousEpisode.empty()) {
				auto previousEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Description.PreviousEpisode);
				if (previousEpisodeEnd != nullptr && (previousEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
					episode.Flags |= ItemFlags::IsAvailable;
				}
			} else {
				episode.Flags |= ItemFlags::IsAvailable;
			}
			
			if ((episode.Flags & ItemFlags::IsAvailable) == ItemFlags::IsAvailable) {
				auto currentEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Description.Name);
				if (currentEpisodeEnd != nullptr && (currentEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
					episode.Flags |= ItemFlags::IsCompleted;
					if ((currentEpisodeEnd->Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
						episode.Flags |= ItemFlags::CheatsUsed;
					}
				}

				auto episodeContinue = PreferencesCache::GetEpisodeContinue(episode.Description.Name);
				if (episodeContinue != nullptr) {
					episode.Flags |= ItemFlags::CanContinue;
				}
			}
		}
	}
}