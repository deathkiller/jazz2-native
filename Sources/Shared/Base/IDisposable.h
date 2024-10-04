#pragma once

#include "../CommonBase.h"

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides base interface for releasing resources on object destruction
	*/
	class
#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
		__declspec(novtable)
#endif		
		IDisposable
	{
	public:
		IDisposable() {}
		virtual ~IDisposable() = 0;
	};

	inline IDisposable::~IDisposable() {}

}