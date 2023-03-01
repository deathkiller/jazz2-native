#include "ControlScheme.h"
#include "../PreferencesCache.h"

namespace Jazz2::UI
{
	ControlSchemeMapping ControlScheme::_mappings[MaxSupportedPlayers * (int32_t)PlayerActions::Count] { };

	void ControlScheme::Reset()
	{
		// Set default mappings
		_mappings[(int32_t)PlayerActions::Up].Key1 = KeySym::UP;
		_mappings[(int32_t)PlayerActions::Down].Key1 = KeySym::DOWN;
		_mappings[(int32_t)PlayerActions::Left].Key1 = KeySym::LEFT;
		_mappings[(int32_t)PlayerActions::Right].Key1 = KeySym::RIGHT;
		_mappings[(int32_t)PlayerActions::Fire].Key1 = KeySym::SPACE;
		_mappings[(int32_t)PlayerActions::Jump].Key1 = KeySym::V;
		_mappings[(int32_t)PlayerActions::Run].Key1 = KeySym::C;
		_mappings[(int32_t)PlayerActions::ChangeWeapon].Key1 = KeySym::X;
		_mappings[(int32_t)PlayerActions::Menu].Key1 = KeySym::ESCAPE;

		_mappings[(int32_t)PlayerActions::Up].GamepadButton = ButtonName::DPAD_UP;
		_mappings[(int32_t)PlayerActions::Down].GamepadButton = ButtonName::DPAD_DOWN;
		_mappings[(int32_t)PlayerActions::Left].GamepadButton = ButtonName::DPAD_LEFT;
		_mappings[(int32_t)PlayerActions::Right].GamepadButton = ButtonName::DPAD_RIGHT;
		_mappings[(int32_t)PlayerActions::Fire].GamepadButton = ButtonName::X;
		_mappings[(int32_t)PlayerActions::Jump].GamepadButton = ButtonName::A;
		_mappings[(int32_t)PlayerActions::Run].GamepadButton = ButtonName::B;
		_mappings[(int32_t)PlayerActions::ChangeWeapon].GamepadButton = ButtonName::Y;
		_mappings[(int32_t)PlayerActions::Menu].GamepadButton = ButtonName::START;

		for (int32_t i = 0; i < (int32_t)PlayerActions::Count; i++) {
			_mappings[i].Key2 = KeySym::UNKNOWN;
		}

		// Unmap all other players by default
		for (int32_t i = (int32_t)PlayerActions::Count; i < MaxSupportedPlayers * (int32_t)PlayerActions::Count; i++) {
			_mappings[i].Key1 = KeySym::UNKNOWN;
			_mappings[i].Key2 = KeySym::UNKNOWN;
			_mappings[i].GamepadIndex = -1;
		}
	}
}