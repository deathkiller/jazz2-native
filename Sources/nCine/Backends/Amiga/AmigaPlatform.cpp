#if defined(WITH_AMIGA)

#include "AmigaPlatform.h"
#include "../../../Main.h"

#include <Environment.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <exec/execbase.h>
#include <exec/memory.h>
#include <proto/exec.h>

#if defined(WITH_AMMX)
#	include "../../Graphics/RHI/Software/SwAmmxOps.h"
#endif

// The library bases the NDK's inline headers call through. SysBase/DOSBase come from the C runtime;
// these are the ones this backend opens itself. They are plain globals by contract - every inline
// header names its base - and the NDK's proto headers declare them WITHOUT extern "C", so the
// definitions here match that (C++) linkage.
struct Library* CyberGfxBase = nullptr;
struct Library* LowLevelBase = nullptr;
struct Library* KeymapBase = nullptr;
// intuition.library and graphics.library are opened by the C runtime on some setups but not all,
// and the backend must not depend on which - it opens its own references either way
struct IntuitionBase* IntuitionBase = nullptr;
struct GfxBase* GfxBase = nullptr;

extern "C" {
// libnix reads this at startup and swaps to an allocated stack of this size before main() runs.
// AmigaOS default stacks are 4-8 KB and this engine's deepest paths (level loading, the software
// rasterizer's dispatch) need far more; a megabyte is cheap on the 32 MB+ machines this port targets.
unsigned long __stack = 1024 * 1024;
}

namespace nCine::Backends
{
	namespace
	{
		// The E-clock rate as Death::Environment reported it at startup, kept here so the callers that
		// need it every frame (the frame clock, the audio device) do not go back to the device for it
		std::uint32_t _timerFrequency = 709379;

		AmigaPlatform::PerformanceClass _performanceClass = AmigaPlatform::PerformanceClass::Low;

		const char* PerformanceClassName(AmigaPlatform::PerformanceClass performanceClass)
		{
			switch (performanceClass) {
				case AmigaPlatform::PerformanceClass::Ultra: return "Ultra";
				case AmigaPlatform::PerformanceClass::High: return "High";
				case AmigaPlatform::PerformanceClass::Medium: return "Medium";
				default: return "Low";
			}
		}

		const char* DescribeCpu(std::uint16_t attnFlags)
		{
			// Bit 10 is the Apollo core's unofficial 68080 flag; the official bits stop at the 060.
			// Ordered from the top so the best hit wins.
			if (attnFlags & (1 << 10)) return "68080";
			if (attnFlags & AFF_68060) return "68060";
			if (attnFlags & AFF_68040) return "68040";
			if (attnFlags & AFF_68030) return "68030";
			if (attnFlags & AFF_68020) return "68020";
			if (attnFlags & AFF_68010) return "68010";
			return "68000";
		}
	}

	Screen* AmigaPlatform::GameScreen = nullptr;
	Window* AmigaPlatform::GameWindow = nullptr;

	namespace
	{
		/** @brief ASCII case-insensitive comparison, so `NCINE_PRESET` may be set in any case */
		bool EqualsIgnoreCase(const char* a, const char* b)
		{
			for (; *a != '\0' && *b != '\0'; a++, b++) {
				const char ca = (*a >= 'A' && *a <= 'Z' ? char(*a - 'A' + 'a') : *a);
				const char cb = (*b >= 'A' && *b <= 'Z' ? char(*b - 'A' + 'a') : *b);
				if (ca != cb) {
					return false;
				}
			}
			return (*a == *b);
		}
	}

