#include "RemapControlsSection.h"
#include "../ControlScheme.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"

namespace Jazz2::UI::Menu
{
	RemapControlsSection::RemapControlsSection()
		: _selectedIndex(0), _selectedColumn(0), _currentPlayerIndex(0), _animation(0.0f), _isDirty(false), _waitForInput(false),
			_timeout(0.0f), _prevKeyPressed((uint32_t)KeySym::COUNT), _prevJoyPressed(8 * JoyMappedState::NumButtons)
	{
	}

	RemapControlsSection::~RemapControlsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void RemapControlsSection::OnShow(IMenuContainer* root)
	{
		RefreshCollisions();

		_animation = 0.0f;
		MenuSection::OnShow(root);
	}

	void RemapControlsSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_waitForInput) {
			auto& input = theApplication().inputManager();
			auto& keyState = input.keyboardState();

			_timeout -= timeMult;
			if (keyState.isKeyDown(KeySym::ESCAPE) || _timeout <= 0.0f) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				return;
			}

			switch (_selectedColumn) {
				case 0: // Keyboard
				case 1:
					for (int32_t key = 0; key < (int32_t)KeySym::COUNT; key++) {
						if (keyState.isKeyDown((KeySym)key) && !_prevKeyPressed[key] && !KeyToName((KeySym)key).empty()) {
							auto& mapping = ControlScheme::_mappings[_currentPlayerIndex * (int32_t)PlayerActions::Count + _selectedIndex];

							if (_selectedColumn == 0) {
								if (mapping.Key1 != (KeySym)key) {
									mapping.Key1 = (KeySym)key;
									_isDirty = true;
								}
							} else {
								if (mapping.Key2 != (KeySym)key) {
									mapping.Key2 = (KeySym)key;
									_isDirty = true;
								}
							}

							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_waitForInput = false;
							RefreshCollisions();
							break;
						}
					}
					break;

				case 2: // Gamepad
					for (int32_t i = 0, jc = 0; i < IInputManager::MaxNumJoysticks && jc < ControlScheme::MaxConnectedGamepads && _waitForInput; i++) {
						if (input.isJoyMapped(i)) {
							auto& joyState = input.joyMappedState(i);
							for (int32_t j = 0; j < JoyMappedState::NumButtons; j++) {
								if (joyState.isButtonPressed((ButtonName)j) && !_prevJoyPressed[jc * JoyMappedState::NumButtons + j]) {
									auto& mapping = ControlScheme::_mappings[_currentPlayerIndex * (int32_t)PlayerActions::Count + _selectedIndex];

									if (mapping.GamepadIndex != jc || mapping.GamepadButton != (ButtonName)j) {
										mapping.GamepadIndex = jc;
										mapping.GamepadButton = (ButtonName)j;
										_isDirty = true;
									}

									_root->PlaySfx("MenuSelect"_s, 0.5f);
									_waitForInput = false;
									RefreshCollisions();
									break;
								}
							}
							jc++;
						}
					}
					break;
			}

			if (_waitForInput) {
				RefreshPreviousState();
			}
			return;
		}

		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (_root->ActionHit(PlayerActions::Fire)) {
			if (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0) {
				return;
			}

			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			_timeout = 30.0f * FrameTimer::FramesPerSecond;
			_waitForInput = true;

			RefreshPreviousState();
			return;
		} else if (_root->ActionHit(PlayerActions::ChangeWeapon)) {
			if (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0) {
				return;
			}

			_root->PlaySfx("MenuSelect"_s, 0.5f);

			auto& mapping = ControlScheme::_mappings[_currentPlayerIndex * (int32_t)PlayerActions::Count + _selectedIndex];
			switch (_selectedColumn) {
				case 0:
					if (mapping.Key1 != KeySym::UNKNOWN) {
						mapping.Key1 = KeySym::UNKNOWN;
						_isDirty = true;
					}
					break;
				case 1:
					if (mapping.Key2 != KeySym::UNKNOWN) {
						mapping.Key2 = KeySym::UNKNOWN;
						_isDirty = true;
					}
					break;
				case 2:
					if (mapping.GamepadIndex != -1) {
						mapping.GamepadIndex = -1;
						_isDirty = true;
					}
					break;
			}
			return;
		}

		if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (int32_t)PlayerActions::Count - 1;
			}
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (int32_t)PlayerActions::Count - 1) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
		} else if (_root->ActionHit(PlayerActions::Left)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedColumn > 0) {
				_selectedColumn--;
			} else {
				_selectedColumn = PossibleButtons - 1;
			}
		} else if (_root->ActionHit(PlayerActions::Right)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedColumn < PossibleButtons - 1) {
				_selectedColumn++;
			} else {
				_selectedColumn = 0;
			}
		}
	}

	void RemapControlsSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		char stringBuffer[16];
		constexpr float topLine = 131.0f;
		float bottomLine = viewSize.Y - 42.0f;
		_root->DrawElement("MenuDim"_s, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Remap Controls"), charOffset, center.X * 0.3f, 110.0f, IMenuContainer::FontLayer,
			Alignment::Left, Colorf(0.5f, 0.5f, 0.5f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);

		_root->DrawStringShadow(_f("Key %i", 1), charOffset, center.X * (0.9f + 0 * 0.34f), 110.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
		_root->DrawStringShadow(_f("Key %i", 2), charOffset, center.X * (0.9f + 1 * 0.34f), 110.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);
		_root->DrawStringShadow(_("Gamepad"), charOffset, center.X * (0.9f + 2 * 0.34f), 110.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.8f, 0.0f, 4.0f, 4.0f, 0.4f, 0.88f);

		int32_t n = (int32_t)PlayerActions::Count;

		float topItem = topLine - 5.0f;
		float bottomItem = bottomLine + 5.0f;
		float contentHeight = bottomItem - topItem;
		float itemSpacing = contentHeight / (n + 1);

		topItem += itemSpacing;

		for (int32_t i = 0; i < n; i++) {
			StringView name;
			switch ((PlayerActions)i) {
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Up: name = _("Up"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Down: name = _("Down"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Left: name = _("Left"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Right: name = _("Right"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Fire: name = _("Fire"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Jump: name = _("Jump"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Run: name = _("Run"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::ChangeWeapon: name = _("Change Weapon"); break;
				// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
				case PlayerActions::Menu: name = _("Back"); break;
			}

			auto& mapping = ControlScheme::_mappings[_currentPlayerIndex * (int32_t)PlayerActions::Count + i];

			_root->DrawStringShadow(name, charOffset, center.X * 0.3f, topItem, IMenuContainer::FontLayer, Alignment::Left,
				_selectedIndex == i && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor, 0.8f);

			for (int32_t j = 0; j < PossibleButtons; j++) {
				StringView value;
				bool hasCollision = false;
				switch (j) {
					case 0:
						if (mapping.Key1 != KeySym::UNKNOWN) {
							value = KeyToName(mapping.Key1);
							hasCollision = HasCollision(mapping.Key1);
						} else {
							value = "-";
						}
						break;
					case 1:
						if (mapping.Key2 != KeySym::UNKNOWN) {
							value = KeyToName(mapping.Key2);
							hasCollision = HasCollision(mapping.Key2);
						} else {
							value = "-";
						}
						break;
					case 2:
						if (mapping.GamepadIndex != -1) {
							if (_selectedIndex == i && _selectedColumn == j) {
								value = "<    >";
							} else {
								value = nullptr;
							}
							hasCollision = HasCollision(mapping.GamepadIndex, mapping.GamepadButton);

							StringView buttonName;
							switch (mapping.GamepadButton) {
								case ButtonName::A: buttonName = "GamepadA"_s; break;
								case ButtonName::B: buttonName = "GamepadB"_s; break;
								case ButtonName::X: buttonName = "GamepadX"_s; break;
								case ButtonName::Y: buttonName = "GamepadY"_s; break;
								case ButtonName::BACK: buttonName = "GamepadBack"_s; break;
								case ButtonName::GUIDE: buttonName = "GamepadBigButton"_s; break;
								case ButtonName::START: buttonName = "GamepadStart"_s; break;
								case ButtonName::LSTICK: buttonName = "GamepadLeftStick"_s; break;
								case ButtonName::RSTICK: buttonName = "GamepadRightStick"_s; break;
								case ButtonName::LBUMPER: buttonName = "GamepadLeftShoulder"_s; break;
								case ButtonName::RBUMPER: buttonName = "GamepadRightShoulder"_s; break;
								case ButtonName::LTRIGGER: buttonName = "GamepadLeftTrigger"_s; break;
								case ButtonName::RTRIGGER: buttonName = "GamepadRightTrigger"_s; break;
								case ButtonName::DPAD_UP: buttonName = "GamepadDPadUp"_s; break;
								case ButtonName::DPAD_DOWN: buttonName = "GamepadDPadDown"_s; break;
								case ButtonName::DPAD_LEFT: buttonName = "GamepadDPadLeft"_s; break;
								case ButtonName::DPAD_RIGHT: buttonName = "GamepadDPadRight"_s; break;
							}

							if (!buttonName.empty()) {
								_root->DrawElement(buttonName, 0, center.X * (0.9f + j * 0.34f) + 3, topItem, IMenuContainer::MainLayer, Alignment::Center, Colorf::White);

								for (int32_t i = 0; i < mapping.GamepadIndex + 1; i++) {
									stringBuffer[i] = '1';
								}
								stringBuffer[mapping.GamepadIndex + 1] = '\0';

								_root->DrawStringShadow(stringBuffer, charOffset, center.X * (0.9f + j * 0.34f) + 4, topItem - 5, IMenuContainer::FontLayer,
									Alignment::Left, hasCollision ? Colorf(0.5f, 0.32f, 0.32f, 1.0f) : Font::DefaultColor, 0.75f, 0.0f, 0.0f, 0.0f, 0.4f, 0.6f);
							}
						} else {
							value = "-";
						}
						break;

					default: value = nullptr; break;
				}

				if (!value.empty()) {
					if (_selectedIndex == i && _selectedColumn == j) {
						float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.5f;

						Colorf color;
						if (_waitForInput) {
							color = Colorf(0.62f, 0.44f, 0.34f, 0.5f);
						} else {
							color = (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0 ? Font::TransparentRandomColor : Font::RandomColor);
						}

						_root->DrawStringShadow(value, charOffset, center.X * (0.9f + j * 0.34f), topItem, IMenuContainer::MainLayer - 10,
							Alignment::Center, color, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
					} else {
						_root->DrawStringShadow(value, charOffset, center.X * (0.9f + j * 0.34f), topItem, IMenuContainer::MainLayer - 20,
							Alignment::Center, hasCollision ? Colorf(0.5f, 0.32f, 0.32f, 1.0f) : Font::DefaultColor, 0.8f);
					}
				}
			}

			topItem += itemSpacing;
		}
	}

	void RemapControlsSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
				}
			}
		}
	}

	void RemapControlsSection::RefreshPreviousState()
	{
		auto& input = theApplication().inputManager();
		auto& keyState = input.keyboardState();

		_prevKeyPressed.ClearAll();
		_prevJoyPressed.ClearAll();

		for (int32_t key = 0; key < (int32_t)KeySym::COUNT; key++) {
			if (keyState.isKeyDown((KeySym)key)) {
				_prevKeyPressed.Set(key);
			}
		}

		for (int32_t i = 0, jc = 0; i < IInputManager::MaxNumJoysticks && jc < ControlScheme::MaxConnectedGamepads && _waitForInput; i++) {
			if (input.isJoyMapped(i)) {
				auto& joyState = input.joyMappedState(i);
				for (int32_t j = 0; j < JoyMappedState::NumButtons; j++) {
					if (joyState.isButtonPressed((ButtonName)j)) {
						_prevJoyPressed.Set(jc * JoyMappedState::NumButtons + j);
					}
				}
				jc++;
			}
		}
	}

	void RemapControlsSection::RefreshCollisions()
	{
		// TODO: Collisions
	}

	bool RemapControlsSection::HasCollision(KeySym key)
	{
		// TODO: Collisions
		return false;
	}

	bool RemapControlsSection::HasCollision(int32_t gamepadIndex, ButtonName gamepadButton)
	{
		// TODO: Collisions
		return false;
	}

	StringView RemapControlsSection::KeyToName(KeySym key)
	{
		switch (key) {
			case KeySym::BACKSPACE: return "Backspace"_s;
			case KeySym::TAB: return "Tab"_s;
			case KeySym::RETURN: return "Enter"_s;
			case KeySym::ESCAPE: return "Escape"_s;
			case KeySym::SPACE: return "Space"_s;
			case KeySym::QUOTE: return "Quote"_s;
			case KeySym::PLUS: return "+"_s;
			case KeySym::COMMA: return ","_s;
			case KeySym::MINUS: return "-"_s;
			case KeySym::PERIOD: return "."_s;
			case KeySym::SLASH: return "/"_s;
			case KeySym::N0: return "0"_s;
			case KeySym::N1: return "1"_s;
			case KeySym::N2: return "2"_s;
			case KeySym::N3: return "3"_s;
			case KeySym::N4: return "4"_s;
			case KeySym::N5: return "5"_s;
			case KeySym::N6: return "6"_s;
			case KeySym::N7: return "7"_s;
			case KeySym::N8: return "8"_s;
			case KeySym::N9: return "9"_s;
			case KeySym::SEMICOLON: return ";"_s;
			case KeySym::LEFTBRACKET: return "["_s;
			case KeySym::BACKSLASH: return "\\"_s;
			case KeySym::RIGHTBRACKET: return "]"_s;
			case KeySym::BACKQUOTE: return "Backquote"_s;

			case KeySym::A: return "A"_s;
			case KeySym::B: return "B"_s;
			case KeySym::C: return "C"_s;
			case KeySym::D: return "D"_s;
			case KeySym::E: return "E"_s;
			case KeySym::F: return "F"_s;
			case KeySym::G: return "G"_s;
			case KeySym::H: return "H"_s;
			case KeySym::I: return "I"_s;
			case KeySym::J: return "J"_s;
			case KeySym::K: return "K"_s;
			case KeySym::L: return "L"_s;
			case KeySym::M: return "M"_s;
			case KeySym::N: return "N"_s;
			case KeySym::O: return "O"_s;
			case KeySym::P: return "P"_s;
			case KeySym::Q: return "Q"_s;
			case KeySym::R: return "R"_s;
			case KeySym::S: return "S"_s;
			case KeySym::T: return "T"_s;
			case KeySym::U: return "U"_s;
			case KeySym::V: return "V"_s;
			case KeySym::W: return "W"_s;
			case KeySym::X: return "X"_s;
			case KeySym::Y: return "Y"_s;
			case KeySym::Z: return "Z"_s;
			case KeySym::Delete: return "Del"_s;

			case KeySym::KP0: return "0 (N)"_s;
			case KeySym::KP1: return "1 (N)"_s;
			case KeySym::KP2: return "2 (N)"_s;
			case KeySym::KP3: return "3 (N)"_s;
			case KeySym::KP4: return "4 (N)"_s;
			case KeySym::KP5: return "5 (N)"_s;
			case KeySym::KP6: return "6 (N)"_s;
			case KeySym::KP7: return "7 (N)"_s;
			case KeySym::KP8: return "8 (N)"_s;
			case KeySym::KP9: return "9 (N)"_s;
			case KeySym::KP_PERIOD: return ". [N]"_s;
			case KeySym::KP_DIVIDE: return "/ [N]"_s;
			case KeySym::KP_MULTIPLY: return "* [N]"_s;
			case KeySym::KP_MINUS: return "- [N]"_s;
			case KeySym::KP_PLUS: return "+ [N]"_s;
			case KeySym::KP_ENTER: return "Enter [N]"_s;
			case KeySym::KP_EQUALS: return "= [N]"_s;

			case KeySym::UP: return "Up"_s;
			case KeySym::DOWN: return "Down"_s;
			case KeySym::RIGHT: return "Right"_s;
			case KeySym::LEFT: return "Left"_s;
			case KeySym::INSERT: return "Ins"_s;
			case KeySym::HOME: return "Home"_s;
			case KeySym::END: return "End"_s;
			case KeySym::PAGEUP: return "PgUp"_s;
			case KeySym::PAGEDOWN: return "PgDn"_s;

			case KeySym::F1: return "F1"_s;
			case KeySym::F2: return "F2"_s;
			case KeySym::F3: return "F3"_s;
			case KeySym::F4: return "F4"_s;
			case KeySym::F5: return "F5"_s;
			case KeySym::F6: return "F6"_s;
			case KeySym::F7: return "F7"_s;
			case KeySym::F8: return "F8"_s;
			case KeySym::F9: return "F9"_s;
			case KeySym::F10: return "F10"_s;
			case KeySym::F11: return "F11"_s;
			case KeySym::F12: return "F12"_s;
			case KeySym::F13: return "F13"_s;
			case KeySym::F14: return "F14"_s;
			case KeySym::F15: return "F15"_s;

			case KeySym::NUM_LOCK: return "Num Lock"_s;
			case KeySym::CAPS_LOCK: return "Caps Lock"_s;
			case KeySym::SCROLL_LOCK: return "Scroll Lock"_s;
			case KeySym::RSHIFT: return "R. Shift"_s;
			case KeySym::LSHIFT: return "Shift"_s;
			case KeySym::RCTRL: return "R. Ctrl"_s;
			case KeySym::LCTRL: return "Ctrl"_s;
			case KeySym::RALT: return "R. Alt"_s;
			case KeySym::LALT: return "Alt"_s;
			//case KeySym::RSUPER: return "R. Super"_s;
			//case KeySym::LSUPER: return "Super"_s;
			//case KeySym::PRINTSCREEN: return "PrtSc"_s;
			case KeySym::PAUSE: return "Pause"_s;
			case KeySym::MENU: return "Menu"_s;

			default: return nullptr;
		}
	}
}