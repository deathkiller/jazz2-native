#include "ContentTileSetLibrary.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO;

namespace Jazz2::Editor
{
	std::int32_t ContentTileSetLibrary::GetTileCount(StringView name)
	{
		for (const Entry& entry : _entries) {
			if (entry.Name == name) {
				return entry.TileCount;
			}
		}

		std::int32_t tileCount = -1;
		String fullPath = FindTileSetFile(name);
		if (!fullPath.empty()) {
			auto s = fs::Open(fullPath, FileAccess::Read);
			Formats::TileSetHeader header;
			if (s->IsValid() && Formats::TileSetFile::ReadHeader(*s, header)) {
				tileCount = header.TileCount;
			}
		}

		Entry& entry = _entries.emplace_back();
		entry.Name = name;
		entry.TileCount = tileCount;
		return tileCount;
	}

	bool ContentTileSetLibrary::ReadPalette(StringView name, std::uint32_t (&palette)[256])
	{
		String fullPath = FindTileSetFile(name);
		if (fullPath.empty()) {
			return false;
		}
		auto s = fs::Open(fullPath, FileAccess::Read);
		Formats::TileSetHeader header;
		if (!s->IsValid() || !Formats::TileSetFile::ReadHeader(*s, header) || (header.Flags & Formats::TileSetFile::HasPalette) == 0) {
			return false;
		}
		// The palette comes first in the deflated block
		Compression::DeflateStream uc(*s, header.BodySize);
		for (std::int32_t i = 0; i < 256; i++) {
			palette[i] = uc.ReadValueAsLE<std::uint32_t>();
		}
		return uc.IsValid();
	}

	String ContentTileSetLibrary::FindTileSetFile(StringView name)
	{
		return ContentResolver::Get().FindContentFile(fs::CombinePath("Tilesets"_s, String(fs::ToNativeSeparators(name) + ".j2t"_s)));
	}

	String ContentTileSetLibrary::GetWritablePath(StringView name, bool create)
	{
		StringView contentPath = ContentResolver::Get().GetWritableContentPath(create);
		if (contentPath.empty()) {
			return {};
		}
		String directory = fs::CombinePath(contentPath, "Tilesets"_s);
		if (create) {
			fs::CreateDirectories(directory);
		}
		return fs::CombinePath(directory, String(name + ".j2t"_s));
	}

	bool ContentTileSetLibrary::IsValidName(StringView name)
	{
		// As the tile sets of the game are named
		if (name.empty() || name.size() > 32) {
			return false;
		}
		for (char c : name) {
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
				return false;
			}
		}
		return true;
	}

	void ContentTileSetLibrary::Invalidate()
	{
		_entries.clear();
	}
}

#endif
