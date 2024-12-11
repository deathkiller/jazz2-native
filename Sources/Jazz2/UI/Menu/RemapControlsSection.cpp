#include "RemapControlsSection.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	RemapControlsSection::RemapControlsSection(std::int32_t playerIndex)
		: _selectedColumn(0), _playerIndex(playerIndex), _isDirty(false), _waitForInput(false), _timeout(0.0f),
			_hintAnimation(0.0f), _keysPressedLast(ValueInit, (std::size_t)Keys::Count)
	{
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Left, _("Left") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Right, _("Right") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Up, _("Up") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Down, _("Down") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Fire, _("Fire") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Jump, _("Jump") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Run, _("Run") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::ChangeWeapon, _("Change Weapon") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Menu, _("Back") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerActions::Console, _("Toggle Console") });

		for (std::int32_t i = 0; i <= (std::int32_t)PlayerActions::SwitchToThunderbolt - (std::int32_t)PlayerActions::SwitchToBlaster; i++) {
			// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
			_items.emplace_back(RemapControlsItem { (PlayerActions)((std::int32_t)PlayerActions::SwitchToBlaster + i), _f("Weapon %i", i + 1) });
		}
	}

	RemapControlsSection::~RemapControlsSection()
	{
		if (_isDirty) {
			_isDirty = false;
			PreferencesCache::Save();
		}
	}

	void RemapControlsSection::OnUpdate(float timeMult)
	{
		// Move the variable to stack to fix leaving the section
		bool waitingForInput = _waitForInput;

		ScrollableMenuSection::OnUpdate(timeMult);

		if (_hintAnimation < 1.0f) {
			_hintAnimation = std::min(_hintAnimation + timeMult * 0.1f, 1.0f);
		}

		if (waitingForInput) {
			auto& input = theApplication().GetInputManager();
			auto& keyState = input.keyboardState();

			if (keyState.isKeyDown(Keys::Escape) || _timeout <= 0.0f) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				return;
			}

			_timeout -= timeMult;

			MappingTarget newTarget;
			const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
			std::int32_t joyStatesCount = 0;
			for (std::uint32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < static_cast<std::int32_t>(arraySize(joyStates)) && waitingForInput; i++) {
				if (input.isJoyMapped(i)) {
					joyStates[joyStatesCount] = &input.joyMappedState(i);
					auto& prevState = _joyStatesLast[joyStatesCount];
					auto& currentState = *joyStates[joyStatesCount];

					for (std::int32_t j = 0; j < JoyMappedState::NumButtons && waitingForInput; j++) {
						bool wasPressed = prevState.isButtonPressed((ButtonName)j);
						bool isPressed = currentState.isButtonPressed((ButtonName)j);
						if (isPressed != wasPressed && isPressed) {
							newTarget = ControlScheme::CreateTarget(i, (ButtonName)j);
							std::int32_t collidingAction, collidingAssignment;
							if (!HasCollision(newTarget, collidingAction, collidingAssignment)) {
								waitingForInput = false;
							} else if (collidingAction == (std::int32_t)_items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
								// Button has collision, but it's the same as it's already assigned
								_root->PlaySfx("MenuSelect"_s, 0.5f);
								_waitForInput = false;
								return;
							}
						}
					}

					for (std::int32_t j = 0; j < JoyMappedState::NumAxes && waitingForInput; j++) {
						float prevValue = prevState.axisValue((AxisName)j);
						float currentValue = currentState.axisValue((AxisName)j);
						bool wasPressed = std::abs(prevValue) > 0.5f;
						bool isPressed = std::abs(currentValue) > 0.5f;
						if (isPressed != wasPressed && isPressed) {
							newTarget = ControlScheme::CreateTarget(i, (AxisName)j, currentValue < 0.0f);
							std::int32_t collidingAction, collidingAssignment;
							if (!HasCollision(newTarget, collidingAction, collidingAssignment)) {
								waitingForInput = false;
							} else if (collidingAction == (std::int32_t)_items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
								// Axis has collision, but it's the same as it's already assigned
								_root->PlaySfx("MenuSelect"_s, 0.5f);
								_waitForInput = false;
								return;
							}
						}
					}

					prevState = currentState;
					joyStatesCount++;
				}
			}

			for (std::int32_t key = 0; key < (std::int32_t)Keys::Count && waitingForInput; key++) {
				bool isPressed = keyState.isKeyDown((Keys)key);
				if (isPressed != _keysPressedLast[key]) {
					_keysPressedLast.set(key, isPressed);
					if (isPressed) {
						newTarget = ControlScheme::CreateTarget((Keys)key);
						std::int32_t collidingAction, collidingAssignment;
						if (!HasCollision(newTarget, collidingAction, collidingAssignment)) {
							waitingForInput = false;
						} else if (collidingAction == (std::int32_t)_items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
							// Key has collision, but it's the same as it's already assigned
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_waitForInput = false;
							return;
						}
					}
				}
			}

			if (!waitingForInput) {
				auto& mapping = ControlScheme::GetMappings(_playerIndex)[(std::int32_t)_items[_selectedIndex].Item.Type];
				if (_selectedColumn < mapping.Targets.size()) {
					mapping.Targets[_selectedColumn] = newTarget;
				} else {
					mapping.Targets.push_back(newTarget);
				}

				_isDirty = true;
				_waitForInput = false;
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
			}
		}
	}

	void RemapControlsSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		if (ControlScheme::MaxSupportedPlayers > 1) {
			_root->DrawStringShadow(_f("Remap Controls for Player %i", _playerIndex + 1), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(_("Remap Controls"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		}

		if (_waitForInput) {
			Colorf textColor = Font::DefaultColor;
			textColor.SetAlpha(_hintAnimation);
			// TRANSLATORS: Bottom hint in Options > Controls > Remap Controls section
			_root->DrawStringShadow(_("Press any key or button to assign"), charOffset, centerX, contentBounds.Y + contentBounds.H - 18.0f * IMenuContainer::EaseOutCubic(_hintAnimation), IMenuContainer::FontLayer,
				Alignment::Center, textColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);
		} else {
			auto& mapping = ControlScheme::GetMappings(_playerIndex)[_selectedIndex];
			if ((_selectedColumn < mapping.Targets.size() || _selectedColumn == MaxTargetCount - 1) && !(_selectedIndex == (std::int32_t)PlayerActions::Menu && _selectedColumn == 0)) {
				char stringBuffer[64];
				formatString(stringBuffer, sizeof(stringBuffer), "\f[c:#d0705d]%s\f[/c] │", _("Change Weapon").data());

				_root->DrawStringShadow(stringBuffer, charOffset, centerX - 15.0f, contentBounds.Y + contentBounds.H - 18.0f, IMenuContainer::FontLayer,
					Alignment::Right, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);

				_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX - 2.0f, contentBounds.Y + contentBounds.H - 18.0f + 2.0f,
					IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f), 0.8f, 0.8f);
				_root->DrawElement(GetResourceForButtonName(ButtonName::Y), 0, centerX - 2.0f, contentBounds.Y + contentBounds.H - 18.0f,
					IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 0.8f, 0.8f);
				
				// TRANSLATORS: Bottom hint in Options > Controls > Remap Controls section, prefixed with key/button to press
				_root->DrawStringShadow(_("to remove assignment"), charOffset, centerX + 8.0f, contentBounds.Y + contentBounds.H - 18.0f, IMenuContainer::FontLayer,
					Alignment::Left, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.0f, 0.0f, 0.9f);
			}
		}
	}

	void RemapControlsSection::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = ItemHeight * 5 / 8;
	}

	void RemapControlsSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;
		char stringBuffer[16];

		const auto& mapping = ControlScheme::GetMappings(_playerIndex)[(std::int32_t)item.Item.Type];

		if (isSelected) {
			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer - 200, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.1f), 26.0f, 5.0f, true, true);
		}

		_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX * 0.3f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			isSelected && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : (isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor), 0.8f);

		std::int32_t targetCount = (std::int32_t)mapping.Targets.size();
		for (std::int32_t j = 0; j < targetCount; j++) {
			StringView value;

			std::uint32_t data = mapping.Targets[j].Data;
			if (data & ControlScheme::GamepadMask) {
				std::uint32_t joyIdx = (data & ControlScheme::GamepadIndexMask) >> 16;

				if (isSelected && _selectedColumn == j) {
					value = "<    >";
				}

				if (data & ControlScheme::GamepadAnalogMask) {
					StringView axisName;
					AnimState axisAnim = GetResourceForAxisName((AxisName)(data & ControlScheme::ButtonMask), axisName);
					if (axisAnim != AnimState::Default) {
						_root->DrawElement(axisAnim, 0, centerX * (0.81f + j * 0.2f) + 2.0f, item.Y + 2.0f, IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f));
						_root->DrawElement(axisAnim, 0, centerX * (0.81f + j * 0.2f) + 2.0f, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf::White);

						for (std::int32_t i = 0; i < joyIdx + 1; i++) {
							stringBuffer[i] = '1';
						}
						stringBuffer[joyIdx + 1] = '\0';

						_root->DrawStringShadow(stringBuffer, charOffset, centerX * (0.81f + j * 0.2f) + 4.0f, item.Y - 5.0f, IMenuContainer::FontLayer,
							Alignment::Left, Font::DefaultColor, 0.75f, 0.0f, 0.0f, 0.0f, 0.4f, 0.6f);

						bool isNegative = (data & ControlScheme::GamepadNegativeMask) != 0;
						stringBuffer[0] = (isNegative ? '-' : '+');
						stringBuffer[1] = ' ';
						if (!axisName.empty()) {
							std::memcpy(stringBuffer + 2, axisName.data(), axisName.size());
						}
						stringBuffer[axisName.size() + 2] = '\0';

						_root->DrawStringShadow(stringBuffer, charOffset, centerX * (0.81f + j * 0.2f) - 10.0f, item.Y + 6.0f, IMenuContainer::FontLayer,
							Alignment::Left, Font::DefaultColor, 0.75f, 0.0f, 0.0f, 0.0f, 0.4f, 0.9f);
					}
				} else {
					AnimState buttonName = GetResourceForButtonName((ButtonName)(data & ControlScheme::ButtonMask));
					if (buttonName != AnimState::Default) {
						_root->DrawElement(buttonName, 0, centerX * (0.81f + j * 0.2f) + 2.0f, item.Y + 2.0f, IMenuContainer::ShadowLayer, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.16f));
						_root->DrawElement(buttonName, 0, centerX * (0.81f + j * 0.2f) + 2.0f, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf::White);

						for (int32_t i = 0; i < joyIdx + 1; i++) {
							stringBuffer[i] = '1';
						}
						stringBuffer[joyIdx + 1] = '\0';

						_root->DrawStringShadow(stringBuffer, charOffset, centerX * (0.81f + j * 0.2f) + 4.0f, item.Y - 5.0f, IMenuContainer::FontLayer,
							Alignment::Left, Font::DefaultColor, 0.75f, 0.0f, 0.0f, 0.0f, 0.4f, 0.6f);
					}
				}
			} else {
				Keys key = (Keys)(data & ControlScheme::ButtonMask);
				value = KeyToName(key);
			}

			if (!value.empty()) {
				if (isSelected && _selectedColumn == j) {
					float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.5f;

					Colorf color;
					if (_waitForInput) {
						color = Colorf(0.62f, 0.44f, 0.34f, 0.5f);
					} else {
						color = (_selectedIndex == (std::int32_t)PlayerActions::Menu && _selectedColumn == 0 ? Font::TransparentRandomColor : Font::RandomColor);
					}

					_root->DrawStringShadow(value, charOffset, centerX * (0.81f + j * 0.2f), item.Y, IMenuContainer::MainLayer - 10,
						Alignment::Center, color, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					_root->DrawStringShadow(value, charOffset, centerX * (0.81f + j * 0.2f), item.Y, IMenuContainer::MainLayer - 20,
						Alignment::Center, Font::DefaultColor, 0.8f);
				}
			}
		}

		if (isSelected && _selectedColumn == targetCount) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.5f;
			_root->DrawStringShadow("+"_s, charOffset, centerX * (0.81f + targetCount * 0.2f) + 2.0f, item.Y, IMenuContainer::MainLayer - 10,
				Alignment::Center, _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Colorf(0.44f, 0.62f, 0.34f, 0.5f), size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow("+"_s, charOffset, centerX * (0.81f + targetCount * 0.2f) + 2.0f, item.Y, IMenuContainer::MainLayer - 20,
				Alignment::Center, Colorf(0.42f, 0.42f, 0.42f, 0.42f), 0.8f);
		}
	}

	void RemapControlsSection::OnHandleInput()
	{
		if (_waitForInput) {
			return;
		}

		auto mapping = ControlScheme::GetMappings(_playerIndex);

		if (_root->ActionHit(PlayerActions::Menu)) {
			OnBackPressed();
		} else if (_root->ActionHit(PlayerActions::Fire)) {
			OnExecuteSelected();
		} else if (_root->ActionHit(PlayerActions::ChangeWeapon)) {
			if (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0) {
				return;
			}

			if (_selectedColumn < mapping[_selectedIndex].Targets.size()) {
				mapping[_selectedIndex].Targets.erase(_selectedColumn);

				_isDirty = true;
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
			}
		} else if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = (std::int32_t)(_items.size() - 1);
			}
			_selectedColumn = std::min({ _selectedColumn, (std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1 });

			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < (std::int32_t)(_items.size() - 1)) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
			_selectedColumn = std::min({ _selectedColumn, (std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1 });

			EnsureVisibleSelected();
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerActions::Left)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedColumn > 0) {
				_selectedColumn--;
			} else {
				std::int32_t lastColumn = std::min((std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1);
				_selectedColumn = lastColumn;
			}
		} else if (_root->ActionHit(PlayerActions::Right)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			std::int32_t lastColumn = std::min((std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1);
			if (_selectedColumn < lastColumn) {
				_selectedColumn++;
			} else {
				_selectedColumn = 0;
			}
		}
	}

	void RemapControlsSection::OnTouchUp(std::int32_t newIndex, const Vector2i& viewSize, const Vector2i& touchPos)
	{
		auto& mapping = ControlScheme::GetMappings(_playerIndex)[newIndex];

		float centerX = viewSize.X / 2;
		float firstColumnX = centerX * (0.81f - 0.1f);
		float columnWidth = centerX * 0.2f;

		float x = touchPos.X - firstColumnX;
		if (x >= 0.0f && x < columnWidth * MaxTargetCount) {
			std::int32_t newColumn = (std::int32_t)(x / columnWidth);
			if (newColumn <= mapping.Targets.size()) {
				if (_selectedIndex == newIndex && _selectedColumn == newColumn) {
					if (_waitForInput) {
						// If we are already waiting for input, delete the target instead
						_waitForInput = false;

						if (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0) {
							return;
						}

						if (_selectedColumn < mapping.Targets.size()) {
							mapping.Targets.erase(_selectedColumn);

							_isDirty = true;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
						}
					} else {
						OnExecuteSelected();
					}
				} else {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_waitForInput = false;
					_animation = 0.0f;
					_selectedIndex = newIndex;
					_selectedColumn = newColumn;
					EnsureVisibleSelected();
					OnSelectionChanged(_items[_selectedIndex]);
				}
			}
		}
	}

	void RemapControlsSection::OnBackPressed()
	{
		if (_waitForInput) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_waitForInput = false;
			return;
		}

		ScrollableMenuSection::OnBackPressed();
	}

	void RemapControlsSection::OnExecuteSelected()
	{
		if (_selectedIndex == (int32_t)PlayerActions::Menu && _selectedColumn == 0) {
			return;
		}

		_root->PlaySfx("MenuSelect"_s, 0.5f);
		_animation = 0.0f;
		_hintAnimation = 0.0f;
		_timeout = 20.0f * FrameTimer::FramesPerSecond;
		_waitForInput = true;

		RefreshPreviousState();
	}

	void RemapControlsSection::RefreshPreviousState()
	{
		auto& input = theApplication().GetInputManager();
		auto& keyState = input.keyboardState();

		_keysPressedLast.resetAll();

		for (std::int32_t key = 0; key < (int32_t)Keys::Count; key++) {
			if (keyState.isKeyDown((Keys)key)) {
				_keysPressedLast.set(key);
			}
		}

		const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < static_cast<std::int32_t>(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				_joyStatesLast[joyStatesCount++] = input.joyMappedState(i);
			}
		}
	}

	bool RemapControlsSection::HasCollision(MappingTarget target, std::int32_t& collidingAction, std::int32_t& collidingAssignment)
	{
		for (std::int32_t i = 0; i < (std::int32_t)PlayerActions::Count; i++) {
			auto& mapping = ControlScheme::GetMappings(_playerIndex)[i];
			std::int32_t targetCount = (std::int32_t)mapping.Targets.size();
			for (std::int32_t j = 0; j < targetCount; j++) {
				if (mapping.Targets[j].Data == target.Data) {
					collidingAction = i;
					collidingAssignment = j;
					return true;
				}
			}
		}

		return false;
	}

	StringView RemapControlsSection::KeyToName(Keys key)
	{
		switch (key) {
			case Keys::Backspace: return "Backspace"_s;
			case Keys::Tab: return "Tab"_s;
			case Keys::Return: return "Enter"_s;
			case Keys::Escape: return "Escape"_s;
			case Keys::Space: return "Space"_s;
			case Keys::Quote: return "'"_s;
			case Keys::Plus: return "+"_s;
			case Keys::Comma: return ","_s;
			case Keys::Minus: return "-"_s;
			case Keys::Period: return "."_s;
			case Keys::Slash: return "/"_s;
			case Keys::D0: return "0"_s;
			case Keys::D1: return "1"_s;
			case Keys::D2: return "2"_s;
			case Keys::D3: return "3"_s;
			case Keys::D4: return "4"_s;
			case Keys::D5: return "5"_s;
			case Keys::D6: return "6"_s;
			case Keys::D7: return "7"_s;
			case Keys::D8: return "8"_s;
			case Keys::D9: return "9"_s;
			case Keys::Semicolon: return ";"_s;
			case Keys::LeftBracket: return "["_s;
			case Keys::Backslash: return "\\"_s;
			case Keys::RightBracket: return "]"_s;
			case Keys::Backquote: return "`"_s;

			case Keys::A: return "A"_s;
			case Keys::B: return "B"_s;
			case Keys::C: return "C"_s;
			case Keys::D: return "D"_s;
			case Keys::E: return "E"_s;
			case Keys::F: return "F"_s;
			case Keys::G: return "G"_s;
			case Keys::H: return "H"_s;
			case Keys::I: return "I"_s;
			case Keys::J: return "J"_s;
			case Keys::K: return "K"_s;
			case Keys::L: return "L"_s;
			case Keys::M: return "M"_s;
			case Keys::N: return "N"_s;
			case Keys::O: return "O"_s;
			case Keys::P: return "P"_s;
			case Keys::Q: return "Q"_s;
			case Keys::R: return "R"_s;
			case Keys::S: return "S"_s;
			case Keys::T: return "T"_s;
			case Keys::U: return "U"_s;
			case Keys::V: return "V"_s;
			case Keys::W: return "W"_s;
			case Keys::X: return "X"_s;
			case Keys::Y: return "Y"_s;
			case Keys::Z: return "Z"_s;
			case Keys::Delete: return "Del"_s;

			case Keys::NumPad0: return "0 (N)"_s;
			case Keys::NumPad1: return "1 (N)"_s;
			case Keys::NumPad2: return "2 (N)"_s;
			case Keys::NumPad3: return "3 (N)"_s;
			case Keys::NumPad4: return "4 (N)"_s;
			case Keys::NumPad5: return "5 (N)"_s;
			case Keys::NumPad6: return "6 (N)"_s;
			case Keys::NumPad7: return "7 (N)"_s;
			case Keys::NumPad8: return "8 (N)"_s;
			case Keys::NumPad9: return "9 (N)"_s;
			case Keys::NumPadPeriod: return ". [N]"_s;
			case Keys::NumPadDivide: return "/ [N]"_s;
			case Keys::NumPadMultiply: return "* [N]"_s;
			case Keys::NumPadMinus: return "- [N]"_s;
			case Keys::NumPadPlus: return "+ [N]"_s;
			case Keys::NumPadEnter: return "Enter [N]"_s;
			case Keys::NumPadEquals: return "= [N]"_s;

			case Keys::Up: return "Up"_s;
			case Keys::Down: return "Down"_s;
			case Keys::Right: return "Right"_s;
			case Keys::Left: return "Left"_s;
			case Keys::Insert: return "Ins"_s;
			case Keys::Home: return "Home"_s;
			case Keys::End: return "End"_s;
			case Keys::PageUp: return "PgUp"_s;
			case Keys::PageDown: return "PgDn"_s;

			case Keys::F1: return "F1"_s;
			case Keys::F2: return "F2"_s;
			case Keys::F3: return "F3"_s;
			case Keys::F4: return "F4"_s;
			case Keys::F5: return "F5"_s;
			case Keys::F6: return "F6"_s;
			case Keys::F7: return "F7"_s;
			case Keys::F8: return "F8"_s;
			case Keys::F9: return "F9"_s;
			case Keys::F10: return "F10"_s;
			case Keys::F11: return "F11"_s;
			case Keys::F12: return "F12"_s;
			case Keys::F13: return "F13"_s;
			case Keys::F14: return "F14"_s;
			case Keys::F15: return "F15"_s;

			case Keys::NumLock: return "Num Lock"_s;
			case Keys::CapsLock: return "Caps Lock"_s;
			case Keys::ScrollLock: return "Scroll Lock"_s;
			case Keys::RShift: return "R. Shift"_s;
			case Keys::LShift: return "Shift"_s;
			case Keys::RCtrl: return "R. Ctrl"_s;
			case Keys::LCtrl: return "Ctrl"_s;
			case Keys::RAlt: return "R. Alt"_s;
			case Keys::LAlt: return "Alt"_s;
			//case Keys::RSuper: return "R. Super"_s;
			//case Keys::LSuper: return "Super"_s;
			//case Keys::PrintScreen: return "PrtSc"_s;
			case Keys::Pause: return "Pause"_s;
			case Keys::Menu: return "Menu"_s;

			default: return {};
		}
	}
}