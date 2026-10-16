#include "RulesFile.h"

#if defined(WITH_EDITOR)

#include "../Files/ContentTileSetLibrary.h"
#include "../../ContentResolver.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <memory>
#include <string>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	bool RulesFile::Read(StringView tileSet, Json::Value& doc)
	{
		doc = Json::Value(Json::objectValue);
		String path = ContentResolver::Get().FindContentFile(fs::CombinePath("Tilesets"_s, String(fs::ToNativeSeparators(tileSet) + ".rules"_s)));
		if (path.empty()) {
			return false;
		}
		auto s = fs::Open(path, FileAccess::Read);
		std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
		if (size <= 0 || size > 16 * 1024 * 1024) {
			return false;
		}
		std::string text(std::size_t(size), '\0');
		s->Read(text.data(), size);
		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		std::string errors;
		if (!reader->parse(text.data(), text.data() + text.size(), &doc, &errors) || !doc.isObject()) {
			LOGW("Rules of tile set \"{}\" cannot be read: {}", tileSet, errors.c_str());
			doc = Json::Value(Json::objectValue);
			return false;
		}
		return true;
	}

	bool RulesFile::Write(StringView tileSet, const Json::Value& doc)
	{
		String tileSetPath = ContentTileSetLibrary::GetWritablePath(tileSet, true);
		if (tileSetPath.empty()) {
			return false;
		}
		String path = String(tileSetPath.exceptSuffix(std::size_t(4))) + ".rules"_s;
		Json::StreamWriterBuilder builder;
		builder["indentation"] = "\t";
		std::string text = Json::writeString(builder, doc);
		String tempPath = path + ".tmp"_s;
		{
			auto s = fs::Open(tempPath, FileAccess::Write);
			if (!s->IsValid() || s->Write(text.data(), std::int64_t(text.size())) != std::int64_t(text.size())) {
				s = nullptr;
				fs::RemoveFile(tempPath);
				return false;
			}
		}
		if (fs::FileExists(path)) {
			fs::RemoveFile(path);
		}
		return fs::Move(tempPath, path);
	}
}

#endif
