#include "CartridgeImage.h"

#include <algorithm>
#include <cstring>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/StaticArray.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Name of the directory in the file system that holds the game content (`rom:/Content`) */
		constexpr StringView ContentDirectoryName = "Content"_s;

		/** @brief First word of a ROM image in the cartridge's own (big-endian) byte order */
		constexpr std::uint32_t Z64Magic = 0x80371240;
		/** @brief The same word in the byte-swapped (`.v64`) and the little-endian (`.n64`) dumps */
		constexpr std::uint32_t V64Magic = 0x37804012;
		constexpr std::uint32_t N64Magic = 0x40123780;

		/** @brief Largest ROM the cartridge address space maps (see `cmake/n64_check_rom_size.cmake`) */
		constexpr std::size_t MaxRomSize = 64 * 1024 * 1024;
		/** @brief `n64tool` pads the ROM to a multiple of this, and so does this */
		constexpr std::size_t RomPadAlignment = 16 * 1024;

		// libdragon's rompak table of contents (src/rompak.c, tools/n64tool.c). `n64tool` puts it right after
		// the bootcode, 16-aligned; libdragon searches the first 16 KB behind 0x1000 for it the same way.
		constexpr std::uint32_t TocMagic = 0x544F4330;	// "TOC0"
		constexpr std::size_t TocSearchStart = 0x1000;
		constexpr std::size_t TocSearchSteps = 1024;
		constexpr std::size_t TocHeaderSize = 16;

		// DragonFS 2.1 (src/dfs_internal.h, tools/mkdfs/mkdfs.c). Every word is big-endian.
		constexpr StringView DfsRootPath = "DragonFS 2.1"_s;
		constexpr std::uint32_t DfsRootFlags = 0xFFFFFFFF;
		constexpr std::uint32_t DfsEntryHeaderSize = 12;	// next_entry, flags, file_pointer
		constexpr std::uint32_t DfsMaxEntrySize = 256;		// MAX_DIRENT_SIZE, what the reader fetches per entry
		constexpr std::uint32_t DfsFlagsDirectory = 1;
		constexpr std::uint32_t DfsMaxFileSize = 0x0FFFFFFF;
		constexpr std::uint32_t DfsLookupPrime = 31;

		std::uint32_t ReadU32BE(const std::uint8_t* source)
		{
			return (std::uint32_t(source[0]) << 24) | (std::uint32_t(source[1]) << 16) |
				(std::uint32_t(source[2]) << 8) | std::uint32_t(source[3]);
		}

		std::uint16_t ReadU16BE(const std::uint8_t* source)
		{
			return std::uint16_t((source[0] << 8) | source[1]);
		}

		void WriteU32BE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value >> 24);
			target[1] = std::uint8_t(value >> 16);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		/** @brief One file of the new file system, for the hashed lookup table */
		struct DfsFile
		{
			String Path;
			std::uint32_t PathHash;
			std::uint32_t DataOffset;
			std::uint32_t DataLength;
		};

		/**
			@brief Builds a DragonFS image the way `mkdfs` does

			The layout is reproduced exactly, because libdragon reads it back with assumptions `mkdfs` never
			writes down: the root's first child is the entry right behind the root entry (nothing points at it),
			entries of a directory are linked in byte-wise name order with each file's data right behind its
			entry, every block is padded to an even length, and the root entry's `next_entry` and `file_pointer`
			are reused for the size and the position of the table `dfs_open()` finds files by --- sorted by a
			hash of the full path, which is computed over SIGNED characters as the console's compiler does.
		*/
		class DfsBuilder
		{
		public:
			DfsBuilder()
			{
				const std::uint32_t root = Allocate(DfsEntryHeaderSize + std::uint32_t(DfsRootPath.size()) + 1);
				WriteU32BE(&_image[root + 4], DfsRootFlags);
				std::memcpy(&_image[root + DfsEntryHeaderSize], DfsRootPath.data(), DfsRootPath.size());
			}

			/**
				@brief Adds the files and directories of a host directory under `dfsPath` ("" is the root)

				The filter is called as `bool(StringView dfsPath, StringView hostItem, String& dfsName)` for every
				item, and can leave it out or rename it.
			*/
			template<class Filter>
			bool AddDirectory(StringView dfsPath, StringView hostPath, const Filter& filter)
			{
				struct Item {
					String HostPath;
					String DfsName;
				};
				SmallVector<Item, 0> items;
				for (auto hostItem : fs::Directory(hostPath)) {
					String dfsName = fs::GetFileName(hostItem);
					if (filter(dfsPath, hostItem, dfsName)) {
						items.push_back(Item{String(hostItem), Death::move(dfsName)});
					}
				}
				// mkdfs sorts with strcmp(), and it is the order `dfs_dir_findnext()` lists a directory in
				std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
					return std::strcmp(a.DfsName.data(), b.DfsName.data()) < 0;
				});

				std::uint32_t previousEntry = 0;
				for (const Item& item : items) {
					const String& hostItem = item.HostPath;
					const String& dfsName = item.DfsName;
					if (DfsEntryHeaderSize + dfsName.size() + 1 > DfsMaxEntrySize) {
						LOGE("Cannot put \"{}\" into the ROM, its name is longer than DragonFS allows", hostItem);
						return false;
					}

					const bool isDirectory = fs::DirectoryExists(hostItem);
					const String itemPath = (dfsPath.empty() ? dfsName : String(dfsPath + "/"_s + dfsName));

					const std::uint32_t entry = Allocate(DfsEntryHeaderSize + std::uint32_t(dfsName.size()) + 1);
					std::memcpy(&_image[entry + DfsEntryHeaderSize], dfsName.data(), dfsName.size());
					if (previousEntry != 0) {
						WriteU32BE(&_image[previousEntry], entry);
					}
					previousEntry = entry;

					if (isDirectory) {
						WriteU32BE(&_image[entry + 4], DfsFlagsDirectory << 28);
						const std::uint32_t firstChild = std::uint32_t(_image.size());
						const std::uint32_t sizeBefore = firstChild;
						if (!AddDirectory(itemPath, hostItem, filter)) {
							return false;
						}
						// An empty directory has nothing to point at, which is how mkdfs writes it too
						WriteU32BE(&_image[entry + 8], _image.size() != sizeBefore ? firstChild : 0);
						continue;
					}

					const std::int64_t size = fs::GetFileSize(hostItem);
					if (size < 0 || size > std::int64_t(DfsMaxFileSize)) {
						LOGE("Cannot put \"{}\" into the ROM, it is unreadable or larger than DragonFS allows", hostItem);
						return false;
					}
					const std::uint32_t data = Allocate(std::uint32_t(size));
					if (size > 0) {
						auto s = fs::Open(hostItem, FileAccess::Read);
						if (!s->IsValid() || s->Read(&_image[data], size) != size) {
							LOGE("Cannot read \"{}\"", hostItem);
							return false;
						}
					}
					WriteU32BE(&_image[entry + 4], std::uint32_t(size));	// FLAGS_FILE (0) in the top nibble
					WriteU32BE(&_image[entry + 8], data);

					DfsFile& file = _files.emplace_back();
					file.Path = itemPath;
					file.PathHash = HashPath(itemPath);
					file.DataOffset = data;
					file.DataLength = std::uint32_t(size);
				}
				return true;
			}

			/** @brief Adds a host directory as a directory called `dfsName` at the root */
			template<class Filter>
			bool AddNamedDirectory(StringView dfsName, StringView hostPath, const Filter& filter)
			{
				const std::uint32_t entry = Allocate(DfsEntryHeaderSize + std::uint32_t(dfsName.size()) + 1);
				std::memcpy(&_image[entry + DfsEntryHeaderSize], dfsName.data(), dfsName.size());
				WriteU32BE(&_image[entry + 4], DfsFlagsDirectory << 28);
				const std::uint32_t firstChild = std::uint32_t(_image.size());
				if (!AddDirectory(dfsName, hostPath, filter)) {
					return false;
				}
				WriteU32BE(&_image[entry + 8], _image.size() != firstChild ? firstChild : 0);
				return true;
			}

			/** @brief Appends the lookup table and returns the finished image */
			bool Finish(SmallVector<std::uint8_t, 0>& image, std::uint32_t& fileCount)
			{
				std::stable_sort(_files.begin(), _files.end(), [](const DfsFile& a, const DfsFile& b) {
					return a.PathHash < b.PathHash;
				});

				const std::uint32_t lookupSize = 8 + 16 * std::uint32_t(_files.size());
				const std::uint32_t lookup = Allocate(lookupSize);
				WriteU32BE(&_image[0], lookupSize);
				WriteU32BE(&_image[8], lookup);

				std::uint32_t pathsSize = 0;
				for (const DfsFile& file : _files) {
					pathsSize += PaddedPathLength(file.Path);
				}
				// Each path is addressed by 20 bits of offset and 12 bits of length (NUL included)
				if (pathsSize >= (1u << 20)) {
					LOGE("The content has too many files for the DragonFS lookup table");
					return false;
				}
				const std::uint32_t paths = Allocate(pathsSize);

				WriteU32BE(&_image[lookup], std::uint32_t(_files.size()));
				WriteU32BE(&_image[lookup + 4], paths);
				std::uint32_t pathOffset = 0;
				for (std::size_t i = 0; i < _files.size(); i++) {
					const DfsFile& file = _files[i];
					const std::uint32_t length = std::uint32_t(file.Path.size()) + 1;
					if (length >= (1u << 12)) {
						LOGE("Cannot put \"{}\" into the ROM, its path is longer than DragonFS allows", file.Path);
						return false;
					}
					std::uint8_t* record = &_image[lookup + 8 + 16 * std::uint32_t(i)];
					WriteU32BE(record + 0, file.PathHash);
					WriteU32BE(record + 4, (length << 20) | pathOffset);
					WriteU32BE(record + 8, file.DataOffset);
					WriteU32BE(record + 12, file.DataLength);
					std::memcpy(&_image[paths + pathOffset], file.Path.data(), file.Path.size());
					pathOffset += PaddedPathLength(file.Path);
				}

				fileCount = std::uint32_t(_files.size());
				image = Death::move(_image);
				return true;
			}

		private:
			SmallVector<std::uint8_t, 0> _image;
			SmallVector<DfsFile, 0> _files;

			/** @brief Appends a zeroed block, padded to an even length, and returns its offset */
			std::uint32_t Allocate(std::uint32_t size)
			{
				const std::uint32_t offset = std::uint32_t(_image.size());
				_image.resize(_image.size() + ((size + 1) & ~1u), 0);
				return offset;
			}

			static std::uint32_t PaddedPathLength(StringView path)
			{
				return (std::uint32_t(path.size()) + 1 + 1) & ~1u;
			}

			static std::uint32_t HashPath(StringView path)
			{
				std::uint32_t hash = 0;
				for (char c : path) {
					// `char` is signed on MIPS and in mkdfs on x86 alike, so bytes above 0x7F count as negative
					hash = hash * DfsLookupPrime + std::uint32_t(std::int32_t(static_cast<signed char>(c)));
				}
				return hash;
			}
		};

		/** @brief Lowercase extension of a path, without the dot */
		String LowercaseExtension(StringView path)
		{
			return StringUtils::lowercase(fs::GetExtension(path));
		}

		/**
			@brief Decides what of the content directory goes into the ROM, mirroring `cmake/n64_stage_content.cmake`

			Keep the two in step: a ROM made by this and one made by the build from the same tree should have
			the same files in them.
		*/
		bool StageFilter(StringView contentRoot, StringView dfsPath, StringView hostItem, String& dfsName)
		{
			StringView relative = dfsPath.exceptPrefix(ContentDirectoryName.size());
			if (!relative.empty() && relative[0] == '/') {
				relative = relative.exceptPrefix(1);
			}
			const StringView name = fs::GetFileName(hostItem);
			const bool isDirectory = fs::DirectoryExists(hostItem);

			if (relative.empty()) {
				// What a failed conversion left behind
				if (isDirectory && name == ".n64-temp"_s) {
					return false;
				}
				// The console has no writable cache to convert into, so a package named for the desktop is
				// taken as prebaked
				if (!isDirectory && name == "Source.pak"_s) {
					dfsName = "Prebaked.pak"_s;
					return true;
				}
				if (!isDirectory && name == "Prebaked.pak"_s && fs::IsReadableFile(fs::CombinePath(contentRoot, "Source.pak"_s))) {
					return false;	// The build renames Source.pak over it
				}
				return true;
			}
			if (isDirectory) {
				return true;
			}

			const String extension = LowercaseExtension(name);
			const String topDirectory = StringUtils::lowercase(relative.partition('/')[0]);
			if (topDirectory == "music"_s) {
				// The original music formats, which the console has no decoder for
				static const StringView Unplayable[] = { "j2b"_s, "it"_s, "s3m"_s, "mod"_s, "mo3"_s, "xm"_s, "ogg"_s, "umx"_s };
				for (StringView unplayable : Unplayable) {
					if (extension == unplayable) {
						return false;
					}
				}
			} else if (topDirectory == "cinematics"_s) {
				// The original cinematic, where a video replaces it
				if (extension == "j2v"_s) {
					const StringView directory = fs::GetDirectoryName(hostItem);
					const StringView stem = fs::GetFileNameWithoutExtension(hostItem);
					if (fs::IsReadableFile(fs::CombinePath(directory, String(stem + ".m1v"_s))) ||
						fs::IsReadableFile(fs::CombinePath(directory, String(stem + ".h264"_s)))) {
						return false;
					}
				}
			} else if (topDirectory == "translations"_s) {
				// Translation sources, only the compiled ".mo" files are read at run time
				if (extension == "po"_s) {
					return false;
				}
			}
			return true;
		}

		/** @brief 32-bit FNV-1a, for the table of contents' cookie */
		std::uint32_t HashBytes(std::uint32_t hash, const std::uint8_t* data, std::size_t size)
		{
			for (std::size_t i = 0; i < size; i++) {
				hash = (hash ^ data[i]) * 16777619u;
			}
			return hash;
		}
	}

	bool CartridgeImage::IsCartridgeImage(StringView path)
	{
		auto s = fs::Open(path, FileAccess::Read);
		if (!s->IsValid() || s->GetSize() < 0x1000) {
			return false;
		}
		std::uint8_t magic[4];
		if (s->Read(magic, sizeof(magic)) != sizeof(magic)) {
			return false;
		}
		const std::uint32_t word = ReadU32BE(magic);
		return (word == Z64Magic || word == V64Magic || word == N64Magic);
	}

	bool CartridgeImage::SwapContent(StringView sourcePath, StringView targetPath, StringView contentPath)
	{
		if (!fs::DirectoryExists(contentPath)) {
			LOGE("Content directory \"{}\" does not exist", contentPath);
			return false;
		}

		Array<std::uint8_t> rom;
		{
			auto source = fs::Open(sourcePath, FileAccess::Read);
			const std::int64_t size = (source->IsValid() ? source->GetSize() : -1);
			if (size < std::int64_t(TocSearchStart + TocHeaderSize) || size > std::int64_t(MaxRomSize) * 4) {
				LOGE("Cannot open \"{}\"", sourcePath);
				return false;
			}
			rom = Array<std::uint8_t>{NoInit, std::size_t(size)};
			if (source->Read(rom.data(), size) != size) {
				LOGE("Cannot read \"{}\"", sourcePath);
				return false;
			}
		}

		const std::uint32_t magic = ReadU32BE(rom.data());
		if (magic == V64Magic || magic == N64Magic) {
			LOGE("\"{}\" is a byte-swapped dump, convert it to the \".z64\" byte order first", sourcePath);
			return false;
		}
		if (magic != Z64Magic) {
			LOGE("\"{}\" is not a Nintendo 64 ROM image", sourcePath);
			return false;
		}

		std::size_t tocOffset = 0;
		for (std::size_t i = 0; i < TocSearchSteps; i++) {
			const std::size_t offset = TocSearchStart + i * 16;
			if (offset + TocHeaderSize > rom.size()) {
				break;
			}
			if (ReadU32BE(&rom[offset]) == TocMagic) {
				tocOffset = offset;
				break;
			}
		}
		if (tocOffset == 0) {
			LOGE("\"{}\" has no table of contents, it was not built with libdragon's n64tool --toc", sourcePath);
			return false;
		}

		const std::uint32_t tocSize = ReadU32BE(&rom[tocOffset + 8]);
		const std::uint16_t entrySize = ReadU16BE(&rom[tocOffset + 12]);
		const std::uint16_t entryCount = ReadU16BE(&rom[tocOffset + 14]);
		if (entrySize <= 8 || entrySize >= 1024 || entryCount >= 1024 ||
			tocOffset + TocHeaderSize + std::size_t(entrySize) * entryCount > rom.size() ||
			TocHeaderSize + std::size_t(entrySize) * entryCount > tocSize) {
			LOGE("\"{}\" has a damaged table of contents", sourcePath);
			return false;
		}

		// The file system is found by its extension, as rompak_search_ext(".dfs") does at boot
		std::int32_t dfsIndex = -1;
		std::uint32_t lastFileEnd = 0;
		for (std::int32_t i = 0; i < entryCount; i++) {
			const std::uint8_t* entry = &rom[tocOffset + TocHeaderSize + std::size_t(entrySize) * i];
			const std::uint32_t offset = ReadU32BE(entry);
			const std::uint32_t size = ReadU32BE(entry + 4);
			const char* name = reinterpret_cast<const char*>(entry + 8);
			const StringView nameView{name, strnlen(name, entrySize - 8)};
			if (dfsIndex < 0 && nameView.hasSuffix(".dfs"_s)) {
				dfsIndex = i;
			}
			lastFileEnd = std::max(lastFileEnd, offset + size);
		}
		if (dfsIndex < 0) {
			LOGE("\"{}\" has no DragonFS image to replace", sourcePath);
			return false;
		}

		std::uint8_t* dfsEntry = &rom[tocOffset + TocHeaderSize + std::size_t(entrySize) * dfsIndex];
		const std::uint32_t dfsOffset = ReadU32BE(dfsEntry);
		const std::uint32_t dfsSize = ReadU32BE(dfsEntry + 4);
		// Anything behind the file system would have to move, and the executable finds none of it by the table
		// alone (the symbol table, for instance, is found through it, but only after the ELF is running)
		if (std::size_t(dfsOffset) + dfsSize > rom.size() || dfsOffset + dfsSize != lastFileEnd) {
			LOGE("\"{}\" does not end with its DragonFS image, which is the only layout that can be rewritten", sourcePath);
			return false;
		}
		if (dfsSize < DfsEntryHeaderSize + DfsRootPath.size() + 1 || ReadU32BE(&rom[dfsOffset + 4]) != DfsRootFlags ||
			std::memcmp(&rom[dfsOffset + DfsEntryHeaderSize], DfsRootPath.data(), DfsRootPath.size()) != 0) {
			LOGE("\"{}\" does not contain a DragonFS 2.1 image where its table of contents says", sourcePath);
			return false;
		}

		LOGI("Reading \"{}\"...", contentPath);
		const String contentRoot = contentPath;
		DfsBuilder builder;
		// The game only ever looks in "Content"; the rest of what the old file system carried is not the
		// game's to keep (the build puts nothing else there)
		SmallVector<std::uint8_t, 0> dfs;
		std::uint32_t fileCount = 0;
		const auto filter = [&contentRoot](StringView dfsPath, StringView hostItem, String& dfsName) {
			return StageFilter(contentRoot, dfsPath, hostItem, dfsName);
		};
		// The host directory is the one that becomes "Content", whatever it is called here
		if (!builder.AddNamedDirectory(ContentDirectoryName, contentPath, filter) || !builder.Finish(dfs, fileCount)) {
			return false;
		}

		// A ROM is its prefix, the new file system, and n64tool's padding
		const std::size_t romSize = (std::size_t(dfsOffset) + dfs.size() + RomPadAlignment - 1) / RomPadAlignment * RomPadAlignment;
		if (romSize > MaxRomSize) {
			LOGE("The ROM would be {} MiB, which exceeds the 64 MiB cartridge address space - drop or re-encode "
				"something in \"{}\"", (romSize + 1048575) / 1048576, contentPath);
			return false;
		}

		WriteU32BE(dfsEntry + 4, std::uint32_t(dfs.size()));
		// The cookie tells libdragon that the ROM it is reading is still the one it booted from; any value
		// that changes with the content does. n64tool derives it from the files, and so does this.
		std::uint32_t cookie = HashBytes(2166136261u, dfs.data(), dfs.size());
		cookie = HashBytes(cookie, &rom[tocOffset + TocHeaderSize], std::size_t(entrySize) * entryCount);
		WriteU32BE(&rom[tocOffset + 4], cookie);

		const bool inPlace = (targetPath.empty() || targetPath == sourcePath);
		// Written next to the target and moved into place once complete, so an interrupted run leaves nothing
		// half-written behind
		const String writePath = (inPlace ? String(sourcePath + ".tmp"_s) : String(targetPath));
		LOGI("Writing \"{}\"...", inPlace ? sourcePath : targetPath);
		{
			auto target = fs::Open(writePath, FileAccess::Write);
			if (!target->IsValid()) {
				LOGE("Cannot open \"{}\" for writing", writePath);
				return false;
			}
			const std::size_t padding = romSize - dfsOffset - dfs.size();
			Array<std::uint8_t> zeros{ValueInit, padding};
			if (target->Write(rom.data(), dfsOffset) != std::int64_t(dfsOffset) ||
				target->Write(dfs.data(), std::int64_t(dfs.size())) != std::int64_t(dfs.size()) ||
				(padding > 0 && target->Write(zeros.data(), std::int64_t(padding)) != std::int64_t(padding))) {
				LOGE("Cannot write \"{}\"", writePath);
				return false;
			}
		}
		if (inPlace && !fs::Move(writePath, sourcePath)) {
			LOGE("Cannot replace \"{}\" with the image that was written to \"{}\"", sourcePath, writePath);
			return false;
		}

		LOGI("ROM image written with {} files: {} MiB ({} MiB left of the 64 MiB cartridge space)", fileCount,
			romSize / 1048576, (MaxRomSize - romSize) / 1048576);
		return true;
	}
}
