#pragma once

#include "../Common.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Defines constants for read, write, or read/write access to a file, supports a bitwise combination of its member values
	*/
	enum struct FileAccess {
		None = 0,

		Read = 0x01,
		Write = 0x02,
		ReadWrite = Read | Write,

		Exclusive = 0x10,
		InheritHandle = 0x20
	};

	DEATH_ENUM_FLAGS(FileAccess);
	
}}