#include "Cpu.h"

#if defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(__FreeBSD__))
// getauxval() for ARM on Linux, Android with API level 18+ and FreeBSD
#	include <sys/auxv.h>
#elif defined(DEATH_TARGET_ARM) && defined(DEATH_TARGET_APPLE)
// sysctlbyname() for ARM on macOS / iOS
#	include <sys/sysctl.h>
#endif

namespace Death { namespace Cpu {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// As the types all inherit from each other, there should be no members to keep them zero-cost.
	static_assert(sizeof(Cpu::Scalar) == 1, "");
#if defined(DEATH_TARGET_X86)
	static_assert(sizeof(Cpu::Sse2) == 1, "");
	static_assert(sizeof(Cpu::Sse3) == 1, "");
	static_assert(sizeof(Cpu::Ssse3) == 1, "");
	static_assert(sizeof(Cpu::Sse41) == 1, "");
	static_assert(sizeof(Cpu::Sse42) == 1, "");
	static_assert(sizeof(Cpu::Avx) == 1, "");
	static_assert(sizeof(Cpu::AvxF16c) == 1, "");
	static_assert(sizeof(Cpu::AvxFma) == 1, "");
	static_assert(sizeof(Cpu::Avx2) == 1, "");
	static_assert(sizeof(Cpu::Avx512f) == 1, "");
#elif defined(DEATH_TARGET_ARM)
	static_assert(sizeof(Cpu::Neon) == 1, "");
	static_assert(sizeof(Cpu::NeonFma) == 1, "");
	static_assert(sizeof(Cpu::NeonFp16) == 1, "");
#elif defined(DEATH_TARGET_WASM)
	static_assert(sizeof(Cpu::Simd128) == 1, "");
#endif

	// Helper for getting macOS / iOS ARM properties. Yep, it's stringly typed.
#if defined(DEATH_TARGET_ARM) && defined(DEATH_TARGET_APPLE)
	namespace
	{
		int appleSysctlByName(const char* name) {
			int value;
			std::size_t size = sizeof(value);
			// First pointer/size pair is for querying the value, second is for setting the value. Returns 0 on success.
			return sysctlbyname(name, &value, &size, nullptr, 0) ? 0 : value;
		}
	}
#endif

#if defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__))
	Features runtimeFeatures() {
		// Use getauxval() on ARM on Linux and Android
#	if defined(DEATH_TARGET_ARM) && defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)
		// People say getauxval() is "extremely slow": https://lemire.me/blog/2020/07/17/the-cost-of-runtime-dispatch/#comment-538459
		// Like, can anything be worse than reading and parsing the text from /proc/cpuinfo?
		return Implementation::runtimeFeatures(getauxval(AT_HWCAP));

		// Use sysctlbyname() on ARM on macOS / iOS
#	elif defined(DEATH_TARGET_ARM) && defined(DEATH_TARGET_APPLE)
		unsigned int out = 0;
		// https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics,
		// especially "funny" is how most of the values are getting rid of the NEON naming,
		// probably because they want to push their proprietary AMX.
#		if defined(DEATH_TARGET_32BIT)
		// Apple says I should use hw.optional.AdvSIMD instead tho
		if (appleSysctlByName("hw.optional.neon")) out |= TypeTraits<NeonT>::Index;
		// On 32bit I have no idea how to query FMA / vfpv4 support, so that'll only be implied if FP16 is available as well.
		// Since I don't think there are many 32bit iOS devices left, that's not worth bothering with.
#		else
		// To avoid string operations, on 64bit I just assume NEON and FMA being present, like in the Linux case.
		// Again, for extra security make use of the DEATH_TARGET_ defines (which should be always there on ARM64)
		out |=
#			if defined(DEATH_TARGET_NEON)
			TypeTraits<NeonT>::Index |
#			endif
#			if defined(DEATH_TARGET_NEON_FMA)
			TypeTraits<NeonFmaT>::Index |
#			endif
			0;
#		endif
		// Apple says I should use hw.optional.arm.FEAT_FP16 instead though
		if (appleSysctlByName("hw.optional.neon_fp16")) {
			// As noted above, if FP16 is available on 32bit, bite the bullet and assume FMA is there as well
#		if defined(DEATH_TARGET_32BIT)
			out |= TypeTraits<NeonFmaT>::Index;
#		endif
			out |= TypeTraits<NeonFp16T>::Index;
		}

		return Features{out};
#	elif defined(DEATH_TARGET_ARM) && defined(__FreeBSD__)
		// Use elf_aux_info() on ARM on FreeBSD
		unsigned long hwcap = 0;
		elf_aux_info(AT_HWCAP, &hwcap, sizeof(hwcap));
		return Implementation::runtimeFeatures(hwcap);
#	else
		// No other (deinlined) implementation at the moment. The function should not be even defined here
		// in that case -- it's inlined in the headervinstead, including the x86 implementation.
#		error
#	endif
	}
#endif
}}