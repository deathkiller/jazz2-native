#include "ControlScheme.h"
#include "../PreferencesCache.h"
#include "../../nCine/Input/IInputManager.h"

using namespace nCine;

namespace Jazz2::Input
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (std::int32_t)PlayerAction::Count] = {};

	void ControlScheme::Reset()
	{
		// Clear all mappings
		for (auto& mapping : _mappings) {
			mapping.Targets.clear();
		}

		// Fire and Run take a shoulder control in addition to their face button. On a controller with analog
		// triggers that is the trigger, which leaves the bumpers free; the two PlayStation handhelds have a
		// shoulder row of L and R alone, so there it has to be the bumpers. Neither reports a trigger the
		// binding could reach: the PSP's built-in pad declares no trigger axis at all, and the Vita's
		// declares "lefttrigger:a4" / "righttrigger:a5" for axes nothing on the console drives (see
		// JoyMappingDb.h). Binding them is what left both actions on their face button only. Named here so
		// the first player below and the loop over the others stay one description of the same layout.
#if defined(DEATH_TARGET_N64)
		// The Nintendo 64 pad is the odd one out: it has A and B where a modern pad has four face buttons, no
		// second trigger, and Z under the middle grip. The layout follows what the console's own games settled
		// on - A jumps, B shoots, and Z is the held modifier, which is Run here. That leaves the two shoulders
		// for the rest: L is a second Fire within reach of the same hand, and R opens the weapon wheel, which
		// has nowhere else to go because the pad has no Y button (the backend publishes B as X, and reports
		// nothing at all for B, Y and Back - see N64InputManager). Z arrives as the right trigger axis rather
		// than a button, which is why Run takes an axis here while Fire takes a button.
		constexpr ButtonName FireShoulder = ButtonName::LeftBumper;
		constexpr AxisName RunShoulder = AxisName::RightTrigger;
#elif defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_PSP)
		// The 3DS is the same shape again: L and R are the only shoulder buttons every model has (ZL/ZR exist on
		// the New 3DS alone, and its backend reports them as the trigger axes), so the bumpers carry the actions
		constexpr ButtonName FireShoulder = ButtonName::RightBumper;
		constexpr ButtonName RunShoulder = ButtonName::LeftBumper;
#else
		constexpr AxisName FireShoulder = AxisName::RightTrigger;
		constexpr AxisName RunShoulder = AxisName::LeftTrigger;
#endif

		// Set default mappings for 1st player
		auto first = GetMappings(0);
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(Keys::Left));
#endif
		first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(0, ButtonName::Left));
		first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(0, AxisName::LeftX, true));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(Keys::Right));
#endif
		first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(0, ButtonName::Right));
		first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(0, AxisName::LeftX));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(Keys::Up));
#endif
		first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(0, ButtonName::Up));
		first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(0, AxisName::LeftY, true));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(Keys::Down));
#endif
		first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(0, ButtonName::Down));
		first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(0, AxisName::LeftY));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(Keys::Down));
#endif
		first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(0, ButtonName::Down));
		first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(0, AxisName::LeftY));

#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(Keys::Space));
#endif
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(0, ButtonName::X));
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(0, FireShoulder));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(Keys::V));
#endif
		first[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(0, ButtonName::A));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(Keys::C));
#endif
		first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(0, ButtonName::B));
		first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(0, RunShoulder));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(Keys::X));
#endif
		first[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(0, ButtonName::Y));
#if defined(DEATH_TARGET_N64)
		// Nothing on this pad reaches Y, so the wheel would otherwise be unopenable - see the note above
		first[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(0, ButtonName::RightBumper));
#endif
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Menu].Targets.push_back(CreateTarget(Keys::Escape));
#endif
		first[(std::int32_t)PlayerAction::Menu].Targets.push_back(CreateTarget(0, ButtonName::Start));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Console].Targets.push_back(CreateTarget(Keys::Backquote));
		first[(std::int32_t)PlayerAction::Console].Targets.push_back(CreateTarget(Keys::T));

		first[(std::int32_t)PlayerAction::SwitchToBlaster].Targets.push_back(CreateTarget(Keys::D1));
		first[(std::int32_t)PlayerAction::SwitchToBouncer].Targets.push_back(CreateTarget(Keys::D2));
		first[(std::int32_t)PlayerAction::SwitchToFreezer].Targets.push_back(CreateTarget(Keys::D3));
		first[(std::int32_t)PlayerAction::SwitchToSeeker].Targets.push_back(CreateTarget(Keys::D4));
		first[(std::int32_t)PlayerAction::SwitchToRF].Targets.push_back(CreateTarget(Keys::D5));
		first[(std::int32_t)PlayerAction::SwitchToToaster].Targets.push_back(CreateTarget(Keys::D6));
		first[(std::int32_t)PlayerAction::SwitchToTNT].Targets.push_back(CreateTarget(Keys::D7));
		first[(std::int32_t)PlayerAction::SwitchToPepper].Targets.push_back(CreateTarget(Keys::D8));
		first[(std::int32_t)PlayerAction::SwitchToElectro].Targets.push_back(CreateTarget(Keys::D9));
		first[(std::int32_t)PlayerAction::SwitchToThunderbolt].Targets.push_back(CreateTarget(Keys::D0));
