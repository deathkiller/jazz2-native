#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Base/Random.h"
#include "../RegisterArray.h"

#include <angelscript.h>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors
{
	class ActorBase;
	class Player;
}

namespace Jazz2::UI
{
	class HUD;
}

namespace Jazz2::Resources
{
	struct GenericGraphicResource;
}

namespace Jazz2::Tiles
{
	struct LayerDescription;
	struct TileMapLayer;
}

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

	/** @brief Logs that a script called a function that isn't implemented, with where it called it from */
	void Unimplemented(const char* sourceName);
	/** @brief Opens a file a script names, from the content or the directory of the original game */
	std::unique_ptr<Death::IO::Stream> OpenScriptFile(StringView path);
	/** @brief Returns whether a file name a script gives stays inside the data directory of the game */
	bool IsScriptFileNameSafe(StringView filename);

	/** @brief Returns the sine of the given table angle */
	float get_sinTable(std::uint32_t angle);
	/** @brief Returns the cosine of the given table angle */
	float get_cosTable(std::uint32_t angle);
	/** @brief Returns a random 32-bit word */
	std::uint32_t RandWord32();
	/** @brief Returns the current Unix time in seconds */
	std::uint64_t unixTimeSec();
	/** @brief Returns the current Unix time in milliseconds */
	std::uint64_t unixTimeMs();

	/** @brief Returns whether the given regular expression is valid */
	bool jjRegexIsValid(const String& expression);
	/** @brief Returns whether the text fully matches the regular expression */
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase);
	/** @brief Returns whether the text fully matches the regular expression, outputting captured groups */
	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	/** @brief Returns whether the regular expression is found anywhere in the text */
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase);
	/** @brief Returns whether the regular expression is found anywhere in the text, outputting captured groups */
	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	/** @brief Returns the text with regular expression matches replaced */
	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase);

	/** @brief Returns the current frames per second */
	std::int32_t GetFPS();

	/** @brief Returns whether the local player is a server administrator */
	bool isAdmin();

	namespace Legacy
	{
		class AnimationTables;
	}
}

#endif
