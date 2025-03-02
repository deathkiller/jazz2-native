#include "MpInGameLobby.h"

#if defined(WITH_MULTIPLAYER)

#include "../../LevelHandler.h"
#include "../Menu/IMenuContainer.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"

using namespace Jazz2::Multiplayer;

namespace Jazz2::UI::Multiplayer
{
	MpInGameLobby::MpInGameLobby(MpLevelHandler* levelHandler)
		: _levelHandler(levelHandler), _pressedActions(0), _animation(0.0f), _selectedPlayerType(0),
			_allowedPlayerTypes(0), _isVisible(false)
	{
		auto& resolver = ContentResolver::Get();

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

			if (ActionHit(PlayerAction::Fire)) {
				_levelHandler->SetPlayerReady((PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType));
			} else if (ActionHit(PlayerAction::Left)) {
				_selectedPlayerType--;
				if (_selectedPlayerType < 0) {
					_selectedPlayerType = 2;
				}
				_animation = 0.0f;
			} else if (ActionHit(PlayerAction::Right)) {
				_selectedPlayerType++;
				if (_selectedPlayerType > 2) {
					_selectedPlayerType = 0;
				}
				_animation = 0.0f;
			}
		}
	}

	bool MpInGameLobby::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _levelHandler->GetViewSize();

#if defined(DEATH_DEBUG)
		char debugBuffer[64];
		formatString(debugBuffer, sizeof(debugBuffer), "%i b | ", _levelHandler->_debugAverageUpdatePacketSize);
		std::int32_t debugCharOffset = 0;
		_smallFont->DrawString(this, debugBuffer, debugCharOffset, ViewSize.X - 40.0f, 1.0f,
			MainLayer, Alignment::TopRight, Font::DefaultColor, 0.8f);
#endif

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

			_smallFont->DrawString(this, _levelHandler->_lobbyMessage, charOffset, center.X, center.Y / 2, MainLayer,
				Alignment::Center, Font::DefaultColor, 1.0f, 0.7f, 0.6f, 0.6f, 0.4f, 1.0f);

			_smallFont->DrawString(this, _("Character"), charOffset, center.X, center.Y + 10.0f, MainLayer,
				Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			float offset, spacing;
			if (_availableCharacters == 1) {
				offset = 0.0f;
				spacing = 0.0f;
			} else if (_availableCharacters == 2) {
				offset = 50.0f;
				spacing = 100.0f;
			} else {
				offset = 100.0f;
				spacing = 300.0f / _availableCharacters;
			}

			/*if (contentBounds.W < 480) {
				offset *= 0.7f;
				spacing *= 0.7f;
			}*/

			for (std::int32_t j = 0; j < _availableCharacters; j++) {
				float x = center.X - offset + j * spacing;
				if ((std::int32_t)_selectedPlayerType == j) {
					//DrawElement(MenuGlow, 0, x, center.Y - 12.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.2f), (Utf8::GetLength(playerTypes[j]) + 3) * 0.4f, 2.2f, true, true);

					float size = 0.5f + Menu::IMenuContainer::EaseOutElastic(_animation) * 0.6f;

					_smallFont->DrawString(this, playerTypes[j], charOffset, x, center.Y + 50.0f, MainLayer,
						Alignment::Center, playerColors[j], size, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
				} else {
					_smallFont->DrawString(this, playerTypes[j], charOffset, x, center.Y + 50.0f, MainLayer,
						Alignment::Center, Font::TransparentDefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
				}
			}

			//if (_selectedIndex == i) {
				float size = 0.5f + Menu::IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_smallFont->DrawString(this, "<"_s, charOffset, center.X - 110.0f - 30.0f * size, center.Y + 50.0f, MainLayer,
					Alignment::Center, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.7f);
				_smallFont->DrawString(this, ">"_s, charOffset, center.X + 110.0f + 30.0f * size, center.Y + 50.0f, MainLayer,
					Alignment::Center, Colorf(0.5f, 0.5f, 0.5f, 0.5f * std::min(1.0f, 0.6f + _animation)), 0.7f);
			//}

			_smallFont->DrawString(this, _("Press \f[c:#d0705d]Fire\f[/c] to continue"), charOffset, center.X, (ViewSize.Y + (center.Y + 80.0f)) / 2, MainLayer,
				Alignment::Bottom, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
		}

		return true;
	}

	void MpInGameLobby::OnTouchEvent(const nCine::TouchEvent& event)
	{
		// TODO

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
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		_pressedActions |= ControlScheme::FetchNavigation(_levelHandler->_pressedKeys, ArrayView(joyStates, joyStatesCount));
	}
}

#endif