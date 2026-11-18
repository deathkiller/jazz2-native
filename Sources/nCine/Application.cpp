#include "Application.h"

#if defined(DEATH_TARGET_WINDOWS)
extern "C"
{
	_declspec(dllexport) unsigned long int NvOptimusEnablement = 0x00000001;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
};
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#	pragma comment(lib, "opengl32.lib")
#	pragma comment(lib, "winmm.lib")
#	if defined(_M_X64)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x64/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x64/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL3)
#			pragma comment(lib, "../Libs/Windows/x64/SDL3.lib")
#		elif defined(WITH_SDL2)
#			pragma comment(lib, "../Libs/Windows/x64/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x64/OpenAL32.lib")
#		endif
#	elif defined(_M_IX86)
#		if defined(WITH_GLEW)
#			pragma comment(lib, "../Libs/Windows/x86/glew32.lib")
#		endif
#		if defined(WITH_GLFW)
#			pragma comment(lib, "../Libs/Windows/x86/glfw3dll.lib")
#		endif
#		if defined(WITH_SDL3)
#			pragma comment(lib, "../Libs/Windows/x86/SDL3.lib")
#		elif defined(WITH_SDL2)
#			pragma comment(lib, "../Libs/Windows/x86/SDL2.lib")
#		endif
#		if defined(WITH_AUDIO)
#			pragma comment(lib, "../Libs/Windows/x86/OpenAL32.lib")
#		endif
#	else
#		error Unsupported architecture
#	endif
#endif

#include "Base/Algorithms.h"
#include "Base/Random.h"
#include "IAppEventHandler.h"
#include "Graphics/RenderResources.h"
#include "Graphics/RenderBuffersManager.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/ScreenViewport.h"
#include "Graphics/RHI/Rhi.h"
#include "Base/FrameStatistics.h"
#include "Base/FrameTimer.h"
#include "Graphics/SceneNode.h"
#include "Input/IInputManager.h"
#include "Input/JoyMapping.h"
#include "Threading/Thread.h"
#if defined(WITH_THREADS)
#	include "Threading/ThreadSync.h"
#endif
#include "ServiceLocator.h"
#include "tracy.h"
#include "tracy_opengl.h"

#include <atomic>
#include <cstdlib>
#include <new>

#include <Environment.h>
#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringView.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_WINDOWS)
	// Nothing more, but not <unistd.h> below either
#elif defined(DEATH_TARGET_ANDROID)
#	include "Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_SWITCH)
#	include <switch.h>
#elif defined(DEATH_TARGET_DREAMCAST)
#	include <dc/syscalls.h>
#	include "Backends/Dc/DcPlatform.h"
#elif defined(DEATH_TARGET_WII)
#	include <ogc/conf.h>
#	include <ogc/es.h>
#elif defined(DEATH_TARGET_WIIU)
#	include <coreinit/mcp.h>
#	include <unistd.h>
#elif defined(DEATH_TARGET_3DS)
#	include <3ds.h>
#	include "Backends/Ctr/CtrPlatform.h"
#elif defined(DEATH_TARGET_PS2)
extern "C" {
#	include <libcdvd.h>
#	include <debug.h>
}
#elif defined(DEATH_TARGET_PS3)
#	include <ppu-lv2.h>
#elif defined(DEATH_TARGET_PSP)
#	include <pspwlan.h>
#	if defined(WITH_CURL)
#		include "Backends/Psp/PspNetwork.h"
#	endif
#elif defined(DEATH_TARGET_VITA)
#	include <psp2/kernel/openpsid.h>
#elif defined(DEATH_TARGET_N64)
#	include <backtrace.h>
#	include <n64sys.h>
#elif !defined(DEATH_TARGET_GAMECUBE)
#	include <unistd.h>
#endif

#if defined(WITH_AUDIO)
#	if defined(WITH_OPENAL)
#		include "Audio/Backends/AL/ALAudioDevice.h"
#	elif defined(WITH_ASND)
#		include "Audio/Backends/ASND/AsndAudioDevice.h"
#	elif defined(WITH_AICA)
#		include "Audio/Backends/AICA/AicaAudioDevice.h"
#	elif defined(WITH_N64AUDIO)
#		include "Audio/Backends/N64/N64AudioDevice.h"
#	elif defined(WITH_PS3AUDIO)
#		include "Audio/Backends/PS3/Ps3AudioDevice.h"
#	elif defined(WITH_AHIAUDIO)
#		include "Audio/Backends/Amiga/AmigaAudioDevice.h"
#	elif defined(WITH_SDLAUDIO)
#		include "Audio/Backends/SDL/SdlAudioDevice.h"
#	elif defined(WITH_PSPAUDIO)
#		include "Audio/Backends/Psp/PspAudioDevice.h"
#	elif defined(WITH_PS2AUDIO)
#		include "Audio/Backends/Ps2/Ps2AudioDevice.h"
#	elif defined(WITH_NDSP)
#		include "Audio/Backends/Ndsp/NdspAudioDevice.h"
#	endif
#endif

#if defined(WITH_THREADS)
#	include "Threading/Thread.h"
#	include "Threading/ThreadPool.h"
#endif

#if defined(WITH_LUA)
#	include "LuaStatistics.h"
#endif

#if defined(WITH_IMGUI)
#	include "Graphics/ImGuiDrawing.h"
#	include "Graphics/ImGuiDebugOverlay.h"
#endif

#if defined(WITH_RENDERDOC)
#	include "Graphics/RenderDocCapture.h"
#endif

#if defined(WITH_BACKWARD)
#	include <Core/Backward.h>
Backward::ExceptionHandling __eh(Backward::Flags::UseStdError | Backward::Flags::IncludeSnippet | Backward::Flags::CreateMemoryDump);
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

#if defined(DEATH_TRACE)

#include <Containers/StringStlView.h>
#include <Core/Logger.h>
#include <IO/MemoryStream.h>
#if defined(DEATH_TARGET_WINDOWS)
#	include <Environment.h>
#	include <Utf8.h>
#elif defined(DEATH_TARGET_ANDROID)
#	include <stdarg.h>
#	include <time.h>
#	include <unistd.h>
#	include <android/log.h>
#	include "Backends/Android/AndroidJniHelper.h"
#else
#	include <cstdarg>
#	include <unistd.h>
#	if defined(DEATH_TARGET_SWITCH)
#		include <time.h>
#		include <switch.h>
#	elif defined(DEATH_TARGET_VITA)
#		include <psp2/kernel/clib.h>
#	elif defined(DEATH_TARGET_WIIU)
#		include <coreinit/debug.h>
#	elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
#		include <ogc/exi.h>
#		include <ogc/usbgecko.h>
#	elif defined(DEATH_TARGET_DREAMCAST)
#		include <kos/dbgio.h>
#	elif defined(DEATH_TARGET_PSP)
#		include <pspdebug.h>
#	elif defined(DEATH_TARGET_PS3)
#		include <sys/tty.h>
#	endif
#endif

#if defined(DEATH_TRACE_ASYNC) && (!defined(WITH_THREADS) || defined(DEATH_TARGET_EMSCRIPTEN))
#	pragma message("DEATH_TRACE_ASYNC is not supported on this platform")
#	undef DEATH_TRACE_ASYNC
#endif

static constexpr std::int32_t MaxLogEntryLength = 4096;

static const char ColorReset[] = "\x1B[0m";
static const char ColorBold[] = "\x1B[1m";
static const char ColorFaint[] = "\x1B[2m";
static const char ColorDarkGray[] = "\x1B[90m";
static const char ColorBrightRed[] = "\x1B[91m";
static const char ColorBrightYellow[] = "\x1B[93m";
static const char ColorBrightMagenta[] = "\x1B[95m";
static const char ColorDarkString[] = "\x1B[0;38;2;211;161;129m";
static const char ColorLightString[] = "\x1B[0;38;2;145;109;94m";
static const char ColorDimString[] = "\x1B[0;38;2;177;150;132m";

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#else
#	include <IO/FileStream.h>
// The owner is used by the attaching thread, the sink only reads the pointer (separate threads with DEATH_TRACE_ASYNC).
// Published after the header and startup history are written, so they stay at the start of the file.
static std::unique_ptr<Death::IO::Stream> __logFileOwner;
static std::atomic<Death::IO::Stream*> __logFile{nullptr};

// When the session started, for the log file header.
static std::int64_t __startupTimestampMs = 0;

// Entries logged before the log file opened, written to it once it does, so startup isn't missing. Bounded,
// keeps the oldest entries. Only the sink appends to and frees the buffer; the attaching thread only closes it.
enum class LogHistoryState : std::uint8_t {
	// Entries are appended to the buffer
	Open,
	// A target took the entries over, or none is coming
	Closed,
	// The buffer itself is gone
	Released
};

static constexpr std::uint32_t MaxLogHistoryLength = 16 * 1024;
static Death::Containers::Array<char> __logHistory;
static std::atomic<std::uint32_t> __logHistoryLength{0};
static std::atomic<LogHistoryState> __logHistoryState{LogHistoryState::Open};

static void AppendLogHistory(const char* entry, std::uint32_t length)
{
	// Length of capacity + 1 means entries were lost (the buffer itself is then exactly full)
	const std::uint32_t historyLength = __logHistoryLength.load(std::memory_order_relaxed);
	if (historyLength > MaxLogHistoryLength) {
		return;
	}
	if (__logHistory.empty()) {
		__logHistory = Death::Containers::Array<char>(Death::Containers::NoInit, MaxLogHistoryLength);
	}

	const std::uint32_t remaining = MaxLogHistoryLength - historyLength;
	if (length > remaining) {
		// Fill the rest, AttachTraceTarget() appends a note about the cut
		std::memcpy(__logHistory.data() + historyLength, entry, remaining);
		__logHistoryLength.store(MaxLogHistoryLength + 1, std::memory_order_release);
		return;
	}

	std::memcpy(__logHistory.data() + historyLength, entry, length);
	// Release, so the bytes above are visible to whoever reads the new length
	__logHistoryLength.store(historyLength + length, std::memory_order_release);
}

// Called only on the thread that attaches trace targets
static void DiscardLogHistory()
{
	LogHistoryState expected = LogHistoryState::Open;
	__logHistoryState.compare_exchange_strong(expected, LogHistoryState::Closed,
		std::memory_order_release, std::memory_order_relaxed);
}
#endif

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN) && (defined(WITH_ZLIB) || defined(WITH_MINIZ))
#	include <Containers/SmallVector.h>
#	include <IO/Compression/DeflateStream.h>
#	define NCINE_HAS_LOG_ARCHIVE

// Previous sessions' logs are kept in "<log file>.gz", one gzip member per session, oldest first. Concatenated
// members are valid gzip (RFC 1952), so a single `gunzip` returns the whole history in order.
//
// Each member stores its own size in a gzip extra field, so old members can be dropped without inflating
// anything. Other readers ignore the field.
namespace
{
	// gzip magic, compression method and header flags
	constexpr std::uint8_t GzipMagic0 = 0x1F, GzipMagic1 = 0x8B, GzipDeflate = 0x08;
	constexpr std::uint8_t GzipFlagExtra = 0x04, GzipFlagName = 0x08;
	// ID of the subfield holding the member size
	constexpr std::uint8_t MemberSizeFieldId0 = 'D', MemberSizeFieldId1 = 'z';
	constexpr std::uint8_t MemberSizeFieldLength = sizeof(std::uint32_t);
	constexpr std::uint16_t ExtraFieldLength = 2 + 2 + MemberSizeFieldLength;
	// Fixed header + extra field length + subfield header
	constexpr std::int64_t MemberSizeFieldOffset = 10 + 2 + 4;

	// Archive limits, logs compress ~10x so the count usually decides
	constexpr std::int64_t MaxLogArchiveSize = 4 * 1024 * 1024;
	constexpr std::uint32_t MaxLogArchiveParts = 32;
	// Trim well below the limit, so the archive isn't rewritten on every launch once full
	constexpr std::uint32_t TrimLogArchiveToParts = MaxLogArchiveParts / 2;

	// Runs before the first frame, so favor speed on consoles; higher levels gain only a few percent on text
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
	constexpr std::int32_t LogArchiveCompressionLevel = 1;
#else
	constexpr std::int32_t LogArchiveCompressionLevel = 6;
#endif

	// Stack buffer, same sizes as Compression::DeflateWriter uses
#if defined(DEATH_TARGET_CONSTRAINED_MEMORY)
	constexpr std::int32_t LogArchiveBufferSize = 8 * 1024;
#else
	constexpr std::int32_t LogArchiveBufferSize = 16 * 1024;
#endif

	struct LogArchivePart {
		std::int64_t Offset;
		std::int64_t Size;
	};

	// Returns the stored size of the member at the offset, or 0 if it's not a member written by us
	std::int64_t ReadLogArchivePartSize(Stream& s, std::int64_t offset, std::int64_t fileSize)
	{
		if (offset + MemberSizeFieldOffset + MemberSizeFieldLength > fileSize ||
			s.Seek(offset, SeekOrigin::Begin) < 0) {
			return 0;
		}

		std::uint8_t header[10];
		if (s.Read(header, sizeof(header)) != sizeof(header) ||
			header[0] != GzipMagic0 || header[1] != GzipMagic1 || header[2] != GzipDeflate ||
			(header[3] & GzipFlagExtra) != GzipFlagExtra) {
			return 0;
		}

		// Our subfield is always first
		const std::uint16_t extraLength = s.ReadValueAsLE<std::uint16_t>();
		std::uint8_t subfield[4];
		if (extraLength < ExtraFieldLength || s.Read(subfield, sizeof(subfield)) != sizeof(subfield) ||
			subfield[0] != MemberSizeFieldId0 || subfield[1] != MemberSizeFieldId1 ||
			subfield[2] != MemberSizeFieldLength || subfield[3] != 0) {
			return 0;
		}

		const std::int64_t memberSize = s.ReadValueAsLE<std::uint32_t>();
		// Reaching past the end means the file was truncated
		if (memberSize <= MemberSizeFieldOffset || offset + memberSize > fileSize) {
			return 0;
		}
		return memberSize;
	}

