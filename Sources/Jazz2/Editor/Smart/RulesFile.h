#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/String.h>
#include <Containers/StringView.h>

#include <cstdint>

#include <jsoncpp/forwards.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief The rules of a tile set, `Tilesets/<name>.rules`

		A JSON object of parts the editor keeps for a tile set --- prefabs (see @ref TilePrefab) and terrain rules (see
		@ref TerrainRules). It's looked for as tile sets are, the user content first, and saved into the user content.
		Each part is changed on its own, the others are kept as they are. A file that is there but can't be read (a
		syntax error, say) is never written over, it would lose all of its parts, and the previous file is kept as
		`.rules.bak` whenever one is written over.
	*/
	class RulesFile
	{
	public:
		/** @brief What reading the rules of a tile set found */
		enum class ReadResult : std::uint8_t {
			/** The tile set has no rules, an empty object */
			Missing,
			/** The rules were read */
			Ok,
			/** There is a file but it can't be read or isn't a JSON object, an empty object, it must not be written over */
			Unreadable
		};

		RulesFile() = delete;

		/** @brief Reads the rules of a tile set, an empty object if it has none or they can't be read */
		static ReadResult TryRead(StringView tileSet, Json::Value& doc);
		/** @brief Reads the rules of a tile set, an empty object if it has none or they can't be read, returns `false` then */
		static bool Read(StringView tileSet, Json::Value& doc);
		/**
		 * @brief Writes the rules of a tile set into the user content
		 *
		 * Returns `false` without writing anything if the rules that are there can't be read, see @ref ReadResult::Unreadable.
		 */
		static bool Write(StringView tileSet, const Json::Value& doc);
		/**
		 * @brief Writes the rules of a tile set only into the temporary file of @p path, see @ref AtomicFile::Stage()
		 *
		 * For rules that change together with other files, they're committed or discarded with them. Returns `false`
		 * as @ref Write() does, or if the file couldn't be written.
		 */
		static bool Stage(StringView tileSet, const Json::Value& doc, String& path);
		/** @brief Returns the path the rules of a tile set are saved to, in the user content, empty if there's none */
		static String GetWritablePath(StringView tileSet, bool create);
	};
}

#endif
