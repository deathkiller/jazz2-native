#pragma once

#include "../PlayerActions.h"
#include "../../nCine/Input/InputEvents.h"

#include <Containers/ArrayView.h>

using namespace Death::Containers;
using namespace nCine;

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
		friend class Menu::ControlsSection;

	public:
		static constexpr int MaxSupportedPlayers = 1;

		static void Reset();

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

		static ArrayView<ControlSchemeMapping> GetMappings()
		{
			return _mappings;
		}

	private:
		/// Deleted copy constructor
		ControlScheme(const ControlScheme&) = delete;
		/// Deleted assignment operator
		ControlScheme& operator=(const ControlScheme&) = delete;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (int)PlayerActions::Count];
	};
}