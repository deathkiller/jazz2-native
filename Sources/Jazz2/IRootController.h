#pragma once

#include "../Common.h"
#include "LevelInitialization.h"

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

		virtual void GoToMainMenu(bool afterIntro) = 0;
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;

		virtual Flags GetFlags() const = 0;
		virtual const char* GetNewestVersion() const = 0;

		virtual void RefreshCacheLevels() = 0;
		
	private:
		/// Deleted copy constructor
		IRootController(const IRootController&) = delete;
		/// Deleted assignment operator
		IRootController& operator=(const IRootController&) = delete;

	};
}