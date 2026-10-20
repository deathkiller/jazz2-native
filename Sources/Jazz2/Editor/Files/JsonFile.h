#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

#include <jsoncpp/forwards.h>

#include <string>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Reads and writes the JSON files of the editor

		Settings, shortcuts and rules of tile sets are JSON objects, read whole into memory and written through
		@ref AtomicFile, tab-indented so they can be edited by hand. The values read are still of any type, they're
		read through @ref JsonAccess.
	*/
	class JsonFile
	{
	public:
		/** @brief What reading a file found */
		enum class ReadResult : std::uint8_t {
			/** There's no file, or it's empty */
			Missing,
			/** The file was read */
			Ok,
			/** There is a file but it can't be read or isn't a JSON object */
			Unreadable
		};

		JsonFile() = delete;

		/**
		 * @brief Reads a file holding a JSON object
		 *
		 * @param path      File to read
		 * @param value     Receives the object, an empty object unless the file was read
		 * @param maxSize   Largest file that is read, a larger one is @ref ReadResult::Unreadable
		 * @param what      Names the file in the log, as "Editor settings"
		 *
		 * Why a file that is there can't be read is logged.
		 */
		static ReadResult Read(StringView path, Json::Value& value, std::int64_t maxSize, StringView what);
		/** @brief Reads JSON from text, returns `false` and why if it can't be read */
		static bool Parse(StringView text, Json::Value& value, std::string* errors = nullptr);
		/** @brief Returns the text of a value, tab-indented, or with @p compact on a single line */
		static std::string ToString(const Json::Value& value, bool compact = false);
		/** @brief Writes a value into a file through @ref AtomicFile, tab-indented, the previous file is kept as `.bak` with @p keepBackup */
		static bool Write(StringView path, const Json::Value& value, bool keepBackup = false);
		/** @brief Writes a value only into the temporary file of @p path, see @ref AtomicFile::Stage() */
		static bool Stage(StringView path, const Json::Value& value);
	};
}

#endif
