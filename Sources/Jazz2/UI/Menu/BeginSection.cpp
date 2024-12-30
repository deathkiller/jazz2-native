#include "BeginSection.h"
#include "MenuResources.h"
#include "EpisodeSelectSection.h"
#include "StartGameOptionsSection.h"
#include "HighscoresSection.h"
#include "OptionsSection.h"
#include "AboutSection.h"
#include "MainMenu.h"

#if defined(WITH_MULTIPLAYER)
#	include "PlayCustomSection.h"
#else
#	include "CustomLevelSelectSection.h"
#endif

#include <Containers/StringConcatenable.h>
#include <Utf8.h>

#if defined(SHAREWARE_DEMO_ONLY)
#	if defined(DEATH_TARGET_EMSCRIPTEN)
#		include "ImportSection.h"
#	endif
#	include "../../PreferencesCache.h"
#endif

#include "../../../nCine/Application.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	BeginSection::BeginSection()
		: _selectedIndex(0), _animation(0.0f), _isPlayable(true), _skipSecondItem(false),
			_shouldStart(false), _alreadyStarted(false)
	{
	}

	void BeginSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (auto* mainMenu = dynamic_cast<MainMenu*>(_root)) {
			_isPlayable = ((mainMenu->_root->GetFlags() & IRootController::Flags::IsPlayable) == IRootController::Flags::IsPlayable);
		}
#endif

		_animation = 0.0f;
		_items.clear();

#if defined(SHAREWARE_DEMO_ONLY)
		if (PreferencesCache::UnlockedEpisodes != UnlockableEpisodes::None) {
			// TRANSLATORS: Menu item in main menu
			_items.emplace_back(ItemData { Item::PlayEpisodes, _("Play Story") });
		} else {
			// TRANSLATORS: Menu item in main menu (Emscripten only)
			_items.emplace_back(ItemData { Item::PlayEpisodes, _("Play Shareware Demo") });
		}

#	if defined(DEATH_TARGET_EMSCRIPTEN)
		// TRANSLATORS: Menu item in main menu (Emscripten only)
		_items.emplace_back(ItemData { Item::Import, _("Import Episodes") });
#	endif
#else
		if (_isPlayable && root->HasResumableState()) {
			// TRANSLATORS: Menu item in main menu
			_items.emplace_back(ItemData { Item::Continue, _("Continue") });
		}

		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(ItemData { Item::PlayEpisodes, _("Play Story") });

		if (_isPlayable) {
#	if defined(WITH_MULTIPLAYER)
			// TRANSLATORS: Menu item in main menu
			_items.emplace_back(ItemData { Item::PlayCustomLevels, _("Play Custom Game") });
#	else
			// TRANSLATORS: Menu item in main menu
			_items.emplace_back(ItemData { Item::PlayCustomLevels, _("Play Custom Levels") });
#	endif
		}
#endif

		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(ItemData { Item::Highscores, _("Highscores") });
		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(ItemData { Item::Options, _("Options") });
		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(ItemData { Item::About, _("About") });
#if !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
#	if defined(DEATH_TARGET_EMSCRIPTEN)
		// Show quit button only in PWA/standalone environment
		if (PreferencesCache::IsStandalone)
#	endif
		// TRANSLATORS: Menu item in main menu
		_items.emplace_back(ItemData { Item::Quit, _("Quit") });
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!_isPlayable) {
			auto& resolver = ContentResolver::Get();
			_sourcePath = fs::GetAbsolutePath(resolver.GetSourcePath());
			if (_sourcePath.empty()) {
				// If `Source` directory doesn't exist, GetAbsolutePath() will fail
				_sourcePath = resolver.GetSourcePath();
			}
#	if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
			String homeDirectory = fs::GetHomeDirectory();
			if (!homeDirectory.empty()) {
				StringView pathSeparator = fs::PathSeparator;
				if (!homeDirectory.hasSuffix(pathSeparator)) {
					homeDirectory += pathSeparator;
				}
				if (_sourcePath.hasPrefix(homeDirectory)) {
					_sourcePath = "~"_s + _sourcePath.exceptPrefix(homeDirectory.size() - pathSeparator.size());
				}
			}
#	endif
		}
