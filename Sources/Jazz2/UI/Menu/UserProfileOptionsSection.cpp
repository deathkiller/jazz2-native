#include "UserProfileOptionsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../DiscordRpcClient.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"

#include <Utf8.h>
#include <Containers/StringConcatenable.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	UserProfileOptionsSection::UserProfileOptionsSection()
		: _isDirty(false), _waitForInput(false), _textCursor(0), _carretAnim(0.0f)
#if defined(DEATH_TARGET_ANDROID)
			, _recalcVisibleBoundsTimeLeft(30.0f)
#endif
	{
		_localPlayerName = PreferencesCache::PlayerName;
		if (_localPlayerName.empty()) {
			_localPlayerName = theApplication().GetUserName();
			if (_localPlayerName.empty()) {
				_localPlayerName = "Unknown"_s;
			}
		}

#if defined(DEATH_TARGET_ANDROID)
		auto androidId = Backends::AndroidJniWrap_Secure::getAndroidId();
		if (!androidId.empty()) {
			_deviceId = "A:"_s + androidId;
		}

		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
		_initialVisibleSize.X = _currentVisibleBounds.W;
		_initialVisibleSize.Y = _currentVisibleBounds.H;
#endif
	}

	UserProfileOptionsSection::~UserProfileOptionsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::PlayerName = std::move(_localPlayerName);
			PreferencesCache::Save();
		}
	}

	void UserProfileOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		bool isInGame = runtime_cast<InGameMenu*>(_root);

		_items.clear();

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::EnableDiscordIntegration, _("Discord Integration"), true, isInGame });
#endif
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::PlayerName, _("Player Name"), false, isInGame });
#if defined(WITH_MULTIPLAYER)
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::UniquePlayerID, _("Unique Player ID") });
#endif
	}

	void UserProfileOptionsSection::OnHandleInput()
	{
		if (_waitForInput) {
			return;
		}

		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}


	void UserProfileOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
#if defined(DEATH_TARGET_ANDROID)
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (x < 0.2f && y < 80.0f && _waitForInput) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					auto& app = static_cast<AndroidApplication&>(theApplication());
					app.ShowSoftInput();
					RecalcLayoutForSoftInput();
					return;
				}
			}
		}