	// Compresses the whole input stream into one gzip member appended to the output stream
	bool WriteLogArchivePart(Stream& output, Stream& input, Containers::StringView originalName)
	{
		const std::int64_t startOffset = output.GetPosition();

		const std::uint8_t header[4] = {
			GzipMagic0, GzipMagic1, GzipDeflate, std::uint8_t(GzipFlagExtra | GzipFlagName)
		};
		output.Write(header, sizeof(header));
		// Modification time (Unix seconds), 0 if unknown
		output.WriteValueAsLE<std::uint32_t>(std::uint32_t(__startupTimestampMs > 0 ? __startupTimestampMs / 1000 : 0));
		// XFL byte matching the compression level
		output.WriteValue<std::uint8_t>(LogArchiveCompressionLevel >= 9 ? 0x02 : (LogArchiveCompressionLevel <= 1 ? 0x04 : 0x00));
		output.WriteValue<std::uint8_t>(0xFF);	// Unknown operating system

		output.WriteValueAsLE<std::uint16_t>(ExtraFieldLength);
		const std::uint8_t subfield[4] = { MemberSizeFieldId0, MemberSizeFieldId1, MemberSizeFieldLength, 0 };
		output.Write(subfield, sizeof(subfield));
		output.WriteValueAsLE<std::uint32_t>(0);	// Size of the member, filled in at the end

		// Original file name
		output.Write(originalName.data(), originalName.size());
		output.WriteValue<std::uint8_t>(0);

		std::uint32_t crc = std::uint32_t(crc32(0, nullptr, 0));
		std::uint64_t uncompressedSize = 0;
		{
			// Raw deflate, the gzip header and trailer are written here
			Compression::DeflateWriter deflater(output, LogArchiveCompressionLevel, true);
			if (!deflater.IsValid()) {
				return false;
			}

			char buffer[LogArchiveBufferSize];
			while (true) {
				const std::int64_t bytesRead = input.Read(buffer, sizeof(buffer));
				if (bytesRead <= 0) {
					break;
				}
				crc = std::uint32_t(crc32(crc, reinterpret_cast<const Bytef*>(buffer), uInt(bytesRead)));
				uncompressedSize += std::uint64_t(bytesRead);
				if (deflater.Write(buffer, bytesRead) != bytesRead) {
					return false;
				}
			}
			// Finish the stream before writing the trailer
			deflater.Dispose();
		}

		output.WriteValueAsLE<std::uint32_t>(crc);
		output.WriteValueAsLE<std::uint32_t>(std::uint32_t(uncompressedSize));

		// Fill in the member size, so it can be skipped without inflating
		const std::int64_t endOffset = output.GetPosition();
		if (output.Seek(startOffset + MemberSizeFieldOffset, SeekOrigin::Begin) < 0) {
			return false;
		}
		output.WriteValueAsLE<std::uint32_t>(std::uint32_t(endOffset - startOffset));
		return (output.Seek(endOffset, SeekOrigin::Begin) >= 0);
	}

	// Copies a range of one stream to the current position of another
	bool CopyLogArchiveRange(Stream& output, Stream& input, std::int64_t offset, std::int64_t size)
	{
		if (input.Seek(offset, SeekOrigin::Begin) < 0) {
			return false;
		}

		char buffer[LogArchiveBufferSize];
		while (size > 0) {
			const std::int64_t bytesToRead = (size < std::int64_t(sizeof(buffer)) ? size : std::int64_t(sizeof(buffer)));
			const std::int64_t bytesRead = input.Read(buffer, bytesToRead);
			if (bytesRead <= 0 || output.Write(buffer, bytesRead) != bytesRead) {
				return false;
			}
			size -= bytesRead;
		}
		return true;
	}

	// Moves the previous session's log into the archive, must be called before the log is opened for writing
	void ArchivePreviousLogFile(Containers::StringView targetPath)
	{
		String archivePath = targetPath + ".gz"_s;
		String stagingPath = targetPath + ".gz.tmp"_s;

		// Checked first, so the very first run doesn't report a missing file
		if (fs::GetFileSize(targetPath) <= 0) {
			return;
		}

		const bool hasArchive = fs::Exists(archivePath);

		{
			auto previousLog = fs::Open(targetPath, FileAccess::Read | FileAccess::Sequential);
			if (!previousLog->IsValid() || previousLog->GetSize() <= 0) {
				return;
			}

			// With an existing archive, compress to a staging file first (its size decides what to trim, and a crash
			// loses only the staging file). Without one, write the archive directly - an unfinished member is
			// rejected next launch and the log is archived again.
			StringView partPath = (hasArchive ? stagingPath : archivePath);
			auto part = fs::Open(partPath, FileAccess::Write);
			if (!part->IsValid() || !WriteLogArchivePart(*part, *previousLog, fs::GetFileName(targetPath))) {
				part->Dispose();
				fs::RemoveFile(partPath);
				return;
			}
		}

		if (!hasArchive) {
			return;
		}

		const std::int64_t newPartSize = fs::GetFileSize(stagingPath);
		if (newPartSize <= 0) {
			fs::RemoveFile(stagingPath);
			return;
		}

		// Collect the existing members
		SmallVector<LogArchivePart, 16> parts;
		auto archive = fs::Open(archivePath, FileAccess::Read);
		const std::int64_t archiveSize = (archive->IsValid() ? archive->GetSize() : 0);
		std::int64_t parsedSize = 0, totalPartSize = 0;
		for (std::int64_t offset = 0; offset < archiveSize; ) {
			const std::int64_t partSize = ReadLogArchivePartSize(*archive, offset, archiveSize);
			if (partSize <= 0) {
				// Unknown or truncated member, nothing after it is kept
				break;
			}
			parts.push_back(LogArchivePart{offset, partSize});
			offset += partSize;
			parsedSize = offset;
			totalPartSize += partSize;
		}

		// A write interrupted by a crash leaves a broken member at the end, the archive must be rewritten to drop it
		const bool endsInGarbage = (parsedSize != archiveSize);

		// If everything fits, just append; otherwise trim down to TrimLogArchiveToParts
		const bool fitsAsIs = (!endsInGarbage && !parts.empty() &&
			totalPartSize + newPartSize <= MaxLogArchiveSize && parts.size() + 1 <= MaxLogArchiveParts);
		const std::int64_t sizeLimit = (fitsAsIs ? MaxLogArchiveSize : MaxLogArchiveSize / 2);
		const std::uint32_t partsLimit = (fitsAsIs ? MaxLogArchiveParts : TrimLogArchiveToParts);

		// Keep the newest members that fit
		std::size_t firstKept = parts.size();
		std::int64_t keptSize = 0;
		std::uint32_t keptCount = 0;
		for (std::size_t i = parts.size(); i > 0; i--) {
			if (keptSize + parts[i - 1].Size + newPartSize > sizeLimit ||
				keptCount + 1 >= partsLimit) {
				break;
			}
			keptSize += parts[i - 1].Size;
			keptCount++;
			firstKept = i - 1;
		}

		bool archived = false;
		// Tells a failed update apart from "nothing worth keeping"
		bool updateAttempted = false;
		if (fitsAsIs && firstKept == 0) {
			updateAttempted = true;
			// Everything stays, just append
			archive->Dispose();
			auto appendTo = fs::Open(archivePath, FileAccess::ReadWrite);
			auto staging = fs::Open(stagingPath, FileAccess::Read | FileAccess::Sequential);
			archived = (appendTo->IsValid() && staging->IsValid() &&
				appendTo->Seek(0, SeekOrigin::End) >= 0 && staging->CopyTo(*appendTo) == newPartSize);
		} else if (firstKept < parts.size()) {
			updateAttempted = true;

			// Drop the oldest members by rewriting the archive
			String rebuiltPath = targetPath + ".gz.new"_s;

			{
				auto rebuilt = fs::Open(rebuiltPath, FileAccess::Write);
				auto staging = fs::Open(stagingPath, FileAccess::Read | FileAccess::Sequential);
				archived = (rebuilt->IsValid() && staging->IsValid() &&
					CopyLogArchiveRange(*rebuilt, *archive, parts[firstKept].Offset, keptSize) &&
					staging->CopyTo(*rebuilt) == newPartSize);
			}
			archive->Dispose();
			if (archived) {
				fs::RemoveFile(archivePath);
				// If the move fails, ".gz.new" is the only copy left, so it's kept rather than deleted
				archived = fs::Move(rebuiltPath, archivePath);
			} else {
				fs::RemoveFile(rebuiltPath);
			}
		}

		if (archived) {
			fs::RemoveFile(stagingPath);
		} else if (!updateAttempted) {
			// Nothing worth keeping, the new member becomes the archive
			archive->Dispose();
			fs::RemoveFile(archivePath);
			if (!fs::Move(stagingPath, archivePath)) {
				fs::RemoveFile(stagingPath);
			}
		} else {
			// Update failed (full card, file locked), keep the existing history and drop only the new member
			archive->Dispose();
			LOGW("Cannot add the previous session to \"{}\", the archive is left as it is", archivePath);
			fs::RemoveFile(stagingPath);
		}
	}
}
#endif

enum class ConsoleType {
	None,
	Redirect,
	WinApi,
	EscapeCodes,
	EscapeCodes8bit,
	EscapeCodes24bit
};

static ConsoleType __consoleType = ConsoleType::None;
// Prevents attaching the trace sink twice (entry point and PreInitCommon())
static bool __traceInitialized = false;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_VITA) && !defined(DEATH_TARGET_WINDOWS_RT)
static bool __consoleDarkMode = true;
static bool __consoleSixelSupported = false;
#endif

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	if !defined(ENABLE_VIRTUAL_TERMINAL_INPUT)
#		define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#	endif
#	if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#		define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#	endif

extern "C" IMAGE_DOS_HEADER __ImageBase;

static HANDLE __consoleHandleOut;
static SHORT __consoleCursorY;
static Array<wchar_t> __consolePrompt;

static bool EnableVirtualTerminalProcessing(HANDLE consoleHandleOut)
{
	if (consoleHandleOut == INVALID_HANDLE_VALUE || !Environment::IsWindows10()) {
		return false;
	}

	DWORD dwMode = 0;
	return (::GetConsoleMode(consoleHandleOut, &dwMode) &&
			::SetConsoleMode(consoleHandleOut, dwMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING));
}

template<typename TCallback>
static bool FillBufferWithTimeout(HANDLE handle, OVERLAPPED& ov, char* buffer, std::uint32_t bufferSize, TCallback callback)
{
	constexpr std::uint64_t TimeoutMs = 100;

	std::uint32_t bytesRead = 0;
	std::uint64_t startTime = ::GetTickCount64();
	std::uint64_t now = startTime;

	while (true) {
		if (!::ReadFile(handle, buffer + bytesRead, bufferSize - bytesRead, NULL, &ov)) {
			DWORD err = GetLastError();
			if (err == ERROR_IO_PENDING) {
				DWORD waitResult = ::WaitForSingleObject(ov.hEvent, (DWORD)(TimeoutMs - (now - startTime)));
				if (waitResult == WAIT_TIMEOUT) {
					::CancelIo(handle);
					break;
				} else if (waitResult != WAIT_OBJECT_0) {
					break;
				}
			} else {
				break;
			}
		}

		DWORD partialBytesRead = 0;
		if (!::GetOverlappedResult(handle, &ov, &partialBytesRead, FALSE)) {
			break;
		}

		bytesRead += partialBytesRead;
		if (callback(StringView(buffer, bytesRead))) {
			return true;
		}

		now = ::GetTickCount64();
		if (now - startTime >= TimeoutMs || bytesRead >= bufferSize) {
			break;
		}
	}

	return false;
}

static void CheckConsoleCapabilities()
{
	HANDLE hStdIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
	if (hStdIn == INVALID_HANDLE_VALUE) {
		LOGD("Failed to open async \"CONIN$\" handle with error 0x{:.8x}", ::GetLastError());
		return;
	}

	OVERLAPPED ov{};
	ov.hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ov.hEvent == NULL) {
		::CloseHandle(hStdIn);
		return;
	}

	HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);

	DWORD stdInMode = 0;
	::GetConsoleMode(hStdIn, &stdInMode);
	::SetConsoleMode(hStdIn, (stdInMode & ~ENABLE_LINE_INPUT) | ENABLE_VIRTUAL_TERMINAL_INPUT);

	CHAR buffer[128];
	DWORD bytesWritten;

	static const char daRequest[] = "\x1B[c";
	::WriteConsoleA(hStdOut, daRequest, sizeof(daRequest) - 1, &bytesWritten, nullptr);
	::FlushConsoleInputBuffer(hStdIn);

	FillBufferWithTimeout(hStdIn, ov, buffer, sizeof(buffer), [](StringView bufferView) {
		if (StringView end = bufferView.find('c')) {
			if (StringView begin = bufferView.find("\x1B[?"_s)) {
				if (begin.end() < end.begin()) {
					String response = ';' + bufferView.slice(begin.end(), end.begin()) + ';';
					if (response.contains(";4;"_s)) {
						__consoleSixelSupported = true;
					}
				}
			}
			return true;
		}
		return false;
	});

	static const char bgColorRequest[] = "\x1B]11;?\x07";
	::WriteConsoleA(hStdOut, bgColorRequest, sizeof(bgColorRequest) - 1, &bytesWritten, nullptr);
	::FlushConsoleInputBuffer(hStdIn);
	
	FillBufferWithTimeout(hStdIn, ov, buffer, sizeof(buffer), [](StringView bufferView) {
		if (StringView end = bufferView.find("\x1B\\"_s)) {
			if (StringView begin = bufferView.find("\x1B]11;rgb:"_s)) {
				if (begin.end() < end.begin()) {
					auto rrggbb = bufferView.slice(begin.end(), end.begin()).split('/');
					if (rrggbb.size() == 3) {
						String part = rrggbb[0];
						std::uint32_t r = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[1];
						std::uint32_t g = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[2];
						std::uint32_t b = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						std::uint32_t luminance = ((13933 * r) + (46871 * g) + (4732 * b)) >> 16;
						__consoleDarkMode = (luminance < 128);
					}
				}
			}
			return true;
		}
		return false;
	});


	::SetConsoleMode(hStdIn, stdInMode);
}

#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <termios.h>
#	include <sys/select.h>

static void CheckConsoleCapabilities()
{
	// Save the terminal settings
	termios oldt;
	::tcgetattr(STDIN_FILENO, &oldt);

	// Set the terminal to raw mode (no buffering or echoing)
	termios newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	::tcsetattr(STDIN_FILENO, TCSANOW, &newt);

	char buffer[128];

	// Send the escape sequence
	::fputs("\x1b]11;?\x07", stdout);
	::fflush(stdout);

	// Wait for input with a timeout
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 400000; // 400 ms
	std::int32_t result = ::select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);

	if (result > 0) {
		ssize_t totalRead = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		StringView bufferView = StringView(buffer, totalRead);
		if (StringView end = bufferView.find('c')) {
			if (StringView begin = bufferView.find("\x1B[?"_s)) {
				if (begin.end() < end.begin()) {
					String response = ';' + bufferView.slice(begin.end(), end.begin()) + ';';
					if (response.contains(";4;"_s)) {
						__consoleSixelSupported = true;
					}
				}
			}
		}
	}

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	timeout.tv_sec = 0;
	timeout.tv_usec = 400000; // 400 ms
	result = ::select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout);

	if (result > 0) {
		ssize_t totalRead = ::read(STDIN_FILENO, buffer, sizeof(buffer));
		StringView bufferView = StringView(buffer, totalRead);
		if (StringView end = bufferView.find("\x1B\\"_s)) {
			if (StringView begin = bufferView.find("\x1B]11;rgb:"_s)) {
				if (begin.end() < end.begin()) {
					auto rrggbb = bufferView.slice(begin.end(), end.begin()).split('/');
					if (rrggbb.size() == 3) {
						String part = rrggbb[0];
						std::uint32_t r = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[1];
						std::uint32_t g = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						part = rrggbb[2];
						std::uint32_t b = (strtoul(part.data(), nullptr, 16) >> 8) & 0xFF;
						std::uint32_t luminance = ((13933 * r) + (46871 * g) + (4732 * b)) >> 16;
						__consoleDarkMode = (luminance < 128);
					}
				}
			}
		}
	}

	// Restore the terminal settings
	::tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

#endif

template<std::int32_t N>
static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char(&newPart)[N])
{
	length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, N - 1);
}

static DEATH_ALWAYS_INLINE void AppendPart(char* dest, std::int32_t& length, const char* newPart, std::int32_t newPartLength)
{
	length += nCine::copyStringFirst(dest + length, MaxLogEntryLength - length - 1, newPart, newPartLength);
}

