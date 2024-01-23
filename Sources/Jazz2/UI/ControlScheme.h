#pragma once

#include "../PlayerActions.h"
#include "../../nCine/Base/BitArray.h"
#include "../../nCine/Input/InputEvents.h"
#include "../../nCine/Primitives/Vector2.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI
{
	namespace Menu
	{
		class RemapControlsSection;
	}

	struct MappingTarget
	{
		std::uint32_t Data;
	};

	struct ControlSchemeMapping
	{
		SmallVector<MappingTarget, 3> Targets;
	};

	struct ProcessedInput
	{
		std::uint64_t PressedActions;
		Vector2f Movement;
	};

	class ControlScheme
	{
		friend class Menu::RemapControlsSection;

	public:
		static constexpr std::int32_t MaxSupportedPlayers = 1;
#if defined(DEATH_TARGET_SWITCH)
		// TODO: Game is crashing on Switch if more than 1 gamepad is used
		static constexpr std::int32_t MaxConnectedGamepads = 1;
#else
		static constexpr std::int32_t MaxConnectedGamepads = 4;
#endif

		static void Reset();

		static ProcessedInput FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool analogAsButtons = true);
		static std::uint32_t FetchNativation(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool allowGamepads = true);

		static ArrayView<ControlSchemeMapping> GetMappings();

		static MappingTarget CreateTarget(KeySym key);
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, ButtonName button);
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, AxisName axis, bool negative = false);

	private:
		ControlScheme(const ControlScheme&) = delete;
		ControlScheme& operator=(const ControlScheme&) = delete;

		static constexpr std::uint32_t GamepadMask = 0x80000000u;
		static constexpr std::uint32_t GamepadAnalogMask = 0x40000000u;
		static constexpr std::uint32_t GamepadNegativeMask = 0x20000000u;
		static constexpr std::uint32_t GamepadIndexMask = 0x0FFF0000u;
		static constexpr std::uint32_t ButtonMask = 0x0000FFFFu;
		static constexpr float GamepadDeadzone = 0.1f;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count];
	};
}