#endif

		// Set default mappings for 2nd player
		if (MaxSupportedPlayers >= 2) {
#if defined(NCINE_HAS_KEYBOARD)
			auto second = GetMappings(1);
			second[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(Keys::A));
			second[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(Keys::D));
			second[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(Keys::W));
			second[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(Keys::S));
			second[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(Keys::S));

			second[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(Keys::R));
			second[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(Keys::F));
			second[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(Keys::G));
			second[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(Keys::Q));
#endif

			if (MaxSupportedPlayers >= 3) {
#if defined(NCINE_HAS_KEYBOARD)
				auto third = GetMappings(2);
				third[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(Keys::NumPad4));
				third[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(Keys::NumPad6));
				third[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(Keys::NumPad8));
				third[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(Keys::NumPad5));
				third[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(Keys::NumPad5));

				third[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(Keys::NumPadEnter));
				third[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(Keys::NumPadPlus));
				third[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(Keys::NumPadMinus));
				third[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(Keys::NumPad7));
#endif

				if (MaxSupportedPlayers >= 4) {
#if defined(NCINE_HAS_KEYBOARD)
					auto fourth = GetMappings(3);
					fourth[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(Keys::H));
					fourth[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(Keys::K));
					fourth[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(Keys::U));
					fourth[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(Keys::J));
					fourth[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(Keys::J));

					fourth[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(Keys::O));
					fourth[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(Keys::L));
					fourth[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(Keys::Semicolon));
					fourth[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(Keys::Y));
#endif
				}
			}

			for (std::int32_t i = 1; i < MaxSupportedPlayers; i++) {
				auto current = GetMappings(i);
				current[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(i, ButtonName::Left));
				current[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(i, AxisName::LeftX, true));
				current[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(i, ButtonName::Right));
				current[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(i, AxisName::LeftX));
				current[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(i, ButtonName::Up));
				current[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(i, AxisName::LeftY, true));
				current[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(i, ButtonName::Down));
				current[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(i, AxisName::LeftY));
				current[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(i, ButtonName::Down));
				current[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(i, AxisName::LeftY));
				
				current[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(i, ButtonName::X));
				current[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(i, FireShoulder));
				current[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(i, ButtonName::A));
				current[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(i, ButtonName::B));
				current[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(i, RunShoulder));
				current[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(i, ButtonName::Y));
#if defined(DEATH_TARGET_N64)
				current[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(i, ButtonName::RightBumper));
#endif
				current[(std::int32_t)PlayerAction::Menu].Targets.push_back(CreateTarget(i, ButtonName::Start));
			}
		}
	}

	ProcessedInput ControlScheme::FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, std::uint64_t prevPressedActions, bool analogAsButtons)
	{
		ProcessedInput result = {};
		std::size_t joyStateCount = joyStates.size();

		auto mappings = GetMappings(playerIndex);
		for (std::size_t i = 0; i < (std::size_t)PlayerAction::Count; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					// Gamepad
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					if (joyIdx < joyStateCount) {
						if (target.Data & GamepadAnalogMask) {
							// Analog axis
							AxisName axisName = (AxisName)(target.Data & ButtonMask);
							float axisValue = joyStates[joyIdx]->axisValue(axisName);
							if (target.Data & GamepadNegativeMask) {
								axisValue = -axisValue;
							}

							std::uint64_t maskedBits = (1ull << (std::uint32_t)i) | (1ull << (32 + (std::uint32_t)i));
							if (analogAsButtons && axisValue >= (axisName >= AxisName::LeftTrigger
									? IInputManager::TriggerButtonDeadZone
									: ((prevPressedActions & maskedBits) != maskedBits ? IInputManager::AnalogInButtonDeadZone : IInputManager::AnalogOutButtonDeadZone))) {
								result.PressedActions |= maskedBits;
							}

							if (i < 4 && axisValue > GamepadDeadZone) {
								switch ((PlayerAction)i) {
									case PlayerAction::Left: {
										float axisValueAdjusted = -axisValue;
										if (result.Movement.X < GamepadDeadZone && axisValueAdjusted < result.Movement.X) {
											result.Movement.X = axisValueAdjusted;
										}
										break;
									}
									case PlayerAction::Right: {
										if (result.Movement.X > -GamepadDeadZone && axisValue > result.Movement.X) {
											result.Movement.X = axisValue;
										}
										break;
									}
									case PlayerAction::Up: {
										float axisValueAdjusted = -axisValue;
										if (result.Movement.Y < GamepadDeadZone && axisValueAdjusted < result.Movement.Y) {
											result.Movement.Y = axisValueAdjusted;
										}
										break;
									}
									case PlayerAction::Down: {
										if (result.Movement.Y > -GamepadDeadZone && axisValue > result.Movement.Y) {
											result.Movement.Y = axisValue;
										}
										break;
									}
								}
							}
						} else {
							// Button
							if (joyStates[joyIdx]->isButtonPressed((ButtonName)(target.Data & ButtonMask))) {
								result.PressedActions |= (1ull << (std::uint32_t)i) | (1ull << (32 + (std::uint32_t)i));
								if (analogAsButtons) {
									switch ((PlayerAction)i) {
										case PlayerAction::Left: result.Movement.X = -1.0f; break;
										case PlayerAction::Right: result.Movement.X = 1.0f; break;
										case PlayerAction::Up: result.Movement.Y = -1.0f; break;
										case PlayerAction::Down: result.Movement.Y = 1.0f; break;
									}
								}
								break;
							}
						}
					}
				} else {
					// Keyboard
					if (pressedKeys[target.Data]) {
						result.PressedActions |= (1ull << (std::uint32_t)i);
						if (analogAsButtons) {
							switch ((PlayerAction)i) {
								case PlayerAction::Left: result.Movement.X = -1.0f; break;
								case PlayerAction::Right: result.Movement.X = 1.0f; break;
								case PlayerAction::Up: result.Movement.Y = -1.0f; break;
								case PlayerAction::Down: result.Movement.Y = 1.0f; break;
							}
						}
						break;
					}
				}
			}
		}

		// Normalize both axes
		float movementLengthX = std::abs(result.Movement.X);
		float normalizedLength = (movementLengthX - GamepadDeadZone) / (1.0f - GamepadDeadZone * 2.0f);
		normalizedLength = std::clamp(normalizedLength, 0.0f, 1.0f);
		result.Movement.X = std::copysign(normalizedLength, result.Movement.X);

		float movementLengthY = std::abs(result.Movement.Y);
		normalizedLength = (movementLengthY - GamepadDeadZone) / (1.0f - GamepadDeadZone * 2.0f);
		normalizedLength = std::clamp(normalizedLength, 0.0f, 1.0f);
		result.Movement.Y = std::copysign(normalizedLength, result.Movement.Y);

