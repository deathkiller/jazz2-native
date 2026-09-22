#pragma once

// Set default name and version if not provided by CMake
/** @brief Application name */
#if !defined(NCINE_APP)
#	define NCINE_APP "jazz2"
#endif
/** @brief Application full name */
#if !defined(NCINE_APP_NAME)
#	define NCINE_APP_NAME "Jazz² Resurrection"
#endif
/** @brief Application version */
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "3.8.0"
#endif
/**
	@brief Application multiplayer protocol version

	Decides whether a client and a server can play together, independently of @ref NCINE_VERSION. Bump it
	whenever the wire changes incompatibly --- packet types, property types, field layouts or the meaning of a
	field --- and leave it alone for releases that don't touch the wire, so those can still play together.

	It is compared in full, patch included, so a bump of any component is enough to separate two builds and the
	number is free to move independently of the release it ships in. Only ever set here: it is deliberately not
	derived from the build or from Git, so two builds of the same wire format always agree on it.
*/
#if !defined(NCINE_PROTOCOL_VERSION)
// Bumped past the released 3.8.0: `ServerPacketType::UpdateAllActors` gained a variable-length light block
// behind flag bit 0x80 and `CreateRemoteActor` gained a blending-preset byte plus a light block, so the field
// layout no longer matches what a 3.8.0 peer parses. A player always emits at least one remoted light, so the
// flag is set on the very first update - an unbumped 3.8.0 client reads the light bytes as the next entry's
// actor id and every remote actor after the first one in the packet gets a garbage id, flags and position.
//
// A patch bump is enough because the comparison is exact (see GameEventHandler::OnPacketReceived()); this
// number tracks the wire format, not the release it happens to ship in, so it does not have to wait for the
// next minor.
#	define NCINE_PROTOCOL_VERSION "3.8.1"
#endif
/** @brief Application build year */
#if !defined(NCINE_BUILD_YEAR)
#	define NCINE_BUILD_YEAR "2026"
#endif
/** @brief Application package name on Linux */
#if !defined(NCINE_LINUX_PACKAGE)
#	define NCINE_LINUX_PACKAGE NCINE_APP_NAME
#endif

// Prefer local version of shared libraries in CMake build
#if defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("../Shared/Common.h")
#		define __HAS_LOCAL_COMMON
#	endif
#endif
#ifdef __HAS_LOCAL_COMMON
#	include "../Shared/Common.h"
#	include "../Shared/Asserts.h"
#else
#	include <Common.h>
#	include <Asserts.h>
#endif

#include <stdlib.h>

/** @brief Install prefix on Unix systems, usually `"/usr/local"` */
#if (!defined(NCINE_INSTALL_PREFIX) && defined(DEATH_TARGET_UNIX)) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_INSTALL_PREFIX "/usr/local"
#endif

// Check platform-specific capabilities
/** @brief Whether the current platform supports a gamepad rumble, see @relativeref{nCine,IInputManager::joystickRumble()} */
#if (defined(WITH_SDL2) || defined(WITH_SDL3)) || defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_WINDOWS_RT) || defined(DEATH_TARGET_N64) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_GAMEPAD_RUMBLE
#endif
/**
	@brief Whether the current platform can deliver keyboard input

	The consoles excluded below have no keyboard the engine can read: their input backends implement
	@relativeref{nCine,IInputManager::keyboardState()} only to satisfy the interface and never produce a key
	event, so a key binding on them can never fire. It would still be built into the mapping tables and listed
	in the controls screen, which is why the defaults skip them entirely.
*/
#if (!defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_GAMECUBE) && \
	!defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_3DS) && !defined(DEATH_TARGET_PSP) && \
	!defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PS3)) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_KEYBOARD
#endif
/** @brief Whether the current platform has a native (hardware) back button */
#if defined(DEATH_TARGET_ANDROID) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_NATIVE_BACK_BUTTON
#endif
/** @brief Whether the current platform supports vibrations of the device */
#if defined(DEATH_TARGET_ANDROID)
#	define NCINE_HAS_VIBRATIONS
#endif
/** @brief Whether the current platform has non-fullscreen windows */
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && \
		!defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_3DS) && !defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_PS2) && \
		!defined(DEATH_TARGET_PS3) && !defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_AMIGAOS)
