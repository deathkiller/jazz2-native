#pragma once

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides base interface for releasing resources on object destruction
	*/
	class IDisposable
	{
	public:
		IDisposable() {}
		virtual ~IDisposable() = 0;
	};

	inline IDisposable::~IDisposable() {}

}