static void AppendDateTime(char* dest, std::int32_t& length, std::uint64_t timestamp, std::atomic<std::int32_t>* lastDay = nullptr)
{
	// Convert nanoseconds to milliseconds
	auto dt = DateTime::FromUnixMilliseconds(timestamp / 1000000ULL);
	auto p = dt.Partitioned();

	if (lastDay != nullptr) {
		// Prepend the full date to the first entry of each day, exchange() lets only one thread write it
		std::int32_t day = (p.Year * 12 + p.Month) * 31 + p.Day;
		if (lastDay->exchange(day, std::memory_order_relaxed) != day) {
			length += (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
				"{}/{:.2}/{:.2} ", p.Year, p.Month + 1, p.Day);
		}
	}

	length += (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
		"{:.2}:{:.2}:{:.2}.{:.3}", p.Hour, p.Minute, p.Second, p.Millisecond);
}

static void AppendLevel(char* dest, std::int32_t& length, TraceLevel level, StringView threadId)
{
	if (length >= MaxLogEntryLength) {
		return;
	}

	char levelIdentifier;
	switch (level) {
		case TraceLevel::Fatal:		levelIdentifier = 'F'; break;
		case TraceLevel::Assert:	levelIdentifier = 'A'; break;
		case TraceLevel::Error:		levelIdentifier = 'E'; break;
		case TraceLevel::Warning:	levelIdentifier = 'W'; break;
		case TraceLevel::Info:		levelIdentifier = 'I'; break;
		default:					levelIdentifier = 'D'; break;
	}

	std::int32_t partLength = (std::int32_t)formatInto({ dest + length, (std::size_t)(MaxLogEntryLength - length - 1) },
		!threadId.empty() ? "[{:c}]{}}}" : "[{:c}]", levelIdentifier, threadId);
	length += partLength;

	while (partLength < 10) {
		dest[length++] = ' ';
		partLength++;
	}
	dest[length++] = ' ';
}

#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
// Appends a part of a function name, rewriting anonymous namespaces to GCC's "{anonymous}" on every compiler.
// Clang is checked first, because Clang-CL defines both macros and reports __PRETTY_FUNCTION__.
static void AppendFunctionNamePart(char* dest, std::int32_t& length, const char* functionName, std::int32_t functionNameLength)
{
#	if defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
	static constexpr StringView AnonymousNamespace = "{anonymous}"_s;
#		if defined(DEATH_TARGET_CLANG)
	// e.g. "void nCine::(anonymous namespace)::Function()"
	static constexpr StringView AnonymousNamespaceForeign = "(anonymous namespace)"_s;
#		elif defined(DEATH_TARGET_MSVC)
	// e.g. "nCine::`anonymous-namespace'::Function"
	static constexpr StringView AnonymousNamespaceForeign = "`anonymous-namespace'"_s;
#		endif

	// There can be more than one (e.g. in the return type)
	StringView remaining = StringView(functionName, functionNameLength);
	while (auto found = remaining.find(AnonymousNamespaceForeign)) {
		AppendPart(dest, length, remaining.data(), (std::int32_t)(found.begin() - remaining.data()));
		AppendPart(dest, length, AnonymousNamespace.data(), (std::int32_t)AnonymousNamespace.size());
		remaining = remaining.suffix(found.end());
	}

	AppendPart(dest, length, remaining.data(), (std::int32_t)remaining.size());
#	else
	// GCC already uses "{anonymous}"
	AppendPart(dest, length, functionName, functionNameLength);
#	endif
}

static void AppendShortenedFunctionName(char* dest, std::int32_t& length, const char* functionName, std::int32_t functionNameLength)
{
#	if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
	// Strip function specifiers, return type and arguments from function name, because GCC/Clang includes full function signature
	static constexpr StringView LambdaSuffix = "::<lambda()>"_s;
	static constexpr StringView OperatorPrefix = "operator"_s;

	// Separates the enclosing function from the lambda (Clang first, it also defines DEATH_TARGET_GCC):
	//  - GCC: "Class::Method(bool)::<lambda(int)>"
	//  - Clang: "auto Class::Method(bool)::(lambda)::operator()() const", "(anonymous class)" before LLVM 22
	// The marker ends before the lambda's arguments, so lambdas with parameters match too
#		if defined(DEATH_TARGET_CLANG)
#			if __clang_major__ >= 22
	static constexpr StringView LambdaMarker = "::(lambda)::operator("_s;
#			else
	static constexpr StringView LambdaMarker = "::(anonymous class)::operator("_s;
#			endif
#		else
	static constexpr StringView LambdaMarker = "::<lambda("_s;
#		endif

	StringView functionNameView = StringView(functionName, functionNameLength);
	std::int32_t i = functionNameLength - 1; bool isLambda = false;
	// The first occurrence is the outermost lambda, nested ones are reported against the same function
	if (auto lambdaMarker = functionNameView.find(LambdaMarker)) {
		i = std::int32_t(lambdaMarker.begin() - functionName);
		isLambda = true;
	}

	// Go backwards until we find the first opening parenthesis (arguments)
	std::int32_t parethesisCount = 0;
	for (; i >= 0; i--) {
		if (functionName[i] == ')') {
			parethesisCount++;
		} else if (functionName[i] == '(') {
			parethesisCount--;
			if (parethesisCount == 0) {
				break;
			}
		} else if (functionName[i] == '>') {
			parethesisCount++;
		} else if (functionName[i] == '<') {
			parethesisCount--;
		} else if (functionName[i] == ']') {
			parethesisCount++;
		} else if (functionName[i] == '[') {
			parethesisCount--;
		}
	}

	if (i > 0) {
		std::int32_t end = i;

		// Start before "operator", its brackets ("operator<<") and spaces ("operator new") belong to the name
		if (auto operatorKeyword = functionNameView.prefix(end).find(OperatorPrefix)) {
			i = std::int32_t(operatorKeyword.begin() - functionName);
		}

		// Go backwards until the space before the name (after the return type)
		for (i--; i >= 0; i--) {
			if (functionName[i] == ')') {
				parethesisCount++;
			} else if (functionName[i] == '(') {
				parethesisCount--;
			} else if (functionName[i] == '>') {
				parethesisCount++;
			} else if (functionName[i] == '<') {
				parethesisCount--;
			} else if (functionName[i] == ' ' && parethesisCount == 0) {
				break;
			}
		}
		i++;

		// Skip '*' or '&' of the return type
		while (i < end && (functionName[i] == '*' || functionName[i] == '&')) {
			i++;
		}

		AppendFunctionNamePart(dest, length, &functionName[i], end - i);
		AppendPart(dest, length, "()");
		if (isLambda) {
			AppendPart(dest, length, LambdaSuffix.data(), (std::int32_t)LambdaSuffix.size());
		}
	} else {
		AppendFunctionNamePart(dest, length, functionName, functionNameLength);
	}
#	else
	// MSVC gives a bare name, so only lambdas need shortening. Cut at the first "<lambda_" (e.g.
	// "Class::Method::<lambda_1>::operator ()()") and write "()::<lambda()>" like GCC/Clang do.
	static constexpr StringView LambdaPrefixMsvc = "<lambda_"_s;
	static constexpr StringView ScopeSeparator = "::"_s;

	StringView functionNameView = StringView(functionName, functionNameLength);
	if (auto lambdaPrefix = functionNameView.find(LambdaPrefixMsvc)) {
		StringView enclosingFunction = functionNameView.prefix(lambdaPrefix.begin());
		if (enclosingFunction.hasSuffix(ScopeSeparator)) {
			enclosingFunction = enclosingFunction.exceptSuffix(ScopeSeparator);
		}

		// A lambda at namespace scope has no enclosing function
		if (!enclosingFunction.empty()) {
			AppendFunctionNamePart(dest, length, enclosingFunction.data(), (std::int32_t)enclosingFunction.size());
			AppendPart(dest, length, "()::<lambda()>");
		} else {
			AppendPart(dest, length, "<lambda()>");
		}
		return;
	}

	AppendFunctionNamePart(dest, length, functionName, functionNameLength);
#	endif
}
#endif

static void AppendFunctionName(char* dest, std::int32_t& length, StringView functionName)
{
	if (!functionName.empty()) {
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
		AppendShortenedFunctionName(dest, length, functionName.data(), (std::int32_t)functionName.size());
#else
		AppendPart(dest, length, functionName.data(), (std::int32_t)functionName.size());
#endif
		AppendPart(dest, length, " ‡ ");
	}
}

static void AppendMessagePrefixIfAny(char* dest, std::int32_t& length, TraceLevel level, StringView functionName)
{
	if (!functionName.empty()) {
		if (__consoleType >= ConsoleType::EscapeCodes) {
			AppendPart(dest, length, ColorFaint);

			switch (level) {
				case TraceLevel::Error:
				case TraceLevel::Fatal:
					AppendPart(dest, length, ColorBrightRed);
					break;
				case TraceLevel::Assert:
					AppendPart(dest, length, ColorBrightMagenta);
					break;
				case TraceLevel::Warning:
					AppendPart(dest, length, ColorBrightYellow);
					break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
				case TraceLevel::Debug:
				case TraceLevel::Deferred:
					AppendPart(dest, length, ColorDarkGray);
					break;
#endif
			}
		}

#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG) || defined(DEATH_TARGET_MSVC)
		AppendShortenedFunctionName(dest, length, functionName.data(), (std::int32_t)functionName.size());
#else
		AppendPart(dest, length, functionName.data(), (std::int32_t)functionName.size());
#endif
		AppendPart(dest, length, " ‡ ");
	}
}

static void AppendMessageColor(char* dest, std::int32_t& length, TraceLevel level, bool resetBefore)
{
	if (resetBefore) {
		AppendPart(dest, length, ColorReset);
	}

	switch (level) {
		case TraceLevel::Error:
		case TraceLevel::Fatal:
			AppendPart(dest, length, ColorBrightRed);
			if (level == TraceLevel::Fatal) {
				AppendPart(dest, length, ColorBold);
			}
			break;
		case TraceLevel::Assert:
			AppendPart(dest, length, ColorBrightMagenta);
			break;
#if defined(DEATH_TARGET_EMSCRIPTEN)
		case TraceLevel::Info:
		case TraceLevel::Warning:
			AppendPart(dest, length, ColorBold);
			break;
#else
		case TraceLevel::Warning:
			AppendPart(dest, length, ColorBrightYellow);
			break;
		case TraceLevel::Debug:
		case TraceLevel::Deferred:
			AppendPart(dest, length, ColorDarkGray);
			break;
#endif
	}
}
#endif

// Shutdown handlers are needed even without `DEATH_TRACE`, e.g. for a dedicated server to exit cleanly
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)

static BOOL WINAPI OnHandleConsoleEvent(DWORD signal)
{
	if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
		signal == CTRL_CLOSE_EVENT || signal == CTRL_LOGOFF_EVENT ||
		signal == CTRL_SHUTDOWN_EVENT) {
		auto& app = nCine::theApplication();
		if (!app.ShouldQuit()) {
			LOGW("Received console close event to shut down the application");
			app.Quit();
			return TRUE;
		}
	}

	return FALSE;
}

static void InstallShutdownHandlers()
{
	// Harmless without an attached console
	::SetConsoleCtrlHandler(OnHandleConsoleEvent, TRUE);
}

#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <signal.h>

static void OnHandleTerminationSignal(int sig)
{
	if (sig != SIGINT && sig != SIGTERM) {
		return;
	}

	auto& app = nCine::theApplication();
	if (!app.ShouldQuit()) {
		if (sig == SIGTERM) {
			LOGW("Received termination signal to shut down the application");
		} else {
			LOGW("Received interrupt signal to shut down the application");
		}
		app.Quit();
		return;
	}

	// Second signal during shutdown terminates the process, so a hung shutdown can still be aborted
	::signal(sig, SIG_DFL);
	::raise(sig);
}

static void InstallShutdownHandlers()
{
	// SIGINT for Ctrl+C, SIGTERM for `kill`, service managers and containers
	::signal(SIGINT, OnHandleTerminationSignal);
	::signal(SIGTERM, OnHandleTerminationSignal);
}

#else

static void InstallShutdownHandlers()
{
	// Handled by the platform backends, if at all
}

#endif

namespace nCine
{
	Application::Application()
		: _isSuspended(false), _autoSuspension(false), _hasFocus(true), _shouldQuit(false)
#if defined(DEATH_TRACE)
			, _mainThreadId(Death::Trace::Implementation::GetNativeThreadId())
#endif
	{
	}

	Application::~Application() = default;

#if defined(WITH_IMGUI)
	Application::GuiSettings::GuiSettings()
		: ImguiLayer(0xffff - 1024), ImguiViewport(nullptr)
	{
	}
#endif

	Viewport& Application::GetScreenViewport()
	{
		return *_screenViewport;
	}

	std::uint32_t Application::GetFrameCount() const
	{
		return _frameTimer->GetTotalNumberFrames();
	}

	float Application::GetTimeMult() const
	{
		return _frameTimer->GetTimeMult();
	}

	const FrameTimer& Application::GetFrameTimer() const
	{
		return *_frameTimer;
	}

	void Application::ResizeScreenViewport(std::int32_t width, std::int32_t height)
	{
		if (_screenViewport != nullptr) {
			bool sizeChanged = (width != _screenViewport->_width || height != _screenViewport->_height);
			if (sizeChanged && width > 0 && height > 0) {
				_screenViewport->Resize(width, height);
				_appEventHandler->OnResizeWindow(width, height);
			}
		}
	}

	bool Application::ShouldSuspend()
	{
		return ((!_hasFocus && _autoSuspension) || _isSuspended);
	}

	void Application::Quit()
	{
		_shouldQuit = true;
	}

	void Application::PreInitCommon(std::unique_ptr<IAppEventHandler> appEventHandler)
	{
		// First, so shutdown requests during initialization are handled too
		InstallShutdownHandlers();

#if defined(DEATH_TRACE)
		InitializeTrace();
#endif

		_appEventHandler = Death::move(appEventHandler);
		_appEventHandler->OnPreInitialize(_appCfg);
		LOGB("IAppEventHandler::OnPreInitialize() invoked");

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// The log file is attached in OnPreInitialize() if at all, so the startup history is no longer needed
		DiscardLogHistory();
#endif
	}

	namespace
	{
		/**
			@brief Reports an allocation failure before the process dies

			Consoles are built with `-fno-exceptions`, so a failed allocation is a silent `abort()` that looks
			like a freeze. This at least logs what happened.
		*/
#if defined(DEATH_TARGET_N64)
		/** @brief Size of the failed allocation, set by the `operator new` replacement below */
		std::size_t FailedAllocationSize = 0;
#endif

		void ReportFailedAllocation()
		{
#if defined(DEATH_TARGET_3DS)
			// Must not return (it's called in a loop), log the fatal line first in case the heap status can't allocate
			LOGF("Out of memory: an allocation failed and the process cannot continue");
			Backends::CtrPlatform::LogMemoryStatus("allocation failed");
#elif defined(DEATH_TARGET_DREAMCAST)
			// Same as above, include the heap status
			LOGF("Out of memory: an allocation failed and the process cannot continue");
			Backends::DcPlatform::LogMemoryStatus("allocation failed");
#elif defined(DEATH_TARGET_N64)
			// Usually fragmentation rather than a full heap, so log the size and heap stats. Resolve the raw
			// backtrace with `mips64-elf-addr2line -f -C -e build/n64/jazz2.elf <pc>`.
			heap_stats_t stats;
			sys_get_heap_stats(&stats);
			LOGF("Out of memory: an allocation of {} bytes failed and the process cannot continue ({} bytes used, {} free of {}, {} free in holes, fragmentation {})",
				FailedAllocationSize, stats.used, stats.free, stats.total, stats.fragmented, stats.fragmentation);
			void* frames[24];
			const std::int32_t frameCount = backtrace(frames, std::int32_t(arraySize(frames)));
			for (std::int32_t i = 0; i < frameCount; i++) {
				LOGF("  #{} 0x{:x}", i, std::uintptr_t(frames[i]));
			}
#else
			// Must not return, it's called in a loop
			LOGF("Out of memory: an allocation failed and the process cannot continue");
#endif
			std::abort();
		}
	}
}

