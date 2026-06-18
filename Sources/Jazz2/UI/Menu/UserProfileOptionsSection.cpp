#include "UserProfileOptionsSection.h"
#include "InGameMenu.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../DiscordRpcClient.h"
#include "../../ContentResolver.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#	include "../../../nCine/Backends/Android/AndroidJniHelper.h"
#endif

#include <algorithm>
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
		: _isDirty(false), _nameInput(nullptr)
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

		if (_content != nullptr) {
			return;
		}

		bool isInGame = (runtime_cast<InGameMenu>(root) != nullptr);

		SetTitle(_("User Profile"));

		auto list = std::make_unique<ScrollView>();

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		// TRANSLATORS: Menu item in Options > User Profile section
		auto* discordItem = list->Add<ChoiceItem>(_("Discord Integration"),
			[]() -> StringView { return (PreferencesCache::EnableDiscordIntegration ? _("Enabled") : _("Disabled")); },
			[this](std::int32_t) {
				PreferencesCache::EnableDiscordIntegration = !PreferencesCache::EnableDiscordIntegration;
				if (PreferencesCache::EnableDiscordIntegration) {
					DiscordRpcClient::Get().Connect("591586859960762378"_s);
				} else {
					DiscordRpcClient::Get().Disconnect();
				}
				_isDirty = true;
			}, isInGame);
		discordItem->Height = 64.0f;
#endif

		// TRANSLATORS: Menu item in Options > User Profile section
		auto* nameInput = list->Add<TextInput>(_("Player Name"), MaxPlayerNameLength);
		nameInput->ReadOnly = isInGame;
		nameInput->Icon = ""_s;
		nameInput->Value = [this]() -> String {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported()) {
				StringView discordUserName = DiscordRpcClient::Get().GetUserDisplayName();
				if (!discordUserName.empty()) {
					return String(discordUserName);
				}
			}
#endif
			return _localPlayerName;
		};
		nameInput->CanEdit = []() -> bool {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported() && !DiscordRpcClient::Get().GetUserDisplayName().empty()) {
				return false;
			}
#endif
			return true;
		};
		nameInput->ShowIcon = []() -> bool {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			return (DiscordRpcClient::Get().IsSupported() && !DiscordRpcClient::Get().GetUserDisplayName().empty());
#else
			return false;
#endif
		};
		nameInput->OnCommit = [this](StringView text) {
			_localPlayerName = text;
			_isDirty = true;
		};
		nameInput->OnEditStateChanged = [this](bool active) {
			if (!active) {
				theApplication().HideScreenKeyboard();
			}
			RecalcLayoutForScreenKeyboard();
		};
		_nameInput = nameInput;

		StringView furLabels[] = {
			// TRANSLATORS: Menu item in Options > User Profile section
			_("Character Color 1"),
			// TRANSLATORS: Menu item in Options > User Profile section
			_("Character Color 2"),
			// TRANSLATORS: Menu item in Options > User Profile section
			_("Character Color 3"),
			// TRANSLATORS: Menu item in Options > User Profile section
			_("Character Color 4")
		};
		for (std::int32_t section = 0; section < 4; section++) {
			auto* furItem = list->Add<CustomValueItem>(furLabels[section], 52.0f);
			furItem->ReadOnly = isInGame;
			furItem->OnChange = [this, section](std::int32_t direction) { CycleFurSection(section, direction); };
			furItem->DrawValue = [this, section](IMenuContainer* r, float centerX, float y, std::int32_t& charOffset, bool selected, bool readOnly) {
				std::uint32_t packed = (_furColor >> (section * 8)) & 0xFF;
				std::uint32_t base = packed & ~(std::uint32_t)ContentResolver::FurHueShiftFlag;
				bool hueShift = (packed & ContentResolver::FurHueShiftFlag) != 0;
				Colorf labelColor = (selected
					? Colorf(0.46f, 0.46f, 0.46f, readOnly ? 0.36f : 0.5f)
					: (readOnly ? Font::TransparentDefaultColor : Font::DefaultColor));

				if (base == 0) {
					r->DrawStringShadow(_("Default"), charOffset, centerX, y, IMenuContainer::FontLayer - 10, Alignment::Center, labelColor, 0.8f);
				} else {
					// Preview the chosen gradient: draw its sprite-palette colors (hue-shifted to match the variant) as a small strip
					auto palettes = ContentResolver::Get().GetPalettes();
					constexpr float swatchW = 10.0f;
					float startX = centerX - (ContentResolver::FurSectionSize * swatchW) * 0.5f;
					float alpha = (readOnly ? 0.6f : 1.0f);
					for (std::int32_t i = 0; i < ContentResolver::FurSectionSize; i++) {
						std::uint32_t color = palettes[(base + i) & 0xFF];
						if (hueShift) {
							color = ContentResolver::ShiftHue(color, ContentResolver::HueShiftDegreesForGradient((std::int32_t)base));
						}
						r->DrawSolid(startX + i * swatchW, y, IMenuContainer::FontLayer - 10, Alignment::Left,
							Vector2f(swatchW, 14.0f), ColorFromPacked(color | 0xFF000000u, alpha), false);
					}
				}
			};
		}

		// TRANSLATORS: Menu item in Options > User Profile section
		list->Add<ChoiceItem>(_("Apply Custom Colors"),
			[this]() -> StringView {
				switch (_colorMode) {
					default:
					// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
					case PlayerColorMode::OnlineOnly: return _("Online only");
					// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
					case PlayerColorMode::FirstLocalPlayer: return _("Online and first local player");
					// TRANSLATORS: Value of "Apply Colors" in Options > User Profile
					case PlayerColorMode::AllLocalPlayers: return _("Online and all local players");
				}
			},
			[this](std::int32_t direction) { CycleColorMode(direction); },
			isInGame);