	bool AmigaPlatform::Initialize()
	{
		IntuitionBase = reinterpret_cast<struct IntuitionBase*>(OpenLibrary(reinterpret_cast<CONST_STRPTR>("intuition.library"), 39));
		GfxBase = reinterpret_cast<struct GfxBase*>(OpenLibrary(reinterpret_cast<CONST_STRPTR>("graphics.library"), 39));
		if (IntuitionBase == nullptr || GfxBase == nullptr) {
			// Kickstart below 3.0 - nothing below that can drive an RTG board anyway.
			//
			// stderr rather than LOGF, here and in the other three fatal checks below - not because of when
			// this runs (the trace sink is attached before any of the platform bring-up, see
			// MainApplication::Run), but because LOG* compiles to nothing at all in a build without
			// DEATH_TRACE, and each of these is the only explanation the Shell will ever get for the exit.
			std::fprintf(stderr, "nCine: AmigaOS 3.0 (Kickstart 39) or newer is required\n");
			return false;
		}

		// The whole rendering path presents into an RTG chunky framebuffer through the CyberGraphX API,
		// which Picasso96 implements as well - one code path covers both stacks (and SAGA, PiStorm and
		// UAE, all of which ship P96 drivers). Version 41 is the level both stacks have provided since
		// the late 90s; AROS's implementation numbers itself differently, so anything the system offers
		// is accepted as the fallback. Without the library there is no chunky display to open at all,
		// which is this port's floor.
		CyberGfxBase = OpenLibrary(reinterpret_cast<CONST_STRPTR>("cybergraphics.library"), 41);
		if (CyberGfxBase == nullptr) {
			CyberGfxBase = OpenLibrary(reinterpret_cast<CONST_STRPTR>("cybergraphics.library"), 0);
		}
		if (CyberGfxBase == nullptr) {
			// AGA-only machines are below this port's floor
			std::fprintf(stderr, "nCine: cybergraphics.library is required (install Picasso96 or CyberGraphX with an RTG driver)\n");
			return false;
		}
		LOGI("cybergraphics.library v{}.{}", CyberGfxBase->lib_Version, CyberGfxBase->lib_Revision);

		// Optional: CD32-pad/joystick support. Part of the OS since 3.1, but its absence only costs joysticks.
		LowLevelBase = OpenLibrary(reinterpret_cast<CONST_STRPTR>("lowlevel.library"), 40);
		if (LowLevelBase == nullptr) {
			LOGW("lowlevel.library not found, joysticks and CD32 pads will not be available");
		}

		// Optional: RAWKEY -> character mapping for text input fields
		KeymapBase = OpenLibrary(reinterpret_cast<CONST_STRPTR>("keymap.library"), 37);

		// The EClock is the only fine-grained monotonic clock the OS offers (the CIA E-clock, ~709 kHz).
		// Death::Environment owns it - it opens timer.device on the first reading, because the shared time
		// queries have to answer before any of this runs - so all that is left here is to remember the rate
		// it reports, and to fail the same way as before if the device cannot be opened at all.
		std::uint32_t timerFrequency = 0;
		Death::Environment::Implementation::QueryAmigaEClock(&timerFrequency);
		if (timerFrequency == 0) {
			std::fprintf(stderr, "nCine: cannot open timer.device\n");
			return false;
		}
		_timerFrequency = timerFrequency;

		const struct ExecBase* sysBase = reinterpret_cast<struct ExecBase*>(SysBase);

		const std::uint32_t chipBytes = AvailMem(MEMF_CHIP | MEMF_TOTAL);
		const std::uint32_t fastBytes = AvailMem(MEMF_FAST | MEMF_TOTAL);
		LOGI("Machine: {} (AttnFlags 0x{:x}), {} KB chip + {} KB fast RAM, EClock {} Hz",
			DescribeCpu(sysBase->AttnFlags), sysBase->AttnFlags, chipBytes / 1024, fastBytes / 1024, _timerFrequency);

		if ((sysBase->AttnFlags & (AFF_68881 | AFF_68882 | AFF_FPU40)) == 0) {
			// The binary is compiled -m68881 and the game's update loop is floating point throughout, so
			// this cannot limp along in software emulation - it would trap on the first FPU instruction
			std::fprintf(stderr, "nCine: an FPU is required (this build is compiled for 68040/68060 with FPU)\n");
			return false;
		}

#if defined(WITH_AMMX)
		// The Apollo core reports itself through the unofficial bit 10 of AttnFlags. AMMX use can be
		// vetoed with an environment variable in case a specific setup misbehaves (the kernels are
		// designed bit-identical to the scalar path - SwBackendHarness verifies that on hardware).
		if ((sysBase->AttnFlags & (1 << 10)) != 0) {
			const bool ammxEnabled = (std::getenv("NCINE_NO_AMMX") == nullptr);
			RHI::Software::SetAmmxEnabled(ammxEnabled);
			if (ammxEnabled) {
				LOGI("Apollo 68080 detected, AMMX scanline kernels enabled (set NCINE_NO_AMMX to disable)");
			} else {
				LOGI("Apollo 68080 detected, AMMX kernels disabled by NCINE_NO_AMMX");
			}
		}
#endif

		RunPerformanceProbe();

		// An explicit preset wins over the measurement. This exists for bring-up on a machine the probe
		// reads wrongly (an accelerator whose RAM is faster than its CPU, a PiStorm whose emulated 68040
		// times unlike any real one) and for A/B-ing the presets on one machine without rebuilding.
		bool presetForced = false;
		if (const char* preset = std::getenv("NCINE_PRESET")) {
			presetForced = true;
			if (EqualsIgnoreCase(preset, "low")) {
				SetPerformanceClass(PerformanceClass::Low);
			} else if (EqualsIgnoreCase(preset, "medium")) {
				SetPerformanceClass(PerformanceClass::Medium);
			} else if (EqualsIgnoreCase(preset, "high")) {
				SetPerformanceClass(PerformanceClass::High);
			} else if (EqualsIgnoreCase(preset, "ultra")) {
				SetPerformanceClass(PerformanceClass::Ultra);
			} else {
				presetForced = false;
				LOGW("Ignoring NCINE_PRESET=\"{}\" (expected low, medium, high or ultra)", preset);
			}
		}

		LOGI("Performance preset: {}{}", PerformanceClassName(_performanceClass),
			(presetForced ? " (forced by NCINE_PRESET)" : ""));
		return true;
	}