#if defined(DEATH_TARGET_N64)
// Replaced only to remember the failed size for ReportFailedAllocation(), default `operator delete` still matches
void* operator new(std::size_t size)
{
	void* p = std::malloc(size != 0 ? size : 1);
	if (p == nullptr) {
		nCine::FailedAllocationSize = size;
		nCine::ReportFailedAllocation();
	}
	return p;
}

void* operator new[](std::size_t size)
{
	void* p = std::malloc(size != 0 ? size : 1);
	if (p == nullptr) {
		nCine::FailedAllocationSize = size;
		nCine::ReportFailedAllocation();
	}
	return p;
}

// Nothrow forms must return null (deferred loads can recover), the library ones would call the aborting version above
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	return std::malloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
	return std::malloc(size != 0 ? size : 1);
}
#endif

namespace nCine
{
	void Application::InitCommon()
	{
		TracyGpuContext;
		ZoneScopedC(0x81A861);

		std::set_new_handler(ReportFailedAllocation);
		// This timestamp is needed to initialize random number generator
		_profileStartTime = TimeStamp::Now();

#if defined(WITH_TRACY)
		TracyAppInfo(NCINE_APP, sizeof(NCINE_APP) - 1);
		LOGW("Tracy integration is enabled");
#endif

		// Initialization of the static random generator seeds
		Random().Init(TimeStamp::Now().GetTicks(), _profileStartTime.GetTicks());

		_frameTimer = std::make_unique<FrameTimer>(_appCfg.FrameTimerLogInterval, 0.2f);
#if defined(DEATH_TARGET_WINDOWS)
		_waitableTimer = ::CreateWaitableTimerW(NULL, TRUE, NULL);
#endif

#if defined(WITH_AUDIO)
		if (_appCfg.WithAudio) {
#	if defined(WITH_OPENAL)
			theServiceLocator().RegisterAudioDevice(std::make_unique<ALAudioDevice>());
#	elif defined(WITH_ASND)
			theServiceLocator().RegisterAudioDevice(std::make_unique<AsndAudioDevice>());
#	elif defined(WITH_AICA)
			theServiceLocator().RegisterAudioDevice(std::make_unique<AicaAudioDevice>());
#	elif defined(WITH_N64AUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<N64AudioDevice>());
#	elif defined(WITH_PS3AUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<Ps3AudioDevice>());
#	elif defined(WITH_AHIAUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<AmigaAudioDevice>());
#	elif defined(WITH_SDLAUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<SdlAudioDevice>());
#	elif defined(WITH_PSPAUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<PspAudioDevice>());
#	elif defined(WITH_PS2AUDIO)
			theServiceLocator().RegisterAudioDevice(std::make_unique<Ps2AudioDevice>());
#	elif defined(WITH_NDSP)
			theServiceLocator().RegisterAudioDevice(std::make_unique<NdspAudioDevice>());
#	endif
		}
#endif
#if defined(WITH_THREADS)
		if (_appCfg.WithThreads) {
			theServiceLocator().RegisterThreadPool(std::make_unique<ThreadPool>());
		}
#endif

		if (_appCfg.WithGraphics) {
			theServiceLocator().RegisterRhiCapabilities(std::make_unique<RHI::Capabilities>());
			const auto& rhiCapabilities = theServiceLocator().GetRhiCapabilities();
			RHI::Debug::Init(rhiCapabilities);

#if !defined(WITH_ANGLE) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_WINDOWS_RT)
			if (_appCfg.FixedBatchSize > 0) {
				LOGI("Using fixed batch size: {}", _appCfg.FixedBatchSize);
			} else {
				const auto& info = rhiCapabilities.GetInfoStrings();
				const StringView vendor = info.vendor;
				const StringView renderer = info.renderer;
				// These GPUs render black with dynamic batch size
				if ((vendor == "Imagination Technologies"_s && (renderer == "PowerVR Rogue GE8300"_s || renderer == "PowerVR Rogue GE8320"_s || renderer == "PowerVR Rogue GE9215"_s)) ||
					(vendor == "ARM"_s && renderer == "Mali-T830"_s)) {
					const StringView vendorPrefix = vendor.findOr(' ', vendor.end());
					if DEATH_UNLIKELY(renderer.hasPrefix(vendor.prefix(vendorPrefix.begin()))) {
						LOGW("Detected {}: Using fixed batch size", renderer);
					} else {
						LOGW("Detected {} {}: Using fixed batch size", vendor, renderer);
					}
					_appCfg.FixedBatchSize = 10;
				}
			}
#endif

#if defined(WITH_RENDERDOC)
			RenderDocCapture::Init();
#endif

#if defined(WITH_THREADS) && defined(DEATH_TRACE)
			std::size_t stackSize, stackRemaining;
			if (Thread::GetCurrentStackInfo(stackSize, stackRemaining)) {
				LOGI("Current thread stack size: {}/{} bytes", stackSize - stackRemaining, stackSize);
			}
#endif

			LOGI("Creating rendering resources...");

			// Create a minimal set of render resources before compiling the first shader
			RenderResources::CreateMinimal(); // they are required for rendering even without a scenegraph

			if (_appCfg.WithScenegraph) {
				_gfxDevice->SetupDevice();
				RenderResources::Create();
				_rootNode = std::make_unique<SceneNode>();
				_screenViewport = std::make_unique<ScreenViewport>();
				_screenViewport->SetRootNode(_rootNode.get());
			}

#if defined(WITH_IMGUI)
			_imguiDrawing = std::make_unique<ImGuiDrawing>(_appCfg.WithScenegraph);

			// Debug overlay is available even when scenegraph is not
			if (_appCfg.WithDebugOverlay) {
				_debugOverlay = std::make_unique<ImGuiDebugOverlay>(0.5f);	// 2 updates per second
			}
#endif
		} else {
			// Create scenegraph even without graphics to update nodes properly
			if (_appCfg.WithScenegraph) {
				_rootNode = std::make_unique<SceneNode>();
				_screenViewport = std::make_unique<ScreenViewport>();
				_screenViewport->SetRootNode(_rootNode.get());
			}
		}

		LOGI("Core components initialized");
#if defined(NCINE_PROFILING)
		_timings[(std::int32_t)Timings::InitCommon] = _profileStartTime.GetSecondsSince();
#endif
		{
			ZoneScopedNC("OnInitialize", 0x81A861);
#if defined(NCINE_PROFILING)
			_profileStartTime = TimeStamp::Now();
#endif
			_appEventHandler->OnInitialize();
#if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::AppInit] = _profileStartTime.GetSecondsSince();
#endif
			LOGB("IAppEventHandler::OnInitialize() invoked");
		}

		if (_appCfg.WithGraphics) {
			// Swapping frame now for a cleaner API trace capture when debugging
			_gfxDevice->Update();
			FrameMark;
			TracyGpuCollect;
		}
	}

	void Application::Step()
	{
		_frameTimer->AddFrame();

		// Report the previous frame now that its full duration is known
		if (_frameMeasured) {
			const float phaseTimes[] = {
				_timings[(std::int32_t)Timings::BeginFrame], _timings[(std::int32_t)Timings::Update],
				_timings[(std::int32_t)Timings::PostUpdate], _timings[(std::int32_t)Timings::Visit],
				_timings[(std::int32_t)Timings::Draw], _timings[(std::int32_t)Timings::Audio],
				_timings[(std::int32_t)Timings::EndFrame], _timings[(std::int32_t)Timings::Present],
				_timings[(std::int32_t)Timings::Wait]
			};
			static_assert(arraySize(phaseTimes) == (std::size_t)FrameStatistics::Phase::Count, "Phase count mismatch");
			FrameStatistics::EndFrame(phaseTimes, _frameTimer->GetLastFrameDuration());
		}

		// Time the phases only if performance metrics or profiling need them
		_frameMeasured = FrameStatistics::IsEnabled();
#if defined(NCINE_PROFILING)
		const bool measure = true;
#else
		const bool measure = _frameMeasured;
#endif
		if (measure) {
			// Phases skipped in this frame must report zero
			for (std::int32_t i = (std::int32_t)Timings::BeginFrame; i < (std::int32_t)Timings::Count; i++) {
				_timings[i] = 0.0f;
			}
		}
#if defined(NCINE_PROFILING) && !defined(WITH_IMGUI)
		const TimeStamp stepStart = TimeStamp::Now();
#endif

#if defined(WITH_IMGUI)
		if (_appCfg.WithGraphics) {
			ZoneScopedN("ImGui newFrame");
#	if defined(NCINE_PROFILING)
			_profileStartTime = TimeStamp::Now();
#	endif
			_imguiDrawing->NewFrame();
#	if defined(NCINE_PROFILING)
			_timings[(std::int32_t)Timings::ImGui] = _profileStartTime.GetSecondsSince();
#	endif
		}
#endif
#if defined(WITH_LUA)
		LuaStatistics::update();
#endif

		{
			ZoneScopedNC("OnBeginFrame", 0x81A861);
			if (measure) {
				_profileStartTime = TimeStamp::Now();
			}
			_appEventHandler->OnBeginFrame();
			if (measure) {
				_timings[(std::int32_t)Timings::BeginFrame] = _profileStartTime.GetSecondsSince();
			}
		}

#if defined(WITH_IMGUI)
		if (_debugOverlay != nullptr) {
			_debugOverlay->Update();
		}
#endif

		if (_appCfg.WithScenegraph) {
			ZoneScopedNC("SceneGraph", 0x81A861);
			{
				ZoneScopedNC("Update", 0x81A861);
				if (measure) {
					_profileStartTime = TimeStamp::Now();
				}
				_screenViewport->Update();
				if (measure) {
					_timings[(std::int32_t)Timings::Update] = _profileStartTime.GetSecondsSince();
				}
			}

			{
				ZoneScopedNC("OnPostUpdate", 0x81A861);
				if (measure) {
					_profileStartTime = TimeStamp::Now();
				}
				_appEventHandler->OnPostUpdate();
				if (measure) {
					_timings[(std::int32_t)Timings::PostUpdate] = _profileStartTime.GetSecondsSince();
				}
			}

			if (_appCfg.WithGraphics) {
				{
					ZoneScopedNC("Visit", 0x81A861);
					if (measure) {
						_profileStartTime = TimeStamp::Now();
					}
					_screenViewport->Visit();
					if (measure) {
						_timings[(std::int32_t)Timings::Visit] = _profileStartTime.GetSecondsSince();
					}
				}

#if defined(WITH_IMGUI)
				{
					ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
					_profileStartTime = TimeStamp::Now();
#	endif
					RenderQueue& imguiRenderQueue = (_guiSettings.ImguiViewport ? _guiSettings.ImguiViewport->_renderQueue : _screenViewport->_renderQueue);
					_imguiDrawing->EndFrame(imguiRenderQueue);
#	if defined(NCINE_PROFILING)
					_timings[(std::int32_t)Timings::ImGui] += _profileStartTime.GetSecondsSince();
#	endif
				}
#endif

				{
					ZoneScopedNC("Draw", 0x81A861);
					if (measure) {
						_profileStartTime = TimeStamp::Now();
					}
					_screenViewport->SortAndCommitQueue();
					_screenViewport->Draw();
#if defined(NCINE_HAS_AUXILIARY_WINDOWS)
					// Auxiliary windows are drawn with the frame
					_gfxDevice->PresentAuxiliaryWindows();
#endif
					if (measure) {
						_timings[(std::int32_t)Timings::Draw] = _profileStartTime.GetSecondsSince();
					}
				}
			}
		} else {
#if defined(WITH_IMGUI)
			if (_appCfg.WithGraphics) {
				ZoneScopedN("ImGui endFrame");
#	if defined(NCINE_PROFILING)
				_profileStartTime = TimeStamp::Now();
#	endif
				_imguiDrawing->EndFrame();
#	if defined(NCINE_PROFILING)
				_timings[(std::int32_t)Timings::ImGui] += _profileStartTime.GetSecondsSince();
#	endif
			}
#endif
		}

		{
			if (measure) {
				_profileStartTime = TimeStamp::Now();
			}
			theServiceLocator().GetAudioDevice().UpdatePlayers();
			if (measure) {
				_timings[(std::int32_t)Timings::Audio] = _profileStartTime.GetSecondsSince();
			}
		}

		{
			ZoneScopedNC("OnFrameEnd", 0x81A861);
			if (measure) {
				_profileStartTime = TimeStamp::Now();
			}
			_appEventHandler->OnEndFrame();
			if (measure) {
				_timings[(std::int32_t)Timings::EndFrame] = _profileStartTime.GetSecondsSince();
			}
		}

#if defined(WITH_IMGUI)
		if (_debugOverlay != nullptr) {
			_debugOverlay->UpdateFrameTimings();
		}
#endif

		if (_appCfg.WithGraphics) {
			if (measure) {
				_profileStartTime = TimeStamp::Now();
			}
			_gfxDevice->Update();
			// Wait for the next ring section after the swap, so the GPU had time to finish with it
			RenderResources::GetBuffersManager().WaitForCurrentSection();
			if (measure) {
				_timings[(std::int32_t)Timings::Present] = _profileStartTime.GetSecondsSince();
			}
			FrameMark;
			TracyGpuCollect;
		}
#if defined(NCINE_PROFILING) && !defined(WITH_IMGUI)
		{
			// Without the overlay, log per-phase averages every 5 seconds
			for (std::int32_t i = 0; i < (std::int32_t)Timings::Count; i++) {
				_timingsAccum[i] += _timings[i];
			}
			_stepAccum += stepStart.GetSecondsSince();
			_timingsFrames++;
			if (_timingsLogStart.GetSecondsSince() >= 5.0f) {
				if (_timingsFrames > 0) {
					const float toMs = 1000.0f / (float)_timingsFrames;
					LOGI("Frame profile over {} frames: {:.2f} ms/frame - begin {:.2f}, update {:.2f}, post-update {:.2f}, visit {:.2f}, draw {:.2f}, audio {:.2f}, end {:.2f}, present {:.2f}",
						_timingsFrames, _stepAccum * toMs, _timingsAccum[(std::int32_t)Timings::BeginFrame] * toMs,
						_timingsAccum[(std::int32_t)Timings::Update] * toMs, _timingsAccum[(std::int32_t)Timings::PostUpdate] * toMs,
						_timingsAccum[(std::int32_t)Timings::Visit] * toMs, _timingsAccum[(std::int32_t)Timings::Draw] * toMs,
						_timingsAccum[(std::int32_t)Timings::Audio] * toMs, _timingsAccum[(std::int32_t)Timings::EndFrame] * toMs,
						_timingsAccum[(std::int32_t)Timings::Present] * toMs);
				}
				for (std::int32_t i = 0; i < (std::int32_t)Timings::Count; i++) {
					_timingsAccum[i] = 0.0f;
				}
				_stepAccum = 0.0f;
				_timingsFrames = 0;
				_timingsLogStart = TimeStamp::Now();
			}
		}
#endif

		if (_appCfg.FrameLimit > 0) {
			FrameMarkStart("Frame limiting");
			if (measure) {
				_profileStartTime = TimeStamp::Now();
			}
			const std::int64_t frameTimeDuration = clock().GetFrequency() / _appCfg.FrameLimit;

#if defined(DEATH_TARGET_WINDOWS)
			// It can wait longer than necessary, so subtract 1 ms to compensate
			const std::int64_t remainingTime100ns = ((((std::int64_t)frameTimeDuration - (std::int64_t)_frameTimer->GetFrameDurationAsTicks())
				* 10'000'000LL) / (std::int64_t)clock().GetFrequency()) - 10'000; // 1 ms
			if (remainingTime100ns > 0) {
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -remainingTime100ns;

				::SetWaitableTimer(_waitableTimer, &dueTime, 0, NULL, NULL, FALSE);
				::WaitForSingleObject(_waitableTimer, 1000);
				::CancelWaitableTimer(_waitableTimer);
			}
#elif defined(DEATH_TARGET_APPLE)
			// It can wait longer than necessary, so subtract 0.5 ms to compensate
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)_appCfg.FrameLimit) -
				((std::int64_t)_frameTimer->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().GetFrequency()) - 500'000LL;
			if (remainingTimeNs > 0) {
				timespec dueTime{};
				dueTime.tv_nsec += remainingTimeNs;
				if (dueTime.tv_nsec >= 1'000'000'000L) {
					dueTime.tv_sec += dueTime.tv_nsec / 1'000'000'000L;
					dueTime.tv_nsec %= 1'000'000'000L;
				}
				nanosleep(&dueTime, &dueTime);
			}
#elif defined(DEATH_TARGET_UNIX)
			// It can wait longer than necessary, so subtract 0.5 ms to compensate
			const std::int64_t remainingTimeNs = (1'000'000'000LL / (std::int64_t)_appCfg.FrameLimit) -
				((std::int64_t)_frameTimer->GetFrameDurationAsTicks() * 1'000'000'000LL / (std::int64_t)clock().GetFrequency()) - 500'000LL;
			if (remainingTimeNs > 0) {
				timespec dueTime;
				clock_gettime(CLOCK_MONOTONIC, &dueTime);
				dueTime.tv_nsec += remainingTimeNs;
				if (dueTime.tv_nsec >= 1'000'000'000L) {
					dueTime.tv_sec += dueTime.tv_nsec / 1'000'000'000L;
					dueTime.tv_nsec %= 1'000'000'000L;
				}
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &dueTime, nullptr);
			}
#else
			// Coarse sleep for most of the wait, the loop below spins only for the rest
			const std::int64_t remainingTimeMs = (((std::int64_t)frameTimeDuration - (std::int64_t)_frameTimer->GetFrameDurationAsTicks())
				* 1'000LL) / (std::int64_t)clock().GetFrequency() - 2; // 2 ms slack for coarse sleep granularity
			if (remainingTimeMs > 0) {
				Thread::Sleep((std::uint32_t)remainingTimeMs);
			}
#endif

			while ((std::int64_t)_frameTimer->GetFrameDurationAsTicks() < frameTimeDuration) {
				Thread::Sleep(0);
			}
			if (measure) {
				_timings[(std::int32_t)Timings::Wait] = _profileStartTime.GetSecondsSince();
			}
			FrameMarkEnd("Frame limiting");
		}
	}

