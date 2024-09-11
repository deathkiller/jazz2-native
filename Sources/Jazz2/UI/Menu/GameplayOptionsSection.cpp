#include "GameplayOptionsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "GameplayEnhancementsSection.h"
#include "LanguageSelectSection.h"
#include "RefreshCacheSection.h"
#include "../RgbLights.h"
#include "../../PreferencesCache.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
#	include "../DiscordRpcClient.h"
#endif

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	GameplayOptionsSection::GameplayOptionsSection()
		: _isDirty(false)
	{
	}

	GameplayOptionsSection::~GameplayOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void GameplayOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		bool isInGame = dynamic_cast<InGameMenu*>(_root);

		_items.clear();

		// TRANSLATORS: Menu item in Options > Gameplay section
		_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::Enhancements, _("Enhancements") });
		// TRANSLATORS: Menu item in Options > Gameplay section
		_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::Language, _("Language") });
#if defined(WITH_ANGELSCRIPT)
		if (!isInGame) {
			// TRANSLATORS: Menu item in Options > Gameplay section
			_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::AllowUnsignedScripts, _("Scripting"), true });
		}
#endif
		if (!isInGame) {
			// TRANSLATORS: Menu item in Options > Gameplay section
			_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::AllowCheats, _("Allow Cheats"), true });
			// TRANSLATORS: Menu item in Options > Gameplay section
			_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::OverwriteEpisodeEnd, _("Overwrite Episode Completion"), true });
		}
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > Gameplay section
		_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::EnableDiscordIntegration, _("Discord Integration"), true });
#endif
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
		// TRANSLATORS: Menu item in Options > Gameplay section
		_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::EnableRgbLights, _("Razer Chroma™"), true });
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > Gameplay section
		_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::BrowseSourceDirectory, _("Browse \"Source\" Directory") });
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!isInGame) {
			// TRANSLATORS: Menu item in Options > Gameplay section
			_items.emplace_back(GameplayOptionsItem { GameplayOptionsItemType::RefreshCache, _("Refresh Cache") });
		}
#endif
	}

	void GameplayOptionsSection::OnHandleInput()
	{
		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerActions::Left) || _root->ActionHit(PlayerActions::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}

	void GameplayOptionsSection::OnDraw(Canvas* canvas)
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
		_root->DrawStringShadow(_("Gameplay"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void GameplayOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = (item.Item.HasBooleanValue ? 52 : ItemHeight);
	}

	void GameplayOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			if (item.Item.HasBooleanValue) {
				_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}

		if (item.Item.HasBooleanValue) {
			StringView customText;
			bool enabled;
			switch (item.Item.Type) {
#if defined(WITH_ANGELSCRIPT)
				case GameplayOptionsItemType::AllowUnsignedScripts: enabled = PreferencesCache::AllowUnsignedScripts; break;
#endif
				// TRANSLATORS: Option for Allow Cheats in Options > Gameplay section
				case GameplayOptionsItemType::AllowCheats: enabled = PreferencesCache::AllowCheats; customText = (enabled ? _("\f[c:#d86664]Yes\f[/c]") : _("No")); break;
				case GameplayOptionsItemType::OverwriteEpisodeEnd:
					customText = (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::NoCheatsOnly
						// TRANSLATORS: Option for Overwrite Episode Completion in Options > Gameplay section
						? _("No Cheats Only")
						: (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::HigherScoreOnly
							// TRANSLATORS: Option for Overwrite Episode Completion in Options > Gameplay section
							? _("Higher Score Only")
							// TRANSLATORS: Option for Overwrite Episode Completion in Options > Gameplay section
							: _("Always")));
					break;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
				case GameplayOptionsItemType::EnableDiscordIntegration: enabled = PreferencesCache::EnableDiscordIntegration; break;
#endif
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
				case GameplayOptionsItemType::EnableRgbLights: enabled = PreferencesCache::EnableRgbLights; break;
#endif
				default: enabled = false; break;
			}

#if defined(WITH_ANGELSCRIPT)
			if (item.Item.Type == GameplayOptionsItemType::AllowUnsignedScripts && enabled) {
				_root->DrawStringShadow(_("Enabled"), charOffset, centerX + 12.0f, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
					Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);

				Vector2f textSize = _root->MeasureString(_("Enabled"), 0.8f);
				_root->DrawElement(Uac, 0, ceil(centerX - textSize.X * 0.5f - 6.0f), item.Y + 22.0f - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Center, Colorf::White);
			} else
#endif
			{
				_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
					Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor), 0.8f);
			}
		}
	}

	void GameplayOptionsSection::OnExecuteSelected()
	{
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		switch (_items[_selectedIndex].Item.Type) {
			case GameplayOptionsItemType::Enhancements: _root->SwitchToSection<GameplayEnhancementsSection>(); break;
			case GameplayOptionsItemType::Language: _root->SwitchToSection<LanguageSelectSection>(); break;
#if defined(WITH_ANGELSCRIPT)
			case GameplayOptionsItemType::AllowUnsignedScripts: {
				PreferencesCache::AllowUnsignedScripts = !PreferencesCache::AllowUnsignedScripts;
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
#endif
			case GameplayOptionsItemType::AllowCheats: {
				PreferencesCache::AllowCheats = !PreferencesCache::AllowCheats;
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
			case GameplayOptionsItemType::OverwriteEpisodeEnd: {
				PreferencesCache::OverwriteEpisodeEnd = (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::NoCheatsOnly
					? EpisodeEndOverwriteMode::HigherScoreOnly
					: (PreferencesCache::OverwriteEpisodeEnd == EpisodeEndOverwriteMode::HigherScoreOnly
						? EpisodeEndOverwriteMode::Always
						: EpisodeEndOverwriteMode::NoCheatsOnly));
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			case GameplayOptionsItemType::EnableDiscordIntegration: {
				PreferencesCache::EnableDiscordIntegration = !PreferencesCache::EnableDiscordIntegration;
				if (!PreferencesCache::EnableDiscordIntegration) {
					DiscordRpcClient::Get().Disconnect();
				}
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
#endif
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
			case GameplayOptionsItemType::EnableRgbLights: {
				PreferencesCache::EnableRgbLights = !PreferencesCache::EnableRgbLights;
				if (!PreferencesCache::EnableRgbLights) {
					RgbLights::Get().Clear();
				}
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
#endif
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_UNIX)
			case GameplayOptionsItemType::BrowseSourceDirectory: {
				auto& resolver = ContentResolver::Get();
				String sourcePath = fs::GetAbsolutePath(resolver.GetSourcePath());
				if (sourcePath.empty()) {
					// If `Source` directory doesn't exist, GetAbsolutePath() will fail
					sourcePath = resolver.GetSourcePath();
				}
				fs::CreateDirectories(sourcePath);
				fs::LaunchDirectoryAsync(sourcePath);
				break;
			}
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			case GameplayOptionsItemType::RefreshCache: _root->SwitchToSection<RefreshCacheSection>(); break;
#endif
		}
	}
}