#if defined(DEATH_TARGET_ANDROID)
		// Allow native Android back button as menu key
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(uint32_t)Keys::Back]) {
			result.PressedActions |= (1 << (int32_t)PlayerAction::Menu);
		}
#endif
		return result;
	}

	std::uint32_t ControlScheme::FetchNavigation(const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, NavigationFlags flags)
	{
		std::uint32_t pressedActions = 0;
		std::size_t joyStateCount = joyStates.size();
		bool allowGamepads = (flags & NavigationFlags::AllowGamepads) == NavigationFlags::AllowGamepads;
		bool allowKeyboard = (flags & NavigationFlags::AllowKeyboard) == NavigationFlags::AllowKeyboard;

		for (std::int32_t j = 0; j < MaxSupportedPlayers; j++) {
			const auto* mappings = &_mappings[j * (std::int32_t)PlayerAction::Count];
			for (std::size_t i = 0; i < (std::size_t)PlayerAction::CountInMenu; i++) {
				for (const auto& target : mappings[i].Targets) {
					if (target.Data & GamepadMask) {
						// Gamepad
						std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
						if (allowGamepads && joyIdx < joyStateCount) {
							if (target.Data & GamepadAnalogMask) {
								// Analog axis
								AxisName axisName = (AxisName)(target.Data & ButtonMask);
								float axisValue = joyStates[joyIdx]->axisValue(axisName);
								if (target.Data & GamepadNegativeMask) {
									axisValue = -axisValue;
								}
								if (axisValue >= (axisName >= AxisName::LeftTrigger
										? IInputManager::TriggerButtonDeadZone
										: IInputManager::AnalogOutButtonDeadZone)) {
									pressedActions |= (1 << (std::uint32_t)i);
								}
							} else {
								// Button
								bool isPressed = false;
								switch ((PlayerAction)i) {
									case PlayerAction::Up:
									case PlayerAction::Down:
									case PlayerAction::Left:
									case PlayerAction::Right:
										isPressed = (joyStates[joyIdx]->isButtonPressed((ButtonName)(target.Data & ButtonMask)));
										break;
									case PlayerAction::Fire:
										isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::A) || joyStates[joyIdx]->isButtonPressed(ButtonName::X));
										break;
									case PlayerAction::Menu:
										isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::B) || joyStates[joyIdx]->isButtonPressed(ButtonName::Start));
										break;
									case PlayerAction::ChangeWeapon:
										isPressed = (joyStates[joyIdx]->isButtonPressed(ButtonName::Y));
										break;
								}
								if (isPressed) {
									pressedActions |= (1 << (std::uint32_t)i);
									break;
								}
							}
						}
					} else if (allowKeyboard) {
						// Keyboard
						if (pressedKeys[target.Data]) {
							pressedActions |= (1 << (std::uint32_t)i);
							break;
						}
					}
				}
			}
		}

		// Allow Jump action as confirm key
		if (pressedActions & (1 << (std::uint32_t)PlayerAction::Jump)) {
			pressedActions |= (1 << (std::uint32_t)PlayerAction::Fire);
		}

		if (allowKeyboard) {
			// Also allow Return (Enter) as confirm key
			if (pressedKeys[(std::uint32_t)Keys::Return] || pressedKeys[(std::uint32_t)Keys::NumPadEnter]) {
				pressedActions |= (1 << (std::int32_t)PlayerAction::Fire);
			}
			// Use ChangeWeapon action as delete key
			if (pressedKeys[(std::uint32_t)Keys::Delete]) {
				pressedActions |= (1 << (std::int32_t)PlayerAction::ChangeWeapon);
			}
		}