#endif

		ScrollableMenuSection::OnTouchEvent(event, viewSize);
	}

	void UserProfileOptionsSection::OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos)
	{
		if (!_waitForInput) {
			ScrollableMenuSection::OnTouchUp(newIndex, viewSize, touchPos);
		}
	}

	void UserProfileOptionsSection::OnUpdate(float timeMult)
	{
		// Move the variable to stack to fix leaving the section
		bool waitingForInput = _waitForInput;

		ScrollableMenuSection::OnUpdate(timeMult);

		if (waitingForInput) {
#if defined(DEATH_TARGET_ANDROID)
			_recalcVisibleBoundsTimeLeft -= timeMult;
			if (_recalcVisibleBoundsTimeLeft <= 0.0f) {
				_recalcVisibleBoundsTimeLeft = 60.0f;
				_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();
			}

			if (_root->ActionHit(PlayerAction::ChangeWeapon)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				auto& app = static_cast<AndroidApplication&>(theApplication());
				app.ToggleSoftInput();
				RecalcLayoutForSoftInput();
			} else
#endif
			if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Run)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				_localPlayerName = std::move(_prevPlayerName);
			} else if (_root->ActionHit(PlayerAction::Fire)) {
				if (!_localPlayerName.empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					_isDirty = true;
				}
			}

			EnsureVisibleSelected();

			_carretAnim += timeMult;
		}
	}

	void UserProfileOptionsSection::OnDraw(Canvas* canvas)
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
		_root->DrawStringShadow(_("User Profile"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void UserProfileOptionsSection::OnDrawOverlay(Canvas* canvas)
	{
#if defined(DEATH_TARGET_ANDROID)
		if (_waitForInput) {
			_root->DrawElement(ShowKeyboard, -1, 36.0f, 24.0f, IMenuContainer::MainLayer, Alignment::TopLeft, Colorf::White);

			if (_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y) {
				Vector2i viewSize = _root->GetViewSize();
				if (_currentVisibleBounds.Y * viewSize.Y / _initialVisibleSize.Y < 32.0f) {
					_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft, Vector2f(viewSize.X, viewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

					std::int32_t charOffset = 0;
					_root->DrawStringShadow(_localPlayerName, charOffset, 120.0f, 52.0f, IMenuContainer::MainLayer,
						Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

					Vector2f textToCursorSize = _root->MeasureString(_localPlayerName.prefix(_textCursor), 1.0f);
					_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, 52.0f - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
						Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
				}
			}
		}
#endif
	}

	void UserProfileOptionsSection::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_waitForInput) {
			switch (event.sym) {
				case Keys::Escape: {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					_localPlayerName = std::move(_prevPlayerName);
#if defined(DEATH_TARGET_ANDROID)
					auto& app = static_cast<AndroidApplication&>(theApplication());
					app.HideSoftInput();
					RecalcLayoutForSoftInput();
#endif
					break;
				}
				case Keys::Return: {
					if (!_localPlayerName.empty()) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_waitForInput = false;
						_isDirty = true;
#if defined(DEATH_TARGET_ANDROID)
						auto& app = static_cast<AndroidApplication&>(theApplication());
						app.HideSoftInput();
						RecalcLayoutForSoftInput();
#endif
					}
					break;
				}
				case Keys::Backspace: {
					if (_textCursor > 0) {
						auto [_, prevPos] = Utf8::PrevChar(_localPlayerName, _textCursor);
						_localPlayerName = _localPlayerName.prefix(prevPos) + _localPlayerName.exceptPrefix(_textCursor);
						_textCursor = prevPos;
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Delete: {
					if (_textCursor < _localPlayerName.size()) {
						auto [_, nextPos] = Utf8::NextChar(_localPlayerName, _textCursor);
						_localPlayerName = _localPlayerName.prefix(_textCursor) + _localPlayerName.exceptPrefix(nextPos);
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Left: {
					if (_textCursor > 0) {
						auto [c, prevPos] = Utf8::PrevChar(_localPlayerName, _textCursor);
						_textCursor = prevPos;
						_carretAnim = 0.0f;
					}
					break;
				}
				case Keys::Right: {
					if (_textCursor < _localPlayerName.size()) {
						auto [c, nextPos] = Utf8::NextChar(_localPlayerName, _textCursor);
						_textCursor = nextPos;
						_carretAnim = 0.0f;
					}
					break;
				}
			}
		}
	}

	void UserProfileOptionsSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_waitForInput) {
			if (_localPlayerName.size() + event.length <= MaxPlayerNameLength) {
				_localPlayerName = _localPlayerName.prefix(_textCursor)
					+ StringView(event.text, event.length)
					+ _localPlayerName.exceptPrefix(_textCursor);
				_textCursor += event.length;
				_carretAnim = 0.0f;
			}
		}
	}

	NavigationFlags UserProfileOptionsSection::GetNavigationFlags() const
	{
		return (_waitForInput ? NavigationFlags::AllowGamepads : NavigationFlags::AllowAll);
	}

	void UserProfileOptionsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		switch (item.Item.Type) {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			case UserProfileOptionsItemType::EnableDiscordIntegration: item.Height = 64; break;
#endif
			case UserProfileOptionsItemType::PlayerName: item.Height = 56; break;
#if defined(WITH_MULTIPLAYER)
			case UserProfileOptionsItemType::UniquePlayerID: item.Height = 64; break;
#endif
			default: item.Height = (item.Item.HasBooleanValue ? 52 : ItemHeight); break;
		}
	}

	void UserProfileOptionsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (Utf8::GetLength(item.Item.DisplayName) + 3) * 0.5f * size, 4.0f * size, true, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, item.Item.IsReadOnly ? Font::TransparentRandomColor : Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			if (item.Item.HasBooleanValue && !item.Item.IsReadOnly) {
				_root->DrawStringShadow("<"_s, charOffset, centerX - 70.0f - 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				_root->DrawStringShadow(">"_s, charOffset, centerX + 80.0f + 30.0f * size, item.Y + 22.0f, IMenuContainer::FontLayer + 20,
					Alignment::Right, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor, 0.9f);
		}

		if (item.Item.Type == UserProfileOptionsItemType::PlayerName) {
			String playerName;
			bool isDiscord = false;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported()) {
				StringView discordUserName = DiscordRpcClient::Get().GetUserDisplayName();
				if (!discordUserName.empty()) {
					playerName = discordUserName;
					isDiscord = true;
				}
			}
#endif
			if (playerName.empty()) {
				playerName = _localPlayerName;
			}

			Vector2f textSize = _root->MeasureString(playerName, 0.8f);

			_root->DrawStringShadow(playerName, charOffset, centerX + (isDiscord ? 12.0f : 0.0f), item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor))), 0.8f);
		
			if (isDiscord) {
				_root->DrawStringShadow("\uE000"_s, charOffset, centerX - textSize.X * 0.5f - 5.0f, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
					Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)));
			}

			if (isSelected && _waitForInput) {
				Vector2f textToCursorSize = _root->MeasureString(playerName.prefix(_textCursor), 0.8f);
				_root->DrawSolid(centerX - textSize.X * 0.5f + textToCursorSize.X + 1.0f, item.Y + 22.0f - 1.0f, IMenuContainer::FontLayer + 10, Alignment::Center, Vector2f(1.0f, 12.0f),
					Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_carretAnim * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
			}
		}