#if defined(WITH_MULTIPLAYER)
		// TRANSLATORS: Menu item in Options > User Profile section
		auto* uniqueIdItem = list->Add<CustomValueItem>(_("Unique Player ID"), 64.0f);
		uniqueIdItem->OnActivate = [this]() {
			auto& uuid = PreferencesCache::UniquePlayerID;
			char uniquePlayerId[128];
			std::size_t length = formatInto(uniquePlayerId, "{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}",
				uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
			if (theApplication().GetInputManager().setClipboardText({ uniquePlayerId, length })) {
				_root->PlaySfx("MenuSelect"_s, 0.6f);
			}
		};
		uniqueIdItem->DrawValue = [this](IMenuContainer* r, float centerX, float y, std::int32_t& charOffset, bool selected, bool readOnly) {
			auto& uuid = PreferencesCache::UniquePlayerID;
			char uniquePlayerId[128];
			std::size_t length = formatInto(uniquePlayerId, "{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}:{:.2X}{:.2X}",
				uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
			Colorf c = (selected ? Colorf(0.46f, 0.46f, 0.46f, 0.5f) : Font::DefaultColor);
			r->DrawStringShadow({ uniquePlayerId, length }, charOffset, centerX, y, IMenuContainer::FontLayer - 10, Alignment::Center, c, 0.8f);
#	if defined(DEATH_TARGET_ANDROID)
			r->DrawStringShadow(_deviceId, charOffset, centerX, y + 16.0f, IMenuContainer::FontLayer - 10, Alignment::Center, c, 0.8f);
#	elif (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
			if (DiscordRpcClient::Get().IsSupported()) {
				std::uint64_t userId = DiscordRpcClient::Get().GetUserId();
				if (userId != 0) {
					char discordId[32];
					std::size_t discordLength = formatInto(discordId, "DC:{:.16}", userId);
					r->DrawStringShadow({ discordId, discordLength }, charOffset, centerX, y + 16.0f, IMenuContainer::FontLayer - 10, Alignment::Center, c, 0.8f);
				}
			}
#	endif
		};
#endif

		SetContent(std::move(list));
	}

	void UserProfileOptionsSection::OnUpdate(float timeMult)
	{
		bool wasEditing = (_nameInput != nullptr && _nameInput->IsActive());

		WidgetSection::OnUpdate(timeMult);

		// While editing (and still editing after the base update handled Menu/Fire), handle the editing-only actions
		if (wasEditing && _nameInput != nullptr && _nameInput->IsActive()) {
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
			} else if (_root->ActionHit(PlayerAction::Run)) {
				_nameInput->Cancel(_root);
			}
		}
	}

	void UserProfileOptionsSection::OnDraw(Canvas* canvas)
	{
		WidgetSection::OnDraw(canvas);

		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + 31.0f;
		float bottomLine = contentBounds.Y + contentBounds.H - 42.0f;

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
		if (_nameInput != nullptr && _nameInput->IsActive() && theApplication().CanShowScreenKeyboard()) {
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
					_root->DrawStringShadow(_nameInput->GetText(), charOffset, 120.0f, titleY, IMenuContainer::MainLayer,
						Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 1.0f);

					Vector2f textToCursorSize = _root->MeasureString(_nameInput->GetText().prefix(_nameInput->GetCursor()), 1.0f);
					_root->DrawSolid(120.0f + textToCursorSize.X + 1.0f, titleY - 1.0f, IMenuContainer::MainLayer + 10, Alignment::Left, Vector2f(1.0f, 14.0f),
						Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_nameInput->GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
				}
			}
#endif
		}
	}

	void UserProfileOptionsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (_nameInput != nullptr && _nameInput->IsActive()) {
			if (event.type == TouchEventType::Down) {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float x = event.pointers[pointerIndex].x;
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

					if (x < 0.2f && y < 80.0f && theApplication().CanShowScreenKeyboard()) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						theApplication().ShowScreenKeyboard();
						RecalcLayoutForScreenKeyboard();
					}
				}
			}
			// Don't let taps change the selection while editing the name
			return;
		}

		WidgetSection::OnTouchEvent(event, viewSize);
	}

	void UserProfileOptionsSection::OnBackPressed()
	{
		if (_nameInput != nullptr && _nameInput->IsActive()) {
			_nameInput->Cancel(_root);
			return;
		}

		WidgetSection::OnBackPressed();
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
	}

	void UserProfileOptionsSection::CycleColorMode(std::int32_t direction)
	{
		constexpr std::int32_t count = 3; // PlayerColorMode: OnlineOnly, FirstLocalPlayer, AllLocalPlayers
		std::int32_t index = (((std::int32_t)_colorMode + direction) % count + count) % count;
		_colorMode = (PlayerColorMode)index;
		_isDirty = true;
	}
}
