#pragma once

#include "../PlayerActions.h"
#include "../../nCine/Input/InputEvents.h"

using namespace nCine;

namespace Jazz2
{
	class PreferencesCache;
}

namespace Jazz2::UI
{
	namespace Menu
	{
		class ControlsSection;
	}

	struct ControlSchemeMapping
	{
		KeySym Key1;
		KeySym Key2;

		int GamepadIndex;
		ButtonName GamepadButton;
	};

	class ControlScheme
	{
		friend class PreferencesCache;
		friend class Menu::ControlsSection;

	public:
		static constexpr int MaxSupportedPlayers = 1;

		static KeySym Key1(int playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (int)PlayerActions::Count + (int)action].Key1;
		}

		static KeySym Key2(int playerIndex, PlayerActions action)
		{
			return _mappings[playerIndex * (int)PlayerActions::Count + (int)action].Key2;
		}

		static ButtonName Gamepad(int playerIndex, PlayerActions action, int& gamepadIndex)
		{
			auto& mapping = _mappings[playerIndex * (int)PlayerActions::Count + (int)action];
			gamepadIndex = mapping.GamepadIndex;
			return mapping.GamepadButton;
		}

	private:
		/// Deleted copy constructor
		ControlScheme(const ControlScheme&) = delete;
		/// Deleted assignment operator
		ControlScheme& operator=(const ControlScheme&) = delete;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (int)PlayerActions::Count];

		static void Initialize();
	};
}