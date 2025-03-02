#pragma once

#include "../Main.h"
#include "LevelInitialization.h"

#if defined(WITH_MULTIPLAYER)
#	include "Multiplayer/ServerInitialization.h"
#endif

#include <Containers/Function.h>

using namespace Death::Containers;

namespace Jazz2
{
	/** @brief Base interface of a root controller */
	class IRootController
	{
	public:
		/** @brief State flags of root controller, supports a bitwise combination of its member values */
		enum class Flags {
			None = 0x00,

			IsInitialized = 0x01,
			IsVerified = 0x02,
			IsPlayable = 0x04,

#if defined(DEATH_TARGET_ANDROID)
			HasExternalStoragePermission = 0x10,
			HasExternalStoragePermissionOnResume = 0x20,
#endif
		};

		DEATH_PRIVATE_ENUM_FLAGS(Flags);

		IRootController() { }
		virtual ~IRootController() { }

		IRootController(const IRootController&) = delete;
		IRootController& operator=(const IRootController&) = delete;

		/** @brief Invokes the specified callback asynchronously, usually at the end of current frame */
		virtual void InvokeAsync(Function<void()>&& callback, const char* sourceFunc = nullptr) = 0;
		/** @brief Sets current state handler to main menu */
		virtual void GoToMainMenu(bool afterIntro) = 0;
		/** @brief Sets current state handler to level described by @ref LevelInitialization */
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;
		/** @brief Returns `true` if any resumable state exists */
		virtual bool HasResumableState() const = 0;
		/** @brief Resumes saved resumable state */
		virtual void ResumeSavedState() = 0;
		/** @brief Saves current state if it's resumable */
		virtual bool SaveCurrentStateIfAny() = 0;

#if defined(WITH_MULTIPLAYER)
		/** @brief Sets current state handler to remove multiplayer session */
		virtual bool ConnectToServer(StringView address, std::uint16_t port) = 0;
		/** @brief Creates a multiplayer server */
		virtual bool CreateServer(Multiplayer::ServerInitialization&& serverInit) = 0;

		/** @brief Returns name of the server */
		virtual StringView GetServerName() const = 0;
		/** @brief Sets name of the server */
		virtual void SetServerName(StringView value) = 0;
#endif

		/** @brief Returns current state flags */
		virtual Flags GetFlags() const = 0;
		/** @brief Returns version of the latest update */
		virtual StringView GetNewestVersion() const = 0;

		/** @brief Recreates level cache from `Source` directory */
		virtual void RefreshCacheLevels(bool recreateAll) = 0;
	};
}