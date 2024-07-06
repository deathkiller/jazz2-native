﻿#include "EpisodeSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Base/FrameTimer.h"

#if defined(WITH_MULTIPLAYER)
#	include "CreateServerOptionsSection.h"
#endif

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	EpisodeSelectSection::EpisodeSelectSection(bool multiplayer)
		: _multiplayer(multiplayer), _expandedAnimation(0.0f), _expanded(false), _shouldStart(false)
	{
		auto& resolver = ContentResolver::Get();

		// Search both "Content/Episodes/" and "Cache/Episodes/"
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddEpisode(item);
		}

		for (auto item : fs::Directory(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddEpisode(item);
		}

		sort(_items.begin(), _items.end(), [](const ListViewItem& a, const ListViewItem& b) -> bool {
			return (a.Item.Description.Position < b.Item.Description.Position);
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

		if (!_shouldStart) {
			if (_root->ActionHit(PlayerActions::Menu)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->LeaveSection();
				return;
			} else if (!_items.empty()) {
				if (_root->ActionHit(PlayerActions::Fire)) {
					OnExecuteSelected();
				} else if (_root->ActionHit(PlayerActions::Left)) {
					if (!_multiplayer && _expanded) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = false;
						_expandedAnimation = 0.0f;
						EnsureVisibleSelected();
					}
				} else if (_root->ActionHit(PlayerActions::Right)) {
					if (!_multiplayer && !_expanded && (_items[_selectedIndex].Item.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = true;
						EnsureVisibleSelected();
					}
				} else if (_items.size() > 1) {
					if (_root->ActionHit(PlayerActions::Up)) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_animation = 0.0f;

						_expanded = false;
						_expandedAnimation = 0.0f;

						if (_selectedIndex > 0) {
							_selectedIndex--;
						} else {
							_selectedIndex = (std::int32_t)(_items.size() - 1);
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
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(
#if defined(WITH_MULTIPLAYER)
			_multiplayer ? _("Play Story in Cooperation") : _("Play Story"),
#else
			_("Play Story"),
#endif
			charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void EpisodeSelectSection::OnDrawEmptyText(Canvas* canvas, std::int32_t& charOffset)
	{
		Vector2i viewSize = canvas->ViewSize;

		_root->DrawStringShadow(_("No episode found!"), charOffset, viewSize.X * 0.5f, viewSize.Y * 0.55f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
	}

	void EpisodeSelectSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if ((item.Item.Flags & EpisodeDataFlags::IsMissing) == EpisodeDataFlags::IsMissing) {
			if (isSelected) {
				_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.2f), (Utf8::GetLength(item.Item.Description.DisplayName) + 3) * 0.5f, 4.0f, true, true);
			}

			_root->DrawStringShadow(item.Item.Description.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.51f, 0.51f, 0.51f, 0.35f), 0.9f);
		} else if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			if ((item.Item.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
				float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
				float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));

				if (item.Item.Description.TitleImage != nullptr) {
					Vector2i titleSize = item.Item.Description.TitleImage->size() / 2;
					_root->DrawTexture(*item.Item.Description.TitleImage, centerX, item.Y + 2.2f, IMenuContainer::FontLayer + 8, Alignment::Center, Vector2f(titleSize.X, titleSize.Y) * size * (1.0f - expandedAnimation3 * 0.2f) * 1.02f, Colorf(0.0f, 0.0f, 0.0f, 0.26f - expandedAnimation3 * 0.1f), true);
					float alpha = 1.0f - expandedAnimation3 * 0.4f;
					_root->DrawTexture(*item.Item.Description.TitleImage, centerX, item.Y, IMenuContainer::FontLayer + 10, Alignment::Center, Vector2f(titleSize.X, titleSize.Y) * size * (1.0f - expandedAnimation3 * 0.2f), Colorf(alpha, alpha, alpha, 1.0f), true);
				} else {
					_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.Description.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

					Colorf nameColor = Font::RandomColor;
					nameColor.SetAlpha(0.5f - expandedAnimation3 * 0.15f);
					_root->DrawStringShadow(item.Item.Description.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
						Alignment::Center, nameColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				}

				if (!_multiplayer && (item.Item.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
					float expandX = centerX + (item.Item.Description.DisplayName.size() + 3) * 2.8f * size + (canvas->ViewSize.X >= 400 ? 40.0f : 0.0f);
					float moveX = expandedAnimation3 * -12.0f;
					_root->DrawStringShadow(">"_s, charOffset, expandX + moveX, item.Y, IMenuContainer::FontLayer + 20,
						Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);

					if (_expanded) {
						float expandedAnimation4 = IMenuContainer::EaseOutElastic(_expandedAnimation) * 0.8f;

						// TRANSLATORS: Menu subitem in Play Story section
						Vector2f textSize = _root->MeasureString(_("Restart episode"), 0.8f, 0.8f);
						_root->DrawStringShadow(_("Restart episode"), charOffset, expandX + textSize.X * 0.5f - 2.0f, item.Y, IMenuContainer::FontLayer + 22,
							Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f * std::min(1.0f, 0.4f + expandedAnimation3)), expandedAnimation4, 0.4f, 0.6f, 0.6f, 0.6f, 0.8f);
					}
				}
			} else {
				std::int32_t prevEpisodeIndex = -1;
				if (!item.Item.Description.PreviousEpisode.empty()) {
					for (std::int32_t j = 0; j < _items.size(); j++) {
						if (item.Item.Description.PreviousEpisode == _items[j].Item.Description.Name) {
							prevEpisodeIndex = j;
							break;
						}
					}
				}

				_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.Description.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

				_root->DrawStringShadow(item.Item.Description.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::TransparentRandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				if (prevEpisodeIndex != -1) {
					// TRANSLATORS: Information in Play Story section that episode is locked because the previous episode is not complete
					_root->DrawStringShadow(_f("You must complete \"%s\" first!", _items[prevEpisodeIndex].Item.Description.DisplayName.data()), charOffset, centerX, item.Y, IMenuContainer::FontLayer + 20,
						Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					// TRANSLATORS: Information in Play Story section that episode is locked
					_root->DrawStringShadow(_("Episode is locked!"), charOffset, centerX, item.Y, IMenuContainer::FontLayer + 20,
						Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				}
			}
		} else {
			_root->DrawStringShadow(item.Item.Description.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}

		if ((item.Item.Flags & (EpisodeDataFlags::IsCompleted | EpisodeDataFlags::IsAvailable)) == (EpisodeDataFlags::IsCompleted | EpisodeDataFlags::IsAvailable)) {
			float size = (isSelected ? 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f : 0.7f);
			float textWidth = item.Item.Description.DisplayName.size() + 3;
			if (isSelected) {
				if (item.Item.Description.Name == "prince"_s || item.Item.Description.Name == "share"_s) {
					// "Formerly a Prince" & "Shareware Demo" title image is too narrow, so try to adjust it a bit
					textWidth *= 0.8f;
				} else if (item.Item.Description.Name == "flash"_s) {
					// "Flashback" title image is too wide, so try to adjust it a bit
					textWidth *= 1.5f;
				}
			}
			float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
			float expandX = centerX - textWidth * 4.0f * (isSelected ? (size - (expandedAnimation2 * 0.12f)) : 1.1f) + 10.0f;
			_root->DrawElement(EpisodeComplete, 0, expandX, item.Y - 2.0f, IMenuContainer::MainLayer + (isSelected ? 20 : 10), Alignment::Right,
				((item.Item.Flags & EpisodeDataFlags::CheatsUsed) == EpisodeDataFlags::CheatsUsed ? Colorf::Black : Colorf::White), size, size);
		}
	}

	void EpisodeSelectSection::OnDrawClipped(Canvas* canvas)
	{
		if (!_items.empty()) {
			auto& item = _items[_selectedIndex];
			if (item.Item.Description.BackgroundImage != nullptr) {
				Vector2f center = Vector2f(canvas->ViewSize.X * 0.5f, canvas->ViewSize.Y * 0.7f);
				Vector2i backgroundSize = item.Item.Description.BackgroundImage->size();

				float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
				float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));
				if (expandedAnimation3 > 0.0f) {
					backgroundSize += expandedAnimation3 * 100;
				}

				_root->DrawSolid(center.X, center.Y, IMenuContainer::BackgroundLayer - 10, Alignment::Center, Vector2f(backgroundSize.X + 2.0f, backgroundSize.Y + 2.0f), Colorf(1.0f, 1.0f, 1.0f, 0.26f));
				float alpha = 0.4f - expandedAnimation3 * 0.1f;
				_root->DrawTexture(*item.Item.Description.BackgroundImage, center.X, center.Y, IMenuContainer::BackgroundLayer, Alignment::Center, Vector2f(backgroundSize.X, backgroundSize.Y), Colorf(alpha, alpha, alpha, 1.0f));
			}
		}

		ScrollableMenuSection::OnDrawClipped(canvas);
	}

	void EpisodeSelectSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_shouldStart) {
			Vector2i viewSize = canvas->ViewSize;

			auto* command = canvas->RentRenderCommand();
			if (command->material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto* instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(Vector2f(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y)).Data());
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->setTransformation(Matrix4x4f::Identity);
			command->setLayer(999);

			canvas->DrawRenderCommand(command);
		}
	}

	void EpisodeSelectSection::OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize)
	{
		if (_shouldStart) {
			return;
		}

		ScrollableMenuSection::OnTouchEvent(event, viewSize);
	}

	void EpisodeSelectSection::OnTouchUp(int32_t newIndex, const Vector2i& viewSize, const Vector2i& touchPos)
	{
		int32_t halfW = viewSize.X / 2;
		if (std::abs(touchPos.X - halfW) < 150) {
			if (_selectedIndex == newIndex) {
				bool onExpand = (_touchLast.X > halfW + 100.0f && (_items[newIndex].Item.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue);
				if (onExpand) {
					if (_expanded) {
						OnExecuteSelected();
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
						OnExecuteSelected();
					}
				}
			} else {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				_selectedIndex = newIndex;
				_expanded = false;
				_expandedAnimation = 0.0f;
				EnsureVisibleSelected();
			}
		}
	}

	void EpisodeSelectSection::OnExecuteSelected()
	{
		auto& selectedItem = _items[_selectedIndex];
		if ((selectedItem.Item.Flags & EpisodeDataFlags::IsMissing) == EpisodeDataFlags::IsMissing) {
			return;
		}

		if ((selectedItem.Item.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
			_root->PlaySfx("MenuSelect"_s, 0.6f);

#if defined(WITH_MULTIPLAYER)
			if (_multiplayer) {
				_root->SwitchToSection<CreateServerOptionsSection>(selectedItem.Item.Description.Name, selectedItem.Item.Description.FirstLevel, selectedItem.Item.Description.PreviousEpisode);
				return;
			}
#endif

			if ((selectedItem.Item.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
				if (_expanded) {
					// Remove saved progress and restart the episode
					PreferencesCache::RemoveEpisodeContinue(selectedItem.Item.Description.Name);

					selectedItem.Item.Flags &= ~EpisodeDataFlags::CanContinue;
					_expandedAnimation = 0.0f;
					_expanded = false;
				} else {
					// Continue from the last level
					_shouldStart = true;
					_transitionTime = 1.0f;
					return;
				}
			}

			_root->SwitchToSection<StartGameOptionsSection>(selectedItem.Item.Description.Name, selectedItem.Item.Description.FirstLevel, selectedItem.Item.Description.PreviousEpisode);
		}
	}

	void EpisodeSelectSection::OnAfterTransition()
	{
		auto& selectedItem = _items[_selectedIndex];
		auto* episodeContinue = PreferencesCache::GetEpisodeContinue(selectedItem.Item.Description.Name);

		PlayerType players[] = { (PlayerType)((episodeContinue->State.DifficultyAndPlayerType >> 4) & 0x0f) };
		LevelInitialization levelInit(selectedItem.Item.Description.Name, episodeContinue->LevelName, (GameDifficulty)(episodeContinue->State.DifficultyAndPlayerType & 0x0f),
			PreferencesCache::EnableReforgedGameplay, false, players);

		if ((episodeContinue->State.Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
			levelInit.CheatsUsed = true;
		}

		auto& firstPlayer = levelInit.PlayerCarryOvers[0];
		firstPlayer.Lives = episodeContinue->State.Lives;
		firstPlayer.Score = episodeContinue->State.Score;
		std::memcpy(firstPlayer.Ammo, episodeContinue->State.Ammo, sizeof(levelInit.PlayerCarryOvers[0].Ammo));
		std::memcpy(firstPlayer.WeaponUpgrades, episodeContinue->State.WeaponUpgrades, sizeof(levelInit.PlayerCarryOvers[0].WeaponUpgrades));

		_root->ChangeLevel(std::move(levelInit));
	}

	void EpisodeSelectSection::AddEpisode(const StringView& episodeFile)
	{
		if (fs::GetExtension(episodeFile) != "j2e"_s) {
			return;
		}

		auto& resolver = ContentResolver::Get();
		std::optional<Episode> description = resolver.GetEpisodeByPath(episodeFile, true);
		if (description) {
#if defined(SHAREWARE_DEMO_ONLY)
			// Check if specified episode is unlocked, used only if compiled with SHAREWARE_DEMO_ONLY
			if (description->Name == "prince"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FormerlyAPrince) == UnlockableEpisodes::None) return;
			if (description->Name == "rescue"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::JazzInTime) == UnlockableEpisodes::None) return;
			if (description->Name == "flash"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::Flashback) == UnlockableEpisodes::None) return;
			if (description->Name == "monk"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::FunkyMonkeys) == UnlockableEpisodes::None) return;
			if ((description->Name == "xmas98"_s || description->Name == "xmas99"_s) && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::ChristmasChronicles) == UnlockableEpisodes::None) return;
			if (description->Name == "secretf"_s && (PreferencesCache::UnlockedEpisodes & UnlockableEpisodes::TheSecretFiles) == UnlockableEpisodes::None) return;
#endif

			auto& episode = _items.emplace_back();
			episode.Item.Description = std::move(*description);
			episode.Item.Flags = EpisodeDataFlags::None;

			if (!resolver.LevelExists(episode.Item.Description.Name, episode.Item.Description.FirstLevel)) {
				// Cannot find the first level of episode
				episode.Item.Flags |= EpisodeDataFlags::IsMissing;
			} else {
				if (!episode.Item.Description.PreviousEpisode.empty()) {
					auto previousEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Item.Description.PreviousEpisode);
					if (previousEpisodeEnd != nullptr && (previousEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
						episode.Item.Flags |= EpisodeDataFlags::IsAvailable;
					}
				} else {
					episode.Item.Flags |= EpisodeDataFlags::IsAvailable;
				}

				if ((episode.Item.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable) {
					auto currentEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Item.Description.Name);
					if (currentEpisodeEnd != nullptr && (currentEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
						episode.Item.Flags |= EpisodeDataFlags::IsCompleted;
						if ((currentEpisodeEnd->Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
							episode.Item.Flags |= EpisodeDataFlags::CheatsUsed;
						}
					}

					auto episodeContinue = PreferencesCache::GetEpisodeContinue(episode.Item.Description.Name);
					if (episodeContinue != nullptr) {
						episode.Item.Flags |= EpisodeDataFlags::CanContinue;
					}
				}
			}
		}
	}
}