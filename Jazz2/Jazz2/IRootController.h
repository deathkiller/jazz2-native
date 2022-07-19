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

		virtual void ChangeLevel(const LevelInitialization& levelInit) = 0;
		
	private:
		/// Deleted copy constructor
		IRootController(const IRootController&) = delete;
		/// Deleted assignment operator
		IRootController& operator=(const IRootController&) = delete;

	};
}