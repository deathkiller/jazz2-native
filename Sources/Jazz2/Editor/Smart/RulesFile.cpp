#include "RulesFile.h"

#if defined(WITH_EDITOR)

#include "../Files/ContentTileSetLibrary.h"
#include "../Files/JsonFile.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelFile.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <jsoncpp/json.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Rules larger than this aren't rules of a tile set
		constexpr std::int64_t MaxRulesSize = 16 * 1024 * 1024;

		// Whether the rules found for a tile set can be replaced, those that can't be read would lose all their parts
		bool CanReplace(StringView tileSet)
		{
			Json::Value current;
			if (RulesFile::TryRead(tileSet, current) == RulesFile::ReadResult::Unreadable) {
				LOGE("Rules of tile set \"{}\" can't be read, they're not written over, fix or remove the file first", tileSet);
				return false;
			}
			return true;
		}
	}

	RulesFile::ReadResult RulesFile::TryRead(StringView tileSet, Json::Value& doc)
	{
		doc = Json::Value(Json::objectValue);
		if (!Formats::LevelFile::IsSafeTileSetPath(tileSet)) {
			return ReadResult::Missing;
		}
		String path = ContentResolver::Get().FindContentFile(fs::CombinePath("Tilesets"_s, String(fs::ToNativeSeparators(tileSet) + ".rules"_s)));
		// An empty file left behind counts as none, there's nothing in it to lose
		switch (JsonFile::Read(path, doc, MaxRulesSize, String("Rules of tile set \""_s + tileSet + "\""_s))) {
			case JsonFile::ReadResult::Ok: return ReadResult::Ok;
			case JsonFile::ReadResult::Unreadable: return ReadResult::Unreadable;
			default: return ReadResult::Missing;
		}
	}

	bool RulesFile::Read(StringView tileSet, Json::Value& doc)
	{
		return (TryRead(tileSet, doc) == ReadResult::Ok);
	}

	String RulesFile::GetWritablePath(StringView tileSet, bool create)
	{
		if (!Formats::LevelFile::IsSafeTileSetPath(tileSet)) {
			return {};
		}
		String tileSetPath = ContentTileSetLibrary::GetWritablePath(tileSet, create);
		if (tileSetPath.size() <= 4) {
			return {};
		}
		return String(tileSetPath.exceptSuffix(std::size_t(4))) + ".rules"_s;
	}

	bool RulesFile::Write(StringView tileSet, const Json::Value& doc)
	{
		if (!CanReplace(tileSet)) {
			return false;
		}
		String path = GetWritablePath(tileSet, true);
		if (path.empty()) {
			return false;
		}
		// The rules that were there stay whole if the new ones can't be written, and are kept as .bak
		return JsonFile::Write(path, doc, true);
	}

	bool RulesFile::Stage(StringView tileSet, const Json::Value& doc, String& path)
	{
		path = {};
		if (!CanReplace(tileSet)) {
			return false;
		}
		String target = GetWritablePath(tileSet, true);
		if (target.empty()) {
			return false;
		}
		if (!JsonFile::Stage(target, doc)) {
			return false;
		}
		path = Death::move(target);
		return true;
	}
}

#endif
