#include "JsonFile.h"

#if defined(WITH_EDITOR)

#include "AtomicFile.h"

#include <IO/FileSystem.h>

#include <jsoncpp/json.h>

#include <memory>

using namespace Death::IO;

namespace Jazz2::Editor
{
	JsonFile::ReadResult JsonFile::Read(StringView path, Json::Value& value, std::int64_t maxSize, StringView what)
	{
		value = Json::Value(Json::objectValue);
		if (path.empty() || !fs::FileExists(path)) {
			return ReadResult::Missing;
		}
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			LOGW("{} cannot be opened", what);
			return ReadResult::Unreadable;
		}
		std::int64_t size = s->GetSize();
		if (size == 0) {
			// Nothing in it, as an empty file left behind, there's nothing to lose
			return ReadResult::Missing;
		}
		if (size < 0 || size > maxSize) {
			LOGW("{} cannot be read, the file has a wrong size", what);
			return ReadResult::Unreadable;
		}
		std::unique_ptr<char[]> buffer = std::make_unique<char[]>(std::size_t(size));
		if (s->Read(buffer.get(), size) != size) {
			LOGW("{} cannot be read", what);
			return ReadResult::Unreadable;
		}
		std::string errors;
		if (!Parse(StringView(buffer.get(), std::size_t(size)), value, &errors) || !value.isObject()) {
			LOGW("{} cannot be read: {}", what, errors.c_str());
			value = Json::Value(Json::objectValue);
			return ReadResult::Unreadable;
		}
		return ReadResult::Ok;
	}

	bool JsonFile::Parse(StringView text, Json::Value& value, std::string* errors)
	{
		Json::CharReaderBuilder builder;
		auto reader = std::unique_ptr<Json::CharReader>(builder.newCharReader());
		return reader->parse(text.data(), text.data() + text.size(), &value, errors);
	}

	std::string JsonFile::ToString(const Json::Value& value, bool compact)
	{
		Json::StreamWriterBuilder builder;
		builder["indentation"] = (compact ? "" : "\t");
		return Json::writeString(builder, value);
	}

	bool JsonFile::Write(StringView path, const Json::Value& value, bool keepBackup)
	{
		std::string text = ToString(value);
		return AtomicFile::Write(path, text.data(), std::int64_t(text.size()), keepBackup);
	}

	bool JsonFile::Stage(StringView path, const Json::Value& value)
	{
		std::string text = ToString(value);
		return AtomicFile::Stage(path, [&text](Stream& s) {
			return (text.empty() || s.Write(text.data(), std::int64_t(text.size())) == std::int64_t(text.size()));
		});
	}
}

#endif
