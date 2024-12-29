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

	enum class NavigationFlags
	{
		None = 0,
		AllowKeyboard = 0x01,
		AllowGamepads = 0x02,
		AllowAll = AllowKeyboard | AllowGamepads
	};

	DEFINE_ENUM_OPERATORS(NavigationFlags);

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

	/** @brief Provides access to a user configured control scheme */
	class ControlScheme
	{
		friend class Menu::RemapControlsSection;

	public:
		ControlScheme() = delete;
		~ControlScheme() = delete;

		static constexpr std::int32_t MaxSupportedPlayers = 4;
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WINDOWS_RT)
		static constexpr std::int32_t MaxConnectedGamepads = 4;
#else
		static constexpr std::int32_t MaxConnectedGamepads = 6;
#endif

		static void Reset();

		static ProcessedInput FetchProcessedInput(std::int32_t playerIndex, const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, bool analogAsButtons = true);
		static std::uint32_t FetchNativation(const BitArray& pressedKeys, const ArrayView<const JoyMappedState*> joyStates, NavigationFlags flags = NavigationFlags::AllowAll);

		static ArrayView<ControlSchemeMapping> GetAllMappings();
		static ArrayView<ControlSchemeMapping> GetMappings(std::int32_t playerIdx);
		static std::int32_t GetGamepadForPlayer(std::int32_t playerIdx);

		static MappingTarget CreateTarget(Keys key);
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, ButtonName button);
		static MappingTarget CreateTarget(std::uint32_t gamepadIndex, AxisName axis, bool negative = false);

	private:
		static constexpr std::uint32_t GamepadMask = 0x80000000u;
		static constexpr std::uint32_t GamepadAnalogMask = 0x40000000u;
		static constexpr std::uint32_t GamepadNegativeMask = 0x20000000u;
		static constexpr std::uint32_t GamepadIndexMask = 0x0FFF0000u;
		static constexpr std::uint32_t ButtonMask = 0x0000FFFFu;
		static constexpr float GamepadDeadZone = 0.1f;

		static ControlSchemeMapping _mappings[MaxSupportedPlayers * (std::int32_t)PlayerActions::Count];
	};
}