#if defined(DEATH_TARGET_ANDROID)
		// Allow native Android back button as menu key
		if (PreferencesCache::UseNativeBackButton && pressedKeys[(std::uint32_t)Keys::Back]) {
			pressedActions |= (1 << (std::int32_t)PlayerAction::Menu);
		}
#endif
		return pressedActions;
	}

	ArrayView<ControlSchemeMapping> ControlScheme::GetAllMappings()
	{
		return _mappings;
	}

	ArrayView<ControlSchemeMapping> ControlScheme::GetMappings(std::int32_t playerIdx)
	{
		return ArrayView(&_mappings[playerIdx * (std::int32_t)PlayerAction::Count], (std::int32_t)PlayerAction::Count);
	}

	std::int32_t ControlScheme::GetGamepadForPlayer(std::int32_t playerIdx)
	{
		const auto* mappings = &_mappings[playerIdx * (std::int32_t)PlayerAction::Count];
		for (std::size_t i = 0; i < (std::size_t)PlayerAction::CountInMenu; i++) {
			for (const auto& target : mappings[i].Targets) {
				if (target.Data & GamepadMask) {
					std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
					return joyIdx;
				}
			}
		}
		return -1;
	}

	bool ControlScheme::ContainsTarget(PlayerAction action, MappingTarget target)
	{
		std::int32_t index = (std::int32_t)action;
		if (index < (std::int32_t)PlayerAction::Count) {
			for (std::int32_t j = 0; j < MaxSupportedPlayers; j++) {
				const auto& mappings = _mappings[j * (std::int32_t)PlayerAction::Count + index];
				for (const auto& t : mappings.Targets) {
					if (t.Data == target.Data) {
						return true;
					}
				}
			}
		}

		return false;
	}

	MappingTarget ControlScheme::CreateTarget(Keys key)
	{
		return { (std::uint32_t)key };
	}

	MappingTarget ControlScheme::CreateTarget(std::uint32_t gamepadIndex, ButtonName button)
	{
		return { GamepadMask | ((gamepadIndex << 16) & GamepadIndexMask) | (std::uint32_t)button };
	}

	MappingTarget ControlScheme::CreateTarget(std::uint32_t gamepadIndex, AxisName axis, bool negative)
	{
		std::uint32_t result = GamepadMask | GamepadAnalogMask | ((gamepadIndex << 16) & GamepadIndexMask) | (std::uint32_t)axis;
		if (negative) {
			result |= GamepadNegativeMask;
		}
		return { result };
	}
}