	void Application::ShutdownCommon()
	{
		ZoneScopedC(0x81A861);
		_appEventHandler->OnShutdown();
		LOGI("IAppEventHandler::OnShutdown() invoked");
		_appEventHandler = nullptr;

		_rootNode = nullptr;

		if (_appCfg.WithGraphics) {
#if defined(WITH_IMGUI)
			_imguiDrawing = nullptr;
			_debugOverlay = nullptr;
#endif
#if defined(WITH_RENDERDOC)
			RenderDocCapture::RemoveHooks();
#endif
			RenderResources::Dispose();
			_gfxDevice = nullptr;
		}

		_frameTimer = nullptr;
		_inputManager = nullptr;

#if defined(DEATH_TARGET_WINDOWS)
		::CloseHandle(_waitableTimer);
#endif

		LOGI("Application is shutting down");

		theServiceLocator().UnregisterAll();

#if defined(DEATH_TARGET_PSP) && defined(WITH_CURL)
		// Before the trace is shut down, so a crash here (seen on Vita's PSP emulator) is still logged
		Backends::PspNetwork::Shutdown();
#endif

#if defined(DEATH_TRACE)
		ShutdownTrace();
#endif
	}

	void Application::SetFocus(bool hasFocus)
	{
#if defined(WITH_TRACY) && !defined(DEATH_TARGET_ANDROID)
		hasFocus = true;
#endif

		_hasFocus = hasFocus;
	}

#if defined(DEATH_TRACE)
#	if !defined(DEATH_TRACE_ASYNC) && defined(WITH_THREADS)
	namespace
	{
		/**
			@brief Serializes the trace sink when entries are emitted on the calling thread

			Without `DEATH_TRACE_ASYNC` (all consoles) each thread writes its own entries, which could
			interleave lines and corrupt the log file. The lock covers the whole write.
		*/
		Mutex& TraceSinkLock()
		{
			static Mutex lock;
			return lock;
		}

		class TraceSinkGuard
		{
		public:
			TraceSinkGuard() {
				TraceSinkLock().Lock();
			}
			~TraceSinkGuard() {
				TraceSinkLock().Unlock();
			}

			TraceSinkGuard(const TraceSinkGuard&) = delete;
			TraceSinkGuard& operator=(const TraceSinkGuard&) = delete;
		};
	}
#	endif

	void Application::OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId, StringView functionName, StringView content)
	{
#	if !defined(DEATH_TRACE_ASYNC) && defined(WITH_THREADS)
		TraceSinkGuard sinkGuard;
#	endif

		char logEntryWithColors[MaxLogEntryLength + 24];

#if defined(DEATH_TARGET_ANDROID)
		std::int32_t length2 = 0;
		// Provide actual timestamps even though Android includes its own
		AppendDateTime(logEntryWithColors, length2, timestamp);
		logEntryWithColors[length2++] = ' ';
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());

		android_LogPriority priority;
		switch (level) {
			case TraceLevel::Fatal:		priority = ANDROID_LOG_FATAL; break;
			case TraceLevel::Assert:	// Android doesn't support this priority, use ANDROID_LOG_ERROR instead
			case TraceLevel::Error:		priority = ANDROID_LOG_ERROR; break;
			case TraceLevel::Warning:	priority = ANDROID_LOG_WARN; break;
			case TraceLevel::Info:		priority = ANDROID_LOG_INFO; break;
			default:					priority = ANDROID_LOG_DEBUG; break;
		}

		std::int32_t result = __android_log_write(priority, NCINE_APP, logEntryWithColors);
		std::int32_t n = 0;
		while (result == -11 /*EAGAIN*/ && n < 2) {
			::usleep(2000); // 2ms in microseconds
			result = __android_log_write(priority, NCINE_APP, logEntryWithColors);
			n++;
		}
#elif defined(DEATH_TARGET_SWITCH)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		svcOutputDebugString(logEntryWithColors, length2);
#elif defined(DEATH_TARGET_N64)
		// fd 2 goes to the emulator log (Ares), flashcart USB log and the boot console. Unbuffered write()
		// instead of `stderr`, so the last messages before a hang aren't lost and the stdio lock is avoided.
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		::write(STDERR_FILENO, logEntryWithColors, length2);
#elif defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_WII)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';

		// Boot console (no-op once OgcGfxDevice owns the video output)
		::fwrite(logEntryWithColors, 1, length2, stdout);

		// USB Gecko in slot B, Dolphin exposes it on TCP port 55020 ("SlotB = 7" in Dolphin.ini)
		static const bool __geckoAlive = (usb_isgeckoalive(EXI_CHANNEL_1) != 0);
		if (__geckoAlive) {
			usb_sendbuffer_safe(EXI_CHANNEL_1, logEntryWithColors, length2);
		}
#elif defined(DEATH_TARGET_WIIU)
		// OSReport() goes to Cemu's log and Aroma logging modules. Cemu logs only the format string, so the entry
		// is passed as the format with '%' escaped.
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		char reportEntry[MaxLogEntryLength];
		std::int32_t reportLength = 0;
		for (std::int32_t i = 0; i < length2 && reportLength < MaxLogEntryLength - 3; i++) {
			if (logEntryWithColors[i] == '%') {
				reportEntry[reportLength++] = '%';
			}
			reportEntry[reportLength++] = logEntryWithColors[i];
		}
		reportEntry[reportLength++] = '\n';
		reportEntry[reportLength] = '\0';
		OSReport(reportEntry);
#elif defined(DEATH_TARGET_3DS)
		// Kernel debug channel, shown by Azahar/Citra under "Debug.Emulated" (hidden by default) or a debugger.
		// Also on the bottom-screen boot console, after startup only warnings and errors (it's CPU-rendered).
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		svcOutputDebugString(logEntryWithColors, length2);
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		Backends::CtrPlatform::WriteBootConsole(logEntryWithColors, length2, level >= TraceLevel::Warning);
#elif defined(DEATH_TARGET_DREAMCAST)
		// dbgio (SCIF serial), shown by Flycast/lxdream and dc-tool/dcload
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		dbgio_write_buffer_xlat(reinterpret_cast<const std::uint8_t*>(logEntryWithColors), length2);
#elif defined(DEATH_TARGET_PS2)
		// stdout works only via ps2client, so write to the EE SIO register (PCSX2 "EnableEEConsole", or serial cable)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		{
			volatile std::uint8_t* const sioTx = reinterpret_cast<volatile std::uint8_t*>(0x1000F180);
			for (std::int32_t i = 0; i < length2; i++) {
				*sioTx = std::uint8_t(logEntryWithColors[i]);
			}
		}
		// Also on the boot console, until the renderer takes over the GS
		if (!RHI::Device::HasDisplayOwnership()) {
			logEntryWithColors[length2] = '\0';
			scr_printf("%s", logEntryWithColors);
		}
#elif defined(DEATH_TARGET_PS3)
		// TTY, shown by RPCS3 or on hardware. Direct syscall to avoid the stdio lock.
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		{
			std::uint32_t written = 0;
			sysTtyWrite(STDOUT_FILENO, logEntryWithColors, std::uint32_t(length2), &written);
		}
#elif defined(DEATH_TARGET_PSP)
		// stdout, shown by PPSSPP and psplink
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::fwrite(logEntryWithColors, 1, length2, stdout);
		::fflush(stdout);
		// Also on screen, until the renderer takes over the framebuffer
		if (!RHI::Device::HasDisplayOwnership()) {
			pspDebugScreenPrintf("%s", logEntryWithColors);
		}
#elif defined(DEATH_TARGET_VITA)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		logEntryWithColors[length2] = '\0';
		sceClibPrintf("%s", logEntryWithColors);
#elif defined(DEATH_TARGET_AMIGAOS) || defined(DEATH_TARGET_MORPHOS)
		std::int32_t length2 = 0;
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';

		// Unbuffered, so the last line before a crash isn't lost
		::write(STDOUT_FILENO, logEntryWithColors, length2);
#elif defined(DEATH_TARGET_WINDOWS_RT)
		// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
		std::int32_t length2 = 0;
		AppendDateTime(logEntryWithColors, length2, timestamp);
		logEntryWithColors[length2++] = ' ';
		AppendLevel(logEntryWithColors, length2, level, threadId);
		AppendFunctionName(logEntryWithColors, length2, functionName);
		AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
		if (length2 >= MaxLogEntryLength - 2) {
			length2 = MaxLogEntryLength - 2;
		}
		logEntryWithColors[length2++] = '\n';
		logEntryWithColors[length2] = '\0';
		::OutputDebugStringA(logEntryWithColors);
#elif !defined(DEATH_TRACE_LOG_PATH)	// Log to stdout only if file logging is not force enabled
#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		if (__consoleType >= ConsoleType::Redirect) {
#	endif
			// Colorize the output
			std::int32_t length2 = 0;
			AppendMessagePrefixIfAny(logEntryWithColors, length2, level, functionName);

			if (__consoleType >= ConsoleType::EscapeCodes) {
#	if defined(DEATH_TARGET_EMSCRIPTEN)
				bool shouldResetBefore = (level != TraceLevel::Warning && level != TraceLevel::Debug && level != TraceLevel::Deferred);
#	else
				bool shouldResetBefore = true;
#	endif
				bool shouldResetAfter = (level == TraceLevel::Debug || level == TraceLevel::Deferred || level == TraceLevel::Warning || level == TraceLevel::Error || level == TraceLevel::Assert || level == TraceLevel::Fatal);

				if (level < TraceLevel::Error && __consoleType >= ConsoleType::EscapeCodes24bit) {
					std::int32_t prevState = 0;
					StringView contentPart = content;
					do {
						StringView quotesBegin = contentPart.find('"');
						if (!quotesBegin) {
							break;
						}
						StringView quotesEnd = contentPart.suffix(quotesBegin.end()).find('"');
						if (!quotesEnd) {
							break;
						}

						StringView prefix = contentPart.prefix(quotesBegin.begin());
						if (!prefix.empty()) {
							AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
							shouldResetBefore = false;
							prevState = 1;

							AppendPart(logEntryWithColors, length2, prefix.data(), (std::int32_t)prefix.size());
						}

						if (prevState != 2) {
							if (level == TraceLevel::Debug || level == TraceLevel::Deferred) {
								AppendPart(logEntryWithColors, length2, ColorDimString);
							} else if (__consoleDarkMode) {
								AppendPart(logEntryWithColors, length2, ColorDarkString);
							} else {
								AppendPart(logEntryWithColors, length2, ColorLightString);
							}
							prevState = 2;
						}

						StringView inner = contentPart.suffix(quotesBegin.begin()).prefix(quotesEnd.end());
						AppendPart(logEntryWithColors, length2, inner.data(), (std::int32_t)inner.size());

						contentPart = contentPart.suffix(quotesEnd.end());
					} while (!contentPart.empty());

					if (!contentPart.empty()) {
						AppendMessageColor(logEntryWithColors, length2, level, prevState == 2 || shouldResetBefore);
						AppendPart(logEntryWithColors, length2, contentPart.data(), (std::int32_t)contentPart.size());
					} else if (prevState == 2) {
						// Always reset color after quotes
						shouldResetAfter = true;
					}
				} else {
					AppendMessageColor(logEntryWithColors, length2, level, shouldResetBefore);
					AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
				}

				if (shouldResetAfter) {
					AppendPart(logEntryWithColors, length2, ColorReset);
				}
			} else {
				AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
			}

			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
			}