#endif
	}

	void BeginSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (!_shouldStart) {
			if (_root->ActionHit(PlayerActions::Fire)) {
				ExecuteSelected();
			} else if (_root->ActionHit(PlayerActions::Menu)) {
				if (_selectedIndex != (std::int32_t)_items.size() - 1 && _items.back().Type == Item::Quit) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
					_animation = 0.0f;
					_selectedIndex = (std::int32_t)_items.size() - 1;
				}
			} else if (_root->ActionHit(PlayerActions::Up)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				if (_selectedIndex > 0) {
					_selectedIndex--;
					if (_skipSecondItem && _selectedIndex == 1) {
						_selectedIndex--;
					}
				} else {
					_selectedIndex = (std::int32_t)_items.size() - 1;
				}
			} else if (_root->ActionHit(PlayerActions::Down)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				if (_selectedIndex < (std::int32_t)_items.size() - 1) {
					_selectedIndex++;
					if (_skipSecondItem && _selectedIndex == 1) {
						_selectedIndex++;
					}
				} else {
					_selectedIndex = 0;
				}
			}
		} else {
			_transitionTime -= 0.025f * timeMult;
			if (_transitionTime <= 0.0f) {
				OnAfterTransition();
			}
		}
	}

	void BeginSection::OnDraw(Canvas* canvas)
	{
		bool canGrantPermission = false;
		bool permissionGranted = false;
#if defined(DEATH_TARGET_ANDROID)
		if (auto* mainMenu = dynamic_cast<MainMenu*>(_root)) {
			IRootController::Flags flags = mainMenu->_root->GetFlags();
			canGrantPermission = Backends::AndroidJniHelper::SdkVersion() >= 30 && (flags & IRootController::Flags::HasExternalStoragePermission) != IRootController::Flags::HasExternalStoragePermission;
			permissionGranted = (flags & (IRootController::Flags::HasExternalStoragePermission | IRootController::Flags::HasExternalStoragePermissionOnResume)) == IRootController::Flags::HasExternalStoragePermissionOnResume;
		}
#endif
		_skipSecondItem = canGrantPermission && !permissionGranted;

		std::int32_t itemCount = (std::int32_t)_items.size();
		float baseReduction = (canvas->ViewSize.Y >= 300 ? 10.0f : 24.0f);

		if (!_isPlayable) {
			itemCount += 2;
			baseReduction += (canvas->ViewSize.Y >= 252 ? 50.0f : 60.0f);
		}

		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, contentBounds.Y + baseReduction + (0.2f * (float)canvas->ViewSize.Y / itemCount));

		std::int32_t charOffset = 0;

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (!_isPlayable) {
#	if defined(DEATH_TARGET_ANDROID)
			if (permissionGranted) {
				if (_selectedIndex == 0) {
					_root->DrawElement(MenuGlow, 0, center.X, center.Y * 0.96f - 14.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.16f), 26.0f, 12.0f, true, true);
				}

				_root->DrawStringShadow(_("Access to external storage has been granted!"), charOffset, center.X, center.Y * 0.96f - 20.0f, IMenuContainer::FontLayer,
					Alignment::Bottom, Colorf(0.2f, 0.45f, 0.2f, 0.5f), 1.0f, 0.7f, 0.4f, 0.4f, 0.4f, 0.8f, 1.2f);
				_root->DrawStringShadow(_("\f[c:#337233]Restart the game to read \f[c:#9e7056]Jazz Jackrabbit 2\f[c:#337233] files correctly."), charOffset, center.X, center.Y * 0.96f, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.8f, 0.7f, 0.4f, 0.4f, 0.4f, 0.8f, 1.2f);
			} else