#if defined(WITH_MULTIPLAYER)
		if (item.Item.Type == UserProfileOptionsItemType::UniquePlayerID) {
			auto& uuid = PreferencesCache::UniquePlayerID;
			char uniquePlayerId[128];
			formatString(uniquePlayerId, sizeof(uniquePlayerId), "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
				uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

			_root->DrawStringShadow(uniquePlayerId, charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);

#	if defined(DEATH_TARGET_ANDROID)
			_root->DrawStringShadow(_deviceId, charOffset, centerX, item.Y + 22.0f + 16.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
#	elif (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported()) {
				std::uint64_t userId = DiscordRpcClient::Get().GetUserId();
				if (userId != 0) {
					formatString(uniquePlayerId, sizeof(uniquePlayerId), "DC:%016llu", userId);

					_root->DrawStringShadow(uniquePlayerId, charOffset, centerX, item.Y + 22.0f + 16.0f, IMenuContainer::FontLayer - 10,
						Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
				}
			}
#	endif
		}
#endif
		else if (item.Item.HasBooleanValue) {
			StringView customText;
			bool enabled;
			switch (item.Item.Type) {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
				case UserProfileOptionsItemType::EnableDiscordIntegration: enabled = PreferencesCache::EnableDiscordIntegration; break;
#endif
				default: enabled = false; break;
			}

			_root->DrawStringShadow(!customText.empty() ? customText : (enabled ? _("Enabled") : _("Disabled")), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
		}
	}

	void UserProfileOptionsSection::OnExecuteSelected()
	{
		if (_items[_selectedIndex].Item.IsReadOnly) {
			return;
		}

		switch (_items[_selectedIndex].Item.Type) {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			case UserProfileOptionsItemType::EnableDiscordIntegration: {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				PreferencesCache::EnableDiscordIntegration = !PreferencesCache::EnableDiscordIntegration;
				if (PreferencesCache::EnableDiscordIntegration) {
					DiscordRpcClient::Get().Connect("591586859960762378"_s);
				} else {
					DiscordRpcClient::Get().Disconnect();
				}
				_isDirty = true;
				_animation = 0.0f;
				break;
			}
#endif
			case UserProfileOptionsItemType::PlayerName: {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
				if (DiscordRpcClient::Get().IsSupported() && !DiscordRpcClient::Get().GetUserDisplayName().empty()) {
					break;
				}
#endif
				_root->PlaySfx("MenuSelect"_s, 0.6f);
				_carretAnim = 0.0f;
				_prevPlayerName = _localPlayerName;
				_textCursor = _localPlayerName.size();
				_waitForInput = true;
#if defined(DEATH_TARGET_ANDROID)
				RecalcLayoutForSoftInput();
#endif
				break;
			}
		}
	}

	void UserProfileOptionsSection::OnBackPressed()
	{
		if (_waitForInput) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_waitForInput = false;
			_localPlayerName = std::move(_prevPlayerName);
#if defined(DEATH_TARGET_ANDROID)
			auto& app = static_cast<AndroidApplication&>(theApplication());
			app.HideSoftInput();
			RecalcLayoutForSoftInput();
#endif
			return;
		}

		ScrollableMenuSection::OnBackPressed();
	}

#if defined(DEATH_TARGET_ANDROID)
	void UserProfileOptionsSection::RecalcLayoutForSoftInput()
	{
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();

		if (_recalcVisibleBoundsTimeLeft > 30.0f) {
			_recalcVisibleBoundsTimeLeft = 30.0f;
		}
	}
#endif
}