#	if defined(DEATH_TARGET_WINDOWS)
			// Try to restore previous cursor position (this doesn't work correctly in Windows Terminal v1.19)
			if (__consoleHandleOut != NULL) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					if (__consoleCursorY <= csbi.dwCursorPosition.Y) {
						::SetConsoleCursorPosition(__consoleHandleOut, { 0, __consoleCursorY });
					}
				}
			}
			if (__consoleType >= ConsoleType::EscapeCodes && length2 < MaxLogEntryLength) {
				// Console can be shared with the parent process, so clear the rest of the line (using "\x1b[0K" sequence)
				logEntryWithColors[length2++] = '\x1b';
				logEntryWithColors[length2++] = '[';
				logEntryWithColors[length2++] = '0';
				logEntryWithColors[length2++] = 'K';
			}

			logEntryWithColors[length2++] = '\n';
			::fwrite(logEntryWithColors, 1, length2, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);

			// Save the last cursor position for later
			if (__consoleHandleOut != NULL) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
					__consoleCursorY = csbi.dwCursorPosition.Y;
				}
			}
#	else
			logEntryWithColors[length2++] = '\n';
			::fwrite(logEntryWithColors, 1, length2, level == TraceLevel::Error || level == TraceLevel::Fatal ? stderr : stdout);
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_DEBUG)
		} else {
			// Use OutputDebugStringA() to avoid conversion UTF-8 => UTF-16 => current code page
			std::int32_t length2 = 0;
			AppendDateTime(logEntryWithColors, length2, timestamp);
			logEntryWithColors[length2++] = ' ';
			AppendLevel(logEntryWithColors, length2, level, threadId);
			AppendFunctionName(logEntryWithColors, length2, functionName);
			AppendPart(logEntryWithColors, length2, content.data(), (std::int32_t)content.size());
			if (length2 >= MaxLogEntryLength - 2) {
				length2 = MaxLogEntryLength - 2;
			}
			logEntryWithColors[length2++] = '\n';
			logEntryWithColors[length2] = '\0';
			::OutputDebugStringA(logEntryWithColors);
		}
#	endif
#endif

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		// The startup history is closed elsewhere, but only the sink frees it
		LogHistoryState historyState = __logHistoryState.load(std::memory_order_acquire);
		if DEATH_UNLIKELY(historyState == LogHistoryState::Closed) {
			if (__logHistoryState.compare_exchange_strong(historyState, LogHistoryState::Released,
					std::memory_order_acq_rel, std::memory_order_acquire)) {
				__logHistory = {};
				__logHistoryLength.store(0, std::memory_order_relaxed);
				historyState = LogHistoryState::Released;
			}
		}

		// Write to the target from AttachTraceTarget(), or to the startup history until there is one.
		// Loaded once, so the destination can't change mid-entry.
		Stream* logFile = __logFile.load(std::memory_order_acquire);
		if (logFile != nullptr || historyState == LogHistoryState::Open) {
			std::int32_t length3 = 0;
			static std::atomic<std::int32_t> logFileLastDay{-1};
			AppendDateTime(logEntryWithColors, length3, timestamp, &logFileLastDay);
			logEntryWithColors[length3++] = ' ';
			AppendLevel(logEntryWithColors, length3, level, threadId);
			AppendFunctionName(logEntryWithColors, length3, functionName);
			AppendPart(logEntryWithColors, length3, content.data(), (std::int32_t)content.size());
			logEntryWithColors[length3++] = '\n';

			if (logFile != nullptr) {
				logFile->Write(logEntryWithColors, length3);
#	if defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_WIIU) || defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PS2) || \
		defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA)
				// The log file is the main debugging channel here and a hard kill loses the buffer. Flushing is slow
				// (8-13 ms on a Vita card), so flush Info and above immediately, Debug at most every FlushIntervalMs.
				// 32-bit milliseconds, because devkitPPC has no 64-bit atomics.
				constexpr std::uint32_t FlushIntervalMs = 250;
				static std::atomic<std::uint32_t> lastFlushMs{0};
				const std::uint32_t nowMs = (std::uint32_t)(timestamp / 1000000);
				if (level >= TraceLevel::Info) {
					lastFlushMs.store(nowMs, std::memory_order_relaxed);
					logFile->Flush();
				} else {
					std::uint32_t lastFlush = lastFlushMs.load(std::memory_order_relaxed);
					// A wrap only ever flushes early
					if (nowMs - lastFlush >= FlushIntervalMs &&
						lastFlushMs.compare_exchange_strong(lastFlush, nowMs, std::memory_order_relaxed)) {
						logFile->Flush();
					}
				}
#	endif
			} else {
				AppendLogHistory(logEntryWithColors, std::uint32_t(length3));
			}
		}
#endif

#if defined(WITH_IMGUI)
		auto* debugOverlay = theApplication()._debugOverlay.get();
		if (debugOverlay != nullptr) {
			std::int32_t length3 = 0;
			AppendDateTime(logEntryWithColors, length3, timestamp);
			debugOverlay->Log(level, { logEntryWithColors, (std::size_t)length3 }, threadId, functionName, content);
		}
#endif

#if defined(WITH_TRACY)
		std::uint32_t colorTracy;
		switch (level) {
			case TraceLevel::Fatal:		colorTracy = 0xEC3E40; break;
			case TraceLevel::Assert:	colorTracy = 0xD651B0; break;
			case TraceLevel::Error:		colorTracy = 0xD85050; break;
			case TraceLevel::Warning:	colorTracy = 0xEBC77A; break;
			case TraceLevel::Info:		colorTracy = 0xD2D2D2; break;
			default:					colorTracy = 0x969696; break;
		}

		TracyMessageC(content.data(), content.size(), colorTracy);
#endif
	}

	void Application::OnTraceFlushed()
	{
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (Stream* logFile = __logFile.load(std::memory_order_acquire)) {
			logFile->Flush();
		}
#	endif
	}
#endif

	void Application::Suspend()
	{
		_frameTimer->Suspend();
		if (_appEventHandler != nullptr) {
			_appEventHandler->OnSuspend();
		}
#if defined(WITH_AUDIO)
		if (_appCfg.WithAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.SuspendDevice();
		}
#endif

		LOGI("IAppEventHandler::OnSuspend() invoked");
	}

	void Application::Resume()
	{
		if (_appEventHandler != nullptr) {
			_appEventHandler->OnResume();
		}
#if defined(WITH_AUDIO)
		if (_appCfg.WithAudio) {
			IAudioDevice& audioDevice = theServiceLocator().GetAudioDevice();
			audioDevice.ResumeDevice();
		}
#endif

		DEATH_UNUSED TimeStamp suspensionDuration = _frameTimer->Resume();
		LOGD("Suspended for {:.3} seconds", suspensionDuration.GetSeconds());
#if defined(NCINE_PROFILING)
		_profileStartTime += suspensionDuration;
#endif
		LOGI("IAppEventHandler::OnResume() invoked");
	}

	bool Application::EnablePlayStationExtendedSupport(bool enable)
	{
		// Not implemented in base class
		return false;
	}

	String Application::GetUserName()
	{
		// Not implemented in base class
		return {};
	}

#if defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PS3)
	// Formats bytes as uppercase hex with an optional separator, all zeros or all ones (blank storage) returns empty
	static String FormatIdentifierBytes(const std::uint8_t* bytes, std::size_t count, char separator)
	{
		bool allZeros = true, allOnes = true;
		for (std::size_t i = 0; i < count; i++) {
			if (bytes[i] != 0x00) {
				allZeros = false;
			}
			if (bytes[i] != 0xFF) {
				allOnes = false;
			}
		}
		if (count == 0 || allZeros || allOnes) {
			return {};
		}

		static const char Digits[] = "0123456789ABCDEF";
		String result(NoInit, count * 2 + (separator != '\0' ? count - 1 : 0));
		char* dst = result.data();
		for (std::size_t i = 0; i < count; i++) {
			if (i > 0 && separator != '\0') {
				*dst++ = separator;
			}
			*dst++ = Digits[bytes[i] >> 4];
			*dst++ = Digits[bytes[i] & 0x0F];
		}
		return result;
	}
#endif

#if defined(DEATH_TARGET_PS3)
	// PSL1GHT has no wrapper for `sys_ss_get_open_psid` (lv2 syscall 872), the macros need their own function
	static s32 Ps3GetOpenPsId(std::uint8_t (&psid)[16])
	{
		lv2syscall1(872, (u64)(std::uintptr_t)psid);
		return_to_user_prog(s32);
	}
#endif

	String Application::GetDeviceHostname()
	{
#if defined(DEATH_TARGET_WINDOWS)
		wchar_t hostNameW[128]; DWORD hostNameWLength = (DWORD)arraySize(hostNameW);
		if (::GetComputerNameW(hostNameW, &hostNameWLength) && hostNameWLength > 0) {
			return Utf8::FromUtf16(hostNameW, hostNameWLength);
		}
		return {};
#elif defined(DEATH_TARGET_ANDROID)
		// No host name, use the Android ID instead
		return Backends::AndroidJniWrap_Secure::GetAndroidId();
#elif defined(DEATH_TARGET_SWITCH)
		// Console nickname, or serial number if empty (libnx gethostname() returns the IP address)
		String result;
		if (R_SUCCEEDED(setsysInitialize())) {
			SetSysDeviceNickName nickname {};
			if (R_SUCCEEDED(setsysGetDeviceNickname(&nickname))) {
				nickname.nickname[sizeof(nickname.nickname) - 1] = '\0';
				result = nickname.nickname;
			}
			if (result.empty()) {
				// Not null-terminated if full
				SetSysSerialNumber serialNumber {};
				if (R_SUCCEEDED(setsysGetSerialNumber(&serialNumber))) {
					std::size_t length = 0;
					while (length < sizeof(serialNumber.number) && serialNumber.number[length] != '\0') {
						length++;
					}
					result = StringView(serialNumber.number, length);
				}
			}
			setsysExit();
		}
		return result;
#elif defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_N64)
		// No readable identifier on these consoles
		return {};
#elif defined(DEATH_TARGET_WII)
		// Console nickname (printable ASCII only), or ES device ID if empty
		String result;
		if (CONF_Init() >= 0) {
			u8 nickname[32] {};
			if (CONF_GetNickName(nickname) >= 0) {
				char sanitized[sizeof(nickname)]; std::size_t length = 0;
				for (std::size_t i = 0; i < sizeof(nickname) - 1 && nickname[i] != '\0'; i++) {
					if (nickname[i] >= 0x20 && nickname[i] < 0x7F) {
						sanitized[length++] = (char)nickname[i];
					}
				}
				result = StringView(sanitized, length).trimmed();
			}
		}
		if (result.empty()) {
			u32 deviceId = 0;
			if (ES_GetDeviceID(&deviceId) >= 0 && deviceId != 0) {
				result = format("{:.8x}", deviceId);
			}
		}
		return result;
#elif defined(DEATH_TARGET_WIIU)
		// No console nickname, use the serial number (e.g. "FW123456789")
		String result;
		std::int32_t mcpHandle = MCP_Open();
		if (mcpHandle >= 0) {
			// IPC buffer must be 64-byte aligned
			alignas(0x40) MCPSysProdSettings settings {};
			if (MCP_GetSysProdSettings(mcpHandle, &settings) >= 0) {
				char serial[sizeof(settings.code_id) + sizeof(settings.serial_id)]; std::size_t length = 0;
				auto appendField = [&serial, &length](const char* field, std::size_t size) {
					for (std::size_t i = 0; i < size && field[i] != '\0'; i++) {
						if (field[i] >= 0x20 && field[i] < 0x7F) {
							serial[length++] = field[i];
						}
					}
				};
				appendField(settings.code_id, sizeof(settings.code_id));
				appendField(settings.serial_id, sizeof(settings.serial_id));
				result = StringView(serial, length).trimmed();
			}
			MCP_Close(mcpHandle);
		}
		return result;
#elif defined(DEATH_TARGET_3DS)
		// Console user name, or unique console ID if empty
		return Backends::CtrPlatform::GetDeviceName();
#elif defined(DEATH_TARGET_DREAMCAST)
		// 64-bit BIOS system ID (KOS already initialized SYSINFO), bytes in flashrom order
		std::uint64_t systemId = syscall_sysinfo_id();
		std::uint8_t id[8];
		for (std::size_t i = 0; i < sizeof(id); i++) {
			id[i] = (std::uint8_t)(systemId >> (i * 8));
		}
		return FormatIdentifierBytes(id, sizeof(id), '\0');
#elif defined(DEATH_TARGET_PS2)
		// i.Link ID (every console has one), sceCdRI() returns 1 on success
		u8 id[8] {}; u32 status = 0;
		if (sceCdRI(id, &status) == 1) {
			return FormatIdentifierBytes(id, sizeof(id), '\0');
		}
		return {};
#elif defined(DEATH_TARGET_PS3)
		// OpenPSID
		std::uint8_t psid[16] {};
		if (Ps3GetOpenPsId(psid) == 0) {
			return FormatIdentifierBytes(psid, sizeof(psid), '\0');
		}
		return {};
#elif defined(DEATH_TARGET_PSP)
		// WLAN MAC address from flash (works with WLAN off), the call needs an 8-byte buffer for 6 bytes
		std::uint8_t mac[8] {};
		if (sceWlanGetEtherAddr(mac) >= 0) {
			return FormatIdentifierBytes(mac, 6, ':');
		}
		return {};
#elif defined(DEATH_TARGET_VITA)
		// OpenPSID
		SceKernelOpenPsId psid {};
		if (sceKernelGetOpenPsId(&psid) >= 0) {
			// Declared as `char`, but it's binary
			return FormatIdentifierBytes(reinterpret_cast<const std::uint8_t*>(psid.id), sizeof(psid.id), '\0');
		}
		return {};
#elif !defined(DEATH_TARGET_MORPHOS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Not on MorphOS (would require a TCP/IP stack to start) or the web (returns a constant)
		char hostName[256] {};
		if (::gethostname(hostName, sizeof(hostName)) == 0) {
			hostName[sizeof(hostName) - 1] = '\0';
			return hostName;
		}
		return {};
#else
		return {};
#endif
	}

	bool Application::OpenUrl(StringView url)
	{
		// Not implemented in base class
		return false;
	}
	
	bool Application::CanShowScreenKeyboard()
	{
		return false;
	}

	bool Application::IsScreenKeyboardVisible()
	{
		return false;
	}

	std::int32_t Application::GetScreenKeyboardInset()
	{
		return 0;
	}

	bool Application::ToggleScreenKeyboard()
	{
		return false;
	}
	
	bool Application::ShowScreenKeyboard(Containers::StringView initialText, Containers::Function<void(Containers::StringView)>&& onCompleted)
	{
		return false;
	}
	
	bool Application::HideScreenKeyboard()
	{
		return false;
	}

	void Application::Vibrate(std::int32_t milliseconds)
	{
		// Not implemented in base class
	}

	void Application::ShowStatusBar()
	{
		// Not implemented in base class
	}

	void Application::HideStatusBar()
	{
		// Not implemented in base class
	}

	void Application::AttachTraceTarget(Containers::StringView targetPath, DEATH_UNUSED bool archivePrevious)
	{
#if defined(DEATH_TRACE) && defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (targetPath == ConsoleTarget) {
			if (__consoleType == ConsoleType::None) {
				bool hasVirtualTerminal = false;
				if (CreateTraceConsole(NCINE_APP_NAME, hasVirtualTerminal)) {
					__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);

					CONSOLE_SCREEN_BUFFER_INFO csbi;
					__consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
					if (::GetConsoleScreenBufferInfo(__consoleHandleOut, &csbi)) {
						__consoleCursorY = csbi.dwCursorPosition.Y;
					} else {
						__consoleHandleOut = NULL;
					}

					if (hasVirtualTerminal) {
						CheckConsoleCapabilities();
					}
				} else {
					__consoleType = ConsoleType::Redirect;
				}

#	if defined(WITH_BACKWARD)
				if (__consoleType >= ConsoleType::EscapeCodes) {
					__eh.FeatureFlags |= Backward::Flags::Colorized;
				}
#	endif
			}
			return;
		}