	void AmigaPlatform::Shutdown()
	{
		// The timer is not closed here - see Environment::Implementation::QueryAmigaEClock(), which owns it
		// and keeps it open for the lifetime of the process on purpose
		if (KeymapBase != nullptr) {
			CloseLibrary(KeymapBase);
			KeymapBase = nullptr;
		}
		if (LowLevelBase != nullptr) {
			CloseLibrary(LowLevelBase);
			LowLevelBase = nullptr;
		}
		if (CyberGfxBase != nullptr) {
			CloseLibrary(CyberGfxBase);
			CyberGfxBase = nullptr;
		}
		if (GfxBase != nullptr) {
			CloseLibrary(reinterpret_cast<struct Library*>(GfxBase));
			GfxBase = nullptr;
		}
		if (IntuitionBase != nullptr) {
			CloseLibrary(reinterpret_cast<struct Library*>(IntuitionBase));
			IntuitionBase = nullptr;
		}
	}

	std::uint32_t AmigaPlatform::TimerFrequency()
	{
		return _timerFrequency;
	}

	std::uint64_t AmigaPlatform::TimerTicks()
	{
		// Unconverted ticks, which is what the frame clock and the audio device want - no 64-bit division
		// per reading, which on a 68k is a libgcc call
		return Death::Environment::Implementation::QueryAmigaEClock();
	}

	AmigaPlatform::PerformanceClass AmigaPlatform::GetPerformanceClass()
	{
		return _performanceClass;
	}

	void AmigaPlatform::SetPerformanceClass(PerformanceClass performanceClass)
	{
		_performanceClass = performanceClass;
	}

