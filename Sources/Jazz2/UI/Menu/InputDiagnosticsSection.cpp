#include "InputDiagnosticsSection.h"
#include "MenuResources.h"
#include "../ControlScheme.h"

#include "../../../nCine/Application.h"

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	InputDiagnosticsSection::InputDiagnosticsSection()
		: _itemCount(0), _selectedIndex(0), _animation(0.0f)
	{
	}

	void InputDiagnosticsSection::OnShow(IMenuContainer* root)
	{
		_animation = 0.0f;
		MenuSection::OnShow(root);
	}

	void InputDiagnosticsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		auto& input = theApplication().inputManager();

		bool shouldExit = false;
		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		int32_t jc = 0;
		for (int32_t i = 0; i < IInputManager::MaxNumJoysticks && jc < countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[jc] = &input.joyMappedState(i);
				if (joyStates[jc]->isButtonPressed(ButtonName::START) && joyStates[jc]->isButtonPressed(ButtonName::BACK)) {
					shouldExit = true;
				}

				jc++;
			}
		}
		_itemCount = jc;

		if (_itemCount > 0 && _selectedIndex >= _itemCount) {
			_selectedIndex = _itemCount - 1;
		}

		if (shouldExit) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}

		OnHandleInput();
	}

	void InputDiagnosticsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float TopLine = 131.0f;
		_root->DrawElement(MenuDim, center.X, TopLine - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, -0.7f, 0.7f));
		_root->DrawElement(MenuLine, 0, center.X, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Input Diagnostics"), charOffset, center.X, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// Show gamepad info
		auto& input = theApplication().inputManager();

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		int32_t jc = 0;
		for (int32_t i = 0; i < IInputManager::MaxNumJoysticks && jc < countof(joyStates); i++) {
			if (input.isJoyPresent(i)) {
				jc++;
			}
		}

		if (_itemCount == 0) {
			_root->DrawStringShadow(_("No gamepads are detected!"), charOffset, center.X, TopLine + 40.0f, IMenuContainer::FontLayer,
				Alignment::Top, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.6f, 0.9f, 1.2f);
			return;
		}

		if (_itemCount > 0 && _selectedIndex >= _itemCount) {
			_selectedIndex = _itemCount - 1;
		}

		_root->DrawStringShadow("<", charOffset, center.X * 0.2f, center.Y * 1.2f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, _itemCount > 1 ? 0.5f : 0.36f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
		_root->DrawStringShadow(">", charOffset, center.X * 1.8f, center.Y * 1.2f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, _itemCount > 1 ? 0.5f : 0.36f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		const JoystickState& rawState = input.joystickState(_selectedIndex);
		const JoyMappedState& mappedState = input.joyMappedState(_selectedIndex);
		const char* joyName = input.joyName(_selectedIndex);
		int32_t numAxes = input.joyNumAxes(_selectedIndex);
		int32_t numButtons = input.joyNumButtons(_selectedIndex);
		int32_t numHats = input.joyNumHats(_selectedIndex);

		char buffer[128];
		formatString(buffer, sizeof(buffer), "%s (%i axes, %i buttons, %i hats)", joyName, numAxes, numButtons, numHats);

		size_t joyNameStringLength = Utf8::GetLength(buffer);
		float xMultiplier = joyNameStringLength * 0.5f;
		float easing = IMenuContainer::EaseOutElastic(_animation);
		float x = center.X * 0.4f + xMultiplier - easing * xMultiplier;
		float size = 0.85f + easing * 0.12f;

		_root->DrawElement(MenuGlow, 0, center.X * 0.4f + (joyNameStringLength + 3) * 3.2f, TopLine + 20.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.36f * size), 0.6f * (joyNameStringLength + 3), 6.0f, true);

		_root->DrawStringShadow(buffer, charOffset, x, TopLine + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::RandomColor, size, 0.4f, 0.6f, 0.6f, 0.6f, 0.88f);

		JoystickGuid joyGuid = input.joyGuid(_selectedIndex);
		if (joyGuid == JoystickGuidType::Default) {
			formatString(buffer, sizeof(buffer), "GUID: default");
		} else if (joyGuid == JoystickGuidType::Hidapi) {
			formatString(buffer, sizeof(buffer), "GUID: hidapi");
		} else if (joyGuid == JoystickGuidType::Xinput) {
			formatString(buffer, sizeof(buffer), "GUID: xinput");
		} else {
			formatString(buffer, sizeof(buffer), "GUID: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%s",
				joyGuid.data[0], joyGuid.data[1], joyGuid.data[2], joyGuid.data[3], joyGuid.data[4], joyGuid.data[5], joyGuid.data[6], joyGuid.data[7],
				joyGuid.data[8], joyGuid.data[9], joyGuid.data[10], joyGuid.data[11], joyGuid.data[12], joyGuid.data[13], joyGuid.data[14], joyGuid.data[15],
				input.hasMappingByGuid(joyGuid) ? "" : (input.hasMappingByName(joyName) ? " (similar mapping)" : " (no mapping)"));
		}

		_root->DrawStringShadow(buffer, charOffset, center.X * 0.4f, TopLine + 40.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		_root->DrawStringShadow("Mapped State", charOffset, center.X * 0.4f, TopLine + 60.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		PrintAxisValue("LX", mappedState.axisValue(AxisName::LX), center.X * 0.4f, TopLine + 75.0f);
		PrintAxisValue("LY", mappedState.axisValue(AxisName::LY), center.X * 0.4f + 110.0f, TopLine + 75.0f);
		PrintAxisValue("RX", mappedState.axisValue(AxisName::RX), center.X * 0.4f + 220.0f, TopLine + 75.0f);
		PrintAxisValue("RY", mappedState.axisValue(AxisName::RY), center.X * 0.4f + 330.0f, TopLine + 75.0f);
		PrintAxisValue("LT", mappedState.axisValue(AxisName::LTRIGGER), center.X * 0.4f, TopLine + 90.0f);
		PrintAxisValue("RT", mappedState.axisValue(AxisName::RTRIGGER), center.X * 0.4f + 110.0f, TopLine + 90.0f);

		int32_t buttonsLength = 0;
		buffer[0] = '\0';
		if (mappedState.isButtonPressed(ButtonName::A)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "A ");
		if (mappedState.isButtonPressed(ButtonName::B)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "B ");
		if (mappedState.isButtonPressed(ButtonName::X)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "X ");
		if (mappedState.isButtonPressed(ButtonName::Y)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Y ");
		if (mappedState.isButtonPressed(ButtonName::LBUMPER)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "LB ");
		if (mappedState.isButtonPressed(ButtonName::RBUMPER)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "RB ");
		if (mappedState.isButtonPressed(ButtonName::LSTICK)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "LS ");
		if (mappedState.isButtonPressed(ButtonName::RSTICK)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "RS ");
		if (mappedState.isButtonPressed(ButtonName::BACK)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Back ");
		if (mappedState.isButtonPressed(ButtonName::GUIDE)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Guide ");
		if (mappedState.isButtonPressed(ButtonName::START)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Start ");
		if (mappedState.isButtonPressed(ButtonName::DPAD_UP)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Up ");
		if (mappedState.isButtonPressed(ButtonName::DPAD_DOWN)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Down ");
		if (mappedState.isButtonPressed(ButtonName::DPAD_LEFT)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Left ");
		if (mappedState.isButtonPressed(ButtonName::DPAD_RIGHT)) buttonsLength += formatString(buffer + buttonsLength, sizeof(buffer) - buttonsLength, "Right ");

		_root->DrawStringShadow(buffer, charOffset, center.X * 0.4f, TopLine + 105.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		charOffset = 0;
		_root->DrawStringShadow("Raw State", charOffset, center.X * 0.4f, TopLine + 125.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		float sx = 0, sy = 0;
		for (int32_t i = 0; i < numAxes; i++) {
			formatString(buffer, sizeof(buffer), "a%i", i);
			PrintAxisValue(buffer, rawState.axisValue(i), center.X * 0.4f + sx, TopLine + 140.0f + sy);
			sx += 110.0f;
			if (sx >= 340.0f) {
				sx = 0.0f;
				sy += 15.0f;
			}
		}

		if (x > 0.0f) {
			sx = 0.0f;
			sy += 15.0f;
		}

		for (int32_t i = 0; i < numButtons; i++) {
			formatString(buffer, sizeof(buffer), "b%i: %i", i, rawState.isButtonPressed(i) ? 1 : 0);
			_root->DrawStringShadow(buffer, charOffset, center.X * 0.4f + sx, TopLine + 140.0f + sy, IMenuContainer::FontLayer,
				Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
			sx += 55.0f;
			if (sx >= 340.0f) {
				sx = 0.0f;
				sy += 15.0f;
			}
		}

		if (x > 0.0f) {
			sx = 0.0f;
			sy += 15.0f;
		}

		for (int32_t i = 0; i < numHats; i++) {
			formatString(buffer, sizeof(buffer), "h%i: %i", i, rawState.hatState(i));
			_root->DrawStringShadow(buffer, charOffset, center.X * 0.4f + sx, TopLine + 140.0f + sy, IMenuContainer::FontLayer,
				Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
			sx += 55.0f;
			if (sx >= 340.0f) {
				sx = 0.0f;
				sy += 15.0f;
			}
		}

		_root->DrawElement(GamepadBack, 0, center.X - 37.0f, viewSize.Y - 18.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.9f, 0.9f);
		_root->DrawStringShadow("+"_s, charOffset, center.X - 28.0f, viewSize.Y - 18.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.6f, 0.4f, 0.0f, 1.0f, 0.6f, 0.88f);
		_root->DrawElement(GamepadStart, 0, center.X - 11.0f, viewSize.Y - 18.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.9f, 0.9f);
		_root->DrawStringShadow("to exit"_s, charOffset, center.X, viewSize.Y - 17.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);
	}

	void InputDiagnosticsSection::OnHandleInput()
	{
		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else if (_itemCount > 1) {
			if (_root->ActionHit(PlayerActions::Left)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;

				if (_selectedIndex > 0) {
					_selectedIndex--;
				} else {
					_selectedIndex = _itemCount - 1;
				}
			} else if (_root->ActionHit(PlayerActions::Right)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;

				if (_selectedIndex < _itemCount - 1) {
					_selectedIndex++;
				} else {
					_selectedIndex = 0;
				}
			}
		}
	}

	void InputDiagnosticsSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
				} else if (_itemCount > 1 && std::abs(x - 0.5f) > 0.2f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					if (x < 0.5f) {
						if (_selectedIndex > 0) {
							_selectedIndex--;
						} else {
							_selectedIndex = _itemCount - 1;
						}
					} else {
						if (_selectedIndex < _itemCount - 1) {
							_selectedIndex++;
						} else {
							_selectedIndex = 0;
						}
					}
				}
			}
		}
	}

	void InputDiagnosticsSection::PrintAxisValue(const char* name, float value, float x, float y)
	{
		char text[64];
		formatString(text, sizeof(text), "%s: %0.2f", name, value);

		int32_t charOffset = 0;
		_root->DrawStringShadow(text, charOffset, x, y, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
	}
}