#endif

#if defined(DEATH_TRACE) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	if defined(NCINE_HAS_LOG_ARCHIVE)
		// Must be done before the file is opened for writing
		if (archivePrevious) {
			ArchivePreviousLogFile(targetPath);
		}
#	endif

		auto logFile = fs::Open(targetPath, FileAccess::Write);
		if (logFile->IsValid()) {
			// The sink gets the file only at the end, so nothing can be written before the header
			AppendLogFileHeader(*logFile);

			// Entries logged before the file was opened go right after the header
			if (__logHistoryState.load(std::memory_order_acquire) == LogHistoryState::Open) {
				std::uint32_t historyWritten = 0, historyLength = 0;
				for (;;) {
					historyLength = __logHistoryLength.load(std::memory_order_acquire);
					const std::uint32_t available = (historyLength > MaxLogHistoryLength ? MaxLogHistoryLength : historyLength);
					if (available <= historyWritten) {
						break;
					}
					// Repeat to pick up entries appended in the meantime (the buffer is bounded)
					logFile->Write(__logHistory.data() + historyWritten, available - historyWritten);
					historyWritten = available;
				}
				if (historyLength > MaxLogHistoryLength) {
					// Mark the gap, the leading newline ends the truncated entry
					static const char historyOverflowNote[] = "\n(some entries between here and the next line exceeded the startup buffer and were not recorded)\n";
					logFile->Write(historyOverflowNote, sizeof(historyOverflowNote) - 1);
				}
			}

			if (__logFileOwner != nullptr) {
				// Replacing a previous target, let the sink finish with the old file first
				__logFile.store(nullptr, std::memory_order_release);
				Trace::Flush();
			}

			// Publish the file before closing the history, so no entry is lost in between
			__logFileOwner = Death::move(logFile);
			__logFile.store(__logFileOwner.get(), std::memory_order_release);
			DiscardLogHistory();

#	if defined(WITH_BACKWARD)
			// Try to save crash info to log file
			__eh.Destination = __logFileOwner.get();
#	endif
		} else {
			// Keep the history, another target may be attached later
			__logFile.store(nullptr, std::memory_order_release);
			__logFileOwner = nullptr;
#	if defined(WITH_BACKWARD)
			__eh.Destination = nullptr;
#	endif
		}
#endif
	}

	void Application::SetCrashDumpDirectory(Containers::StringView path)
	{
#if defined(WITH_BACKWARD)
		__eh.DumpDirectory = path;
#endif
	}

#if defined(DEATH_TRACE)
	void Application::InitializeTrace()
	{
		// Idempotent, entry points call it early so platform bring-up can log; PreInitCommon() calls it again
		if (__traceInitialized) {
			return;
		}
		__traceInitialized = true;

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (__startupTimestampMs == 0) {
			// 1 ms back, so the header is never later than the first entry (they use a different clock)
			std::int64_t timestampMs = DateTime::UtcNow().ToUnixMilliseconds() - 1;
			__startupTimestampMs = (timestampMs > 0 ? timestampMs : 0);
		}
#	endif

#	if defined(DEATH_TARGET_EMSCRIPTEN)
		char* userAgent = (char*)EM_ASM_PTR({
			return (typeof navigator !== 'undefined' && navigator !== null &&
					typeof navigator.userAgent !== 'undefined' && navigator.userAgent !== null
						? stringToNewUTF8(navigator.userAgent) : 0);
		});
		if (userAgent != nullptr) {
			// Only Chrome supports ANSI escape sequences for now
			__consoleType = (::strcasestr(userAgent, "chrome") != nullptr ? ConsoleType::EscapeCodes : ConsoleType::Redirect);
			std::free(userAgent);
		} else {
			__consoleType = ConsoleType::Redirect;
		}
#	elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#		if defined(DEATH_TARGET_UNIX)
		::setvbuf(stdout, nullptr, _IONBF, 0);
		::setvbuf(stderr, nullptr, _IONBF, 0);
#		endif

		// Xcode's console reports that it is a TTY, but it doesn't support colors, TERM is not defined in this case
		__consoleType = ConsoleType::Redirect;

		StringView NO_COLOR = ::getenv("NO_COLOR");
		StringView FORCE_COLOR = ::getenv("FORCE_COLOR");
		if (NO_COLOR) {
			// Don't use colors if NO_COLOR is set, even if the console supports it
		} else if (::isatty(1)) {
			StringView COLORTERM = ::getenv("COLORTERM");
			StringView TERM = ::getenv("TERM");

			if (!COLORTERM.empty()) {
				if (COLORTERM.contains("truecolor"_s) || COLORTERM.contains("24bit"_s)) {
					__consoleType = ConsoleType::EscapeCodes24bit;
					CheckConsoleCapabilities();
				} else if (COLORTERM.contains("256color"_s) || COLORTERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				}
			}

			if (__consoleType < ConsoleType::EscapeCodes8bit && !TERM.empty()) {
				if (TERM.contains("256color"_s) || TERM.contains("rxvt-xpm"_s)) {
					__consoleType = ConsoleType::EscapeCodes8bit;
				} else if (TERM.contains("xterm"_s) || TERM.contains("vt1"_s) || TERM.contains("linux"_s)) {
					__consoleType = ConsoleType::EscapeCodes;
				}
			}
		} else if (FORCE_COLOR) {
			// FORCE_COLOR enables colors even when piped (e.g. to a file)
			__consoleType = ConsoleType::EscapeCodes24bit;
		}

#	elif defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		DWORD NO_COLOR = ::GetEnvironmentVariable(L"NO_COLOR", nullptr, 0);
		if (NO_COLOR) {
			// Don't use colors if NO_COLOR is set, even if the console supports it
		} else {
			HANDLE hStdOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			switch (::GetFileType(hStdOut)) {
				case FILE_TYPE_CHAR: {
					bool hasVirtualTerminal = EnableVirtualTerminalProcessing(hStdOut);
					__consoleType = (hasVirtualTerminal ? ConsoleType::EscapeCodes24bit : ConsoleType::WinApi);
					if (hasVirtualTerminal) {
						CheckConsoleCapabilities();
					}
					break;
				}
				case FILE_TYPE_UNKNOWN:
					// Nothing is attached to stdout
					break;
				default:
					__consoleType = ConsoleType::Redirect;

					DWORD FORCE_COLOR = ::GetEnvironmentVariable(L"FORCE_COLOR", nullptr, 0);
					if (FORCE_COLOR) {
						// FORCE_COLOR enables colors even when piped (e.g. to a file)
						__consoleType = ConsoleType::EscapeCodes24bit;
					}
					break;
			}
		}
#	endif

#	if defined(WITH_BACKWARD)
		if (__consoleType >= ConsoleType::EscapeCodes) {
			__eh.FeatureFlags |= Backward::Flags::Colorized;
		}
#	endif

		Trace::AttachSink(this);
#	if !defined(DEATH_DEBUG)
		Trace::InitializeBacktrace(8, TraceLevel::Warning);
#	endif
	}

	void Application::ShutdownTrace()
	{
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (__logFileOwner != nullptr) {
			Trace::Flush();
#		if defined(WITH_BACKWARD)
			__eh.Destination = nullptr;
#		endif
			__logFile.store(nullptr, std::memory_order_release);
		}
#	endif

		Trace::RemoveSink(this);
		__traceInitialized = false;

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
		// The sink is detached (worker joined), so both can be released now
		__logFileOwner = nullptr;
		__logHistoryState.store(LogHistoryState::Released, std::memory_order_relaxed);
		__logHistoryLength.store(0, std::memory_order_relaxed);
		__logHistory = {};
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (__consoleType >= ConsoleType::WinApi) {
			DestroyTraceConsole();
		}
#	endif
	}

#	if !defined(DEATH_TARGET_EMSCRIPTEN)
	// Platform in the TraceDigger metadata header, part of the format, never reassign values
	enum class MetadataPlatform : std::uint32_t {
		Unknown = 0,
		Other = 1,

		Windows = 2,
		WindowsRT = 3,
		Unix = 4,
		Linux = 5,
		Bsd = 6,
		Apple = 7,
		iOS = 8,
		Android = 12,
		Web = 14,

		Nintendo64 = 66,
		GameCube = 67,
		Wii = 68,
		WiiU = 69,
		Switch = 70,
		Nintendo3DS = 77,
		PlayStationPortable = 101,
		PlayStationVita = 102,
		SegaDreamcast = 117,

		Amiga = 176
	};

	// Version format in the metadata header, part of the format, never reassign values
	enum class MetadataVersionForm : std::uint32_t {
		// Unknown pattern, stored as the last string
		Raw = 0,
		// `<major>.<minor>.<patch>` as 3 varints
		Numeric = 1,
		// `<major>.<minor>.r<revision>-<hash>` as 3 varints, hash digit count and hash packed 2 digits per byte
		GitRevision = 2
	};

	// String encoding in the metadata header, part of the format, never reassign values
	enum class MetadataStringEncoding : std::uint32_t {
		// UTF-8 bytes as-is
		Raw = 0,
		// 7 bits per character
		Ascii = 1,
		// 6 bits per character according to `MetadataStringAlphabet`
		LowercaseSet = 2,
		// 6 bits per character according to `MetadataStringMixedCaseAlphabet`
		MixedCaseSet = 3
	};

	// 6-bit alphabet for lowercase text, versions, paths and arguments
	static constexpr char MetadataStringAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789 .,-_:;/\\()[]{}=+*?!@#%&'\"<>";
	static_assert(sizeof(MetadataStringAlphabet) == 64 + 1, "MetadataStringAlphabet must contain exactly 64 characters");

	// 6-bit alphabet with both cases, enough for file names and host names
	static constexpr char MetadataStringMixedCaseAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";
	static_assert(sizeof(MetadataStringMixedCaseAlphabet) == 64 + 1, "MetadataStringMixedCaseAlphabet must contain exactly 64 characters");

	// Minutes east of UTC including DST (unlike `TimeZone::GetOffset()`), so it matches the log entries
	static std::int32_t GetLocalTimeZoneOffsetMinutes(std::int64_t timestampMs)
	{
		DateTime dt = DateTime::FromUnixMilliseconds(timestampMs);
		DateTime::Tm local = dt.Partitioned(DateTime::Local);
		DateTime::Tm utc = dt.Partitioned(DateTime::UTC);
		if (!local.IsValid() || !utc.IsValid()) {
			return 0;
		}

		// They differ by at most a day
		std::int32_t days = (local.Year != utc.Year ? (local.Year > utc.Year ? 1 : -1) : local.DayOfYear - utc.DayOfYear);
		return days * 1440 + (local.Hour * 60 + local.Minute) - (utc.Hour * 60 + utc.Minute);
	}

	void Application::AppendLogFileHeader(Stream& s)
	{
		// Write TraceDigger metadata header, the payload is encoded with URL-safe Base64 (without padding):
		//
		//   u8       Control - bits 0-4: format version (1), bits 5-6: `MetadataVersionForm`,
		//                      bit 7: reserved
		//   varint   Flags
		//   u8       Platform - `MetadataPlatform`
		//   varint   Timestamp - Unix time in milliseconds
		//   varint   Timezone offset (if `HasTimeZoneOffset`) - zig-zag quarter hours east of UTC
		//   varint   Process ID
		//   varint   Main thread ID as zig-zag delta from the process ID
		//   ...      Application version according to `MetadataVersionForm`
		//   ...      Strings: executable name, host name, arguments, compatibility layer (if `HasAppCompatLayer`),
		//            application version (if `MetadataVersionForm::Raw`); trailing empty strings are omitted
		//
		// Each string starts with a varint `(length << 2) | MetadataStringEncoding`, followed by the packed
		// characters, unused bits of the last byte are zero

		// Session start, or now if InitializeTrace() wasn't called
		std::int64_t timestampMs = __startupTimestampMs;
		if (timestampMs <= 0) {
			timestampMs = DateTime::UtcNow().ToUnixMilliseconds();
			if (timestampMs < 0) {
				timestampMs = 0;
			}
		}

		std::uint32_t flags = 0;
		if (Environment::GetCurrentElevation() == Environment::ElevationState::Full) {
			flags |= 0x01;	// Elevated
		}
#		if defined(DEATH_TARGET_32BIT)
		flags |= 0x40;		// Is32Bit
#		endif
#		if defined(DEATH_TARGET_BIG_ENDIAN)
		flags |= 0x80;		// IsBigEndian
#		endif
#		if defined(WITH_LIBRETRO)
		flags |= 0x4000;	// IsHosted
#		endif
		// Always set, even for UTC, to tell it apart from older headers without the field
		flags |= 0x08;		// HasTimeZoneOffset

#		if defined(DEATH_TARGET_WINDOWS_RT)
		constexpr MetadataPlatform platform = MetadataPlatform::WindowsRT;
#		elif defined(DEATH_TARGET_WINDOWS)
		constexpr MetadataPlatform platform = MetadataPlatform::Windows;
#		elif defined(DEATH_TARGET_ANDROID)
		constexpr MetadataPlatform platform = MetadataPlatform::Android;
#		elif defined(DEATH_TARGET_SWITCH)
		constexpr MetadataPlatform platform = MetadataPlatform::Switch;
		#		elif defined(DEATH_TARGET_N64)
				constexpr MetadataPlatform platform = MetadataPlatform::Nintendo64;
#		elif defined(DEATH_TARGET_GAMECUBE)
		constexpr MetadataPlatform platform = MetadataPlatform::GameCube;
#		elif defined(DEATH_TARGET_WII)
		constexpr MetadataPlatform platform = MetadataPlatform::Wii;
#		elif defined(DEATH_TARGET_WIIU)
		constexpr MetadataPlatform platform = MetadataPlatform::WiiU;
#		elif defined(DEATH_TARGET_3DS)
		constexpr MetadataPlatform platform = MetadataPlatform::Nintendo3DS;
#		elif defined(DEATH_TARGET_DREAMCAST)
		constexpr MetadataPlatform platform = MetadataPlatform::SegaDreamcast;
#		elif defined(DEATH_TARGET_PSP)
		constexpr MetadataPlatform platform = MetadataPlatform::PlayStationPortable;
#		elif defined(DEATH_TARGET_VITA)
		constexpr MetadataPlatform platform = MetadataPlatform::PlayStationVita;
#		elif defined(DEATH_TARGET_AMIGAOS) || defined(DEATH_TARGET_AMIGAOS4) || defined(DEATH_TARGET_MORPHOS)
		constexpr MetadataPlatform platform = MetadataPlatform::Amiga;
#		elif defined(DEATH_TARGET_IOS)
		constexpr MetadataPlatform platform = MetadataPlatform::iOS;
#		elif defined(DEATH_TARGET_APPLE)
		constexpr MetadataPlatform platform = MetadataPlatform::Apple;
#		elif defined(DEATH_TARGET_EMSCRIPTEN)
		constexpr MetadataPlatform platform = MetadataPlatform::Web;
#		elif defined(__linux__)
		constexpr MetadataPlatform platform = MetadataPlatform::Linux;
#		elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
		constexpr MetadataPlatform platform = MetadataPlatform::Bsd;
#		elif defined(DEATH_TARGET_UNIX)
		constexpr MetadataPlatform platform = MetadataPlatform::Unix;
#		else
		constexpr MetadataPlatform platform = MetadataPlatform::Unknown;
#		endif

#		if defined(DEATH_TARGET_WINDOWS)
		std::uint32_t processId = (std::uint32_t)::GetCurrentProcessId();
		wchar_t bufferW[128];
		DWORD compatLayerLength = ::GetEnvironmentVariable(L"__COMPAT_LAYER", bufferW, (DWORD)arraySize(bufferW));
		if (compatLayerLength > 0) {
			flags |= 0x1000;	// HasAppCompatLayer
		}
		if (Environment::IsWine()) {
			flags |= 0x2000;	// IsWine
		}
#		elif defined(DEATH_TARGET_ANDROID)
		flags |= 0x04 | 0x20;	// ProcessIdEqualsToMainThreadId | RemoteDevice
		std::uint32_t processId = (std::uint32_t)::getpid();
#		elif defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_WIIU) || defined(DEATH_TARGET_GAMECUBE) || \
				defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PS3) || \
				defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA)
		flags |= 0x20;	// RemoteDevice
		std::uint32_t processId = (std::uint32_t)::getpid();
