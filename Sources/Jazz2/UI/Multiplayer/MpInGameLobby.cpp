#include "MpInGameLobby.h"

#if defined(WITH_MULTIPLAYER)

#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"
#include "../Menu/IMenuContainer.h"
#include "../Menu/MenuResources.h"
#include "../Menu/Tweening.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Input/JoyMapping.h"

using namespace Jazz2::Multiplayer;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Multiplayer
{
	MpInGameLobby::MpInGameLobby(MpLevelHandler* levelHandler)
		: _levelHandler(levelHandler), _pressedActions(0), _animation(0.0f), _selectedPlayerType(0),
			_allowedPlayerTypes(0), _teamMode(false), _allowTeamSelection(false), _teamCount(2),
			_selectedTeam(0), _focusRow(0), _isVisible(false)
	{
		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		DEATH_ASSERT(_metadata != nullptr, "Cannot load required metadata", );

		_smallFont = resolver.GetFont(FontType::Small);
	}

	MpInGameLobby::~MpInGameLobby()
	{
	}

	void MpInGameLobby::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		if (_isVisible) {
			if (_animation < 1.0f) {
				_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
			}

			UpdatePressedActions();

			// Read team config live so the team row works regardless of when the game-mode packet arrived
			RefreshTeamInfo();
			bool teamRowActive = (_teamMode && _allowTeamSelection);
			if (!teamRowActive) {
				_focusRow = 0;
			}

			if (ActionHit(PlayerAction::Fire)) {
				if ((_allowedPlayerTypes & (1 << _selectedPlayerType)) != 0) {
					std::uint8_t team = NoPreferredTeam;
					if (teamRowActive) {
						team = (_selectedTeam >= _teamCount ? NoPreferredTeam : (std::uint8_t)_selectedTeam);
					}
					_levelHandler->SetPlayerReady((PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType), team);
				}
			} else if (ActionHit(PlayerAction::Left)) {
				if (teamRowActive && _focusRow == 1) {
					_selectedTeam--;
					if (_selectedTeam < 0) {
						_selectedTeam = _teamCount;
					}
				} else {
					_selectedPlayerType--;
					if (_selectedPlayerType < 0) {
						_selectedPlayerType = 2;
					}
				}
				_animation = 0.0f;
			} else if (ActionHit(PlayerAction::Right)) {
				if (teamRowActive && _focusRow == 1) {
					_selectedTeam++;
					if (_selectedTeam > _teamCount) {
						_selectedTeam = 0;
					}
				} else {
					_selectedPlayerType++;
					if (_selectedPlayerType > 2) {
						_selectedPlayerType = 0;
					}
				}
				_animation = 0.0f;
			} else if (teamRowActive && (ActionHit(PlayerAction::Up) || ActionHit(PlayerAction::Down))) {
				// Only two rows, so Up/Down just toggle the focus between Character and Team
				_focusRow = (_focusRow == 0 ? 1 : 0);
				_animation = 0.0f;
			}
		}
	}

	bool MpInGameLobby::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _levelHandler->GetViewSize();

		if (_isVisible) {
			DrawSolid(Vector2f(0.0f, 0.0f), 20, ViewSize.As<float>(), Colorf(0.0f, 0.0f, 0.0f, std::min(AnimTime * 5.0f, 1.0f)));
		}

		if (_isVisible && _levelHandler->_pauseMenu == nullptr) {
			std::int32_t charOffset = 0;

			//DrawSolid(Vector2f(0.0f, 0.0f), 20, ViewSize.As<float>(), Colorf(0.0f, 0.0f, 0.0f, std::min(AnimTime * 5.0f, 0.6f)));

			static const StringView playerTypes[] = { "Jazz"_s, "Spaz"_s, "Lori"_s };
			static const Colorf playerColors[] = {
				Colorf(0.2f, 0.45f, 0.2f, 0.5f),
				Colorf(0.45f, 0.27f, 0.22f, 0.5f),
				Colorf(0.5f, 0.45f, 0.22f, 0.5f)
			};

			// TODO: Combine localy available characters with _allowedPlayerTypes
			std::int32_t _availableCharacters = 3;

			Vector2i center = ViewSize / 2;

			const auto& serverConfig = _levelHandler->_networkManager->GetServerConfiguration();
			DrawStringShadow(serverConfig.WelcomeMessage, charOffset, center.X, center.Y / 2, MainLayer,
				Alignment::Center, Font::DefaultColor, 1.0f, 0.7f, 0.6f, 0.6f, 0.4f, 1.0f);

			RefreshTeamInfo();
			bool teamRowActive = (_teamMode && _allowTeamSelection);

			// Vertical layout (moved up so the team row fits comfortably)
			float charLabelY = center.Y - 34.0f;
			float charRowY = center.Y + 2.0f;
			float teamLabelY = center.Y + 44.0f;
			float teamRowY = center.Y + 78.0f;
			float pressY = (_teamMode ? center.Y + 122.0f : center.Y + 56.0f);

			DrawStringShadow(_("Character"), charOffset, center.X, charLabelY, MainLayer,
				Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			float offset = 100.0f;
			float spacing = 100.0f;

			for (std::int32_t j = 0; j < _availableCharacters; j++) {
				float x = center.X - offset + j * spacing;
				bool isFocused = (_focusRow == 0);
				if ((_allowedPlayerTypes & (1 << j)) == 0) {
					if (_selectedPlayerType == j) {
						DrawElement(MenuGlow, 0, x, charRowY, MainLayer - 20, Alignment::Center,
							Colorf(1.0f, 1.0f, 1.0f, 0.2f), 3.6f, 5.0f, true, true);
					}

					DrawStringShadow(playerTypes[j], charOffset, x, charRowY, MainLayer, Alignment::Center,
						Colorf(0.5f, 0.5f, 0.5f, 0.34f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
				} else if ((std::int32_t)_selectedPlayerType == j) {
					float size = (isFocused ? 0.5f + Menu::Easing::OutElastic(_animation) * 0.6f : 0.9f);

					DrawElement(MenuGlow, 0, x, charRowY, MainLayer - 20, Alignment::Center,
						Colorf(1.0f, 1.0f, 1.0f, 0.26f * size), 3.6f * size, 5.0f * size, true, true);

					DrawStringShadow(playerTypes[j], charOffset, x, charRowY, MainLayer,
						Alignment::Center, playerColors[j], size, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
				} else {
					_smallFont->DrawString(this, playerTypes[j], charOffset, x, charRowY, MainLayer,
						Alignment::Center, Font::TransparentDefaultColor, 0.9f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
				}
			}

			float arrowRowY = charRowY;
			float arrowHalfWidth = 110.0f;

			// Team picker row (shown in team modes; interactive only when team selection is allowed)
			if (_teamMode) {
				DrawStringShadow(_("Team"), charOffset, center.X, teamLabelY, MainLayer,
					Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

				std::int32_t optionCount = _teamCount + (_allowTeamSelection ? 1 : 0); // teams + optional "Auto"
				float teamSpacing = 70.0f;
				float teamOffset = teamSpacing * (optionCount - 1) * 0.5f;
				for (std::int32_t t = 0; t < optionCount; t++) {
					float x = center.X - teamOffset + t * teamSpacing;
					bool isAuto = (t >= _teamCount);
					StringView label = (isAuto ? _("Auto") : GetTeamName((std::uint8_t)t));
					Colorf color = (isAuto ? Colorf(0.52f, 0.52f, 0.52f, 0.5f) : GetTeamColor((std::uint8_t)t));

					if (_selectedTeam == t) {
						float tsize = (_focusRow == 1 ? 0.5f + Menu::Easing::OutElastic(_animation) * 0.5f : 0.9f);
						Colorf glowColor = color;
						glowColor.SetAlpha(0.3f);
						DrawElement(MenuGlow, 0, x, teamRowY, MainLayer - 20, Alignment::Center,
							glowColor, 4.0f, 5.0f, true, true);
						DrawStringShadow(label, charOffset, x, teamRowY, MainLayer,
							Alignment::Center, color, tsize, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
					} else {
						DrawStringShadow(label, charOffset, x, teamRowY, MainLayer,
							Alignment::Center, Font::TransparentDefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
					}
				}

				if (teamRowActive && _focusRow == 1) {
					arrowRowY = teamRowY;
					arrowHalfWidth = teamOffset + 30.0f;
				}
			}

			// Navigation arrows at the currently focused row
			{
				float size = 0.5f + Menu::Easing::OutElastic(_animation) * 0.6f;

				DrawElement(MenuGlow, 0, center.X, arrowRowY - 32.0f, MainLayer, Alignment::Center,
					Colorf(1.0f, 1.0f, 1.0f, 0.2f), 5.0f, 5.0f, true, true);

				Colorf fontColor = Font::DefaultColor;
				fontColor.SetAlpha(std::min(1.0f, 0.6f + _animation));
				DrawStringShadow("<"_s, charOffset, center.X - arrowHalfWidth - 30.0f * size, arrowRowY, MainLayer,
					Alignment::Center, fontColor, 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				DrawStringShadow(">"_s, charOffset, center.X + arrowHalfWidth + 30.0f * size, arrowRowY, MainLayer,
					Alignment::Center, fontColor, 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			}

			if (teamRowActive) {
				DrawStringShadow(_("Use \f[c:#d0705d]Up/Down\f[/c] to switch between character and team"), charOffset, center.X, pressY - 22.0f, MainLayer,
					Alignment::Center, Font::DefaultColor, 0.7f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
			}

			DrawStringShadow(_("Press \f[c:#d0705d]Fire\f[/c] to continue"), charOffset, center.X, pressY, MainLayer,
				Alignment::Center, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
		}

		return true;
	}

	void MpInGameLobby::OnTouchEvent(const nCine::TouchEvent& event)
	{
		if (event.type != TouchEventType::Down) {
			return;
		}

		std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
		if (pointerIndex == -1) {
			return;
		}

		// Touch pointers are normalized to [0..1] over the window; map both axes into the lobby's view-pixel space,
		// which is the same space OnDraw() lays the rows out in (the previous code left x normalized, so the hit
		// regions didn't line up with the drawn characters / confirm prompt).
		Vector2i viewSize = _levelHandler->GetViewSize();
		float x = event.pointers[pointerIndex].x * (float)viewSize.X;
		float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
		Vector2i center = viewSize / 2;

		RefreshTeamInfo();
		bool teamRowActive = (_teamMode && _allowTeamSelection);

		// Layout mirrors OnDraw()
		constexpr float RowHalfHeight = 22.0f;
		constexpr std::int32_t AvailableCharacters = 3;
		float charRowY = center.Y + 2.0f;
		float teamRowY = center.Y + 78.0f;
		float pressY = (_teamMode ? center.Y + 122.0f : center.Y + 56.0f);
		float charOffset = 100.0f, charSpacing = 100.0f;

		// Tap a character to select it
		if (y >= charRowY - RowHalfHeight && y <= charRowY + RowHalfHeight) {
			for (std::int32_t j = 0; j < AvailableCharacters; j++) {
				float cx = center.X - charOffset + j * charSpacing;
				if (x >= cx - charSpacing * 0.5f && x <= cx + charSpacing * 0.5f) {
					if ((_allowedPlayerTypes & (1 << j)) != 0) {
						_selectedPlayerType = j;
						_focusRow = 0;
						_animation = 0.0f;
					}
					break;
				}
			}
			return;
		}

		// Tap a team to select it (only interactive when team selection is allowed)
		if (teamRowActive && y >= teamRowY - RowHalfHeight && y <= teamRowY + RowHalfHeight) {
			std::int32_t optionCount = _teamCount + 1; // teams + "Auto"
			float teamSpacing = 70.0f;
			float teamOffset = teamSpacing * (optionCount - 1) * 0.5f;
			for (std::int32_t t = 0; t < optionCount; t++) {
				float tx = center.X - teamOffset + t * teamSpacing;
				if (x >= tx - teamSpacing * 0.5f && x <= tx + teamSpacing * 0.5f) {
					_selectedTeam = t;
					_focusRow = 1;
					_animation = 0.0f;
					break;
				}
			}
			return;
		}

		// Tap the "Press Fire to continue" prompt to confirm the selection
		if (y >= pressY - RowHalfHeight && y <= pressY + RowHalfHeight && x >= center.X - 160.0f && x <= center.X + 160.0f) {
			if ((_allowedPlayerTypes & (1 << _selectedPlayerType)) != 0) {
				std::uint8_t team = NoPreferredTeam;
				if (teamRowActive) {
					team = (_selectedTeam >= _teamCount ? NoPreferredTeam : (std::uint8_t)_selectedTeam);
				}
				_levelHandler->SetPlayerReady((PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType), team);
			}
		}
	}

	bool MpInGameLobby::IsVisible() const
	{
		return _isVisible;
	}

	void MpInGameLobby::Show()
	{
		_isVisible = true;
	}

	void MpInGameLobby::Hide()
	{
		_isVisible = false;
	}

	void MpInGameLobby::SetAllowedPlayerTypes(std::uint8_t playerTypes)
	{
		_allowedPlayerTypes = playerTypes;
	}

	void MpInGameLobby::SetTeamInfo(std::uint8_t currentTeam)
	{
		RefreshTeamInfo();
		// Default the picker to the player's current team, or "Auto" if not on a valid team yet
		_selectedTeam = (currentTeam < _teamCount ? currentTeam : _teamCount);
		_focusRow = 0;
	}

	void MpInGameLobby::RefreshTeamInfo()
	{
		const auto& serverConfig = _levelHandler->_networkManager->GetServerConfiguration();
		_teamMode = IsTeamGameMode(serverConfig.GameMode);
		_teamCount = (serverConfig.TeamCount < 2 ? 2 : (serverConfig.TeamCount > MaxTeamCount ? MaxTeamCount : serverConfig.TeamCount));
		_allowTeamSelection = serverConfig.AllowTeamSelection;
	}

	bool MpInGameLobby::ActionPressed(PlayerAction action)
	{
		return ((_pressedActions & (1 << (std::int32_t)action)) == (1 << (std::int32_t)action));
	}

	bool MpInGameLobby::ActionHit(PlayerAction action)
	{
		return ((_pressedActions & ((1 << (std::int32_t)action) | (1 << (16 + (std::int32_t)action)))) == (1 << (std::int32_t)action));
	}

	void MpInGameLobby::UpdatePressedActions()
	{
		auto& input = theApplication().GetInputManager();
		_pressedActions = ((_pressedActions & 0xFFFF) << 16);

		if (_levelHandler->_pauseMenu != nullptr) {
			return;
		}

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		_pressedActions |= ControlScheme::FetchNavigation(_levelHandler->_pressedKeys, ArrayView(joyStates, joyStatesCount));
	}

	void MpInGameLobby::DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending, bool unaligned)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((std::int32_t)(AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
		}

		GenericGraphicResource* base = res->Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		Vector2i texSize = base->TextureDiffuse->GetSize();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending, 0.0f, paletteOffset);
	}

	void MpInGameLobby::DrawStringShadow(StringView text, int& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(this, text, charOffsetShadow, x, y + 2.8f * scale, z - 10,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(this, text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}
}

#endif