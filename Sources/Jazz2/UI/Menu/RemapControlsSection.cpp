#include "RemapControlsSection.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Input/JoyMapping.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	RemapControlsSection::RemapControlsSection(std::int32_t playerIndex)
		: _selectedColumn(0), _playerIndex(playerIndex), _isDirty(false), _waitForInput(false), _waitForInputPrev(false),
			_timeout(0.0f), _hintAnimation(0.0f)
	{
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Left, _("Left") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Right, _("Right") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Up, _("Up") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Down, _("Down") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Buttstomp, _("Buttstomp") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Fire, _("Fire") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Jump, _("Jump") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Run, _("Run") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::ChangeWeapon, _("Change Weapon") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Menu, _("Back") });
		// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
		_items.emplace_back(RemapControlsItem { PlayerAction::Console, _("Toggle Console") });

		for (std::int32_t i = 0; i <= (std::int32_t)PlayerAction::SwitchToThunderbolt - (std::int32_t)PlayerAction::SwitchToBlaster; i++) {
			// TRANSLATORS: Menu item in Options > Controls > Remap Controls section
			_items.emplace_back(RemapControlsItem { (PlayerAction)((std::int32_t)PlayerAction::SwitchToBlaster + i), _f("Weapon %i", i + 1) });
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
			if (_timeout <= 0.0f) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				_waitForInputPrev = true;
				return;
			}

			_timeout -= timeMult;

			auto& input = theApplication().GetInputManager();
			MappingTarget newTarget;
			const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
			std::int32_t joyStatesCount = 0;
			for (std::uint32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)) && waitingForInput; i++) {
				if (input.isJoyMapped(i)) {
					joyStates[joyStatesCount] = &input.joyMappedState(i);
					auto& prevState = _joyStatesLast[joyStatesCount];
					auto& currentState = *joyStates[joyStatesCount];

					for (std::int32_t j = 0; j < JoyMappedState::NumButtons && waitingForInput; j++) {
						bool wasPressed = prevState.isButtonPressed((ButtonName)j);
						bool isPressed = currentState.isButtonPressed((ButtonName)j);
						if (isPressed != wasPressed && isPressed) {
							newTarget = ControlScheme::CreateTarget(i, (ButtonName)j);
							PlayerAction collidingAction;
							std::int32_t collidingAssignment;
							if (!HasCollision(_items[_selectedIndex].Item.Type, newTarget, collidingAction, collidingAssignment)) {
								waitingForInput = false;
							} else if (collidingAction == _items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
								// Button has collision, but it's the same as it's already assigned
								_root->PlaySfx("MenuSelect"_s, 0.5f);
								_waitForInput = false;
								_waitForInputPrev = true;
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
							PlayerAction collidingAction;
							std::int32_t collidingAssignment;
							if (!HasCollision(_items[_selectedIndex].Item.Type, newTarget, collidingAction, collidingAssignment)) {
								waitingForInput = false;
							} else if (collidingAction == _items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
								// Axis has collision, but it's the same as it's already assigned
								_root->PlaySfx("MenuSelect"_s, 0.5f);
								_waitForInput = false;
								_waitForInputPrev = true;
								return;
							}
						}
					}

					prevState = currentState;
					joyStatesCount++;
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
				_waitForInputPrev = true;
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
				return;
			}
		}

		_waitForInputPrev = false;
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
			if ((_selectedColumn < mapping.Targets.size() || _selectedColumn == MaxTargetCount - 1) && !(_selectedIndex == (std::int32_t)PlayerAction::Menu && _selectedColumn == 0)) {
				char stringBuffer[64];
				formatString(stringBuffer, "\f[c:#d0705d]%s\f[/c] │", _("Change Weapon").data());

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
			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer - 200, Alignment::Center,
				Colorf(1.0f, 1.0f, 1.0f, 0.1f), 26.0f, 5.0f, true, true);
		}

		Vector2f displayNameSize = _root->MeasureString(item.Item.DisplayName, 0.8f);
		_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX * 0.3f, item.Y, IMenuContainer::MainLayer - 100, Alignment::Left,
			isSelected && _waitForInput ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : (isSelected ? Colorf(0.48f, 0.48f, 0.48f, 0.5f) : Font::DefaultColor),
			displayNameSize.X > 120.0f ? (displayNameSize.X > 150.0f ? 0.68f : 0.72f) : 0.8f);

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
				value = theApplication().GetInputManager().getKeyName(key);
			}

			if (!value.empty()) {
				if (isSelected && _selectedColumn == j) {
					float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.5f;

					Colorf color;
					if (_waitForInput) {
						color = Colorf(0.62f, 0.44f, 0.34f, 0.5f);
					} else {
						color = (_selectedIndex == (std::int32_t)PlayerAction::Menu && _selectedColumn == 0 ? Font::TransparentRandomColor : Font::RandomColor);
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

	void RemapControlsSection::OnKeyPressed(const KeyboardEvent& event)
	{
		bool waitingForInput = _waitForInput;

		if (waitingForInput) {
			if (event.sym == Keys::Escape) {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				_waitForInputPrev = true;
				return;
			}

			MappingTarget newTarget = ControlScheme::CreateTarget(event.sym);
			PlayerAction collidingAction;
			std::int32_t collidingAssignment;
			if (!HasCollision(_items[_selectedIndex].Item.Type, newTarget, collidingAction, collidingAssignment)) {
				waitingForInput = false;
			} else if (collidingAction == _items[_selectedIndex].Item.Type && collidingAssignment == _selectedColumn) {
				// Key has collision, but it's the same as it's already assigned
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_waitForInput = false;
				_waitForInputPrev = true;
				return;
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
				_waitForInputPrev = true;
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
			}
		}
	}

	void RemapControlsSection::OnHandleInput()
	{
		if (_waitForInput || _waitForInputPrev) {
			return;
		}

		auto mapping = ControlScheme::GetMappings(_playerIndex);

		if (_root->ActionHit(PlayerAction::Menu)) {
			OnBackPressed();
		} else if (_root->ActionHit(PlayerAction::Fire)) {
			OnExecuteSelected();
		} else if (_root->ActionHit(PlayerAction::ChangeWeapon)) {
			if (_selectedIndex == (int32_t)PlayerAction::Menu && _selectedColumn == 0) {
				return;
			}

			if (_selectedColumn < mapping[_selectedIndex].Targets.size()) {
				mapping[_selectedIndex].Targets.erase(_selectedColumn);

				_isDirty = true;
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_root->ApplyPreferencesChanges(ChangedPreferencesType::ControlScheme);
			}
		} else if (_root->ActionHit(PlayerAction::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			std::int32_t offset;
			if (_selectedIndex > 0) {
				_selectedIndex--;
				offset = -ItemHeight / 3;
			} else {
				_selectedIndex = (std::int32_t)(_items.size() - 1);
				offset = 0;
			}
			_selectedColumn = std::min({ _selectedColumn, (std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1 });

			EnsureVisibleSelected(offset);
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerAction::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			std::int32_t offset;
			if (_selectedIndex < (std::int32_t)(_items.size() - 1)) {
				_selectedIndex++;
				offset = ItemHeight / 3;
			} else {
				_selectedIndex = 0;
				offset = 0;
			}
			_selectedColumn = std::min({ _selectedColumn, (std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1 });

			EnsureVisibleSelected(offset);
			OnSelectionChanged(_items[_selectedIndex]);
		} else if (_root->ActionHit(PlayerAction::Left)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedColumn > 0) {
				_selectedColumn--;
			} else {
				std::int32_t lastColumn = std::min((std::int32_t)mapping[_selectedIndex].Targets.size(), MaxTargetCount - 1);
				_selectedColumn = lastColumn;
			}
		} else if (_root->ActionHit(PlayerAction::Right)) {
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

	void RemapControlsSection::OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos)
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
						_waitForInputPrev = true;

						if (_selectedIndex == (int32_t)PlayerAction::Menu && _selectedColumn == 0) {
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
					_waitForInputPrev = true;
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
			_waitForInputPrev = true;
			return;
		}

		ScrollableMenuSection::OnBackPressed();
	}

	void RemapControlsSection::OnExecuteSelected()
	{
		if (_selectedIndex == (int32_t)PlayerAction::Menu && _selectedColumn == 0) {
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

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				_joyStatesLast[joyStatesCount++] = input.joyMappedState(i);
			}
		}
	}

	bool RemapControlsSection::HasCollision(PlayerAction action, MappingTarget target, PlayerAction& collidingAction, std::int32_t& collidingAssignment)
	{
		for (std::int32_t i = 0; i < (std::int32_t)PlayerAction::Count; i++) {
			PlayerAction ia = (PlayerAction)i;

			if ((action == PlayerAction::Down && ia == PlayerAction::Buttstomp) ||
				(action == PlayerAction::Buttstomp && ia == PlayerAction::Down)) {
				continue;
			}

			auto& mapping = ControlScheme::GetMappings(_playerIndex)[i];
			std::int32_t targetCount = (std::int32_t)mapping.Targets.size();
			for (std::int32_t j = 0; j < targetCount; j++) {
				if (mapping.Targets[j].Data == target.Data) {
					collidingAction = ia;
					collidingAssignment = j;
					return true;
				}
			}
		}

		return false;
	}
}