#		else
#			if !defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_EMSCRIPTEN)
			flags |= 0x04;	// ProcessIdEqualsToMainThreadId
#			endif
#			if defined(DEATH_TARGET_SWITCH)
			flags |= 0x20;	// RemoteDevice
#			endif
		std::uint32_t processId = (std::uint32_t)getpid();
#		endif

		String hostName = GetDeviceHostname();

		// Try to store the application version numerically, the string form is used only as a fallback
		auto parseDecimal = [](StringView value, std::uint32_t& result) {
			if (value.empty() || value.size() > 9) {
				return false;
			}
			std::uint32_t n = 0;
			for (char c : value) {
				if (c < '0' || c > '9') {
					return false;
				}
				n = n * 10 + (std::uint32_t)(c - '0');
			}
			result = n;
			return true;
		};

		MetadataVersionForm versionForm = MetadataVersionForm::Raw;
		std::uint32_t versionMajor = 0, versionMinor = 0, versionPatch = 0;
		StringView versionHash;
		StringView version = NCINE_VERSION;
		if (StringView firstDot = version.find('.')) {
			StringView rest = version.suffix(firstDot.end());
			if (StringView secondDot = rest.find('.')) {
				StringView patchPart = rest.suffix(secondDot.end());
				if (parseDecimal(version.prefix(firstDot.begin()), versionMajor) &&
					parseDecimal(rest.prefix(secondDot.begin()), versionMinor)) {
					if (parseDecimal(patchPart, versionPatch)) {
						versionForm = MetadataVersionForm::Numeric;
					} else if (patchPart.hasPrefix('r')) {
						StringView revisionPart = patchPart.exceptPrefix(1);
						if (StringView dash = revisionPart.find('-')) {
							StringView hash = revisionPart.suffix(dash.end());
							bool isHex = (!hash.empty() && hash.size() <= 40);
							for (char c : hash) {
								if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
									isHex = false;
									break;
								}
							}
							if (isHex && parseDecimal(revisionPart.prefix(dash.begin()), versionPatch)) {
								versionHash = hash;
								versionForm = MetadataVersionForm::GitRevision;
							}
						}
					}
				}
			}
		}

#		if defined(DEATH_TARGET_ANDROID)
		auto executableFileName = nCine::Backends::AndroidJniWrap_Activity::GetPackageName();
#		else
		auto executablePath = fs::GetExecutablePath();
		auto executableFileName = fs::GetFileName(executablePath);
		if (executableFileName.empty()) {
			executableFileName = NCINE_APP;
		}
#		endif

		// The executable itself is not among the arguments, it's stripped when they are parsed
		std::string arguments;
		for (std::size_t i = 0; i < _appCfg.GetArgc(); i++) {
			if (i > 0) {
				arguments += ' ';
			}
			arguments += _appCfg.GetArgv(i);
		}

		char buffer[256];
		MemoryStream ms(buffer, sizeof(buffer));
		ms.WriteValue<std::uint8_t>((std::uint8_t)(1 |					// Format version
			((std::uint32_t)versionForm << 5)));
		ms.WriteVariableUint32(flags);
		ms.WriteValue<std::uint8_t>((std::uint8_t)platform);
		ms.WriteVariableUint64((std::uint64_t)timestampMs);
		// Quarter hours, so all timezones fit in a single byte
		std::int32_t timeZoneOffsetMinutes = GetLocalTimeZoneOffsetMinutes(timestampMs);
		ms.WriteVariableInt32((timeZoneOffsetMinutes + (timeZoneOffsetMinutes >= 0 ? 7 : -7)) / 15);
		ms.WriteVariableUint32(processId);
		// Usually close to the process ID, so the delta is short
		ms.WriteVariableInt64((std::int64_t)_mainThreadId - (std::int64_t)processId);

		if (versionForm != MetadataVersionForm::Raw) {
			ms.WriteVariableUint32(versionMajor);
			ms.WriteVariableUint32(versionMinor);
			ms.WriteVariableUint32(versionPatch);

			if (versionForm == MetadataVersionForm::GitRevision) {
				auto toNibble = [](char c) -> std::uint8_t {
					return (std::uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10);
				};
				ms.WriteValue<std::uint8_t>((std::uint8_t)versionHash.size());
				for (std::size_t i = 0; i < versionHash.size(); i += 2) {
					std::uint8_t packedNibbles = (std::uint8_t)(toNibble(versionHash[i]) << 4);
					if (i + 1 < versionHash.size()) {
						packedNibbles |= toNibble(versionHash[i + 1]);
					}
					ms.WriteValue<std::uint8_t>(packedNibbles);
				}
			}
		}

		// Writes a string in the tightest encoding, clamped to the remaining space minus `reserve` for the next ones
		auto writeString = [&ms](StringView value, std::int64_t reserve) {
			auto indexInAlphabet = [](const char* alphabet, char c) -> std::int32_t {
				for (std::int32_t i = 0; i < 64; i++) {
					if (alphabet[i] == c) {
						return i;
					}
				}
				return -1;
			};

			// Up to 4 bytes are needed for the header itself
			std::int64_t available = ms.GetSize() - ms.GetPosition() - reserve - 4;
			std::int64_t length = (std::int64_t)value.size();
			if (length > available) {
				// Don't cut in the middle of a multi-byte UTF-8 sequence
				length = (std::int64_t)Death::Utf8::FitPrefix(value, std::size_t(available > 0 ? available : 0));
			}

			MetadataStringEncoding encoding = MetadataStringEncoding::Raw;
			if (length > 0) {
				bool fitsLowercase = true, fitsMixedCase = true, isAscii = true;
				for (std::int64_t i = 0; i < length; i++) {
					char c = value[i];
					if ((std::uint8_t)c >= 0x80) {
						isAscii = fitsLowercase = fitsMixedCase = false;
						break;
					}
					if (fitsLowercase && indexInAlphabet(MetadataStringAlphabet, c) < 0) {
						fitsLowercase = false;
					}
					if (fitsMixedCase && indexInAlphabet(MetadataStringMixedCaseAlphabet, c) < 0) {
						fitsMixedCase = false;
					}
				}
				encoding = (fitsLowercase ? MetadataStringEncoding::LowercaseSet
					: (fitsMixedCase ? MetadataStringEncoding::MixedCaseSet
						: (isAscii ? MetadataStringEncoding::Ascii : MetadataStringEncoding::Raw)));
			}

			ms.WriteVariableUint32((std::uint32_t)((length << 2) | (std::int64_t)encoding));

			if (encoding == MetadataStringEncoding::Raw) {
				if (length > 0) {
					ms.Write(value.data(), length);
				}
			} else {
				const char* alphabet = (encoding == MetadataStringEncoding::MixedCaseSet
					? MetadataStringMixedCaseAlphabet : MetadataStringAlphabet);
				std::int32_t bitsPerCharacter = (encoding == MetadataStringEncoding::Ascii ? 7 : 6);
				std::uint32_t accumulator = 0; std::int32_t accumulatorBits = 0;
				for (std::int64_t i = 0; i < length; i++) {
					std::uint32_t code = (bitsPerCharacter == 7
						? (std::uint32_t)(value[i] & 0x7F)
						: (std::uint32_t)indexInAlphabet(alphabet, value[i]));
					accumulator |= (code << accumulatorBits);
					accumulatorBits += bitsPerCharacter;
					while (accumulatorBits >= 8) {
						ms.WriteValue<std::uint8_t>((std::uint8_t)(accumulator & 0xFF));
						accumulator >>= 8;
						accumulatorBits -= 8;
					}
				}
				if (accumulatorBits > 0) {
					ms.WriteValue<std::uint8_t>((std::uint8_t)(accumulator & 0xFF));
				}
			}
		};

		String compatLayer;
#		if defined(DEATH_TARGET_WINDOWS)
		if (compatLayerLength > 0) {
			if (compatLayerLength > (std::uint32_t)arraySize(bufferW)) {
				compatLayerLength = (std::uint32_t)arraySize(bufferW);
			}
			compatLayer = Utf8::FromUtf16(bufferW, compatLayerLength);
		}
#		endif

		StringView strings[5];
		std::int32_t stringCount = 0;
		strings[stringCount++] = executableFileName;
		strings[stringCount++] = hostName;
		strings[stringCount++] = { arguments.data(), arguments.size() };
		if ((flags & 0x1000) != 0) {	// HasAppCompatLayer
			strings[stringCount++] = compatLayer;
		}
		if (versionForm == MetadataVersionForm::Raw) {
			strings[stringCount++] = version;
		}

		// Trailing empty strings don't need to be stored at all
		while (stringCount > 0 && strings[stringCount - 1].empty()) {
			stringCount--;
		}

		// Space reserved for each remaining string
		constexpr std::int64_t ReservedPerString = 24;

		for (std::int32_t i = 0; i < stringCount; i++) {
			writeString(strings[i], (std::int64_t)(stringCount - i - 1) * ReservedPerString);
		}

		auto metadataBase64 = nCine::toBase64Url(buffer, buffer + (std::size_t)ms.GetPosition());

		constexpr StringView FileHeader = "#! /usr/bin/tracedigger :"_s;
		s.Write(FileHeader.data(), (std::int64_t)FileHeader.size());
		s.Write(metadataBase64.data(), (std::int64_t)metadataBase64.size());
		s.Write("\n", 1);
	}
#	endif

#	if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
	bool Application::CreateTraceConsole(StringView title, bool& hasVirtualTerminal)
	{
		FILE* fDummy = nullptr;

		if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stdout);
				::setvbuf(stdout, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleError = ::GetStdHandle(STD_ERROR_HANDLE);
			if (consoleHandleError != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONOUT$", "w", stderr);
				::setvbuf(stderr, NULL, _IONBF, 0);
			}

			HANDLE consoleHandleIn = ::GetStdHandle(STD_INPUT_HANDLE);
			if (consoleHandleIn != INVALID_HANDLE_VALUE) {
				::freopen_s(&fDummy, "CONIN$", "r", stdin);
				::setvbuf(stdin, NULL, _IONBF, 0);
			}

			::SetConsoleOutputCP(CP_UTF8);
			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			// Try to get command prompt to be able to reprint it when the game exits
			CONSOLE_SCREEN_BUFFER_INFO csbi;
			if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
				DWORD dwConsoleColumnWidth = (DWORD)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
				SHORT xEnd = csbi.dwCursorPosition.X;
				SHORT yEnd = csbi.dwCursorPosition.Y;
				if (xEnd != 0 || yEnd != 0) {
					DWORD dwNumberOfChars;
					SHORT yBegin = yEnd;
					if (dwConsoleColumnWidth > 16) {
						Array<wchar_t> tmp(NoInit, dwConsoleColumnWidth);
						while (yBegin > 0) {
							COORD dwReadCoord = { 0, yBegin };
							if (!::ReadConsoleOutputCharacter(consoleHandleOut, tmp.data(), dwConsoleColumnWidth, dwReadCoord, &dwNumberOfChars)) {
								break;
							}

							for (DWORD i = dwNumberOfChars - 8; i < dwNumberOfChars; i++) {
								wchar_t wchar = tmp[i];
								if (wchar != L' ') {
									yBegin--;
									continue;
								}
							}

							if (yBegin < yEnd) {
								yBegin++;
							}
							break;
						}
					}

					DWORD promptLength = (yEnd - yBegin) * dwConsoleColumnWidth + xEnd;
					__consolePrompt = Array<wchar_t>(NoInit, promptLength);
					COORD dwPromptCoord = { 0, yEnd };
					if (::ReadConsoleOutputCharacter(consoleHandleOut, __consolePrompt.data(), promptLength, dwPromptCoord, &dwNumberOfChars)) {
						if (::SetConsoleCursorPosition(consoleHandleOut, dwPromptCoord)) {
							::FillConsoleOutputCharacter(consoleHandleOut, L' ', promptLength, dwPromptCoord, &dwNumberOfChars);
						}
					} else {
						__consolePrompt = {};
					}
				}
			}

			return true;
		} else if (::AllocConsole()) {
			::freopen_s(&fDummy, "CONOUT$", "w", stdout);
			::freopen_s(&fDummy, "CONOUT$", "w", stderr);
			::freopen_s(&fDummy, "CONIN$", "r", stdin);

			HANDLE consoleHandleOut = ::CreateFile(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			HANDLE consoleHandleIn = ::CreateFile(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			::SetStdHandle(STD_OUTPUT_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_ERROR_HANDLE, consoleHandleOut);
			::SetStdHandle(STD_INPUT_HANDLE, consoleHandleIn);

			::SetConsoleOutputCP(CP_UTF8);
			hasVirtualTerminal = EnableVirtualTerminalProcessing(consoleHandleOut);

			::SetConsoleTitle(Death::Utf8::ToUtf16(title));
			HWND hWnd = ::GetConsoleWindow();
			if (hWnd != nullptr) {
				HINSTANCE inst = ((HINSTANCE)&__ImageBase);
				HICON windowIcon = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTSIZE);
				HICON windowIconSmall = (HICON)::LoadImage(inst, L"WINDOW_ICON", IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTSIZE);
				if (windowIconSmall != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)windowIconSmall);
				if (windowIcon != NULL) ::SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)windowIcon);
			}

			return true;
		} else {
			hasVirtualTerminal = false;
		}

		return false;
	}

	void Application::DestroyTraceConsole()
	{
		if (!__consolePrompt.empty()) {
			HANDLE consoleHandleOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
			if (consoleHandleOut != INVALID_HANDLE_VALUE) {
				CONSOLE_SCREEN_BUFFER_INFO csbi;
				if (::GetConsoleScreenBufferInfo(consoleHandleOut, &csbi)) {
					DWORD xEnd = csbi.dwCursorPosition.X;
					DWORD yEnd = csbi.dwCursorPosition.Y;
					if (xEnd != 0 || yEnd != 0) {
						DWORD dwNumberOfCharsWritten;
						::WriteConsoleW(consoleHandleOut, L"\r\n", (DWORD)arraySize(L"\r\n") - 1, &dwNumberOfCharsWritten, NULL);
						::WriteConsoleW(consoleHandleOut, __consolePrompt.data(), (DWORD)__consolePrompt.size(), &dwNumberOfCharsWritten, NULL);
					}
				}
			}
		}

		::FreeConsole();
	}
#	endif
#endif
}
