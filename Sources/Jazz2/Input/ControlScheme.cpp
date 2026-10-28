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

		// Fire and Run also take a shoulder control - the trigger, or the bumper on pads without usable triggers
#if defined(DEATH_TARGET_N64)
		// N64: A jumps, B shoots, Z (reported as the right trigger) runs, L is a second Fire, R changes weapon
		constexpr ButtonName FireShoulder = ButtonName::LeftBumper;
		constexpr AxisName RunShoulder = AxisName::RightTrigger;
#elif defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_AMIGAOS)
		// Only L and R exist (or the Vita's trigger axes are never driven, see JoyMappingDb.h)
		constexpr ButtonName FireShoulder = ButtonName::RightBumper;
		constexpr ButtonName RunShoulder = ButtonName::LeftBumper;
#else
		constexpr AxisName FireShoulder = AxisName::RightTrigger;
		constexpr AxisName RunShoulder = AxisName::LeftTrigger;
#endif
#if defined(DEATH_TARGET_N64)
		// B stays B so it goes back in menus, there is no X/Y for Run and Change Weapon
		constexpr ButtonName FireButton = ButtonName::B;
		constexpr ButtonName RunButton = ButtonName::Unknown;
		constexpr ButtonName ChangeWeaponButton = ButtonName::RightBumper;
#else
		constexpr ButtonName FireButton = ButtonName::X;
		constexpr ButtonName RunButton = ButtonName::B;
		constexpr ButtonName ChangeWeaponButton = ButtonName::Y;
#endif
#if defined(DEATH_TARGET_AMIGAOS)
		// The Amiga's joysticks and the CD32 pad are digital, their directions arrive as the D-pad (see AmigaInputManager)
		constexpr bool HasAnalogStick = false;
#else
		constexpr bool HasAnalogStick = true;
#endif

		// Set default mappings for 1st player
		auto first = GetMappings(0);
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(Keys::Left));
#endif
		first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(0, ButtonName::Left));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(Keys::Right));
#endif
		first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(0, ButtonName::Right));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(Keys::Up));
#endif
		first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(0, ButtonName::Up));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(Keys::Down));
#endif
		first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(0, ButtonName::Down));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(Keys::Down));
#endif
		first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(0, ButtonName::Down));
		if constexpr (HasAnalogStick) {
			first[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(0, AxisName::LeftX, true));
			first[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(0, AxisName::LeftX));
			first[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(0, AxisName::LeftY, true));
			first[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(0, AxisName::LeftY));
			first[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(0, AxisName::LeftY));
		}

#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(Keys::Space));
#endif
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(0, FireButton));
		first[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(0, FireShoulder));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(Keys::V));
#endif
		first[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(0, ButtonName::A));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(Keys::C));
#endif
		if constexpr (RunButton != ButtonName::Unknown) {
			first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(0, RunButton));
		}
		first[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(0, RunShoulder));
#if defined(NCINE_HAS_KEYBOARD)
		first[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(Keys::X));
#endif
		first[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(0, ChangeWeaponButton));
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
		if constexpr (MaxSupportedPlayers >= 2) {
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

			if constexpr (MaxSupportedPlayers >= 3) {
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

				if constexpr (MaxSupportedPlayers >= 4) {
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
				current[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(i, ButtonName::Right));
				current[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(i, ButtonName::Up));
				current[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(i, ButtonName::Down));
				current[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(i, ButtonName::Down));
				if constexpr (HasAnalogStick) {
					current[(std::int32_t)PlayerAction::Left].Targets.push_back(CreateTarget(i, AxisName::LeftX, true));
					current[(std::int32_t)PlayerAction::Right].Targets.push_back(CreateTarget(i, AxisName::LeftX));
					current[(std::int32_t)PlayerAction::Up].Targets.push_back(CreateTarget(i, AxisName::LeftY, true));
					current[(std::int32_t)PlayerAction::Down].Targets.push_back(CreateTarget(i, AxisName::LeftY));
					current[(std::int32_t)PlayerAction::Buttstomp].Targets.push_back(CreateTarget(i, AxisName::LeftY));
				}
				
				current[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(i, FireButton));
				current[(std::int32_t)PlayerAction::Fire].Targets.push_back(CreateTarget(i, FireShoulder));
				current[(std::int32_t)PlayerAction::Jump].Targets.push_back(CreateTarget(i, ButtonName::A));
				if constexpr (RunButton != ButtonName::Unknown) {
					current[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(i, RunButton));
				}
				current[(std::int32_t)PlayerAction::Run].Targets.push_back(CreateTarget(i, RunShoulder));
				current[(std::int32_t)PlayerAction::ChangeWeapon].Targets.push_back(CreateTarget(i, ChangeWeaponButton));
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
		std::uint32_t assignedGamepads = 0;

		for (std::int32_t j = 0; j < MaxSupportedPlayers; j++) {
			const auto* mappings = &_mappings[j * (std::int32_t)PlayerAction::Count];
			for (std::size_t i = 0; i < (std::size_t)PlayerAction::CountInMenu; i++) {
				for (const auto& target : mappings[i].Targets) {
					if (target.Data & GamepadMask) {
						// Gamepad
						std::uint32_t joyIdx = (target.Data & GamepadIndexMask) >> 16;
						if (allowGamepads && joyIdx < joyStateCount) {
							if (joyIdx < 32) {
								assignedGamepads |= (1u << joyIdx);
							}
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
							} else if (i == (std::size_t)PlayerAction::Up || i == (std::size_t)PlayerAction::Down ||
								i == (std::size_t)PlayerAction::Left || i == (std::size_t)PlayerAction::Right) {
								// Button, only directions follow bindings, the rest is fixed below
								if (joyStates[joyIdx]->isButtonPressed((ButtonName)(target.Data & ButtonMask))) {
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

		// Menu buttons are fixed on every gamepad assigned to a player, regardless of action bindings
		for (std::uint32_t joyIdx = 0; joyIdx < joyStateCount && joyIdx < 32; joyIdx++) {
			if ((assignedGamepads & (1u << joyIdx)) == 0) {
				continue;
			}
			const auto* joyState = joyStates[joyIdx];
			if (joyState->isButtonPressed(ButtonName::A) || joyState->isButtonPressed(ButtonName::X)) {
				pressedActions |= (1 << (std::uint32_t)PlayerAction::Fire);
			}
			if (joyState->isButtonPressed(ButtonName::B) || joyState->isButtonPressed(ButtonName::Start)) {
				pressedActions |= (1 << (std::uint32_t)PlayerAction::Menu);
			}
			if (joyState->isButtonPressed(ChangeWeaponMenuButton)) {
				pressedActions |= (1 << (std::uint32_t)PlayerAction::ChangeWeapon);
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