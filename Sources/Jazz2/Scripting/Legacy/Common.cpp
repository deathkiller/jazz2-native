#if defined(WITH_ANGELSCRIPT)

#include "Common.h"
#include "Internal.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <regex>
#include <IO/FileSystem.h>

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	void Unimplemented(const char* sourceName)
	{
		const char* sectionName = nullptr;
		std::int32_t lineNumber = 0;
		auto ctx = asGetActiveContext();
		if (ctx != nullptr) {
			lineNumber = ctx->GetLineNumber(0, nullptr, &sectionName);
		}

		// Many of them are called every frame, each place in the script is reported only once
		static HashMap<std::uint64_t, bool> reported;
		std::uint64_t key = (std::uint64_t(reinterpret_cast<std::uintptr_t>(sourceName)) * 0x9E3779B97F4A7C15ull) ^
			(std::uint64_t(reinterpret_cast<std::uintptr_t>(sectionName)) * 0xC2B2AE3D27D4EB4Full) ^ std::uint32_t(lineNumber);
		if (!reported.emplace(key, true).second) {
			return;
		}

		if (ctx != nullptr) {
			LOGW("{} (called from \"{}:{}\")", sourceName, sectionName, lineNumber);
		} else {
			LOGW("{}", sourceName);
		}
	}

	// Opens a file requested by a script: tries the converted content first, then the original "Source" directory
	// (where a level's custom/original files live), so scripts can load files by their original name
	std::unique_ptr<Stream> OpenScriptFile(StringView path)
	{
		auto& resolver = ContentResolver::Get();
		// A file that's only in "Source" isn't looked for in the content, the last place of it is opened without looking
		// and that logs an error for every file
		if (resolver.FindContentFile(path).empty() && fs::IsReadableFile(fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), path)))) {
			return resolver.OpenSourceFile(path);
		}
		auto s = resolver.OpenContentFile(path);
		if (s != nullptr && s->IsValid()) {
			return s;
		}
		return resolver.OpenSourceFile(path);
	}

	// A filename that came from a script has to stay inside the game's own data directory. `fs::CombinePath()`
	// normalizes nothing, so without this a downloaded level could read or overwrite any file the process can
	// open just by prefixing enough `..` segments.
	bool IsScriptFileNameSafe(StringView filename)
	{
		if (filename.empty() || fs::IsAbsolutePath(filename)) {
			return false;
		}

		// Walked as path segments rather than searched as a substring, so only a real `..` segment is rejected
		// and a filename that merely contains dots ("save..1.asdat") still works
		const char* begin = filename.data();
		const char* end = begin + filename.size();
		const char* segment = begin;
		for (const char* it = begin; it <= end; it++) {
			if (it == end || *it == '/' || *it == '\\') {
				if (it - segment == 2 && segment[0] == '.' && segment[1] == '.') {
					return false;
				}
				segment = it + 1;
			}
		}
		return true;
	}

	// The table of JJ2 has 1024 entries, an angle is taken of it as it wraps round - a negative one a script passes as `uint`
	// would have no precision left as a float (the strings of puppets)
	float get_sinTable(std::uint32_t angle) {
		return sinf(float(angle & 1023) * fTwoPi / 1024.0f);
	};
	float get_cosTable(std::uint32_t angle) {
		return cosf(float(angle & 1023) * fTwoPi / 1024.0f);
	};
	std::uint32_t RandWord32() {
		return Random().Next();
	}
	std::uint64_t unixTimeSec() {
		return Containers::DateTime::UtcNow().ToUnixMilliseconds() / 1000;
	}
	std::uint64_t unixTimeMs() {
		return Containers::DateTime::UtcNow().ToUnixMilliseconds();
	}

	namespace
	{
		// std::regex reports an invalid expression only by throwing, without exceptions it terminates instead
		template<class T, class Func>
		T RunRegex(Func&& func, T fallback, bool logError = true)
		{
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
			try {
				return func();
			} catch (std::regex_error& e) {
				if (logError) {
					LOGE("Failed to process regular expression: {}", e.what());
				}
				return fallback;
			}
#else
			static_cast<void>(fallback);
			static_cast<void>(logError);
			return func();
#endif
		}

		std::regex CreateRegex(const String& expression, bool ignoreCase)
		{
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			return std::regex(expression.begin(), expression.end(), flags);
		}
	}

	bool jjRegexIsValid(const String& expression)
	{
		return RunRegex([&]() {
			std::regex r(expression.begin(), expression.end());
			return true;
		}, false, false);
	}
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase)
	{
		return RunRegex([&]() {
			std::regex r = CreateRegex(expression, ignoreCase);
			return std::regex_match(text.begin(), text.end(), r);
		}, false);
	}

	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase)
	{
		return RunRegex([&]() {
			std::match_results<const char*> m;
			std::regex r = CreateRegex(expression, ignoreCase);
			bool success = std::regex_match(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		}, false);
	}
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase)
	{
		return RunRegex([&]() {
			std::regex r = CreateRegex(expression, ignoreCase);
			return std::regex_search(text.begin(), text.end(), r);
		}, false);
	}

	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase)
	{
		return RunRegex([&]() {
			std::match_results<const char*> m;
			std::regex r = CreateRegex(expression, ignoreCase);
			bool success = std::regex_search(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		}, false);
	}

	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase)
	{
		return RunRegex([&]() {
			std::regex r = CreateRegex(expression, ignoreCase);
			std::string textStr = text;
			std::string replacementStr = replacement;
			return String(std::regex_replace(textStr, r, replacementStr));
		}, String(text));
	}

	std::int32_t GetFPS() {
		return (std::int32_t)theApplication().GetFrameTimer().GetAverageFps();
	}

	bool isAdmin() {
		noop(); return false;
	}
}

#endif
