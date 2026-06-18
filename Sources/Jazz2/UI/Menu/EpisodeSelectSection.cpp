#include "EpisodeSelectSection.h"
#include "CustomLevelSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#if defined(WITH_MULTIPLAYER)
#	include "CreateServerOptionsSection.h"
#endif

#include <cstring>
#include <Containers/StringConcatenable.h>
#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	// Row height matching the original section (ItemHeight * 4 / 5)
	static constexpr float RowHeight = 40 * 4 / 5;	// 32

	EpisodeSelectSection::EpisodeSelectSection(bool multiplayer, bool privateServer)
		: _expandedAnimation(0.0f), _transitionTime(0.0f), _transitionFromEpisode(-1), _transitionFromEpisodeTime(0.0f),
			_animation(0.0f), _multiplayer(multiplayer), _privateServer(privateServer), _expanded(false), _shouldStart(false), _list(nullptr)
	{
		auto& resolver = ContentResolver::Get();

		// Search both "Content/Episodes/" and "Cache/Episodes/"
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetContentPath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddEpisode(item);
		}

		for (auto item : fs::Directory(fs::CombinePath(resolver.GetCachePath(), "Episodes"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddEpisode(item);
		}

		std::int32_t maxPosition = 0;
		for (const EpisodeData& item : _episodes) {
			if (maxPosition < item.Description.Position && item.Description.Position < UINT16_MAX - 2) {
				maxPosition = item.Description.Position;
			}
		}

		// Move all episodes with Position = 0 to the end of the list
		for (EpisodeData& item : _episodes) {
			if (item.Description.Position == 0) {
				item.Description.Position = ++maxPosition;
			}
		}

		nCine::sort(_episodes.begin(), _episodes.end(), [](const EpisodeData& a, const EpisodeData& b) -> bool {
			return (a.Description.Position < b.Description.Position);
		});
	}

	void EpisodeSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		auto list = std::make_unique<ScrollView>();
		// Episodes draw oversized title logos, so reserve a little extra space at the top and bottom of the scroll content
		list->ContentPadding = 6.0f;
		for (std::int32_t i = 0; i < (std::int32_t)_episodes.size(); i++) {
			std::int32_t row = i;
			auto* item = list->Add<CanvasWidget>(RowHeight);
			item->OnDrawContent = [this, row](IMenuContainer* r, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation) {
				DrawEpisodeRow(r, canvas, bounds, charOffset, selected, row);
			};
			item->OnTouch = [this, row](Vector2i touchPos) {
				HandleTap(row, touchPos);
			};
		}
		_list = list.get();

		SetContent(std::move(list));
	}

	void EpisodeSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_content != nullptr) {
			// Drive the scroll view's momentum and item animations (navigation is handled here instead)
			_content->OnUpdate(timeMult);
		}

		if (_expanded && _expandedAnimation < 1.0f) {
			_expandedAnimation = std::min(_expandedAnimation + timeMult * 0.016f, 1.0f);
		}
		if (_transitionFromEpisode != -1) {
			_transitionFromEpisodeTime = lerpByTime(_transitionFromEpisodeTime, 0.0f, 0.07f, timeMult);
			if (std::abs(_transitionFromEpisodeTime) < 0.01f) {
				_transitionFromEpisode = -1;
			}
		}

		bool navUp, navDown, navSound;
		UpdateNavigation(timeMult, navUp, navDown, navSound);

		if (!_shouldStart) {
			if (_root->ActionHit(PlayerAction::Menu)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->LeaveSection();
				return;
			} else if (!_episodes.empty()) {
				std::int32_t row = GetSelectedRow();
				if (_root->ActionHit(PlayerAction::Fire)) {
					ExecuteSelected();
				} else if (_root->ActionHit(PlayerAction::Left)) {
					if (!_multiplayer && _expanded) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = false;
						_expandedAnimation = 0.0f;
					}
				} else if (_root->ActionHit(PlayerAction::Right)) {
					if (!_multiplayer && !_expanded && row >= 0 && (_episodes[row].Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_expanded = true;
					}
				} else if (_episodes.size() > 1) {
					if (navUp) {
						if (navSound) _root->PlaySfx("MenuSelect"_s, 0.5f);
						_animation = 0.0f;
						_transitionFromEpisode = row;
						_transitionFromEpisodeTime = -1.0f;
						_expanded = false;
						_expandedAnimation = 0.0f;
						std::int32_t newRow = (row > 0 ? row - 1 : (std::int32_t)_episodes.size() - 1);
						_list->SetSelectedIndex(newRow, false);
					} else if (navDown) {
						if (navSound) _root->PlaySfx("MenuSelect"_s, 0.5f);
						_animation = 0.0f;
						_transitionFromEpisode = row;
						_transitionFromEpisodeTime = 1.0f;
						_expanded = false;
						_expandedAnimation = 0.0f;
						std::int32_t newRow = (row < (std::int32_t)_episodes.size() - 1 ? row + 1 : 0);
						_list->SetSelectedIndex(newRow, false);
					}
				}
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
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 42.0f;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(
#if defined(WITH_MULTIPLAYER)
			_multiplayer ? (_privateServer ? _("Create Private Server") : _("Create Public Server")) : _("Play Story"),
#else
			_("Play Story"),
#endif
			charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer, Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f),
			0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void EpisodeSelectSection::OnDrawClipped(Canvas* canvas)
	{
		if (_episodes.empty()) {
			Vector2i viewSize = canvas->ViewSize;
			std::int32_t charOffset = 0;
			_root->DrawStringShadow(_("No episode found!"), charOffset, viewSize.X * 0.5f, viewSize.Y * 0.55f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		std::int32_t row = GetSelectedRow();
		bool inTransition = false;
		if (_transitionFromEpisode != -1 && _transitionFromEpisode != row) {
			auto& item = _episodes[_transitionFromEpisode];
			if (item.Description.BackgroundImage != nullptr) {
				Vector2f center = Vector2f(canvas->ViewSize.X * 0.5f, canvas->ViewSize.Y * 0.7f);
				Vector2i backgroundSize = item.Description.BackgroundImage->GetSize();

				float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
				float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));
				if (expandedAnimation3 > 0.0f) {
					backgroundSize += expandedAnimation3 * 100;
				}

				inTransition = true;
				_root->DrawSolid(center.X, center.Y, IMenuContainer::BackgroundLayer - 10, Alignment::Center, Vector2f(backgroundSize.X + 2.0f, backgroundSize.Y + 2.0f), Colorf(1.0f, 1.0f, 1.0f, 0.26f));
				float alpha = 0.4f - expandedAnimation3 * 0.1f;
				_root->DrawTexture(*item.Description.BackgroundImage, center.X, center.Y, IMenuContainer::BackgroundLayer - 1, Alignment::Center, Vector2f(backgroundSize.X, backgroundSize.Y), Colorf(alpha, alpha, alpha, 1.0f));
			}
		}

		if (row >= 0) {
			auto& item = _episodes[row];
			if (item.Description.BackgroundImage != nullptr) {
				Vector2f center = Vector2f(canvas->ViewSize.X * 0.5f, canvas->ViewSize.Y * 0.7f);
				Vector2i backgroundSize = item.Description.BackgroundImage->GetSize();

				float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
				float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));
				if (expandedAnimation3 > 0.0f) {
					backgroundSize += expandedAnimation3 * 100;
				}

				float transitionOffset = 0.0f, transitionAlpha = 1.0f;
				if (!inTransition) {
					_root->DrawSolid(center.X, center.Y, IMenuContainer::BackgroundLayer - 10, Alignment::Center, Vector2f(backgroundSize.X + 2.0f, backgroundSize.Y + 2.0f), Colorf(1.0f, 1.0f, 1.0f, 0.26f));
				} else {
					transitionOffset = 50.0f * _transitionFromEpisodeTime;
					transitionAlpha = 1.0f - 0.5 * std::abs(_transitionFromEpisodeTime);
				}

				float alpha = 0.4f - expandedAnimation3 * 0.1f;
				_root->DrawTexture(*item.Description.BackgroundImage, center.X, center.Y + transitionOffset, IMenuContainer::BackgroundLayer, Alignment::Center, Vector2f(backgroundSize.X, backgroundSize.Y), Colorf(alpha, alpha, alpha, transitionAlpha));
			}
		}

		WidgetSection::OnDrawClipped(canvas);
	}

	void EpisodeSelectSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_shouldStart) {
			Vector2i viewSize = canvas->ViewSize;

			auto* command = canvas->RentRenderCommand();
			if (command->GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Transition))) {
				command->GetMaterial().ReserveUniformsDataMemory();
				command->GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			}

			command->GetMaterial().SetBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto* instanceBlock = command->GetMaterial().UniformBlock(Material::InstanceBlockName);
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatVector(Vector4f(1.0f, 0.0f, 1.0f, 0.0f).Data());
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(Vector2f(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y)).Data());
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(0.0f, 0.0f, 0.0f, _transitionTime).Data());

			command->SetTransformation(Matrix4x4f::Identity);
			command->SetLayer(999);

			canvas->DrawRenderCommand(command);
		}
	}

	void EpisodeSelectSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (_shouldStart) {
			return;
		}

		WidgetSection::OnTouchEvent(event, viewSize);
	}

	void EpisodeSelectSection::DrawEpisodeRow(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool isSelected, std::int32_t row)
	{
		auto& item = _episodes[row];
		float centerX = canvas->ViewSize.X * 0.5f;
		float itemY = bounds.Y + bounds.H * 0.5f;

		if ((item.Flags & EpisodeDataFlags::IsMissing) == EpisodeDataFlags::IsMissing) {
			if (isSelected) {
				root->DrawStringGlow(item.Description.DisplayName, charOffset, centerX, itemY, IMenuContainer::FontLayer,
					Alignment::Center, Colorf(0.51f, 0.51f, 0.51f, 0.35f), 0.9f);
			} else {
				root->DrawStringShadow(item.Description.DisplayName, charOffset, centerX, itemY, IMenuContainer::FontLayer,
					Alignment::Center, Colorf(0.51f, 0.51f, 0.51f, 0.35f), 0.9f);
			}
		} else if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			if ((item.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
				float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
				float expandedAnimation3 = (expandedAnimation2 * expandedAnimation2 * (3.0f - 2.0f * expandedAnimation2));

				if (item.Description.TitleImage != nullptr) {
					Vector2i titleSize = item.Description.TitleImage->GetSize() / 2;
					root->DrawTexture(*item.Description.TitleImage, centerX, itemY + 2.2f, IMenuContainer::FontLayer + 8, Alignment::Center, Vector2f(titleSize.X, titleSize.Y) * size * (1.0f - expandedAnimation3 * 0.2f) * 1.02f, Colorf(0.0f, 0.0f, 0.0f, 0.26f - expandedAnimation3 * 0.1f), true);
					float alpha = 1.0f - expandedAnimation3 * 0.4f;
					root->DrawTexture(*item.Description.TitleImage, centerX, itemY, IMenuContainer::FontLayer + 10, Alignment::Center, Vector2f(titleSize.X, titleSize.Y) * size * (1.0f - expandedAnimation3 * 0.2f), Colorf(alpha, alpha, alpha, 1.0f), true);
				} else {
					Colorf nameColor = Font::RandomColor;
					nameColor.SetAlpha(0.5f - expandedAnimation3 * 0.15f);
					root->DrawStringGlow(item.Description.DisplayName, charOffset, centerX, itemY, IMenuContainer::FontLayer + 10,
						Alignment::Center, nameColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				}

				if (!_multiplayer && (item.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
					float expandX = centerX + (item.Description.DisplayName.size() + 3) * 2.8f * size + (canvas->ViewSize.X >= 400 ? 40.0f : 0.0f);
					float moveX = expandedAnimation3 * -12.0f;
					root->DrawStringShadow(">"_s, charOffset, expandX + moveX, itemY, IMenuContainer::FontLayer + 20,
						Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);

					if (_expanded) {
						float expandedAnimation4 = IMenuContainer::EaseOutElastic(_expandedAnimation) * 0.8f;

						// TRANSLATORS: Menu subitem in Play Story section
						Vector2f textSize = root->MeasureString(_("Restart episode"), 0.8f, 0.8f);
						root->DrawStringShadow(_("Restart episode"), charOffset, expandX + textSize.X * 0.5f - 2.0f, itemY, IMenuContainer::FontLayer + 22,
							Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f * std::min(1.0f, 0.4f + expandedAnimation3)), expandedAnimation4, 0.4f, 0.6f, 0.6f, 0.6f, 0.8f);
					}
				}
			} else {
				std::int32_t prevEpisodeIndex = -1;
				if (!item.Description.PreviousEpisode.empty()) {
					for (std::int32_t j = 0; j < (std::int32_t)_episodes.size(); j++) {
						if (item.Description.PreviousEpisode == _episodes[j].Description.Name) {
							prevEpisodeIndex = j;
							break;
						}
					}
				}

				root->DrawStringGlow(item.Description.DisplayName, charOffset, centerX, itemY, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::TransparentRandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				if (prevEpisodeIndex != -1) {
					// TRANSLATORS: Information in Play Story section that episode is locked because the previous episode is not complete
					root->DrawStringShadow(_f("You must complete \"{}\" first!", _episodes[prevEpisodeIndex].Description.DisplayName), charOffset, centerX, itemY, IMenuContainer::FontLayer + 20,
						Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					// TRANSLATORS: Information in Play Story section that episode is locked
					root->DrawStringShadow(_("Episode is locked!"), charOffset, centerX, itemY, IMenuContainer::FontLayer + 20,
						Alignment::Center, Colorf(0.66f, 0.42f, 0.32f, std::min(0.5f, 0.2f + 2.0f * _animation)), 0.7f * size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				}
			}
		} else {
			root->DrawStringShadow(item.Description.DisplayName, charOffset, centerX, itemY, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}

		if ((item.Flags & (EpisodeDataFlags::IsCompleted | EpisodeDataFlags::IsAvailable)) == (EpisodeDataFlags::IsCompleted | EpisodeDataFlags::IsAvailable)) {
			float size = (isSelected ? 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f : 0.7f);
			float textWidth = item.Description.DisplayName.size() + 3;
			if (isSelected) {
				if (item.Description.Name == "prince"_s || item.Description.Name == "share"_s) {
					// "Formerly a Prince" & "Shareware Demo" title image is too narrow, so try to adjust it a bit
					textWidth *= 0.8f;
				} else if (item.Description.Name == "flash"_s) {
					// "Flashback" title image is too wide, so try to adjust it a bit
					textWidth *= 1.5f;
				}
			}
			float expandedAnimation2 = std::min(_expandedAnimation * 6.0f, 1.0f);
			float expandX = centerX - textWidth * 4.0f * (isSelected ? (size - (expandedAnimation2 * 0.12f)) : 1.1f) + 10.0f;
			root->DrawElement(EpisodeComplete, 0, expandX, itemY - 2.0f, IMenuContainer::MainLayer + (isSelected ? 20 : 10), Alignment::Right,
				((item.Flags & EpisodeDataFlags::CheatsUsed) == EpisodeDataFlags::CheatsUsed ? Colorf::Black : Colorf::White), size, size);
		}
	}

	void EpisodeSelectSection::ExecuteSelected()
	{
		std::int32_t row = GetSelectedRow();
		if (row < 0) {
			return;
		}
		auto& selectedItem = _episodes[row];
		if ((selectedItem.Flags & EpisodeDataFlags::IsMissing) == EpisodeDataFlags::IsMissing) {
			return;
		}

		if ((selectedItem.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable || PreferencesCache::AllowCheatsUnlock) {
			_root->PlaySfx("MenuSelect"_s, 0.6f);

#if defined(WITH_MULTIPLAYER)
			if (_multiplayer) {
				if ((selectedItem.Flags & EpisodeDataFlags::RedirectToCustomLevels) == EpisodeDataFlags::RedirectToCustomLevels) {
					_root->SwitchToSection<CustomLevelSelectSection>(true, _privateServer);
					return;
				}

				_root->SwitchToSection<CreateServerOptionsSection>(String(selectedItem.Description.Name + '/' + selectedItem.Description.FirstLevel),
					selectedItem.Description.PreviousEpisode, _privateServer);
				return;
			}
#endif

			if ((selectedItem.Flags & EpisodeDataFlags::RedirectToCustomLevels) == EpisodeDataFlags::RedirectToCustomLevels) {
				_root->SwitchToSection<CustomLevelSelectSection>();
				return;
			}

			if ((selectedItem.Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue) {
				if (_expanded) {
					// Remove saved progress and restart the episode
					PreferencesCache::RemoveEpisodeContinue(selectedItem.Description.Name);

					selectedItem.Flags &= ~EpisodeDataFlags::CanContinue;
					_expandedAnimation = 0.0f;
					_expanded = false;
				} else {
					// Continue from the last level
					_shouldStart = true;
					_transitionTime = 1.0f;
					return;
				}
			}

			_root->SwitchToSection<StartGameOptionsSection>(String(selectedItem.Description.Name + '/' + selectedItem.Description.FirstLevel),
				selectedItem.Description.PreviousEpisode);
		}
	}

	void EpisodeSelectSection::HandleTap(std::int32_t newIndex, Vector2i touchPos)
	{
		if (newIndex < 0 || newIndex >= (std::int32_t)_episodes.size()) {
			return;
		}

		Vector2i viewSize = _root->GetViewSize();
		std::int32_t halfW = viewSize.X / 2;
		if (std::abs(touchPos.X - halfW) < 150) {
			if (GetSelectedRow() == newIndex) {
				bool onExpand = (touchPos.X > halfW + 100.0f && (_episodes[newIndex].Flags & EpisodeDataFlags::CanContinue) == EpisodeDataFlags::CanContinue);
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
				_expanded = false;
				_expandedAnimation = 0.0f;
				_list->SetSelectedIndex(newIndex, false);
			}
		}
	}

	void EpisodeSelectSection::OnAfterTransition()
	{
		std::int32_t row = GetSelectedRow();
		if (row < 0) {
			return;
		}
		auto& selectedItem = _episodes[row];
		auto* episodeContinue = PreferencesCache::GetEpisodeContinue(selectedItem.Description.Name);

		PlayerType players[] = { (PlayerType)((episodeContinue->State.DifficultyAndPlayerType >> 4) & 0x0f) };
		LevelInitialization levelInit(String(selectedItem.Description.Name + '/' + episodeContinue->LevelName), (GameDifficulty)(episodeContinue->State.DifficultyAndPlayerType & 0x0f),
			PreferencesCache::EnableReforgedGameplay, false, players);

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

		_root->ChangeLevel(std::move(levelInit));
	}

	std::int32_t EpisodeSelectSection::GetSelectedRow() const
	{
		return (_list != nullptr ? _list->GetSelectedIndex() : -1);
	}

	void EpisodeSelectSection::AddEpisode(StringView episodeFile)
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

			auto& episode = _episodes.emplace_back();
			episode.Description = std::move(*description);
			episode.Flags = EpisodeDataFlags::None;

			if (episode.Description.FirstLevel == ":custom-levels"_s) {
				episode.Flags |= EpisodeDataFlags::RedirectToCustomLevels | EpisodeDataFlags::IsAvailable;
				episode.Description.DisplayName = _("Play Custom Levels");
			} else {
				if (!resolver.LevelExists(String(episode.Description.Name + '/' + episode.Description.FirstLevel))) {
					// Cannot find the first level of episode in dedicated directory, try to search also "unknown" directory
					if (resolver.LevelExists(String("unknown/"_s + episode.Description.FirstLevel))) {
						episode.Flags |= EpisodeDataFlags::LevelsInUnknownDirectory;
					} else {
						episode.Flags |= EpisodeDataFlags::IsMissing;
					}
				}

				if ((episode.Flags & EpisodeDataFlags::IsMissing) != EpisodeDataFlags::IsMissing) {
					if (!episode.Description.PreviousEpisode.empty()) {
						auto previousEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Description.PreviousEpisode);
						if (previousEpisodeEnd != nullptr && (previousEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
							episode.Flags |= EpisodeDataFlags::IsAvailable;
						}
					} else {
						episode.Flags |= EpisodeDataFlags::IsAvailable;
					}

					if ((episode.Flags & EpisodeDataFlags::IsAvailable) == EpisodeDataFlags::IsAvailable) {
						auto currentEpisodeEnd = PreferencesCache::GetEpisodeEnd(episode.Description.Name);
						if (currentEpisodeEnd != nullptr && (currentEpisodeEnd->Flags & EpisodeContinuationFlags::IsCompleted) == EpisodeContinuationFlags::IsCompleted) {
							episode.Flags |= EpisodeDataFlags::IsCompleted;
							if ((currentEpisodeEnd->Flags & EpisodeContinuationFlags::CheatsUsed) == EpisodeContinuationFlags::CheatsUsed) {
								episode.Flags |= EpisodeDataFlags::CheatsUsed;
							}
						}

						auto* episodeContinue = PreferencesCache::GetEpisodeContinue(episode.Description.Name);
						if (episodeContinue != nullptr) {
							episode.Flags |= EpisodeDataFlags::CanContinue;
						}
					}
				}
			}
		}
	}
}
