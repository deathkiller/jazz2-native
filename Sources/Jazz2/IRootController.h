#pragma once

#include "../Main.h"
#include "LevelInitialization.h"

#include <Containers/Function.h>

using namespace Death::Containers;

namespace Jazz2
{
	/** @brief Base interface of a root controller */
	class IRootController
	{
	public:
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

		DEFINE_PRIVATE_ENUM_OPERATORS(Flags);

		IRootController() { }
		virtual ~IRootController() { }

		IRootController(const IRootController&) = delete;
		IRootController& operator=(const IRootController&) = delete;

		virtual void InvokeAsync(Function<void()>&& callback, const char* sourceFunc = nullptr) = 0;
		virtual void GoToMainMenu(bool afterIntro) = 0;
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;
		virtual bool HasResumableState() const = 0;
		virtual void ResumeSavedState() = 0;
		virtual bool SaveCurrentStateIfAny() = 0;

#if defined(WITH_MULTIPLAYER)
		virtual bool ConnectToServer(StringView address, std::uint16_t port) = 0;
		virtual bool CreateServer(LevelInitialization&& levelInit, std::uint16_t port) = 0;

		virtual StringView GetServerName() const = 0;
		virtual void SetServerName(StringView value) = 0;
#endif

		virtual Flags GetFlags() const = 0;
		virtual StringView GetNewestVersion() const = 0;

		virtual void RefreshCacheLevels() = 0;
	};
}