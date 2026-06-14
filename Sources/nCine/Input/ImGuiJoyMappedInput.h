#pragma once

#if defined(WITH_IMGUI)

namespace nCine
{
	/** @brief Feeds the first mapped joystick state into ImGui as gamepad navigation input; returns `true` if a mapped joystick was present */
	bool imGuiJoyMappedInput();
}

#endif