	void AmigaPlatform::RunPerformanceProbe()
	{
		// The two loops are shaped like the two things a frame is made of here. The first is the software
		// rasterizer's inner loop in miniature - read a byte, look it up in a 256-entry table, alpha-blend
		// it into a 16-bit destination - so its throughput prices exactly what sprite compositing will
		// cost: integer ALU, L1 behaviour and the fast-RAM bus together. The second is a dependent
		// floating-point multiply-add chain, which is what the game's update loop (positions, physics,
		// animation timing) reduces to. Measured, not guessed, because this hardware range spans a 68060,
		// an FPGA 68080 and a JIT-recompiling ARM - no CPU flag tells those apart honestly.

		constexpr std::int32_t BufferPixels = 32 * 1024;
		std::uint8_t* source = static_cast<std::uint8_t*>(std::malloc(BufferPixels));
		std::uint16_t* dest = static_cast<std::uint16_t*>(std::malloc(BufferPixels * sizeof(std::uint16_t)));
		std::uint16_t lut[256];
		if (source == nullptr || dest == nullptr) {
			std::free(source);
			std::free(dest);
			LOGW("Performance probe skipped (out of memory), assuming the Low class");
			_performanceClass = PerformanceClass::Low;
			return;
		}
		for (std::int32_t i = 0; i < BufferPixels; i++) {
			source[i] = std::uint8_t(i * 31);
			dest[i] = std::uint16_t(i * 7);
		}
		for (std::int32_t i = 0; i < 256; i++) {
			lut[i] = std::uint16_t(i * 257);
		}

		// Composite loop: run whole passes until ~40 ms have elapsed, then price one pixel. The volatile
		// sink keeps the optimizer from noticing the results are never read.
		volatile std::uint32_t sink = 0;
		const std::uint64_t frequency = _timerFrequency;
		const std::uint64_t minTicks = (frequency * 40) / 1000;
		std::uint64_t start = TimerTicks();
		std::uint64_t elapsed = 0;
		std::int32_t passes = 0;
		do {
			std::uint32_t accumulator = 0;
			for (std::int32_t i = 0; i < BufferPixels; i++) {
				const std::uint16_t texel = lut[source[i]];
				const std::uint16_t back = dest[i];
				// A 565 half-blend in the two-field trick, the shape of BlendScanlineSrcAlpha's work
				const std::uint32_t mixed = ((texel & 0xF7DEu) >> 1) + ((back & 0xF7DEu) >> 1);
				dest[i] = std::uint16_t(mixed);
				accumulator += mixed;
			}
			sink += accumulator;
			passes++;
			elapsed = TimerTicks() - start;
		} while (elapsed < minTicks);
		// Nanoseconds one composited pixel costs
		const std::uint32_t nsPerPixel = std::uint32_t((elapsed * 1000000000ull) / (frequency * std::uint64_t(passes) * BufferPixels));

		// Float loop: two independent dependent chains, so a pipelined FPU shows its pipelining
		constexpr std::int32_t FloatIterations = 200000;
		start = TimerTicks();
		float x = 1.0001f, y = 0.9999f;
		for (std::int32_t i = 0; i < FloatIterations; i++) {
			x = x * 1.000001f + 0.000001f;
			y = y * 0.999999f + 0.000002f;
		}
		elapsed = TimerTicks() - start;
		volatile float floatSink = x + y;
		static_cast<void>(floatSink);
		// Nanoseconds one multiply-add costs (two per iteration)
		const std::uint32_t nsPerFlop = std::uint32_t((elapsed * 1000000000ull) / (frequency * std::uint64_t(FloatIterations) * 2));

		// The thresholds bracket the known hardware tiers: a 68060/50 composites a pixel in roughly
		// 300-500 ns (Low), a Vampire V4 in 100-200 (Medium), a PiStorm Pi3A+ under 60 (High) and a
		// CM4-class PiStorm under 25 (Ultra). The float cost acts as a brake only: a machine whose FPU
		// is an order of magnitude behind its integer pipe (some emulation setups) drops one class,
		// because the game's update loop would become the bottleneck instead of the rasterizer.
		PerformanceClass measured;
		if (nsPerPixel < 25) {
			measured = PerformanceClass::Ultra;
		} else if (nsPerPixel < 60) {
			measured = PerformanceClass::High;
		} else if (nsPerPixel < 150) {
			measured = PerformanceClass::Medium;
		} else {
			measured = PerformanceClass::Low;
		}
		if (nsPerFlop > nsPerPixel * 10 && measured != PerformanceClass::Low) {
			measured = static_cast<PerformanceClass>(static_cast<int>(measured) - 1);
		}

		_performanceClass = measured;
		LOGI("Performance probe: {} ns/composited pixel, {} ns/FPU multiply-add", nsPerPixel, nsPerFlop);

		std::free(source);
		std::free(dest);
	}

	bool AmigaPlatform::HasLowLevel()
	{
		return (LowLevelBase != nullptr);
	}

	bool AmigaPlatform::HasKeymap()
	{
		return (KeymapBase != nullptr);
	}
}

#endif
