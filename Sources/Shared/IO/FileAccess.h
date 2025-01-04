#pragma once

#include "../Common.h"

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Defines constants for read, write, or read/write access to a file, supports a bitwise combination of its member values
	*/
	enum struct FileAccess {
		None = 0,
		
		/** @brief Read access to the file */
		Read = 0x01,
		/** @brief Write access to the file */
		Write = 0x02,
		/** @brief Read and write access to the file */
		ReadWrite = Read | Write,

		/** @brief Access to the file should be exclusive (not shared) */
		Exclusive = 0x10,
		/** @brief A child process can inherit this handle */
		InheritHandle = 0x20
	};

	DEATH_ENUM_FLAGS(FileAccess);
	
}}