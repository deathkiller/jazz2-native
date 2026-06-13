#include "UserProfileOptionsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../DiscordRpcClient.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

#include <Containers/StringConcatenable.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	namespace
	{
		// Standard fur gradient starting indices the player can choose for each of the 4 sections (0 = original
		// colors). 0x38 is intentionally omitted - it has no proper gradient in the original game.
		static const std::uint8_t FurGradientStarts[] = { 0x00, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40, 0x48, 0x50, 0x58 };

		static Colorf ColorFromPacked(std::uint32_t c, float alpha)
		{
			return Colorf((c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, alpha * ((c >> 24) & 0xFF) / 255.0f);
		}
	}

	UserProfileOptionsSection::UserProfileOptionsSection()
		: _isDirty(false), _waitForInput(false), _textInput(MaxPlayerNameLength)
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

		_furColor = PreferencesCache::PlayerFurColor;
		_colorMode = PreferencesCache::PlayerColors;
		_previewMetadata[0] = _previewMetadata[1] = _previewMetadata[2] = nullptr;
		_previewLoaded = false;
		_previewPaletteColor = 0;

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
			PreferencesCache::PlayerFurColor = _furColor;
			PreferencesCache::PlayerColors = _colorMode;
			PreferencesCache::Save();
		}
	}

	void UserProfileOptionsSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		bool isInGame = runtime_cast<InGameMenu>(_root);

		_items.clear();

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::EnableDiscordIntegration, _("Discord Integration"), true, isInGame });
#endif
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::PlayerName, _("Player Name"), false, isInGame });
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::FurColor1, _("Character Color 1"), true, isInGame });
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::FurColor2, _("Character Color 2"), true, isInGame });
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::FurColor3, _("Character Color 3"), true, isInGame });
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::FurColor4, _("Character Color 4"), true, isInGame });
		// TRANSLATORS: Menu item in Options > User Profile section
		_items.emplace_back(UserProfileOptionsItem { UserProfileOptionsItemType::ColorMode, _("Apply Custom Colors"), true, isInGame });
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

		// Color items cycle through gradient presets (and the apply-mode cycles its values) with Left/Right -
		// direction matters here, unlike boolean toggles
		if (!_items.empty() && !_items[_selectedIndex].Item.IsReadOnly) {
			UserProfileOptionsItemType type = _items[_selectedIndex].Item.Type;
			std::int32_t section = GetFurSectionIndex(type);
			if (section >= 0) {
				if (_root->ActionHit(PlayerAction::Left)) {
					CycleFurSection(section, -1);
					return;
				} else if (_root->ActionHit(PlayerAction::Right)) {
					CycleFurSection(section, 1);
					return;
				}
			} else if (type == UserProfileOptionsItemType::ColorMode) {
				if (_root->ActionHit(PlayerAction::Left)) {
					CycleColorMode(-1);
					return;
				} else if (_root->ActionHit(PlayerAction::Right)) {
					CycleColorMode(1);
					return;
				}
			}
		}

		if (!_items.empty() && _items[_selectedIndex].Item.HasBooleanValue && (_root->ActionHit(PlayerAction::Left) || _root->ActionHit(PlayerAction::Right))) {
			OnExecuteSelected();
		} else {
			ScrollableMenuSection::OnHandleInput();
		}
	}


	void UserProfileOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (x < 0.2f && y < 80.0f && _waitForInput && theApplication().CanShowScreenKeyboard()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					theApplication().ShowScreenKeyboard();
					RecalcLayoutForScreenKeyboard();
					return;
				}
			}
		}

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
#endif
			if (_root->ActionHit(PlayerAction::ChangeWeapon) && theApplication().CanShowScreenKeyboard()) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				theApplication().ToggleScreenKeyboard();
				RecalcLayoutForScreenKeyboard();
			} else if (_root->ActionHit(PlayerAction::Menu) || _root->ActionHit(PlayerAction::Run)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				_textInput.Deactivate();
			} else if (_root->ActionHit(PlayerAction::Fire)) {
				if (!_textInput.GetText().empty()) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_localPlayerName = _textInput.GetText();
					_waitForInput = false;
					_textInput.Deactivate();
					_isDirty = true;
				}
			}

			EnsureVisibleSelected();

			_textInput.Update(timeMult);
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

		// Live recolored character preview (Jazz/Spaz/Lori idle, those available) in a row on the right side of the
		// panel, reflecting the chosen colors
		if (!_previewLoaded) {
			_previewLoaded = true;
			_previewMetadata[0] = ContentResolver::Get().RequestMetadata("Interactive/PlayerJazz"_s, true);
			_previewMetadata[1] = ContentResolver::Get().RequestMetadata("Interactive/PlayerSpaz"_s, true);
			_previewMetadata[2] = ContentResolver::Get().RequestMetadata("Interactive/PlayerLori"_s, true);
		}

		if (_previewPalette == nullptr || _previewPaletteColor != _furColor) {
			ContentResolver::Get().ApplyPlayerColorPalette(_previewPalette, _furColor);
			_previewPaletteColor = _furColor;
		}

		if (_previewPalette != nullptr) {
			std::int32_t available = 0;
			for (std::int32_t i = 0; i < 3; i++) {
				if (_previewMetadata[i] != nullptr) {
					available++;
				}
			}

			if (available > 0) {
				constexpr float scale = 1.0f;
				constexpr float step = 32.0f;
				float rowY = (topLine + bottomLine) * 0.5f;
				float slotX = centerX + 200.0f - step * (available - 1) * 0.5f;

				for (std::int32_t i = 0; i < 3; i++) {
					if (_previewMetadata[i] == nullptr) {
						continue;
					}

					auto* res = _previewMetadata[i]->FindAnimation(AnimState::Idle);
					if (res != nullptr && res->Base != nullptr && res->Base->TextureDiffuse != nullptr) {
						GenericGraphicResource* base = res->Base;
						std::int32_t frameCount = (res->FrameCount > 0 ? res->FrameCount : 1);
						float animDuration = (res->AnimDuration > 0.0f ? res->AnimDuration : 1.0f);
						std::int32_t frame = res->FrameOffset + ((std::int32_t)(canvas->AnimTime * frameCount / animDuration) % frameCount);
						Vector2i texSize = base->TextureDiffuse->GetSize();
						std::int32_t col = frame % base->FrameConfiguration.X;
						std::int32_t row = frame / base->FrameConfiguration.X;
						Vector4f texCoords = Vector4f(
							(float)base->FrameDimensions.X / texSize.X,
							(float)(base->FrameDimensions.X * col) / texSize.X,
							(float)base->FrameDimensions.Y / texSize.Y,
							(float)(base->FrameDimensions.Y * row) / texSize.Y);

						Vector2f size = Vector2f(base->FrameDimensions.X * scale, base->FrameDimensions.Y * scale);
						Vector2f pos = Canvas::ApplyAlignment(Alignment::Center, Vector2f(slotX, rowY), size);
						canvas->DrawTextureWithPalette(*base->TextureDiffuse, *_previewPalette, pos, IMenuContainer::MainLayer, size, texCoords, Colorf::White);
					}

					slotX += step;
				}
			}
		}
	}

	void UserProfileOptionsSection::OnDrawOverlay(Canvas* canvas)
	{
		if (_waitForInput && theApplication().CanShowScreenKeyboard()) {
			auto contentBounds = _root->GetContentBounds();
			float titleY = contentBounds.Y - (canvas->ViewSize.Y >= 300 ? 30.0f : 12.0f) - 2.0f;

			_root->DrawElement(MenuGlow, 0, 72.0f, titleY - 2.0f, IMenuContainer::MainLayer - 20, Alignment::Center,
				Colorf(1.0f, 1.0f, 1.0f, 0.16f), 4.0f, 8.0f, true, true);

			_root->DrawElement(ShowKeyboard, -1, 72.0f, titleY - 2.0f + 2.0f, IMenuContainer::MainLayer - 10, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.2f));
			_root->DrawElement(ShowKeyboard, -1, 72.0f, titleY - 2.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White);

#if defined(DEATH_TARGET_ANDROID)
			if (_currentVisibleBounds.W < _initialVisibleSize.X || _currentVisibleBounds.H < _initialVisibleSize.Y) {
				Vector2i viewSize = _root->GetViewSize();
				if (_currentVisibleBounds.Y * viewSize.Y / _initialVisibleSize.Y < 32.0f) {
					_root->DrawSolid(0.0f, 0.0f, IMenuContainer::MainLayer - 10, Alignment::TopLeft, Vector2f(viewSize.X, viewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.6f));

					std::int32_t charOffset = 0;
					_root->DrawStringShadow(_textInput.GetText(), charOffset, 120.0f, titleY, IMenuContainer::MainLayer,
						Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

					Vector2f textToCursorSize = _root->MeasureString(_textInput.GetText().prefix(_textInput.GetCursor()), 1.0f);
					_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, titleY - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
						Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_textInput.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
				}
			}
#endif
		}
	}

	void UserProfileOptionsSection::OnKeyPressed(const KeyboardEvent& event)
	{
		if (_waitForInput) {
			switch (event.sym) {
				case Keys::Escape: {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					_textInput.Deactivate();
					theApplication().HideScreenKeyboard();
					RecalcLayoutForScreenKeyboard();
					break;
				}
				case Keys::Return: {
					if (!_textInput.GetText().empty()) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_localPlayerName = _textInput.GetText();
						_waitForInput = false;
						_textInput.Deactivate();
						_isDirty = true;
						theApplication().HideScreenKeyboard();
						RecalcLayoutForScreenKeyboard();
					}
					break;
				}
				default: {
					_textInput.OnKeyPressed(event);
					break;
				}
			}
		}
	}

	void UserProfileOptionsSection::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_waitForInput) {
			_textInput.OnTextInput(event);
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

			_root->DrawStringGlow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
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
				playerName = (_waitForInput ? String(_textInput.GetText()) : _localPlayerName);
			}

			Vector2f textSize = _root->MeasureString(playerName, 0.8f);

			_root->DrawStringShadow(playerName, charOffset, centerX + (isDiscord ? 12.0f : 0.0f), item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor))), 0.8f);
		
			if (isDiscord) {
				_root->DrawStringShadow("\uE000"_s, charOffset, centerX - textSize.X * 0.5f - 5.0f, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
					Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)));
			}

			if (isSelected && _waitForInput) {
				Vector2f textToCursorSize = _root->MeasureString(playerName.prefix(_textInput.GetCursor()), 0.8f);
				_root->DrawSolid(centerX - textSize.X * 0.5f + textToCursorSize.X + 1.0f, item.Y + 22.0f - 1.0f, IMenuContainer::FontLayer + 10, Alignment::Center, Vector2f(1.0f, 12.0f),
					Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_textInput.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
			}
		}
