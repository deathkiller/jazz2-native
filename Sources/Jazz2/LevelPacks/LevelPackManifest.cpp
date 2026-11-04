#include "LevelPackManifest.h"

#if defined(WITH_LEVEL_PACKS)

#include "../Utils/JsonAccess.h"
#include "../Utils/JsonFile.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

#include <jsoncpp/json.h>

#include <iterator>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Jazz2::Utils;

namespace Jazz2::LevelPacks
{
	namespace
	{
		constexpr std::int64_t MaxManifestSize = 16 * 1024 * 1024;

		static const StringView FileKindNames[] = {
			"Other"_s, "Level"_s, "TileSet"_s, "Episode"_s, "Script"_s, "Music"_s
		};

		Json::Value ToJson(StringView value)
		{
			return Json::Value(value.data(), value.data() + value.size());
		}

		std::uint64_t ToUInt64(const Json::Value& value)
		{
			return (value.isUInt64() ? value.asUInt64() : 0);
		}

		std::int64_t ToInt64(const Json::Value& value)
		{
			return (value.isInt64() ? value.asInt64() : 0);
		}

		LevelPackFileKind ParseFileKind(StringView name)
		{
			for (std::size_t i = 0; i < std::size(FileKindNames); i++) {
				if (name == FileKindNames[i]) {
					return LevelPackFileKind(i);
				}
			}
			return LevelPackFileKind::Other;
		}
	}

	bool LevelPackManifest::Read(StringView path)
	{
		Json::Value doc;
		if (JsonFile::Read(path, doc, MaxManifestSize, "Level pack"_s) != JsonFile::ReadResult::Ok) {
			return false;
		}
		if (JsonAccess::GetInt(doc, "Version", 0) != FormatVersion) {
			LOGW("Level pack \"{}\" has an unsupported version", path);
			return false;
		}

		Id = JsonAccess::GetString(doc, "Id");
		if (Id.empty()) {
			return false;
		}
		Source = JsonAccess::GetString(doc, "Source");
		Title = JsonAccess::GetString(doc, "Title");
		Authors = JsonAccess::GetString(doc, "Authors");
		Description = JsonAccess::GetString(doc, "Description");
		Version = JsonAccess::GetString(doc, "PackVersion");
		ArchiveName = JsonAccess::GetString(doc, "ArchiveName");
		ArchiveSha256 = JsonAccess::GetString(doc, "ArchiveSha256");
		InstalledAt = ToInt64(JsonAccess::Member(doc, "InstalledAt"));
		CacheVersion = JsonAccess::GetUInt(doc, "CacheVersion", 0);

		Files.clear();
		const Json::Value& files = JsonAccess::Member(doc, "Files");
		if (files.isArray()) {
			for (const Json::Value& item : files) {
				LevelPackFile& file = Files.emplace_back();
				file.Name = JsonAccess::GetString(item, "Name");
				file.Key = JsonAccess::GetString(item, "Key");
				file.Sha256 = JsonAccess::GetString(item, "Sha256");
				file.Size = ToUInt64(JsonAccess::Member(item, "Size"));
				file.Kind = ParseFileKind(JsonAccess::GetString(item, "Kind"));
				if (file.Name.empty()) {
					Files.pop_back();
				}
			}
		}

		Levels.clear();
		const Json::Value& levels = JsonAccess::Member(doc, "Levels");
		if (levels.isArray()) {
			for (const Json::Value& item : levels) {
				LevelPackLevel& level = Levels.emplace_back();
				level.Name = JsonAccess::GetString(item, "Name");
				level.DisplayName = JsonAccess::GetString(item, "DisplayName");
				if (level.Name.empty()) {
					Levels.pop_back();
				}
			}
		}

		Episodes.clear();
		const Json::Value& episodes = JsonAccess::Member(doc, "Episodes");
		if (episodes.isArray()) {
			for (const Json::Value& item : episodes) {
				LevelPackEpisode& episode = Episodes.emplace_back();
				episode.Name = JsonAccess::GetString(item, "Name");
				episode.DisplayName = JsonAccess::GetString(item, "DisplayName");
				episode.FirstLevel = JsonAccess::GetString(item, "FirstLevel");
				if (episode.Name.empty()) {
					Episodes.pop_back();
				}
			}
		}

		MissingAssets.clear();
		const Json::Value& missing = JsonAccess::Member(doc, "MissingAssets");
		if (missing.isArray()) {
			for (const Json::Value& item : missing) {
				String name = JsonAccess::ToString(item);
				if (!name.empty()) {
					MissingAssets.push_back(Death::move(name));
				}
			}
		}
		return true;
	}

	bool LevelPackManifest::Write(StringView path) const
	{
		Json::Value doc(Json::objectValue);
		doc["Version"] = Json::Int(FormatVersion);
		doc["Id"] = ToJson(Id);
		doc["Source"] = ToJson(Source);
		doc["Title"] = ToJson(Title);
		doc["Authors"] = ToJson(Authors);
		doc["Description"] = ToJson(Description);
		doc["PackVersion"] = ToJson(Version);
		doc["ArchiveName"] = ToJson(ArchiveName);
		doc["ArchiveSha256"] = ToJson(ArchiveSha256);
		doc["InstalledAt"] = Json::Int64(InstalledAt);
		doc["CacheVersion"] = Json::UInt(CacheVersion);

		Json::Value& files = doc["Files"];
		files = Json::Value(Json::arrayValue);
		for (const LevelPackFile& file : Files) {
			Json::Value item(Json::objectValue);
			item["Name"] = ToJson(file.Name);
			item["Key"] = ToJson(file.Key);
			item["Sha256"] = ToJson(file.Sha256);
			item["Size"] = Json::UInt64(file.Size);
			item["Kind"] = ToJson(FileKindNames[std::size_t(file.Kind)]);
			files.append(Death::move(item));
		}

		Json::Value& levels = doc["Levels"];
		levels = Json::Value(Json::arrayValue);
		for (const LevelPackLevel& level : Levels) {
			Json::Value item(Json::objectValue);
			item["Name"] = ToJson(level.Name);
			item["DisplayName"] = ToJson(level.DisplayName);
			levels.append(Death::move(item));
		}

		Json::Value& episodes = doc["Episodes"];
		episodes = Json::Value(Json::arrayValue);
		for (const LevelPackEpisode& episode : Episodes) {
			Json::Value item(Json::objectValue);
			item["Name"] = ToJson(episode.Name);
			item["DisplayName"] = ToJson(episode.DisplayName);
			item["FirstLevel"] = ToJson(episode.FirstLevel);
			episodes.append(Death::move(item));
		}

		Json::Value& missing = doc["MissingAssets"];
		missing = Json::Value(Json::arrayValue);
		for (const String& name : MissingAssets) {
			missing.append(ToJson(name));
		}

		return JsonFile::Write(path, doc);
	}

	String LevelPackManifest::GetEpisodePrefix() const
	{
		return String("+"_s + Id);
	}

	std::uint64_t LevelPackManifest::GetTotalSize() const
	{
		std::uint64_t size = 0;
		for (const LevelPackFile& file : Files) {
			size += file.Size;
		}
		return size;
	}

	const LevelPackFile* LevelPackManifest::FindFile(StringView name) const
	{
		for (const LevelPackFile& file : Files) {
			if (StringUtils::equalsIgnoreCase(file.Name, name)) {
				return &file;
			}
		}
		return nullptr;
	}
}

#endif
