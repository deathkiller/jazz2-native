#include "EpisodeFile.h"

#if defined(WITH_EDITOR)

#include "AtomicFile.h"
#include "../../ContentFileTypes.h"
#include "../../Compatibility/JJ2Anims.h"
#include "../../Formats/LevelFile.h"

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

#include <algorithm>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::uint64_t Signature = 0x2095A59FF0BFBBEF;
		// Larger images than this aren't in any episode, the size is damaged
		constexpr std::int32_t MaxImageSize = 4096;

		bool ReadString8(Stream& s, String& value)
		{
			std::uint8_t length = s.ReadValue<std::uint8_t>();
			value = String(NoInit, length);
			return (length == 0 || s.Read(value.data(), length) == length);
		}

		void WriteString8(Stream& s, StringView value)
		{
			// Not in the middle of a character
			std::size_t length = Death::Utf8::FitPrefix(value, UINT8_MAX);
			s.WriteValue<std::uint8_t>(std::uint8_t(length));
			s.Write(value.data(), std::int64_t(length));
		}

		bool ReadImage(Stream& s, EpisodeImage& image)
		{
			if (s.GetPosition() + 4 > s.GetSize()) {
				// An episode without images
				return true;
			}
			std::int32_t width = s.ReadValueAsLE<std::uint16_t>();
			std::int32_t height = s.ReadValueAsLE<std::uint16_t>();
			if (width <= 0 || height <= 0) {
				return true;
			}
			if (width > MaxImageSize || height > MaxImageSize) {
				return false;
			}
			image.Width = width;
			image.Height = height;
			image.Pixels.resize_for_overwrite(std::size_t(width) * std::size_t(height) * 4);
			Compatibility::JJ2Anims::ReadImageContent(s, image.Pixels.data(), width, height, 4);
			return true;
		}

		void WriteImage(Stream& s, const EpisodeImage& image)
		{
			if (image.IsEmpty() || image.Pixels.size() < std::size_t(image.Width) * std::size_t(image.Height) * 4) {
				s.WriteValueAsLE<std::uint16_t>(0);
				s.WriteValueAsLE<std::uint16_t>(0);
				return;
			}
			s.WriteValueAsLE<std::uint16_t>(std::uint16_t(image.Width));
			s.WriteValueAsLE<std::uint16_t>(std::uint16_t(image.Height));
			Compatibility::JJ2Anims::WriteImageContent(s, image.Pixels.data(), image.Width, image.Height, 4);
		}
	}

	bool EpisodeFile::Read(StringView path, EpisodeFile& episode)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid() || s->GetSize() < 16) {
			return false;
		}
		std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		if (signature != Signature || fileType != ContentFileType::Episode) {
			return false;
		}
		episode.Flags = s->ReadValueAsLE<std::uint16_t>();
		if (!ReadString8(*s, episode.DisplayName)) {
			return false;
		}
		episode.Position = s->ReadValueAsLE<std::uint16_t>();
		if (!ReadString8(*s, episode.FirstLevel) || !ReadString8(*s, episode.PreviousEpisode) || !ReadString8(*s, episode.NextEpisode)) {
			return false;
		}
		return (ReadImage(*s, episode.Title) && ReadImage(*s, episode.Background));
	}

	bool EpisodeFile::Write(StringView path, bool keepBackup) const
	{
		return AtomicFile::Write(path, [this](Stream& s) {
			s.WriteValueAsLE<std::uint64_t>(Signature);
			s.WriteValue<std::uint8_t>(ContentFileType::Episode);
			s.WriteValueAsLE<std::uint16_t>(Flags);
			WriteString8(s, DisplayName);
			s.WriteValueAsLE<std::uint16_t>(Position);
			WriteString8(s, FirstLevel);
			WriteString8(s, PreviousEpisode);
			WriteString8(s, NextEpisode);
			WriteImage(s, Title);
			WriteImage(s, Background);
			return true;
		}, keepBackup);
	}

	bool EpisodeLevelInfo::Read(StringView path, EpisodeLevelInfo& info)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid()) {
			return false;
		}
		Formats::LevelHeader header;
		if (!Formats::LevelFile::ReadHeader(*s, header)) {
			return false;
		}
		Formats::LevelReader reader(*s, header);
		Formats::LevelProperties properties;
		reader.ReadProperties(properties);
		reader.ReadPalettes(nullptr, nullptr);
		SmallVector<Formats::ExtraTileSetRecord, 0> extraTileSets;
		reader.ReadExtraTileSets(extraTileSets);
		if (!reader.IsValid()) {
			return false;
		}

		info.Name = fs::GetFileNameWithoutExtension(path);
		StringUtils::lowercaseInPlace(info.Name);
		info.Path = path;
		info.DisplayName = Death::move(properties.DisplayName);
		info.NextLevel = Death::move(properties.NextLevel);
		info.SecretLevel = Death::move(properties.SecretLevel);
		info.BonusLevel = Death::move(properties.BonusLevel);
		info.DefaultTileSet = Death::move(properties.DefaultTileSet);
		info.Music = Death::move(properties.Music);
		info.ExtraTileSets.clear();
		for (const Formats::ExtraTileSetRecord& tileSet : extraTileSets) {
			if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
				info.ExtraTileSets.push_back(tileSet.Name);
			}
		}
		return true;
	}

	void EpisodeLevelInfo::ReadDirectory(StringView directory, SmallVectorImpl<EpisodeLevelInfo>& levels)
	{
		levels.clear();
		for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
			if (StringUtils::lowercase(fs::GetExtension(item)) != "j2l"_s) {
				continue;
			}
			EpisodeLevelInfo info;
			if (Read(item, info)) {
				levels.push_back(Death::move(info));
			}
		}
		std::sort(levels.begin(), levels.end(), [](const EpisodeLevelInfo& a, const EpisodeLevelInfo& b) {
			return (a.Name < b.Name);
		});
	}

	String SplitLevelReference(StringView reference, String& level)
	{
		String lower = StringUtils::lowercase(reference.trimmed());
		auto parts = lower.partition('/');
		if (parts[1].empty()) {
			level = parts[0];
			return {};
		}
		level = parts[2];
		return parts[0];
	}

	String FindLevelFile(StringView reference, StringView currentLevelName, ArrayView<const String> episodeRoots)
	{
		StringView trimmed = reference.trimmed();
		if (trimmed.empty() || trimmed[0] == ':') {
			return {};
		}
		String level;
		String episode = SplitLevelReference(trimmed, level);
		if (episode.empty()) {
			episode = StringUtils::lowercase(currentLevelName.partition('/')[0]);
		}
		if (level.empty() || episode.empty()) {
			return {};
		}
		for (const String& root : episodeRoots) {
			String path = fs::FindPathCaseInsensitive(fs::CombinePath({ root, episode, String(level + ".j2l"_s) }));
			if (fs::IsReadableFile(path)) {
				return path;
			}
		}
		return {};
	}
}

#endif
