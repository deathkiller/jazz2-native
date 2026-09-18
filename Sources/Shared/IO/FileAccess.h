#pragma once

/** @file
	@brief Enum @ref Death::IO::FileAccess
*/

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
		Sequential = 0x40,
		/**
		 * @brief The handle does not pin the file's name: other processes may delete, rename or replace the
		 *        file while it is open
		 *
		 * On Windows, an open file cannot be deleted, renamed or replaced by anyone else unless every handle to
		 * it was opened with this flag --- which is what a reader following a log another process rotates wants.
		 * Has no effect together with @ref Exclusive. On Unix, a name is never pinned by an open descriptor, so
		 * the flag is accepted and ignored.
		 */
		Unpinned = 0x80
	};

	DEATH_ENUM_FLAGS(FileAccess);
	
}}