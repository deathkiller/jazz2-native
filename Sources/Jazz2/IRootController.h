#pragma once

#include "../Common.h"
#include "LevelInitialization.h"

namespace Jazz2
{
	class IRootController
	{
	public:
		IRootController() { }
		virtual ~IRootController() { }

		virtual void GoToMainMenu(bool afterIntro) = 0;
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;

		virtual bool IsVerified() const = 0;
		virtual bool IsPlayable() const = 0;
		virtual const char* GetNewestVersion() const = 0;

		virtual void RefreshCacheLevels() = 0;
		
	private:
		/// Deleted copy constructor
		IRootController(const IRootController&) = delete;
		/// Deleted assignment operator
		IRootController& operator=(const IRootController&) = delete;

	};
}