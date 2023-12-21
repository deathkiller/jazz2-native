#pragma once

#include "../Common.h"
#include "LevelInitialization.h"

#include <functional>

namespace Jazz2
{
	class IRootController
	{
	public:
		enum class Flags {
			None = 0x00,

			IsVerified = 0x01,
			IsPlayable = 0x02,

#if defined(DEATH_TARGET_ANDROID)
			HasExternalStoragePermission = 0x10,
			HasExternalStoragePermissionOnResume = 0x20,
#endif
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(Flags);

		IRootController() { }
		virtual ~IRootController() { }

		virtual void InvokeAsync(const std::function<void()>& callback) = 0;
		virtual void InvokeAsync(std::function<void()>&& callback) = 0;
		virtual void GoToMainMenu(bool afterIntro) = 0;
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;
		virtual bool HasResumableState() const = 0;
		virtual void ResumeSavedState() = 0;
		virtual bool SaveCurrentStateIfAny() = 0;

#if defined(WITH_MULTIPLAYER)
		virtual bool ConnectToServer(const StringView address, std::uint16_t port) = 0;
		virtual bool CreateServer(LevelInitialization&& levelInit, std::uint16_t port) = 0;
#endif

		virtual Flags GetFlags() const = 0;
		virtual const char* GetNewestVersion() const = 0;

		virtual void RefreshCacheLevels() = 0;
		
	private:
		IRootController(const IRootController&) = delete;
		IRootController& operator=(const IRootController&) = delete;
	};
}