#	endif
			{
				if (_selectedIndex == 0) {
					_root->DrawElement(MenuGlow, 0, center.X, center.Y * 0.96f - 26.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.16f), 26.0f, 12.0f, true, true);
				}

				_root->DrawStringShadow(_("\f[c:#704a4a]This game requires original \f[c:#9e7056]Jazz Jackrabbit 2\f[c:#704a4a] files!"), charOffset, center.X, center.Y - 40.0f, IMenuContainer::FontLayer,
					Alignment::Bottom, Font::DefaultColor, 1.0f, 0.7f, 0.4f, 0.4f, 0.4f, 0.8f, 1.2f);
				_root->DrawStringShadow(_("Make sure Jazz Jackrabbit 2 files are present in following path:"), charOffset, center.X, center.Y - 20.0f, IMenuContainer::FontLayer,
					Alignment::Center, Colorf(0.44f, 0.29f, 0.29f, 0.5f), 0.8f, 0.7f, 0.4f, 0.4f, 0.4f, 0.8f, 1.2f);
				_root->DrawStringShadow(_sourcePath.data(), charOffset, center.X, center.Y - 10.0f, IMenuContainer::FontLayer,
					Alignment::Top, Colorf(0.44f, 0.44f, 0.44f, 0.5f), 0.8f, 0.7f, 0.4f, 0.4f, 0.4f, 0.8f, 1.2f);

#	if defined(DEATH_TARGET_ANDROID)
				if (canGrantPermission) {
					// TRANSLATORS: Menu item in main menu (Android 11+ only)
					auto grantPermissionText = _("Allow access to external storage");
					float grantPermissionY = (contentBounds.H >= 260 ? 40.0f : 30.0f);
					if (_selectedIndex == 0) {
						float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;
						_root->DrawElement(MenuGlow, 0, center.X, center.Y * 0.96f + grantPermissionY, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(grantPermissionText) + 1) * 0.5f * size, 4.0f * size, true, true);
						_root->DrawStringShadow(grantPermissionText, charOffset, center.X + 12.0f, center.Y * 0.96f + grantPermissionY, IMenuContainer::FontLayer,
							Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.8f);

						Vector2f grantPermissionSize = _root->MeasureString(grantPermissionText, size, 0.8f);
						_root->DrawElement(Uac, 0, ceil(center.X - grantPermissionSize.X * 0.5f - 6.0f), center.Y * 0.96f + grantPermissionY + round(sinf(canvas->AnimTime * 4.6f * fPi)), IMenuContainer::MainLayer + 10, Alignment::Center, Colorf::White, 1.0f, 1.0f);
					} else {
						_root->DrawStringShadow(grantPermissionText, charOffset, center.X + 12.0f, center.Y * 0.96f + grantPermissionY, IMenuContainer::FontLayer,
							Alignment::Center, Font::DefaultColor, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.84f);

						Vector2f grantPermissionSize = _root->MeasureString(grantPermissionText, 0.9f, 0.84f);
						_root->DrawElement(Uac, 0, ceil(center.X - grantPermissionSize.X * 0.5f - 6.0f), center.Y * 0.96f + grantPermissionY, IMenuContainer::MainLayer + 10, Alignment::Center, Colorf::White, 1.0f, 1.0f);
					}
				}
#	endif
			}
		}
#endif

		for (std::int32_t i = 0; i < (std::int32_t)_items.size(); i++) {
			_items[i].Y = center.Y;

#if !defined(DEATH_TARGET_EMSCRIPTEN)
			if (_items[i].Type <= Item::Options && !_isPlayable) {
				if (i != 0 && (i != 1 || !_skipSecondItem)) {
					if (_selectedIndex == i) {
						_root->DrawElement(MenuGlow, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.2f), (Utf8::GetLength(_items[i].Name) + 3) * 0.5f, 4.0f, true, true);
					}

					_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
						Alignment::Center, Colorf(0.51f, 0.51f, 0.51f, 0.35f), 0.9f);
				}
			}
			else