#if defined(WITH_MULTIPLAYER)
		if (item.Item.Type == UserProfileOptionsItemType::UniquePlayerID) {
			auto& uuid = PreferencesCache::UniquePlayerID;
			char uniquePlayerId[128];
			std::size_t length = formatInto(uniquePlayerId, "{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}",
				uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

			_root->DrawStringShadow({ uniquePlayerId, length }, charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);

#	if defined(DEATH_TARGET_ANDROID)
			_root->DrawStringShadow(_deviceId, charOffset, centerX, item.Y + 22.0f + 16.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
#	elif (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported()) {
				std::uint64_t userId = DiscordRpcClient::Get().GetUserId();
				if (userId != 0) {
					std::size_t length = formatInto(uniquePlayerId, "DC:{:.16}", userId);
					_root->DrawStringShadow({ uniquePlayerId, length }, charOffset, centerX, item.Y + 22.0f + 16.0f, IMenuContainer::FontLayer - 10,
						Alignment::Center, (isSelected ? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f) : (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)), 0.8f);
				}
			}
#	endif
		}
#endif
		else if (GetFurSectionIndex(item.Item.Type) >= 0) {
			std::int32_t section = GetFurSectionIndex(item.Item.Type);
			std::uint32_t packed = (_furColor >> (section * 8)) & 0xFF;
			std::uint32_t base = packed & ~(std::uint32_t)ContentResolver::FurHueShiftFlag;
			bool hueShift = (packed & ContentResolver::FurHueShiftFlag) != 0;
			Colorf labelColor = (isSelected
				? Colorf(0.46f, 0.46f, 0.46f, item.Item.IsReadOnly ? 0.36f : 0.5f)
				: (item.Item.IsReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor));

			if (base == 0) {
				_root->DrawStringShadow(_("Default"), charOffset, centerX, item.Y + 22.0f, IMenuContainer::FontLayer - 10, Alignment::Center, labelColor, 0.8f);
			} else {
				// Preview the chosen gradient: draw its 8 sprite-palette colors (hue-shifted to match the variant) as a small strip
				auto palettes = ContentResolver::Get().GetPalettes();
				constexpr float swatchW = 10.0f;
				float startX = centerX - (ContentResolver::FurSectionSize * swatchW) * 0.5f;
				float alpha = (item.Item.IsReadOnly ? 0.6f : 1.0f);
				for (std::int32_t i = 0; i < ContentResolver::FurSectionSize; i++) {
					std::uint32_t color = palettes[(base + i) & 0xFF];
					if (hueShift) {
						color = ContentResolver::ShiftHue(color, ContentResolver::HueShiftDegreesForGradient((std::int32_t)base));
					}
					_root->DrawSolid(startX + i * swatchW, item.Y + 22.0f, IMenuContainer::FontLayer - 10, Alignment::Left,
						Vector2f(swatchW, 14.0f), ColorFromPacked(color | 0xFF000000u, alpha), false);
				}
			}
		}
		else if (item.Item.HasBooleanValue) {
			StringView customText;
			bool enabled;
			switch (item.Item.Type) {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
				case UserProfileOptionsItemType::EnableDiscordIntegration: enabled = PreferencesCache::EnableDiscordIntegration; break;
#endif
				case UserProfileOptionsItemType::ColorMode:
					switch (_colorMode) {
						default:
						// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
						case PlayerColorMode::OnlineOnly: customText = _("Online only"); break;
						// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
						case PlayerColorMode::FirstLocalPlayer: customText = _("Online and first local player"); break;
						// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
						case PlayerColorMode::AllLocalPlayers: customText = _("Online and all local players"); break;
					}
					enabled = false;
					break;
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
				_textInput.Activate(_localPlayerName);
				_waitForInput = true;
				RecalcLayoutForScreenKeyboard();
				break;
			}
			case UserProfileOptionsItemType::FurColor1:
			case UserProfileOptionsItemType::FurColor2:
			case UserProfileOptionsItemType::FurColor3:
			case UserProfileOptionsItemType::FurColor4: {
				CycleFurSection(GetFurSectionIndex(_items[_selectedIndex].Item.Type), 1);
				break;
			}
			case UserProfileOptionsItemType::ColorMode: {
				CycleColorMode(1);
				break;
			}
#if defined(WITH_MULTIPLAYER)
			case UserProfileOptionsItemType::UniquePlayerID: {
				auto& uuid = PreferencesCache::UniquePlayerID;
				char uniquePlayerId[128];
				std::size_t length = formatInto(uniquePlayerId, "{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}",
					uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);

				if (theApplication().GetInputManager().setClipboardText({ uniquePlayerId, length })) {
					_root->PlaySfx("MenuSelect"_s, 0.6f);
				}
				break;
			}
#endif
		}
	}

	void UserProfileOptionsSection::OnBackPressed()
	{
		if (_waitForInput) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_waitForInput = false;
			_textInput.Deactivate();

			theApplication().HideScreenKeyboard();
			RecalcLayoutForScreenKeyboard();
			return;
		}

		ScrollableMenuSection::OnBackPressed();
	}


	std::int32_t UserProfileOptionsSection::GetFurSectionIndex(UserProfileOptionsItemType type)
	{
		switch (type) {
			case UserProfileOptionsItemType::FurColor1: return 0;
			case UserProfileOptionsItemType::FurColor2: return 1;
			case UserProfileOptionsItemType::FurColor3: return 2;
			case UserProfileOptionsItemType::FurColor4: return 3;
			default: return -1;
		}
	}

	void UserProfileOptionsSection::CycleFurSection(std::int32_t section, std::int32_t direction)
	{
		// Build the ordered list of selectable values: Default (0) and all base gradients first, then their
		// hue-shifted twins (high bit set) grouped at the end. Default has no twin, and gradients that carry no hue
		// rotation (the near-neutral ones - see HueShiftDegreesForGradient) get no twin either, as it would look identical.
		std::uint8_t variants[arraySize(FurGradientStarts) * 2];
		std::int32_t count = 0;
		for (std::int32_t i = 0; i < (std::int32_t)arraySize(FurGradientStarts); i++) {
			variants[count++] = FurGradientStarts[i];
		}
		for (std::int32_t i = 0; i < (std::int32_t)arraySize(FurGradientStarts); i++) {
			if (FurGradientStarts[i] != 0x00 && ContentResolver::HueShiftDegreesForGradient(FurGradientStarts[i]) != 0.0f) {
				variants[count++] = (std::uint8_t)(FurGradientStarts[i] | ContentResolver::FurHueShiftFlag);
			}
		}

		std::uint32_t shift = (std::uint32_t)(section * 8);
		std::uint8_t current = (std::uint8_t)((_furColor >> shift) & 0xFF);

		std::int32_t index = 0;
		for (std::int32_t i = 0; i < count; i++) {
			if (variants[i] == current) {
				index = i;
				break;
			}
		}

		index = (index + direction + count) % count;
		std::uint8_t value = variants[index];
		_furColor = (_furColor & ~(0xFFu << shift)) | ((std::uint32_t)value << shift);
		_isDirty = true;
		_animation = 0.0f;
		_root->PlaySfx("MenuSelect"_s, 0.5f);
	}

	void UserProfileOptionsSection::CycleColorMode(std::int32_t direction)
	{
		constexpr std::int32_t count = 3; // PlayerColorMode: OnlineOnly, FirstLocalPlayer, AllLocalPlayers
		std::int32_t index = (((std::int32_t)_colorMode + direction) % count + count) % count;
		_colorMode = (PlayerColorMode)index;
		_isDirty = true;
		_animation = 0.0f;
		_root->PlaySfx("MenuSelect"_s, 0.5f);
	}

	void UserProfileOptionsSection::RecalcLayoutForScreenKeyboard()
	{
#if defined(DEATH_TARGET_ANDROID)
		_currentVisibleBounds = Backends::AndroidJniWrap_Activity::getVisibleBounds();

		if (_recalcVisibleBoundsTimeLeft > 30.0f) {
			_recalcVisibleBoundsTimeLeft = 30.0f;
		}
#endif
	}
}