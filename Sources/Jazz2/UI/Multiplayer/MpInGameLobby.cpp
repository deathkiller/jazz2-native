#include "MpInGameLobby.h"

#if defined(WITH_MULTIPLAYER)

#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"
#include "../Menu/IMenuContainer.h"
#include "../Menu/MenuResources.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Input/JoyMapping.h"

using namespace Jazz2::Multiplayer;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Multiplayer
{
	MpInGameLobby::MpInGameLobby(MpLevelHandler* levelHandler)
		: _levelHandler(levelHandler), _pressedActions(0), _animation(0.0f), _selectedPlayerType(0),
			_allowedPlayerTypes(0), _isVisible(false)
	{
		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		ASSERT_MSG(_metadata != nullptr, "Cannot load required metadata");

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
				if ((_allowedPlayerTypes & (1 << _selectedPlayerType)) != 0) {
					_levelHandler->SetPlayerReady((PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType));
				}
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

		// Debug information
		std::int32_t debugCharOffset = 0, debugShadowCharOffset = 0;
		_smallFont->DrawString(this, "This is online multiplayer preview, not final release!"_s, debugShadowCharOffset, ViewSize.X / 2, 1.0f + 1.0f,
			180, Alignment::Top, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.76f, 0.7f, 0.7f, 0.7f, 0.2f, 0.9f);
		_smallFont->DrawString(this, "This is online multiplayer preview, not final release!"_s, debugCharOffset, ViewSize.X / 2, 1.0f,
			190, Alignment::Top, Colorf(0.62f, 0.44f, 0.34f, 0.46f), 0.76f, 0.7f, 0.7f, 0.7f, 0.2f, 0.9f);


		if (PreferencesCache::ShowPerformanceMetrics) {
			if (_levelHandler->_isServer) {
#if defined(DEATH_DEBUG)
				char debugBuffer[64];
				formatString(debugBuffer, sizeof(debugBuffer), "%i b |", _levelHandler->_debugAverageUpdatePacketSize);
				_smallFont->DrawString(this, debugBuffer, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
					200, Alignment::TopRight, Font::DefaultColor, 0.8f);
#endif
			} else {
				char debugBuffer[64];
				formatString(debugBuffer, sizeof(debugBuffer), "%u ms |", _levelHandler->_networkManager->GetRoundTripTimeMs());
				_smallFont->DrawString(this, debugBuffer, debugCharOffset, ViewSize.X - 44.0f, 1.0f,
					200, Alignment::TopRight, Font::DefaultColor, 0.8f);
			}
		}

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

			DrawStringShadow(_("Character"), charOffset, center.X, center.Y + 10.0f, MainLayer,
				Alignment::Center, Font::DefaultColor, 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

			float offset = 100.0f;
			float spacing = 100.0f;

			/*if (contentBounds.W < 480) {
				offset *= 0.7f;
				spacing *= 0.7f;
			}*/

			for (std::int32_t j = 0; j < _availableCharacters; j++) {
				float x = center.X - offset + j * spacing;
				if ((_allowedPlayerTypes & (1 << j)) == 0) {
					if (_selectedPlayerType == j) {
						DrawElement(MenuGlow, 0, x, center.Y + 50.0f, MainLayer - 20, Alignment::Center,
							Colorf(1.0f, 1.0f, 1.0f, 0.2f), 3.6f, 5.0f, true, true);
					}

					DrawStringShadow(playerTypes[j], charOffset, x, center.Y + 50.0f, MainLayer,
						Alignment::Center, Colorf(0.5f, 0.5f, 0.5f, 0.34f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
				} else if ((std::int32_t)_selectedPlayerType == j) {
					float size = 0.5f + Menu::IMenuContainer::EaseOutElastic(_animation) * 0.6f;

					DrawElement(MenuGlow, 0, x, center.Y + 50.0f, MainLayer - 20, Alignment::Center,
						Colorf(1.0f, 1.0f, 1.0f, 0.26f * size), 3.6f * size, 5.0f * size, true, true);

					DrawStringShadow(playerTypes[j], charOffset, x, center.Y + 50.0f, MainLayer,
						Alignment::Center, playerColors[j], size, 0.4f, 0.9f, 0.9f, 0.8f, 0.9f);
				} else {
					_smallFont->DrawString(this, playerTypes[j], charOffset, x, center.Y + 50.0f, MainLayer,
						Alignment::Center, Font::TransparentDefaultColor, 0.9f, 0.0f, 4.0f, 4.0f, 0.4f, 0.9f);
				}
			}

			//if (_selectedIndex == i) {
				float size = 0.5f + Menu::IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				Colorf fontColor = Font::DefaultColor;
				fontColor.SetAlpha(std::min(1.0f, 0.6f + _animation));
				DrawStringShadow("<"_s, charOffset, center.X - 110.0f - 30.0f * size, center.Y + 50.0f, MainLayer,
					Alignment::Center, fontColor, 0.8f, 1.1f, -1.1f, 0.4f, 0.4f);
				DrawStringShadow(">"_s, charOffset, center.X + 110.0f + 30.0f * size, center.Y + 50.0f, MainLayer,
					Alignment::Center, fontColor, 0.8f, 1.1f, 1.1f, 0.4f, 0.4f);
			//}

			DrawStringShadow(_("Press \f[c:#d0705d]Fire\f[/c] to continue"), charOffset, center.X, (ViewSize.Y + (center.Y + 80.0f)) / 2, MainLayer,
				Alignment::Center, Font::DefaultColor, 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
		}

		return true;
	}

	void MpInGameLobby::OnTouchEvent(const nCine::TouchEvent& event)
	{
		// TODO
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)ViewSize.Y;
				Vector2i center = ViewSize / 2;

				if (y >= center.Y + 50.0f - 24.f && y <= center.Y + 50.0f + 24.0f) {
					if (x > 0.3f && x < 0.44f) {
						_selectedPlayerType = 0;
					} else if (x <= 0.56f) {
						_selectedPlayerType = 1;
					} else if (x < 0.7f) {
						_selectedPlayerType = 2;
					}
					_animation = 0.0f;
				} else if (y >= ((ViewSize.Y + (center.Y + 80.0f)) / 2) - 24.f && y <= ((ViewSize.Y + (center.Y + 80.0f)) / 2) + 24.f &&
						   x > 0.3f && x < 0.7f) {
					if ((_allowedPlayerTypes & (1 << _selectedPlayerType)) != 0) {
						_levelHandler->SetPlayerReady((PlayerType)((std::int32_t)PlayerType::Jazz + _selectedPlayerType));
					}
				}
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

		Vector2i texSize = base->TextureDiffuse->size();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending);
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