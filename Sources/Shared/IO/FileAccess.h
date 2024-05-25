#pragma once

#include "../Common.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Defines constants for read, write, or read/write access to a file
	*/
	enum struct FileAccess {
		None = 0,
		Read = 0x01,
		Write = 0x02,
		ReadWrite = Read | Write,
		Exclusive = 0x08
	};

	DEFINE_ENUM_OPERATORS(FileAccess);
	
}}