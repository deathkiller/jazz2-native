#if defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)

#include "OgcDvd.h"
#include "../../../Main.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <memory>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <unistd.h>

#include <ogc/cache.h>
#include <ogc/dvd.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringUtils.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine::Backends
{
	namespace
	{
		constexpr std::uint32_t SectorSize = 2048;

		/**
			@brief Sectors read at once when a read does not cover them itself

			Most of what the game reads is small - the records of a package, a level, a few kilobytes of a
			music module at a time - and the drive spends as long finding a sector as it does reading dozens
			of them, so reading ahead is what makes small reads affordable. 32 KB is what libogc's own ISO
			9660 driver uses, and a larger read that starts on a sector goes around the cache entirely.
		*/
		constexpr std::uint32_t CacheSectors = 16;

		/** @brief Sectors a single direct read transfers at most, so one request never ties the drive up for long */
		constexpr std::uint32_t MaxDirectSectors = 256;

		/** @brief How long the drive is given to answer when the console booted from the disc, in milliseconds */
		constexpr std::uint32_t ProbeTimeoutMs = 5000;
		/** @brief How long a reset and a spin-up may take, which includes the drive finding the disc at all */
		constexpr std::uint32_t MountTimeoutMs = 15000;

		/** @brief Offsets of the volume descriptor fields read here, see Iso9660.cpp in AssetPacker */
		enum : std::uint32_t {
			VolumeSpaceSizeOffset = 80,
			EscapeSequencesOffset = 88,
			RootRecordOffset = 156
		};

		struct Entry {
			String Name;
			std::uint32_t Lba = 0;
			std::uint32_t Size = 0;
			bool IsDirectory = false;
		};

		/** @brief The entries of one directory, read the first time anything below it is asked for */
		struct Listing {
			std::uint32_t Lba = 0;
			SmallVector<Entry, 0> Entries;
		};

		struct FileHandle {
			std::uint32_t Lba;
			std::uint32_t Size;
			std::uint32_t Position;
		};

		struct DirectoryHandle {
			const Listing* Directory;
			std::uint32_t Position;
		};

		/** @brief What a look at the disc came to */
		enum class Answer {
			/** @brief The read completed */
			Read,
			/** @brief No disc, or the cover open - nothing a reset would change */
			NoDisc,
			/** @brief Any other failure, which a drive that was left stopped or reset by a loader gives */
			NotReady,
			/** @brief No answer at all in time */
			Abandoned
		};

		bool _mounted = false;
		bool _probed = false;
		/** @brief What the last look at the disc came to - the first one is made once, and a reset follows it at most once */
		Answer _answer = Answer::Abandoned;
		bool _joliet = false;
		/** @brief Set once the drive has been left with a request it never finished, after which it is not asked again */
		bool _driveAbandoned = false;
		std::uint32_t _volumeSectors = 0;
		Entry _root;
		SmallVector<std::unique_ptr<Listing>, 0> _listings;
		mutex_t _lock = LWP_MUTEX_NULL;
		std::uint8_t* _cache = nullptr;
		std::uint32_t _cacheLba = 0;
		std::uint32_t _cacheCount = 0;
		// libogc keeps a request it cannot finish - no disc, an open cover - as its current one until the cover
		// closes, so a block is never handed to it a second time once it may still hold it: one for the first
		// look at the disc, one for the reset, and one for every read after that
		dvdcmdblk _probeBlock;
		dvdcmdblk _mountBlock;
		dvdcmdblk _block;
		std::uint8_t _header[32] __attribute__((aligned(32)));

		std::uint32_t ReadU32BE(const std::uint8_t* source)
		{
			return (std::uint32_t(source[0]) << 24) | (std::uint32_t(source[1]) << 16) |
				(std::uint32_t(source[2]) << 8) | std::uint32_t(source[3]);
		}

		/** @brief Waits for a request with a deadline, which a drive with no disc in it may never meet */
		Answer WaitForBlock(dvdcmdblk& block, std::uint32_t timeoutMs)
		{
			std::uint64_t start = gettime();
			while (true) {
				std::int32_t status = DVD_GetCmdBlockStatus(&block);
				switch (status) {
					case DVD_STATE_END:
						return Answer::Read;
					case DVD_STATE_BUSY:
					case DVD_STATE_WAITING:
					case DVD_STATE_RETRY:
						break;
					case DVD_STATE_NO_DISK:
					case DVD_STATE_COVER_OPEN:
						LOGI("There is no disc in the drive");
						return Answer::NoDisc;
					default:
						// A stopped motor, a cancelled request or a fatal error
						LOGI("The disc drive answered with state {}", status);
						return Answer::NotReady;
				}
				if (ticks_to_millisecs(diff_ticks(start, gettime())) > timeoutMs) {
					// libogc has no way to take back a request that is still queued; the drive is left alone from
					// now on rather than handed a second one behind it
					LOGW("The disc drive did not answer within {} ms", timeoutMs);
					_driveAbandoned = true;
					return Answer::Abandoned;
				}
				usleep(10000);
			}
		}

		/** @brief Reads the first 32 bytes of the disc, which is where its game ID and magic word are */
		Answer ReadDiscHeader(dvdcmdblk& block)
		{
			if (!DVD_ReadAbsAsyncPrio(&block, _header, sizeof(_header), 0, nullptr, 2)) {
				return Answer::NotReady;
			}
			return WaitForBlock(block, ProbeTimeoutMs);
		}

		/** @brief Reads whole sectors into a buffer aligned to a cache line, which is what the drive's DMA requires */
		bool ReadSectors(std::uint32_t lba, std::uint32_t count, void* destination)
		{
			std::int32_t expected = std::int32_t(count * SectorSize);
			return (DVD_ReadPrio(&_block, destination, count * SectorSize, std::int64_t(lba) * SectorSize, 2) == expected);
		}

		/** @brief Reads any range of the disc, through the cache unless it is a long run of whole sectors */
		bool Read(std::uint64_t offset, std::uint8_t* destination, std::uint32_t length)
		{
			while (length > 0) {
				std::uint32_t lba = std::uint32_t(offset / SectorSize);
				std::uint32_t inSector = std::uint32_t(offset % SectorSize);

				if (inSector == 0 && (reinterpret_cast<std::uintptr_t>(destination) & 31) == 0 && length >= CacheSectors * SectorSize) {
					std::uint32_t count = length / SectorSize;
					if (count > MaxDirectSectors) {
						count = MaxDirectSectors;
					}
					if (!ReadSectors(lba, count, destination)) {
						return false;
					}
					offset += count * SectorSize;
					destination += count * SectorSize;
					length -= count * SectorSize;
					continue;
				}

				if (lba < _cacheLba || lba >= _cacheLba + _cacheCount) {
					// The volume says how long it is, and nothing is ever read past it - an image may end right there
					std::uint32_t count = CacheSectors;
					if (_volumeSectors != 0 && lba + count > _volumeSectors) {
						count = (lba < _volumeSectors ? _volumeSectors - lba : 1);
					}
					if (!ReadSectors(lba, count, _cache)) {
						_cacheCount = 0;
						return false;
					}
					_cacheLba = lba;
					_cacheCount = count;
				}

				std::uint32_t at = (lba - _cacheLba) * SectorSize + inSector;
				std::uint32_t chunk = _cacheCount * SectorSize - at;
				if (chunk > length) {
					chunk = length;
				}
				std::memcpy(destination, _cache + at, chunk);
				offset += chunk;
				destination += chunk;
				length -= chunk;
			}
			return true;
		}

		/** @brief Turns the UCS-2 of a Joliet name into UTF-8, the way the rest of the engine spells it */
		String FromUcs2(const std::uint8_t* source, std::size_t size)
		{
			char buffer[256 * 3];
			std::size_t length = 0;
			for (std::size_t i = 0; i + 1 < size; i += 2) {
				char32_t c = (char32_t(source[i]) << 8) | source[i + 1];
				if (c < 0x80) {
					buffer[length++] = char(c);
				} else if (c < 0x800) {
					buffer[length++] = char(0xC0 | (c >> 6));
					buffer[length++] = char(0x80 | (c & 0x3F));
				} else {
					buffer[length++] = char(0xE0 | (c >> 12));
					buffer[length++] = char(0x80 | ((c >> 6) & 0x3F));
					buffer[length++] = char(0x80 | (c & 0x3F));
				}
			}
			return String(buffer, length);
		}

		/** @brief Returns the entries of a directory, reading them from the disc the first time */
		const Listing* GetListing(const Entry& directory)
		{
			for (const auto& listing : _listings) {
				if (listing->Lba == directory.Lba) {
					return listing.get();
				}
			}

			std::uint32_t sectorCount = (directory.Size + SectorSize - 1) / SectorSize;
			std::unique_ptr<std::uint8_t[]> data = std::make_unique<std::uint8_t[]>(sectorCount * SectorSize);
			if (!Read(std::uint64_t(directory.Lba) * SectorSize, data.get(), sectorCount * SectorSize)) {
				LOGE("Cannot read the directory at sector {} of the disc", directory.Lba);
				return nullptr;
			}

			auto listing = std::make_unique<Listing>();
			listing->Lba = directory.Lba;

			std::uint32_t offset = 0;
			while (offset < directory.Size) {
				std::uint8_t recordSize = data[offset];
				if (recordSize == 0) {
					// A record never crosses into the next sector, so the rest of this one is padding
					offset = (offset / SectorSize + 1) * SectorSize;
					continue;
				}
				if (recordSize < 34 || offset + recordSize > sectorCount * SectorSize) {
					break;
				}

				const std::uint8_t* record = &data[offset];
				offset += recordSize;

				std::size_t nameLength = record[32];
				const std::uint8_t* name = record + 33;
				if (nameLength == 0 || (nameLength == 1 && name[0] <= 1)) {
					continue;	// The directory itself and its parent
				}

				Entry& entry = listing->Entries.emplace_back();
				// Both byte orders are stored, the second of each pair is big-endian
				entry.Lba = ReadU32BE(record + 6);
				entry.Size = ReadU32BE(record + 14);
				entry.IsDirectory = ((record[25] & 0x02) != 0);
				entry.Name = (_joliet ? FromUcs2(name, nameLength) : String(reinterpret_cast<const char*>(name), nameLength));

				// A file identifier ends with a version number that is not part of the name
				StringView separator = StringView(entry.Name).findLast(';');
				if (!separator.empty()) {
					entry.Name = String(StringView(entry.Name).prefix(separator.begin()));
				}
			}

			_listings.push_back(std::move(listing));
			return _listings.back().get();
		}

		/** @brief Finds what a path names, @cpp "dvd:/" @ce prefix included */
		bool Resolve(const char* path, Entry& result)
		{
			const char* colon = std::strchr(path, ':');
			StringView remaining = (colon != nullptr ? StringView(colon + 1) : StringView(path));

			const Entry* current = &_root;
			for (StringView part : remaining.split('/')) {
				if (part.empty() || part == "."_s) {
					continue;
				}
				if (!current->IsDirectory) {
					return false;
				}
				const Listing* listing = GetListing(*current);
				if (listing == nullptr) {
					return false;
				}
				const Entry* next = nullptr;
				for (const Entry& entry : listing->Entries) {
					if (StringUtils::equalsIgnoreCase(entry.Name, part)) {
						next = &entry;
						break;
					}
				}
				if (next == nullptr) {
					return false;
				}
				current = next;
			}

			result = *current;
			return true;
		}

		void FillStat(const Entry& entry, struct stat* st)
		{
			std::memset(st, 0, sizeof(struct stat));
			st->st_mode = (entry.IsDirectory ? (S_IFDIR | 0555) : (S_IFREG | 0444));
			st->st_nlink = 1;
			st->st_ino = entry.Lba;
			st->st_size = entry.Size;
			st->st_blksize = SectorSize;
			st->st_blocks = (entry.Size + SectorSize - 1) / SectorSize;
		}

		class LockGuard
		{
		public:
			LockGuard() {
				LWP_MutexLock(_lock);
			}
			~LockGuard() {
				LWP_MutexUnlock(_lock);
			}
		};

		int DvdOpen(struct _reent* r, void* fileStruct, const char* path, int flags, int mode)
		{
			static_cast<void>(mode);
			if ((flags & O_ACCMODE) != O_RDONLY) {
				r->_errno = EROFS;
				return -1;
			}

			LockGuard guard;
			Entry entry;
			if (!Resolve(path, entry)) {
				r->_errno = ENOENT;
				return -1;
			}
			if (entry.IsDirectory) {
				r->_errno = EISDIR;
				return -1;
			}

			FileHandle* file = static_cast<FileHandle*>(fileStruct);
			file->Lba = entry.Lba;
			file->Size = entry.Size;
			file->Position = 0;
			return 0;
		}

		int DvdClose(struct _reent* r, void* fd)
		{
			static_cast<void>(r);
			static_cast<void>(fd);
			return 0;
		}

		ssize_t DvdRead(struct _reent* r, void* fd, char* ptr, size_t len)
		{
			FileHandle* file = static_cast<FileHandle*>(fd);
			if (file->Position >= file->Size) {
				return 0;
			}
			std::uint32_t length = std::uint32_t(len < file->Size - file->Position ? len : file->Size - file->Position);

			LockGuard guard;
			if (!Read(std::uint64_t(file->Lba) * SectorSize + file->Position, reinterpret_cast<std::uint8_t*>(ptr), length)) {
				r->_errno = EIO;
				return -1;
			}
			file->Position += length;
			return ssize_t(length);
		}

		off_t DvdSeek(struct _reent* r, void* fd, off_t pos, int dir)
		{
			FileHandle* file = static_cast<FileHandle*>(fd);
			std::int64_t position;
			switch (dir) {
				case SEEK_SET: position = pos; break;
				case SEEK_CUR: position = std::int64_t(file->Position) + pos; break;
				case SEEK_END: position = std::int64_t(file->Size) + pos; break;
				default: r->_errno = EINVAL; return -1;
			}
			if (position < 0 || position > std::int64_t(file->Size)) {
				r->_errno = EINVAL;
				return -1;
			}
			file->Position = std::uint32_t(position);
			return off_t(position);
		}

		int DvdFstat(struct _reent* r, void* fd, struct stat* st)
		{
			static_cast<void>(r);
			FileHandle* file = static_cast<FileHandle*>(fd);
			Entry entry;
			entry.Lba = file->Lba;
			entry.Size = file->Size;
			FillStat(entry, st);
			return 0;
		}

		int DvdStat(struct _reent* r, const char* path, struct stat* st)
		{
			LockGuard guard;
			Entry entry;
			if (!Resolve(path, entry)) {
				r->_errno = ENOENT;
				return -1;
			}
			FillStat(entry, st);
			return 0;
		}

		DIR_ITER* DvdDirOpen(struct _reent* r, DIR_ITER* dirState, const char* path)
		{
			LockGuard guard;
			Entry entry;
			if (!Resolve(path, entry)) {
				r->_errno = ENOENT;
				return nullptr;
			}
			if (!entry.IsDirectory) {
				r->_errno = ENOTDIR;
				return nullptr;
			}
			const Listing* listing = GetListing(entry);
			if (listing == nullptr) {
				r->_errno = EIO;
				return nullptr;
			}

			DirectoryHandle* directory = static_cast<DirectoryHandle*>(dirState->dirStruct);
			directory->Directory = listing;
			directory->Position = 0;
			return dirState;
		}

		int DvdDirReset(struct _reent* r, DIR_ITER* dirState)
		{
			static_cast<void>(r);
			static_cast<DirectoryHandle*>(dirState->dirStruct)->Position = 0;
			return 0;
		}

		int DvdDirNext(struct _reent* r, DIR_ITER* dirState, char* filename, struct stat* filestat)
		{
			DirectoryHandle* directory = static_cast<DirectoryHandle*>(dirState->dirStruct);
			// The listings are never released or moved once read, so the pointer stays good without the lock
			if (directory->Position >= directory->Directory->Entries.size()) {
				r->_errno = ENOENT;
				return -1;
			}
			const Entry& entry = directory->Directory->Entries[directory->Position++];
			std::size_t length = (entry.Name.size() < NAME_MAX ? entry.Name.size() : NAME_MAX);
			std::memcpy(filename, entry.Name.data(), length);
			filename[length] = '\0';
			if (filestat != nullptr) {
				FillStat(entry, filestat);
			}
			return 0;
		}

		int DvdDirClose(struct _reent* r, DIR_ITER* dirState)
		{
			static_cast<void>(r);
			static_cast<void>(dirState);
			return 0;
		}

		const devoptab_t DvdDevice = {
			"dvd",
			sizeof(FileHandle),
			DvdOpen,
			DvdClose,
			nullptr,
			DvdRead,
			DvdSeek,
			DvdFstat,
			DvdStat,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			sizeof(DirectoryHandle),
			DvdDirOpen,
			DvdDirReset,
			DvdDirNext,
			DvdDirClose,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr,
			nullptr
		};

		/** @brief Reads the volume descriptors, taking the Joliet hierarchy where there is one */
		bool ReadVolume()
		{
			std::uint8_t* sector = _cache;
			if (!ReadSectors(16, 1, sector) || sector[0] != 1 || std::memcmp(sector + 1, "CD001", 5) != 0) {
				LOGE("The disc carries no ISO 9660 file system");
				return false;
			}
			_volumeSectors = ReadU32BE(sector + VolumeSpaceSizeOffset + 4);
			_root.Lba = ReadU32BE(sector + RootRecordOffset + 6);
			_root.Size = ReadU32BE(sector + RootRecordOffset + 14);
			_root.IsDirectory = true;
			_joliet = false;

			for (std::uint32_t lba = 17; lba <= 19; lba++) {
				if (!ReadSectors(lba, 1, sector) || sector[0] == 255) {
					break;
				}
				const std::uint8_t* escape = sector + EscapeSequencesOffset;
				if (sector[0] == 2 && std::memcmp(sector + 1, "CD001", 5) == 0 &&
					escape[0] == '%' && escape[1] == '/' && (escape[2] == '@' || escape[2] == 'C' || escape[2] == 'E')) {
					_root.Lba = ReadU32BE(sector + RootRecordOffset + 6);
					_root.Size = ReadU32BE(sector + RootRecordOffset + 14);
					_joliet = true;
					break;
				}
			}
			// Nothing above was read through the cache, and it was used as the buffer
			_cacheCount = 0;
			return (_root.Lba != 0 && _root.Size != 0);
		}
	}

	namespace OgcDvd
	{
		bool Mount(StringView gameId, bool allowDriveReset)
		{
			if (_mounted) {
				return true;
			}
			if (_driveAbandoned) {
				return false;
			}

			DVD_Init();
			// The drive interface signals a finished transfer only if its interrupts are unmasked, which libogc
			// does nowhere but in DVD_Reset() - and a console that booted from the disc has no reason to be reset.
			// Without a reset mode, that only initializes the interface registers.
			DVD_Reset(DVD_RESETNONE);

			// A console that booted from the disc left the drive spun up and the disc authenticated, so its
			// header can simply be read. Anything else - no disc, a stopped motor, a drive a loader left reset -
			// fails here quickly, and only a caller with nowhere else to look asks for the slow way, which is
			// pointless without a disc in the drive. The first look is made once, the reset at most once.
			if (!_probed) {
				_probed = true;
				_answer = ReadDiscHeader(_probeBlock);
			}
			if (_answer == Answer::NotReady && allowDriveReset) {
				LOGI("Resetting the disc drive...");
				_answer = Answer::Abandoned;
				if (DVD_MountAsync(&_mountBlock, nullptr) && WaitForBlock(_mountBlock, MountTimeoutMs) == Answer::Read) {
					_answer = ReadDiscHeader(_block);
				}
			}
			if (_answer != Answer::Read) {
				return false;
			}

			if (std::memcmp(_header, gameId.data(), gameId.size() < 6 ? gameId.size() : 6) != 0 ||
				ReadU32BE(_header + 0x1C) != 0xC2339F3D) {
				LOGI("The disc in the drive is not this game's (\"{}\")", StringView(reinterpret_cast<const char*>(_header), 6));
				return false;
			}

			if (_lock == LWP_MUTEX_NULL) {
				LWP_MutexInit(&_lock, false);
			}
			if (_cache == nullptr) {
				_cache = static_cast<std::uint8_t*>(memalign(32, CacheSectors * SectorSize));
				if (_cache == nullptr) {
					return false;
				}
			}
			_cacheCount = 0;

			if (!ReadVolume()) {
				return false;
			}

			if (AddDevice(&DvdDevice) < 0) {
				LOGE("Cannot register the \"dvd:\" device");
				return false;
			}

			_mounted = true;
			LOGI("Disc mounted as \"dvd:\" ({} sectors{})", _volumeSectors, _joliet ? ", Joliet" : "");
			return true;
		}

		bool IsMounted()
		{
			return _mounted;
		}
	}
}

#endif
