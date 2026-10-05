// The apploader of the GameCube disc image - the small program every GameCube disc carries right after its
// header, which the console's boot ROM (BS2) loads to 0x81200000 and runs to bring the game's executable into
// memory. BS2 knows nothing about the executable itself: it calls the three functions the entry point hands out
// and performs every disc read the second of them asks for, until it says there is nothing left to read, and
// then jumps wherever the third one returns. Dolphin does exactly the same when it boots a disc without the
// real boot ROM (see CBoot::RunApploader there).
//
// Nintendo's own apploader is part of every retail disc and cannot be redistributed, and the devkitPro
// toolchain ships none, so this is a minimal one: it reads the DOL the disc header points at (offset 0x420)
// and copies its sections where they belong. Built by the GameCube target with the bare compiler - no libogc,
// no C library, no startup code - and placed on the disc by `AssetPacker create-image` (see GameCubeApploader.ld
// for the layout and for the header the disc carries in front of the code).

typedef unsigned char u8;
typedef unsigned int u32;

typedef void (*ReportFunction)(const char* format, ...);
typedef void (*InitFunction)(ReportFunction report);
typedef int (*MainFunction)(void** destination, u32* length, u32* offset);
typedef void* (*CloseFunction)(void);

/** Where the disc header stores the offset of the executable, see AssetPacker's DiscImage.cpp */
#define DiscHeaderDolOffset 0x420
#define DolSectionCount 18
/** Size of the reads the sections are brought in by - the DMA has to land somewhere aligned first */
#define BounceBufferSize (256 * 1024)
#define CacheLineSize 32

typedef struct {
	u32 SectionOffset[DolSectionCount];
	u32 SectionAddress[DolSectionCount];
	u32 SectionSize[DolSectionCount];
	u32 BssAddress;
	u32 BssSize;
	u32 EntryPoint;
	u32 Reserved[7];
} DolHeader;

enum {
	StateReadDiscHeader,
	StateReadDolHeader,
	StateParseDolHeader,
	StateCopySections,
	StateDone
};

// Everything below lives in .bss, which is not part of the image BS2 loads, so it holds garbage until AppInit()
// has set it - nothing may rely on a zero initializer
static ReportFunction _report;
static u32 _state;
static u32 _dolOffset;
static DolHeader _dol;
static u32 _section;
static u32 _sectionDone;
static u32 _pendingLength;
static u32 _pendingLead;
static u32 _pendingRead;

// The disc interface transfers by DMA, which needs a destination aligned to a cache line and a length that is a
// multiple of one, and the sections of a DOL need neither - so every read lands here first and is copied into
// place on the next call. It sits right behind the code, well below 0x81300000, where BS2 keeps itself.
static u8 _bounceBuffer[BounceBufferSize] __attribute__((aligned(CacheLineSize)));

static void InvalidateDataCache(const void* address, u32 length)
{
	u32 current = (u32)address & ~(CacheLineSize - 1);
	u32 end = (u32)address + length;
	for (; current < end; current += CacheLineSize) {
		__asm__ volatile("dcbi 0,%0" : : "r"(current) : "memory");
	}
	__asm__ volatile("sync" : : : "memory");
}

static void FlushDataCache(const void* address, u32 length)
{
	u32 current = (u32)address & ~(CacheLineSize - 1);
	u32 end = (u32)address + length;
	for (; current < end; current += CacheLineSize) {
		__asm__ volatile("dcbf 0,%0" : : "r"(current) : "memory");
	}
	__asm__ volatile("sync" : : : "memory");
}

static void InvalidateInstructionCache(const void* address, u32 length)
{
	u32 current = (u32)address & ~(CacheLineSize - 1);
	u32 end = (u32)address + length;
	for (; current < end; current += CacheLineSize) {
		__asm__ volatile("icbi 0,%0" : : "r"(current) : "memory");
	}
	__asm__ volatile("sync; isync" : : : "memory");
}

static void CopyMemory(u8* destination, const u8* source, u32 length)
{
	if ((((u32)destination | (u32)source) & 3) == 0) {
		while (length >= 4) {
			*(u32*)destination = *(const u32*)source;
			destination += 4;
			source += 4;
			length -= 4;
		}
	}
	while (length > 0) {
		*destination++ = *source++;
		length--;
	}
}

static void AppInit(ReportFunction report)
{
	_report = report;
	_state = StateReadDiscHeader;
	_dolOffset = 0;
	_section = 0;
	_sectionDone = 0;
	_pendingLength = 0;
	_pendingLead = 0;
	_pendingRead = 0;
	_report("Jazz2 Resurrection apploader\n");
}

