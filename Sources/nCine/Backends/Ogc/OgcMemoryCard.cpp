#if defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)

#include "OgcMemoryCard.h"
#include "../../../Main.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ogc/card.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>

#include <Utf8.h>

// The same 32x32 icon the Dreamcast lists its saves with, generated from "Sources/Icons/Dreamcast/Icon.ico"
// (see cmake/ncine_dreamcast_icon.cmake) and turned into the GameCube's texture format below
#include "DreamcastIcon.h"

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine::Backends
{
	namespace
	{
		/** @brief "J2MC", which a file this did not write does not start with */
		constexpr std::uint32_t FileSignature = 0x4A324D43;
		constexpr std::uint32_t FileVersion = 1;

		/**
			@brief Layout of a file on the card

			The header comes first, then the comment and the icon the memory card manager reads - which must start
			in the first 512 bytes of the file, a limit of the directory entry - and then the data itself.
		*/
		enum : std::uint32_t {
			HeaderSize = 0x20,
			CommentOffset = 0x20,
			CommentLineSize = 32,
			IconOffset = 0x60,
			IconSize = 32 * 32 * 2,
			DataOffset = IconOffset + IconSize
		};

		constexpr std::int32_t SlotCount = 2;

		/** @brief How long a card that was just inserted is given to identify itself, in milliseconds */
		constexpr std::uint32_t ProbeTimeoutMs = 1000;

		struct FileHandle {
			std::int32_t Slot;
			char Name[CARD_FILENAMELEN + 1];
			std::uint8_t* Data;
			std::uint32_t Size;
			std::uint32_t Capacity;
			std::uint32_t Position;
			bool Writable;
			bool Dirty;
		};

		struct SlotState {
			bool Mounted;
			std::uint32_t SectorSize;
			std::uint8_t* WorkArea;
		};

		SlotState _slots[SlotCount];
		mutex_t _lock = LWP_MUTEX_NULL;
		char _nextTitle[CommentLineSize];
		char _nextDescription[CommentLineSize];
		/** @brief The icon in RGB5A3, in the 4x4 tiles the hardware reads textures in */
		std::uint16_t _icon[32 * 32];

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

		void WriteU32BE(std::uint8_t* target, std::uint32_t value)
		{
			target[0] = std::uint8_t(value >> 24);
			target[1] = std::uint8_t(value >> 16);
			target[2] = std::uint8_t(value >> 8);
			target[3] = std::uint8_t(value);
		}

		std::uint32_t ReadU32BE(const std::uint8_t* source)
		{
			return (std::uint32_t(source[0]) << 24) | (std::uint32_t(source[1]) << 16) |
				(std::uint32_t(source[2]) << 8) | std::uint32_t(source[3]);
		}

		std::uint32_t Checksum(const std::uint8_t* data, std::uint32_t size)
		{
			// FNV-1a, which only has to tell a torn write from a complete one
			std::uint32_t hash = 0x811C9DC5;
			for (std::uint32_t i = 0; i < size; i++) {
				hash = (hash ^ data[i]) * 0x01000193;
			}
			return hash;
		}

		/** @brief Copies text into one line of the comment, in the Windows-1252 the console's font spells */
		void SetCommentLine(char* line, StringView text)
		{
			std::memset(line, 0, CommentLineSize);
			std::size_t length = 0;
			for (std::size_t i = 0; i < text.size() && length < CommentLineSize; ) {
				Pair<char32_t, std::size_t> next = Death::Utf8::NextChar(arrayView(text.data(), text.size()), i);
				i = next.second();
				line[length++] = char(next.first() < 0x100 ? next.first() : U'?');
			}
		}

		void PrepareIcon()
		{
			for (std::int32_t y = 0; y < 32; y++) {
				for (std::int32_t x = 0; x < 32; x++) {
					std::uint8_t pair = DreamcastIconData[y * 16 + x / 2];
					std::uint16_t argb = DreamcastIconPalette[(x & 1) != 0 ? (pair & 0x0F) : (pair >> 4)];
					std::uint32_t a = (argb >> 12), r = (argb >> 8) & 0x0F, g = (argb >> 4) & 0x0F, b = argb & 0x0F;
					std::uint16_t texel;
					if (a == 0x0F) {
						// Opaque: five bits per channel, the top bit set
						texel = std::uint16_t(0x8000 | (((r << 1) | (r >> 3)) << 10) | (((g << 1) | (g >> 3)) << 5) | ((b << 1) | (b >> 3)));
					} else {
						// Translucent: three bits of alpha and four per channel
						texel = std::uint16_t(((a >> 1) << 12) | (r << 8) | (g << 4) | b);
					}
					_icon[((y / 4) * 8 + (x / 4)) * 16 + (y % 4) * 4 + (x % 4)] = texel;
				}
			}
		}

		StringView SlotName(std::int32_t slot)
		{
			return (slot == 0 ? "A"_s : "B"_s);
		}

		void DetachCallback(s32 chn, s32 result)
		{
			static_cast<void>(result);
			if (chn >= 0 && chn < SlotCount) {
				_slots[chn].Mounted = false;
			}
		}

		bool MountSlot(std::int32_t slot)
		{
			SlotState& state = _slots[slot];
			if (state.Mounted) {
				return true;
			}

			s32 memorySize = 0, sectorSize = 0, result;
			std::uint64_t start = gettime();
			while ((result = CARD_ProbeEx(slot, &memorySize, &sectorSize)) == CARD_ERROR_BUSY &&
				ticks_to_millisecs(diff_ticks(start, gettime())) < ProbeTimeoutMs) {
				usleep(10000);
			}
			if (result != CARD_ERROR_READY) {
				if (result == CARD_ERROR_WRONGDEVICE) {
					LOGI("Slot {} holds something other than a memory card", SlotName(slot));
				}
				return false;
			}

			if (state.WorkArea == nullptr) {
				state.WorkArea = static_cast<std::uint8_t*>(memalign(32, CARD_WORKAREA_SIZE));
				if (state.WorkArea == nullptr) {
					return false;
				}
			}
			result = CARD_Mount(slot, state.WorkArea, DetachCallback);
			if (result != CARD_ERROR_READY) {
				if (result == CARD_ERROR_ENCODING) {
					LOGW("The memory card in slot {} was formatted by a console of another region", SlotName(slot));
				} else {
					LOGW("Cannot mount the memory card in slot {} (error {})", SlotName(slot), result);
				}
				return false;
			}

			state.SectorSize = std::uint32_t(sectorSize);
			state.Mounted = true;
			// The size is reported in megabits, one block of a card is a sector
			LOGI("Memory card in slot {} mounted ({} blocks of {} KB)", SlotName(slot),
				std::uint32_t(memorySize) * (1024 * 1024 / 8) / state.SectorSize - 5, state.SectorSize / 1024);
			return true;
		}

		/** @brief Splits @cpp "mca:/Jazz2.config" @ce into the slot and the name, an empty name being the root */
		bool ParsePath(const char* path, std::int32_t& slot, const char*& name)
		{
			if (std::strncmp(path, "mca:", 4) == 0) {
				slot = 0;
			} else if (std::strncmp(path, "mcb:", 4) == 0) {
				slot = 1;
			} else {
				return false;
			}
			name = path + 4;
			while (*name == '/') {
				name++;
			}
			std::size_t length = std::strlen(name);
			return (length <= CARD_FILENAMELEN && std::strchr(name, '/') == nullptr);
		}

		/** @brief Reads a whole file into memory, returning an @cpp errno @ce value */
		std::int32_t LoadFile(std::int32_t slot, const char* name, std::uint8_t*& data, std::uint32_t& size)
		{
			data = nullptr;
			size = 0;

			card_file file;
			s32 result = CARD_Open(slot, name, &file);
			if (result == CARD_ERROR_NOFILE) {
				return ENOENT;
			}
			if (result < 0) {
				return EIO;
			}

			std::uint32_t sectorSize = _slots[slot].SectorSize;
			std::uint32_t length = std::uint32_t(file.len);
			std::uint8_t* image = static_cast<std::uint8_t*>(memalign(32, length));
			if (image == nullptr) {
				CARD_Close(&file);
				return ENOMEM;
			}
			for (std::uint32_t offset = 0; offset < length; offset += sectorSize) {
				if (CARD_Read(&file, image + offset, sectorSize, offset) < 0) {
					CARD_Close(&file);
					std::free(image);
					return EIO;
				}
			}
			CARD_Close(&file);

			std::uint32_t dataSize = ReadU32BE(image + 8);
			if (ReadU32BE(image) != FileSignature || ReadU32BE(image + 4) != FileVersion || length < DataOffset ||
				dataSize > length - DataOffset || ReadU32BE(image + 12) != Checksum(image + DataOffset, dataSize)) {
				// Torn, or not written by this - either way there is nothing usable in it
				LOGW("\"{}\" on the memory card in slot {} is damaged, it is treated as missing", name, SlotName(slot));
				std::free(image);
				return ENOENT;
			}

			data = static_cast<std::uint8_t*>(std::malloc(dataSize > 0 ? dataSize : 1));
			if (data == nullptr) {
				std::free(image);
				return ENOMEM;
			}
			std::memcpy(data, image + DataOffset, dataSize);
			size = dataSize;
			std::free(image);
			return 0;
		}

		/** @brief Writes a whole file to the card, re-creating it if it no longer has the right size */
		std::int32_t CommitFile(std::int32_t slot, const char* name, const std::uint8_t* data, std::uint32_t size)
		{
			std::uint32_t sectorSize = _slots[slot].SectorSize;
			std::uint32_t length = ((DataOffset + size + sectorSize - 1) / sectorSize) * sectorSize;
			std::uint8_t* image = static_cast<std::uint8_t*>(memalign(32, length));
			if (image == nullptr) {
				return ENOMEM;
			}
			std::memset(image, 0, length);
			WriteU32BE(image, FileSignature);
			WriteU32BE(image + 4, FileVersion);
			WriteU32BE(image + 8, size);
			WriteU32BE(image + 12, Checksum(data, size));
			if (_nextTitle[0] == '\0') {
				SetCommentLine(_nextTitle, "Jazz² Resurrection"_s);
			}
			if (_nextDescription[0] == '\0') {
				SetCommentLine(_nextDescription, name);
			}
			std::memcpy(image + CommentOffset, _nextTitle, CommentLineSize);
			std::memcpy(image + CommentOffset + CommentLineSize, _nextDescription, CommentLineSize);
			std::memcpy(image + IconOffset, _icon, IconSize);
			std::memcpy(image + DataOffset, data, size);
			// A description belongs to one file, the next one is described again or listed by its name
			_nextTitle[0] = '\0';
			_nextDescription[0] = '\0';

			card_file file;
			s32 result = CARD_Open(slot, name, &file);
			if (result >= 0 && std::uint32_t(file.len) != length) {
				CARD_Close(&file);
				CARD_Delete(slot, name);
				result = CARD_ERROR_NOFILE;
			}
			if (result == CARD_ERROR_NOFILE) {
				result = CARD_Create(slot, name, length, &file);
			}
			if (result < 0) {
				LOGW("Cannot create \"{}\" on the memory card in slot {} (error {})", name, SlotName(slot), result);
				std::free(image);
				return (result == CARD_ERROR_INSSPACE || result == CARD_ERROR_NOENT ? ENOSPC : EIO);
			}

			std::int32_t error = 0;
			for (std::uint32_t offset = 0; offset < length; offset += sectorSize) {
				if ((result = CARD_Write(&file, image + offset, sectorSize, offset)) < 0) {
					LOGW("Cannot write \"{}\" to the memory card in slot {} (error {})", name, SlotName(slot), result);
					error = EIO;
					break;
				}
			}

			if (error == 0) {
				card_stat stat;
				if (CARD_GetStatus(slot, file.filenum, &stat) >= 0) {
					stat.banner_fmt = CARD_BANNER_NONE;
					stat.icon_addr = IconOffset;
					// One frame, which has to have a speed for the console to show it at all
					stat.icon_fmt = CARD_ICON_RGB;
					stat.icon_speed = CARD_SPEED_SLOW;
					stat.comment_addr = CommentOffset;
					CARD_SetStatus(slot, file.filenum, &stat);
				}
			}

			CARD_Close(&file);
			std::free(image);
			return error;
		}

		int CardOpen(struct _reent* r, void* fileStruct, const char* path, int flags, int mode)
		{
			static_cast<void>(mode);
			std::int32_t slot;
			const char* name = nullptr;
			if (!ParsePath(path, slot, name) || name[0] == '\0') {
				r->_errno = (name != nullptr && name[0] == '\0' ? EISDIR : EINVAL);
				return -1;
			}

			LockGuard guard;
			if (!MountSlot(slot)) {
				r->_errno = ENODEV;
				return -1;
			}

			FileHandle* file = static_cast<FileHandle*>(fileStruct);
			std::memset(file, 0, sizeof(FileHandle));
			file->Slot = slot;
			std::strncpy(file->Name, name, CARD_FILENAMELEN);
			file->Writable = ((flags & O_ACCMODE) != O_RDONLY);

			if ((flags & O_TRUNC) != 0 && file->Writable) {
				// Written from scratch, so the file is committed on close even if nothing is written to it
				file->Dirty = true;
				return 0;
			}

			std::int32_t error = LoadFile(slot, name, file->Data, file->Size);
			if (error == ENOENT && (flags & O_CREAT) != 0 && file->Writable) {
				file->Dirty = true;
				return 0;
			}
			if (error != 0) {
				r->_errno = error;
				return -1;
			}
			file->Capacity = file->Size;
			if ((flags & O_APPEND) != 0) {
				file->Position = file->Size;
			}
			return 0;
		}

		int CardClose(struct _reent* r, void* fd)
		{
			FileHandle* file = static_cast<FileHandle*>(fd);
			std::int32_t error = 0;
			if (file->Dirty) {
				LockGuard guard;
				if (!MountSlot(file->Slot)) {
					error = ENODEV;
				} else {
					error = CommitFile(file->Slot, file->Name, file->Data, file->Size);
				}
			}
			std::free(file->Data);
			file->Data = nullptr;
			if (error != 0) {
				r->_errno = error;
				return -1;
			}
			return 0;
		}

		ssize_t CardWrite(struct _reent* r, void* fd, const char* ptr, size_t len)
		{
			FileHandle* file = static_cast<FileHandle*>(fd);
			if (!file->Writable) {
				r->_errno = EBADF;
				return -1;
			}
			std::uint32_t end = file->Position + std::uint32_t(len);
			if (end > file->Capacity) {
				std::uint32_t capacity = (file->Capacity < 4096 ? 4096 : file->Capacity);
				while (capacity < end) {
					capacity *= 2;
				}
				std::uint8_t* data = static_cast<std::uint8_t*>(std::realloc(file->Data, capacity));
				if (data == nullptr) {
					r->_errno = ENOSPC;
					return -1;
				}
				file->Data = data;
				file->Capacity = capacity;
			}
			if (file->Position > file->Size) {
				std::memset(file->Data + file->Size, 0, file->Position - file->Size);
			}
			std::memcpy(file->Data + file->Position, ptr, len);
			file->Position = end;
			if (end > file->Size) {
				file->Size = end;
			}
			file->Dirty = true;
			return ssize_t(len);
		}

		ssize_t CardRead(struct _reent* r, void* fd, char* ptr, size_t len)
		{
			static_cast<void>(r);
			FileHandle* file = static_cast<FileHandle*>(fd);
			if (file->Position >= file->Size) {
				return 0;
			}
			std::uint32_t length = std::uint32_t(len < file->Size - file->Position ? len : file->Size - file->Position);
			std::memcpy(ptr, file->Data + file->Position, length);
			file->Position += length;
			return ssize_t(length);
		}

		off_t CardSeek(struct _reent* r, void* fd, off_t pos, int dir)
		{
			FileHandle* file = static_cast<FileHandle*>(fd);
			std::int64_t position;
			switch (dir) {
				case SEEK_SET: position = pos; break;
				case SEEK_CUR: position = std::int64_t(file->Position) + pos; break;
				case SEEK_END: position = std::int64_t(file->Size) + pos; break;
				default: r->_errno = EINVAL; return -1;
			}
			if (position < 0 || position > INT32_MAX) {
				r->_errno = EINVAL;
				return -1;
			}
			file->Position = std::uint32_t(position);
			return off_t(position);
		}

		int CardFstat(struct _reent* r, void* fd, struct stat* st)
		{
			static_cast<void>(r);
			FileHandle* file = static_cast<FileHandle*>(fd);
			std::memset(st, 0, sizeof(struct stat));
			st->st_mode = S_IFREG | 0666;
			st->st_nlink = 1;
			st->st_size = file->Size;
			return 0;
		}

		int CardStat(struct _reent* r, const char* path, struct stat* st)
		{
			std::int32_t slot;
			const char* name;
			if (!ParsePath(path, slot, name)) {
				r->_errno = ENOENT;
				return -1;
			}

			LockGuard guard;
			if (!MountSlot(slot)) {
				r->_errno = ENODEV;
				return -1;
			}

			std::memset(st, 0, sizeof(struct stat));
			st->st_nlink = 1;
			if (name[0] == '\0') {
				st->st_mode = S_IFDIR | 0777;
				return 0;
			}

			// The data length is inside the file, so it is the one thing that cannot be had from the directory
			std::uint8_t* data;
			std::uint32_t size;
			std::int32_t error = LoadFile(slot, name, data, size);
			if (error != 0) {
				r->_errno = error;
				return -1;
			}
			std::free(data);
			st->st_mode = S_IFREG | 0666;
			st->st_size = size;
			return 0;
		}

		int CardUnlink(struct _reent* r, const char* path)
		{
			std::int32_t slot;
			const char* name;
			if (!ParsePath(path, slot, name) || name[0] == '\0') {
				r->_errno = ENOENT;
				return -1;
			}

			LockGuard guard;
			if (!MountSlot(slot)) {
				r->_errno = ENODEV;
				return -1;
			}
			s32 result = CARD_Delete(slot, name);
			if (result < 0) {
				r->_errno = (result == CARD_ERROR_NOFILE ? ENOENT : EIO);
				return -1;
			}
			return 0;
		}

		int CardMkdir(struct _reent* r, const char* path, int mode)
		{
			static_cast<void>(mode);
			std::int32_t slot;
			const char* name;
			// The root is the only directory there is, and it always exists
			r->_errno = (ParsePath(path, slot, name) && name[0] == '\0' ? EEXIST : ENOSYS);
			return -1;
		}

		int CardFsync(struct _reent* r, void* fd)
		{
			// Everything is written when the file is closed
			static_cast<void>(r);
			static_cast<void>(fd);
			return 0;
		}

		devoptab_t MakeDevice(const char* name)
		{
			devoptab_t device = {};
			device.name = name;
			device.structSize = sizeof(FileHandle);
			device.open_r = CardOpen;
			device.close_r = CardClose;
			device.write_r = CardWrite;
			device.read_r = CardRead;
			device.seek_r = CardSeek;
			device.fstat_r = CardFstat;
			device.stat_r = CardStat;
			device.unlink_r = CardUnlink;
			device.mkdir_r = CardMkdir;
			device.fsync_r = CardFsync;
			return device;
		}

		devoptab_t _deviceA = MakeDevice("mca");
		devoptab_t _deviceB = MakeDevice("mcb");
	}

	namespace OgcMemoryCard
	{
		void Initialize(StringView gameId)
		{
			if (_lock != LWP_MUTEX_NULL) {
				return;
			}
			LWP_MutexInit(&_lock, false);
			PrepareIcon();

			// Files are created under, and only found under, the game code and the maker code of the disc
			char gameCode[5] = {}, makerCode[3] = {};
			std::memcpy(gameCode, gameId.data(), gameId.size() >= 4 ? 4 : gameId.size());
			if (gameId.size() >= 6) {
				std::memcpy(makerCode, gameId.data() + 4, 2);
			}
			CARD_Init(gameCode, makerCode);

			AddDevice(&_deviceA);
			AddDevice(&_deviceB);
		}

		bool IsUsable(std::int32_t slot)
		{
			if (slot < 0 || slot >= SlotCount || _lock == LWP_MUTEX_NULL) {
				return false;
			}
			LockGuard guard;
			return MountSlot(slot);
		}

		void SetNextFileDescription(StringView title, StringView description)
		{
			if (_lock == LWP_MUTEX_NULL) {
				return;
			}
			LockGuard guard;
			SetCommentLine(_nextTitle, title);
			SetCommentLine(_nextDescription, description);
		}
	}
}

#endif