#	define NCINE_HAS_WINDOWS
#endif
/**
	@brief Whether the current platform can have a touchscreen

	Listed rather than excluded, so a platform that has no touchscreen doesn't acquire one by not being
	mentioned - which is how the consoles ended up offering to configure touch controls.

	Where it is not defined, no touch event ever reaches the game (the input backends drop them at the
	source), so the on-screen controls can never appear and the section that configures them is gone from
	the options. The PS Vita does have a front touchscreen and a rear touchpad, but both sit exactly where
	the console is held, so they only ever fire by accident - it is played with the sticks and buttons.
*/
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_EMSCRIPTEN) || \
		defined(DEATH_TARGET_SWITCH) || \
		defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_UNIX) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_TOUCH_CONTROLS
#endif
/**
	@brief Whether the current platform can drive the RGB lighting of connected devices

	Only Windows (through the Razer Chroma™ SDK) and the web build (through a local bridge) actually do so;
	the other desktop platforms are included because that is where such a device is plugged in, and where
	@relativeref{Jazz2::Input,RgbLights} could gain a backend without the option having to reappear.
*/
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_EMSCRIPTEN) || \
		defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_IOS)) || \
		defined(DOXYGEN_GENERATING_OUTPUT)
#	define NCINE_HAS_RGB_LIGHTS
#endif
/**
	@brief Whether the current platform can convert the original game data into a cache of its own

	The Dreamcast and PlayStation 2 play from a disc, the Nintendo 64 from a read-only ROM image,
	and the GameCube has nowhere to put a cache the size of a converted installation, and the web
	build is prepared entirely ahead of time - none of them can write one, so they consume a content
	tree prebaked with @ref asset-packer "AssetPacker", skip the conversion altogether and never look
	for the original game files, which are not there in the first place.

	Everywhere else the first-run conversion exists, including the PlayStation Portable and the Wii,
	where the content sits on a writable memory stick or SD card. Those two are usually given a prebaked
	tree as well, which is recognized at runtime rather than assumed here, see
	@relativeref{Jazz2,ContentResolver::IsContentPrebaked()}.
*/
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_GAMECUBE) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_N64)
#	define NCINE_HAS_WRITABLE_CACHE
#endif
/**
	@brief Whether the current platform can store a resumable mid-level session

	The state file is written next to the configuration, and on the Nintendo 64 that is the cartridge
	EEPROM: libdragon's `eepromfs` is a fixed table of files declared up front, and the two kilobytes it
	has hold the configuration alone, so a second file cannot be created there at all. Everything else
	writes it, including the platforms with no writable content cache --- the Dreamcast and the GameCube
	put it on a memory card next to their settings.

	Episode progress does not depend on this. That lives in the configuration itself
	@m_span{m-text m-dim} (@relativeref{Jazz2,PreferencesCache::GetEpisodeContinue()}) @m_endspan and is
	saved on every platform, so the N64 remembers which levels have been finished and where an episode
	was left --- only continuing from the middle of a level is unavailable there.
*/
#if !defined(DEATH_TARGET_N64)
#	define NCINE_HAS_RESUMABLE_STATE
#endif

/** @brief Function name */
#if defined(__DEATH_CURRENT_FUNCTION)
#	define NCINE_CURRENT_FUNCTION __DEATH_CURRENT_FUNCTION
#else
#	define NCINE_CURRENT_FUNCTION ""
#endif

#ifndef DOXYGEN_GENERATING_OUTPUT

// Return assert macros
#define RETURN_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURN_ASSERT(" #x ")"); return; } } while (false)

// Return false assert macros
#define RETURNF_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURNF_ASSERT(" #x ")"); return false; } } while (false)

// Fatal assert macros
#define FATAL_ASSERT(x)						\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF("FATAL_ASSERT(" #x ")");	\
			DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

#define FATAL_ASSERT_MSG(x, fmt, ...)		\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF(fmt, ##__VA_ARGS__);		\
			DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

#endif
