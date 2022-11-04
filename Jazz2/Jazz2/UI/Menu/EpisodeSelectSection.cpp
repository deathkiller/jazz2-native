#include "EpisodeSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::UI::Menu
{
	EpisodeSelectSection::EpisodeSelectSection()
		:
		_selectedIndex(0),
		_animation(0.0f),
		_expandedAnimation(0.0f),
		_expanded(false),
		_transitionTime(0.0f),
		_shouldStart(false),
		_y(0.0f),
		_height(0.0f)
	{
		auto& resolver = ContentResolver::Current();

		// Search both "Content/Episodes/" and "Cache/Episodes/"
		fs::Directory dir(fs::JoinPath(resolver.GetContentPath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dir.GetNext();
			if (item == nullptr) {
				break;
			}

			AddEpisode(item);
		}

		fs::Directory dirCache(fs::JoinPath(resolver.GetCachePath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
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

	Recti EpisodeSelectSection::GetClipRectangle(const Vector2i& viewSize)
	{
		return Recti(0, TopLine - 1.0f, viewSize.X, viewSize.Y - TopLine - BottomLine + 2.0f);
	}

	void EpisodeSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void EpisodeSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_expanded && _expandedAnimation < 1.0f) {
			_expandedAnimation = std::min(_expandedAnimation + timeMult * 0.016f, 1.0f);
		}

		if (!_shouldStart) {
			if (!_items.empty()) {
				if (_root->ActionHit(PlayerActions::Fire)) {
					ExecuteSelected();
				} else if (_root->ActionHit(PlayerActions::Left)) {
					if (_expanded) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = false;
						_expandedAnimation = 0.0f;
						EnsureVisibleSelected();
					}
				} else if (_root->ActionHit(PlayerActions::Right)) {
					if ((_items[_selectedIndex].Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = true;
						EnsureVisibleSelected();
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
						EnsureVisibleSelected();
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
						EnsureVisibleSelected();
					}
				}

				_touchTime += timeMult;
			}
		} else {
			_transitionTime -= 0.025f * timeMult;
			if (_transitionTime <= 0.0f) {
				OnAfterTransition();
			}
		}
	}

	void EpisodeSelectSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement("MenuDim"_s, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int charOffset = 0;
		_root->DrawStringShadow("Play Episodes"_s, charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void EpisodeSelectSection::OnDrawClipped(Canvas* canvas)
	{
		if (_items.empty()) {
			_scrollable = false;
			return;
		}

		Vector2i viewSize = canvas->ViewSize;
		float bottomLine = viewSize.Y - BottomLine;
		float availableHeight = (bottomLine - TopLine);
		float spacing = availableHeight * 0.95f / _items.size();
		if (spacing <= 30.0f) {
			spacing = 30.0f;
			_y = (availableHeight - _height < 0.0f ? std::clamp(_y, availableHeight - _height, 0.0f) : 0.0f);
			_scrollable = true;
		} else {
			_y = 0.0;
			_scrollable = false;
		}

		Vector2f center = Vector2f(viewSize.X * 0.5f, TopLine + ItemHeight * 0.5f + _y);
		float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
		float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));
		int charOffset = 0;

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

			center.Y += spacing;
		}

		_height = center.Y - (TopLine + _y);
	}

	void EpisodeSelectSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_shouldStart) {
			Vector2i viewSize = canvas->ViewSize;

			auto command = canvas->RentRenderCommand();
			if (command->material().setShader(ContentResolver::Current().GetShader(PrecompiledShader::Transition))) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(Vector2f(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y)).Data());
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->setTransformation(Matrix4x4f::Identity);

			canvas->DrawRenderCommand(command);
		}
	}

	void EpisodeSelectSection::OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize)
	{
		if (_shouldStart) {
			return;
		}

		switch (event.type) {
			case TouchEventType::Down: {
				int pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					int pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
						if (_scrollable) {
							_y += touchMove.Y - _touchLast.Y;
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).Length() > 10.0f || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}

				float halfW = viewSize.X * 0.5f;
				for (int i = 0; i < _items.size(); i++) {
					if (std::abs(_touchLast.X - halfW) < 150.0f && std::abs(_touchLast.Y - _items[i].TouchY) < 22.0f) {
						if (_selectedIndex == i) {
							bool onExpand = (_touchLast.X > halfW + 100.0f && (_items[i].Flags & ItemFlags::CanContinue) == ItemFlags::CanContinue);
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
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
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
					_shouldStart = true;
					_transitionTime = 1.0f;
				}
			} else {
				_root->SwitchToSectionPtr(std::make_unique<StartGameOptionsSection>(selectedItem.Description.Name, selectedItem.Description.FirstLevel, selectedItem.Description.PreviousEpisode));
			}
		}
	}

	void EpisodeSelectSection::EnsureVisibleSelected()
	{
		if (!_scrollable) {
			return;
		}

		float bottomLine = _root->GetViewSize().Y - BottomLine;
		if (_items[_selectedIndex].TouchY < TopLine + ItemHeight * 0.5f) {
			_y += (TopLine + ItemHeight * 0.5f - _items[_selectedIndex].TouchY);
		} else if (_items[_selectedIndex].TouchY > bottomLine - ItemHeight * 0.5f) {
			_y += (bottomLine - ItemHeight * 0.5f - _items[_selectedIndex].TouchY);
		}
	}

	void EpisodeSelectSection::OnAfterTransition()
	{
		auto& selectedItem = _items[_selectedIndex];
		auto episodeContinue = PreferencesCache::GetEpisodeContinue(selectedItem.Description.Name);

		PlayerType players[] = { (PlayerType)((episodeContinue->State.DifficultyAndPlayerType >> 4) & 0x0f) };
		LevelInitialization levelInit(selectedItem.Description.Name, episodeContinue->LevelName, (GameDifficulty)(episodeContinue->State.DifficultyAndPlayerType & 0x0f),
			PreferencesCache::EnableReforged, false, players, _countof(players));

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

	void EpisodeSelectSection::AddEpisode(const StringView& episodeFile)
	{
		if (fs::GetExtension(episodeFile) != "j2e"_s) {
			return;
		}

		std::optional<Episode> description = ContentResolver::Current().GetEpisodeByPath(episodeFile);
		if (description.has_value()) {
#if defined(SHAREWARE_DEMO_ONLY)
			// Check if specified episode is unlocked, used only if compiled with SHAREWARE_DEMO_ONLY
			if (description->Name == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) return;
			if (description->Name == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) return;
			if (description->Name == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) return;
			if (description->Name == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) return;
			if ((description->Name == "xmas98"_s || description->Name == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::HolidayHare98) == UnlockableEpisodes::None) return;
			if (description->Name == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None) return;
#endif

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