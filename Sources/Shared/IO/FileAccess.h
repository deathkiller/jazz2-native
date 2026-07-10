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

		/**
		 * @brief Access to the file should be exclusive (not shared)
		 * 
		 * On Windows, the file is opened with a share mode of 0, so no other process can open it while the handle
		 * is alive. On Unix, this is emulated with an advisory `flock()` lock, which only excludes other processes
		 * that also request exclusive access. In both cases opening fails immediately if the file is already
		 * opened exclusively elsewhere.
		*/
		Exclusive = 0x10,
		/** @brief A child process can inherit this handle */
		InheritHandle = 0x20,
		/** @brief Indicates that the file is to be accessed sequentially from beginning to end */
		Sequential = 0x40
	};

	DEATH_ENUM_FLAGS(FileAccess);
	
}}