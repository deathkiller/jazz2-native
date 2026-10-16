#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/StringView.h>

#include <jsoncpp/json.h>

namespace Jazz2::Editor
{
	using Death::Containers::StringView;

	/**
		@brief The rules of a tile set, `Tilesets/<name>.rules`

		A JSON object of parts the editor keeps for a tile set --- prefabs (see @ref TilePrefab) and terrain rules (see
		@ref TerrainRules). It's looked for as tile sets are, the user content first, and saved into the user content.
		Each part is changed on its own, the others are kept as they are.
	*/
	class RulesFile
	{
	public:
		RulesFile() = delete;

		/** @brief Reads the rules of a tile set, an empty object if it has none, returns `false` then */
		static bool Read(StringView tileSet, Json::Value& doc);
		/** @brief Writes the rules of a tile set into the user content */
		static bool Write(StringView tileSet, const Json::Value& doc);
	};
}

#endif
