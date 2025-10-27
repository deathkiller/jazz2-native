#include "InputDiagnosticsSection.h"
#include "MenuResources.h"
#include "../../Input/ControlScheme.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Input/JoyMapping.h"

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

		auto& input = theApplication().GetInputManager();

		bool shouldExit = false;
		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t jc = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && jc < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[jc] = &input.joyMappedState(i);
				if (joyStates[jc]->isButtonPressed(ButtonName::Start) && joyStates[jc]->isButtonPressed(ButtonName::Back)) {
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
		Recti contentBounds = _root->GetContentBounds();
		Vector2f center = Vector2f(contentBounds.X + contentBounds.W * 0.5f, (contentBounds.Y + contentBounds.H) * 0.5f);
		float topLine = contentBounds.Y + 31.0f;

		_root->DrawElement(MenuDim, center.X, topLine - 2.0f, IMenuContainer::BackgroundLayer,
			Alignment::Top, Colorf::Black, Vector2f(680.0f, 200.0f), Vector4f(1.0f, 0.0f, -0.7f, 0.7f));
		_root->DrawElement(MenuLine, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Input Diagnostics"), charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// Show gamepad info
		auto& input = theApplication().GetInputManager();

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t jc = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && jc < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyPresent(i)) {
				jc++;
			}
		}

		if (_itemCount == 0) {
			_root->DrawStringShadow(_("No gamepads are detected!"), charOffset, center.X, topLine + 40.0f, IMenuContainer::FontLayer,
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
		std::int32_t numAxes = input.joyNumAxes(_selectedIndex);
		std::int32_t numButtons = input.joyNumButtons(_selectedIndex);
		std::int32_t numHats = input.joyNumHats(_selectedIndex);

		char buffer[128];
		std::size_t length = formatInto(buffer, "{} ({} axes, {} buttons, {} hats)", joyName, numAxes, numButtons, numHats);

		std::size_t joyNameStringLength = _root->MeasureStringApprox({ buffer, length }).X * 0.04f;
		float xMultiplier = joyNameStringLength * 0.5f;
		float easing = IMenuContainer::EaseOutElastic(_animation);
		float x = center.X * 0.4f + (1.0f - easing) * xMultiplier;
		float size = 0.85f + easing * 0.12f;

		_root->DrawElement(MenuGlow, 0, center.X * 0.4f + (joyNameStringLength + 3) * 3.8f, topLine + 20.0f, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.36f * size), 0.6f * (joyNameStringLength + 3), 6.0f, true, true);

		_root->DrawStringShadow({ buffer, length }, charOffset, x, topLine + 20.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::RandomColor, size, 0.4f, 0.6f, 0.6f, 0.6f, 0.88f);

		JoystickGuid joyGuid = input.joyGuid(_selectedIndex);
		if (joyGuid == JoystickGuidType::Default) {
			length = copyStringFirst(buffer, "GUID: default");
		} else if (joyGuid == JoystickGuidType::Hidapi) {
			length = copyStringFirst(buffer, "GUID: hidapi");
		} else if (joyGuid == JoystickGuidType::Xinput) {
			length = copyStringFirst(buffer, "GUID: xinput");
		} else {
			const std::uint8_t* g = joyGuid.data;
			length = formatInto(buffer, "GUID: {:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{:.2x}{}",
				g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7],
				g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15],
				input.hasMappingByGuid(joyGuid) ? ""_s : (input.hasMappingByName(joyName) ? " (similar mapping)"_s : " (no mapping)"_s));
		}

		_root->DrawStringShadow({ buffer, length }, charOffset, center.X * 0.4f, topLine + 40.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		_root->DrawStringShadow("Mapped State"_s, charOffset, center.X * 0.4f, topLine + 60.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		PrintAxisValue("LX"_s, mappedState.axisValue(AxisName::LeftX), center.X * 0.4f, topLine + 75.0f);
		PrintAxisValue("LY"_s, mappedState.axisValue(AxisName::LeftY), center.X * 0.4f + 110.0f, topLine + 75.0f);
		PrintAxisValue("RX"_s, mappedState.axisValue(AxisName::RightX), center.X * 0.4f + 220.0f, topLine + 75.0f);
		PrintAxisValue("RY"_s, mappedState.axisValue(AxisName::RightY), center.X * 0.4f + 330.0f, topLine + 75.0f);
		PrintAxisValue("LT"_s, mappedState.axisValue(AxisName::LeftTrigger), center.X * 0.4f, topLine + 90.0f);
		PrintAxisValue("RT"_s, mappedState.axisValue(AxisName::RightTrigger), center.X * 0.4f + 110.0f, topLine + 90.0f);

		MutableStringView bufferView = { buffer, sizeof(buffer) };
		std::size_t buttonsLength = 0;
		if (mappedState.isButtonPressed(ButtonName::A)) AppendPressedButton(bufferView, buttonsLength, "A"_s);
		if (mappedState.isButtonPressed(ButtonName::B)) AppendPressedButton(bufferView, buttonsLength, "B"_s);
		if (mappedState.isButtonPressed(ButtonName::X)) AppendPressedButton(bufferView, buttonsLength, "X"_s);
		if (mappedState.isButtonPressed(ButtonName::Y)) AppendPressedButton(bufferView, buttonsLength, "Y"_s);
		if (mappedState.isButtonPressed(ButtonName::LeftBumper)) AppendPressedButton(bufferView, buttonsLength, "LB"_s);
		if (mappedState.isButtonPressed(ButtonName::RightBumper)) AppendPressedButton(bufferView, buttonsLength, "RB"_s);
		if (mappedState.isButtonPressed(ButtonName::LeftStick)) AppendPressedButton(bufferView, buttonsLength, "LS"_s);
		if (mappedState.isButtonPressed(ButtonName::RightStick)) AppendPressedButton(bufferView, buttonsLength, "RS"_s);
		if (mappedState.isButtonPressed(ButtonName::Back)) AppendPressedButton(bufferView, buttonsLength, "Back"_s);
		if (mappedState.isButtonPressed(ButtonName::Guide)) AppendPressedButton(bufferView, buttonsLength, "Guide"_s);
		if (mappedState.isButtonPressed(ButtonName::Start)) AppendPressedButton(bufferView, buttonsLength, "Start"_s);
		if (mappedState.isButtonPressed(ButtonName::Up)) AppendPressedButton(bufferView, buttonsLength, "Up"_s);
		if (mappedState.isButtonPressed(ButtonName::Down)) AppendPressedButton(bufferView, buttonsLength, "Down"_s);
		if (mappedState.isButtonPressed(ButtonName::Left)) AppendPressedButton(bufferView, buttonsLength, "Left"_s);
		if (mappedState.isButtonPressed(ButtonName::Right)) AppendPressedButton(bufferView, buttonsLength, "Right"_s);
		if (mappedState.isButtonPressed(ButtonName::Misc1)) AppendPressedButton(bufferView, buttonsLength, "Misc1"_s);
		if (mappedState.isButtonPressed(ButtonName::Paddle1)) AppendPressedButton(bufferView, buttonsLength, "Paddle1"_s);
		if (mappedState.isButtonPressed(ButtonName::Paddle2)) AppendPressedButton(bufferView, buttonsLength, "Paddle2"_s);
		if (mappedState.isButtonPressed(ButtonName::Paddle3)) AppendPressedButton(bufferView, buttonsLength, "Paddle3"_s);
		if (mappedState.isButtonPressed(ButtonName::Paddle4)) AppendPressedButton(bufferView, buttonsLength, "Paddle4"_s);
		if (mappedState.isButtonPressed(ButtonName::Touchpad)) AppendPressedButton(bufferView, buttonsLength, "Touchpad"_s);

		_root->DrawStringShadow({ buffer, buttonsLength }, charOffset, center.X * 0.4f, topLine + 105.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		charOffset = 0;
		_root->DrawStringShadow("Raw State"_s, charOffset, center.X * 0.4f, topLine + 125.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		float sx = 0, sy = 0;
		for (std::int32_t i = 0; i < numAxes; i++) {
			length = formatInto(buffer, "a{}", i);
			PrintAxisValue({ buffer, length }, rawState.axisValue(i), center.X * 0.4f + sx, topLine + 140.0f + sy);
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

		for (std::int32_t i = 0; i < numButtons; i++) {
			length = formatInto(buffer, "b{}: {}", i, rawState.isButtonPressed(i) ? 1 : 0);
			_root->DrawStringShadow({ buffer, length }, charOffset, center.X * 0.4f + sx, topLine + 140.0f + sy, IMenuContainer::FontLayer,
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

		for (std::int32_t i = 0; i < numHats; i++) {
			length = formatInto(buffer, "h{}: {}", i, rawState.hatState(i));
			_root->DrawStringShadow({ buffer, length }, charOffset, center.X * 0.4f + sx, topLine + 140.0f + sy, IMenuContainer::FontLayer,
				Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
			sx += 55.0f;
			if (sx >= 340.0f) {
				sx = 0.0f;
				sy += 15.0f;
			}
		}

		_root->DrawElement(GetResourceForButtonName(ButtonName::Back), 0, center.X - 37.0f, contentBounds.Y + contentBounds.H - 17.0f + 2.0f,
			IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f), 0.9f, 0.9f);
		_root->DrawElement(GetResourceForButtonName(ButtonName::Back), 0, center.X - 37.0f, contentBounds.Y + contentBounds.H - 17.0f,
			IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.9f, 0.9f);
		
		_root->DrawStringShadow("+"_s, charOffset, center.X - 28.0f, contentBounds.Y + contentBounds.H - 18.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.6f, 0.4f, 0.0f, 0.0f, 0.46f, 0.88f);

		_root->DrawElement(GetResourceForButtonName(ButtonName::Start), 0, center.X - 11.0f, contentBounds.Y + contentBounds.H - 17.0f + 2.0f,
			IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f), 0.9f, 0.9f);
		_root->DrawElement(GetResourceForButtonName(ButtonName::Start), 0, center.X - 11.0f, contentBounds.Y + contentBounds.H - 17.0f,
			IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.9f, 0.9f);
		
		_root->DrawStringShadow("to exit"_s, charOffset, center.X, contentBounds.Y + contentBounds.H - 17.0f, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.4f, 0.0f, 0.0f, 0.46f, 0.8f);
	}

	void InputDiagnosticsSection::OnHandleInput()
	{
		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
		} else if (_itemCount > 1) {
			if (_root->ActionHit(PlayerAction::Left)) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;

				if (_selectedIndex > 0) {
					_selectedIndex--;
				} else {
					_selectedIndex = _itemCount - 1;
				}
			} else if (_root->ActionHit(PlayerAction::Right)) {
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

	void InputDiagnosticsSection::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
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

	void InputDiagnosticsSection::PrintAxisValue(StringView name, float value, float x, float y)
	{
		char text[64];
		std::size_t length = formatInto(text, "{}: {:.2f}", name, value);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow({ text, length }, charOffset, x, y, IMenuContainer::FontLayer,
			Alignment::Left, Font::DefaultColor, 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
	}

	void InputDiagnosticsSection::AppendPressedButton(MutableStringView output, std::size_t& offset, StringView name)
	{
		std::size_t length = name.size();
		if (offset + length + 1 < output.size()) {
			if (offset > 0) {
				output[offset++] = ' ';
			}
			std::memcpy(&output[offset], name.data(), length);
			offset += length;
		}
	}
}