#endif
			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawElement(MenuGlow, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(_items[i].Name) + 3) * 0.5f * size, 4.0f * size, true, true);

				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
			} else {
				_root->DrawStringShadow(_items[i].Name, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			center.Y += 6.0f + (0.54f * (float)canvas->ViewSize.Y / itemCount);
		}
	}

	void BeginSection::OnDrawOverlay(Canvas* canvas)
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

	void BeginSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				for (std::int32_t i = 0; i < (std::int32_t)_items.size(); i++) {
					float itemHeight = (!_isPlayable && i == 0 ? 60.0f : 22.0f);
					if (std::abs(x - 0.5f) < 0.22f && std::abs(y - _items[i].Y) < itemHeight) {
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
			}
		}
	}

	void BeginSection::ExecuteSelected()
	{
		switch (_items[_selectedIndex].Type) {
#if !defined(SHAREWARE_DEMO_ONLY)
			case Item::Continue:
				if (_isPlayable) {
					_shouldStart = true;
					_transitionTime = 1.0f;
				}
				break;
#endif
			case Item::PlayEpisodes:
				if (_isPlayable) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
#if defined(SHAREWARE_DEMO_ONLY)
					if (PreferencesCache::UnlockedEpisodes != UnlockableEpisodes::None) {
						_root->SwitchToSection<EpisodeSelectSection>();
					} else {
						_root->SwitchToSection<StartGameOptionsSection>("share"_s, "01_share1"_s, nullptr);
					}
#else
					_root->SwitchToSection<EpisodeSelectSection>();
#endif
				}
#if !defined(DEATH_TARGET_EMSCRIPTEN)
				else {
#	if defined(DEATH_TARGET_ANDROID)
					// Show `ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION` intent on Android
					Backends::AndroidJniWrap_Activity::requestExternalStoragePermission();
#	else
					// `_sourcePath` contains adjusted path for display purposes
					auto& resolver = ContentResolver::Get();
					String sourcePath = fs::GetAbsolutePath(resolver.GetSourcePath());
					if (sourcePath.empty()) {
						// If `Source` directory doesn't exist, GetAbsolutePath() will fail
						sourcePath = resolver.GetSourcePath();
					}
					fs::CreateDirectories(sourcePath);
					if (fs::LaunchDirectoryAsync(sourcePath)) {
						_root->PlaySfx("MenuSelect"_s, 0.6f);
					}
#	endif
				}
#endif
				break;
#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)
			case Item::Import:
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->SwitchToSection<ImportSection>();
				break;
#else
			case Item::PlayCustomLevels:
				if (_isPlayable) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
#	if defined(WITH_MULTIPLAYER)
					_root->SwitchToSection<PlayCustomSection>();
#	else
					_root->SwitchToSection<CustomLevelSelectSection>();
#	endif
				}
				break;
#endif
			case Item::Highscores:
				if (_isPlayable) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
					_root->SwitchToSection<HighscoresSection>();
				}
				break;
			case Item::Options:
				if (_isPlayable) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
					_root->SwitchToSection<OptionsSection>();
				}
				break;
			case Item::About:
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_root->SwitchToSection<AboutSection>();
				break;
#if !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
			case Item::Quit:
				theApplication().Quit();
				break;
#endif
		}
	}

	void BeginSection::OnAfterTransition()
	{
#if !defined(SHAREWARE_DEMO_ONLY)
		switch (_items[_selectedIndex].Type) {
			case Item::Continue:
				if (!_alreadyStarted) {
					_root->ResumeSavedState();
					// Start it only once to avoid infinite loops in case deserialization fails (e.g., incompatible versions)
					_alreadyStarted = true;
				}
				break;
		}
#endif
	}
}