static int AppMain(void** destination, u32* length, u32* offset)
{
	switch (_state) {
		case StateReadDiscHeader: {
			// The disc header itself is not in memory (BS2 copies only the first 32 bytes, to 0x80000000), and
			// the offset of the executable is near its end
			*destination = _bounceBuffer;
			*length = CacheLineSize;
			*offset = DiscHeaderDolOffset;
			_state = StateReadDolHeader;
			return 1;
		}
		case StateReadDolHeader: {
			InvalidateDataCache(_bounceBuffer, CacheLineSize);
			_dolOffset = *(const u32*)_bounceBuffer;
			if (_dolOffset == 0 || (_dolOffset & 3) != 0) {
				_report("Jazz2 apploader: the disc header names no executable (0x%08x)\n", _dolOffset);
				_state = StateDone;
				return 0;
			}
			*destination = _bounceBuffer;
			*length = sizeof(DolHeader);
			*offset = _dolOffset;
			_state = StateParseDolHeader;
			return 1;
		}
		case StateParseDolHeader: {
			InvalidateDataCache(_bounceBuffer, sizeof(DolHeader));
			CopyMemory((u8*)&_dol, _bounceBuffer, sizeof(DolHeader));
			_section = 0;
			_sectionDone = 0;
			_pendingLength = 0;
			_state = StateCopySections;
		}
		// Fall through
		case StateCopySections: {
			if (_pendingLength != 0) {
				// The read asked for on the previous call has completed, so its part of the section goes into place
				u8* target = (u8*)(_dol.SectionAddress[_section] + _sectionDone);
				InvalidateDataCache(_bounceBuffer, _pendingRead);
				CopyMemory(target, _bounceBuffer + _pendingLead, _pendingLength);
				FlushDataCache(target, _pendingLength);
				if (_section < 7) {
					// One of the text sections, which the CPU is about to fetch instructions from
					InvalidateInstructionCache(target, _pendingLength);
				}
				_sectionDone += _pendingLength;
				_pendingLength = 0;
			}

			while (_section < DolSectionCount && _sectionDone >= _dol.SectionSize[_section]) {
				_section++;
				_sectionDone = 0;
			}
			if (_section >= DolSectionCount) {
				_state = StateDone;
				return 0;
			}

			// The disc is addressed in 4-byte units, a cache line is a safe superset of that. Reading up to a line
			// past the section is fine - the image always has something after the executable to read.
			u32 fileOffset = _dolOffset + _dol.SectionOffset[_section] + _sectionDone;
			u32 alignedOffset = fileOffset & ~(CacheLineSize - 1);
			u32 remaining = _dol.SectionSize[_section] - _sectionDone;
			_pendingLead = fileOffset - alignedOffset;
			_pendingLength = (remaining < BounceBufferSize - CacheLineSize ? remaining : BounceBufferSize - CacheLineSize);
			_pendingRead = (_pendingLead + _pendingLength + CacheLineSize - 1) & ~(CacheLineSize - 1);

			*destination = _bounceBuffer;
			*length = _pendingRead;
			*offset = alignedOffset;
			return 1;
		}
		default: {
			return 0;
		}
	}
}

static void* AppClose(void)
{
	if (_state != StateDone || _section < DolSectionCount) {
		_report("Jazz2 apploader: the executable was not loaded\n");
	} else {
		_report("Jazz2 apploader: starting the executable at 0x%08x\n", _dol.EntryPoint);
	}
	return (void*)_dol.EntryPoint;
}

// Kept first in the image by the linker script, although nothing depends on that - the disc carries its address
__attribute__((section(".apploader.entry"), used))
void ApploaderEntry(InitFunction* init, MainFunction* mainFunction, CloseFunction* close)
{
	*init = AppInit;
	*mainFunction = AppMain;
	*close = AppClose;
}

extern const char _apploaderCodeSize[];

// The 32 bytes the disc carries in front of the code: the build date BS2 prints in its own debug output (fixed,
// so the same sources always produce the same disc), where to call, and how much to load. The "trailer" is a
// second, unused part of the image BS2 would load behind the code.
__attribute__((section(".apploader.header"), used))
const struct {
	char Date[16];
	void (*Entry)(InitFunction*, MainFunction*, CloseFunction*);
	u32 Size;
	u32 TrailerSize;
	u32 Reserved;
} ApploaderHeader = {
	"2026/10/05",
	ApploaderEntry,
	(u32)_apploaderCodeSize,
	0,
	0
};
