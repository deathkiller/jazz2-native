#pragma once

#include "../Graphics/AuxiliaryWindow.h"

#if (defined(DEATH_TARGET_WINDOWS) && defined(NCINE_HAS_AUXILIARY_WINDOWS)) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace nCine::Backends
{
	/**
	 * @brief Gives a window besides the main one its place among the windows of the system, see @ref AuxiliaryWindow
	 *
	 * Called while it's still hidden. It's owned by the main window, so it stays in front of it and is minimized with it.
	 * A popup isn't on the taskbar and has small rounded corners, other windows are on the taskbar with the icon of
	 * windows besides the main one (`WINDOW_AUXILIARY_ICON` in the resources of the executable, the icon of the main
	 * window if there's none). Both get the shadow of the system where it has one. With @p passThrough, the pointer goes
	 * through it to the windows of the application below. Returns `true` if the system rounds its corners.
	 */
	bool SetupAuxiliaryWindowWin32(void* hwnd, void* mainHwnd, bool popup, bool passThrough = false);
}

#endif
