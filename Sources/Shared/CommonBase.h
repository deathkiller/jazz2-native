#pragma once

#if defined(__EMSCRIPTEN__)
#	define DEATH_TARGET_EMSCRIPTEN
#elif defined(_WIN32)
#	define DEATH_TARGET_WINDOWS
#elif defined(__ANDROID__)
#	define DEATH_TARGET_ANDROID
#elif defined(__APPLE__)
#	include <TargetConditionals.h>
#	define DEATH_TARGET_APPLE
#	if TARGET_OS_IPHONE
#		define DEATH_TARGET_IOS
#	endif
#	if TARGET_IPHONE_SIMULATOR
#		define DEATH_TARGET_IOS
#		define DEATH_TARGET_IOS_SIMULATOR 
#	endif
#elif defined(__unix__) || defined(__linux__)
#	define DEATH_TARGET_UNIX
#endif

// First two is GCC/Clang for 32/64-bit, second two is MSVC 32/64-bit
#if defined(__i386) || defined(__x86_64) || defined(_M_IX86) || defined(_M_X64)
#	define DEATH_TARGET_X86

// First two is GCC/Clang for 32/64-bit, second two is MSVC 32/64-bit.
#elif defined(__arm__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
#	define DEATH_TARGET_ARM

// First two is GCC/Clang, third is MSVC. Not sure about 64-bit MSVC.
#elif defined(__powerpc__) || defined(__powerpc64__) || defined(_M_PPC)
#	define DEATH_TARGET_POWERPC

// This is for both RISC-V 32-bit and 64-bit.
#elif defined(__riscv)
#	define DEATH_TARGET_RISCV

// WebAssembly (on Emscripten). Old pure asm.js toolchains did not define this, recent Emscripten does that even with `-s WASM=0`.
#elif defined(__wasm__)
#	define DEATH_TARGET_WASM

#endif

// Sanity checks. This might happen when using Emscripten-compiled code with native compilers, at which point we should just die.
#if defined(DEATH_TARGET_EMSCRIPTEN) && (defined(DEATH_TARGET_X86) || defined(DEATH_TARGET_ARM) || defined(DEATH_TARGET_POWERPC) || defined(DEATH_TARGET_RISCV))
#	error DEATH_TARGET_X86 / _ARM / _POWERPC defined on Emscripten
#endif

// 64-bit WebAssembly macro was tested by passing -m64 to emcc
#if !defined(__x86_64) && !defined(_M_X64) && !defined(__aarch64__) && !defined(_M_ARM64) && !defined(__powerpc64__) && !defined(__wasm64__)
#	define DEATH_TARGET_32BIT
#endif

// C++ standard
#if defined(_MSC_VER)
#	if defined(_MSVC_LANG)
#		define DEATH_CXX_STANDARD _MSVC_LANG
#	else
#		define DEATH_CXX_STANDARD 201103L
#	endif
#else
#	define DEATH_CXX_STANDARD __cplusplus
#endif

// <ciso646> is deprecated and replaced by <version> in C++20
#if DEATH_CXX_STANDARD >= 202002L
#	include <version>
#else
#	include <ciso646>
#endif
#if defined(_LIBCPP_VERSION)
#	define DEATH_TARGET_LIBCXX
#elif defined(_CPPLIB_VER)
#	define DEATH_TARGET_DINKUMWARE
#elif defined(__GLIBCXX__)
#	define DEATH_TARGET_LIBSTDCXX
// GCC's <ciso646> provides the __GLIBCXX__ macro only since 6.1, so on older versions we'll try to get it from <bits/c++config.h>
#elif defined(__has_include)
#	if __has_include(<bits/c++config.h>)
#		include <bits/c++config.h>
#		if defined(__GLIBCXX__)
#			define DEATH_TARGET_LIBSTDCXX
#		endif
#	endif
// GCC < 5.0 doesn't have __has_include, so on these versions we'll just assume it's libstdc++ as I don't think those versions
// are used with anything else nowadays anyway. Clang reports itself as GCC 4.4, so exclude that one.
#elif defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 5
#	define DEATH_TARGET_LIBSTDCXX
#else
// Otherwise no idea
#endif

#if defined(__GNUC__)
#	define DEATH_TARGET_GCC
#endif

#if defined(__clang__)
#	define DEATH_TARGET_CLANG
#endif

#if defined(__clang__) && defined(_MSC_VER)
#	define DEATH_TARGET_CLANG_CL
#endif

#if defined(__clang__) && defined(__apple_build_version__)
#	define DEATH_TARGET_APPLE_CLANG
#endif

#if defined(__INTEL_LLVM_COMPILER)
#	define DEATH_TARGET_INTEL_LLVM
#endif

#if defined(_MSC_VER)
#	define DEATH_TARGET_MSVC
#	if _MSC_VER <= 1900
#		define DEATH_MSVC2015_COMPATIBILITY
#	endif
#	if _MSC_VER <= 1930
#		define DEATH_MSVC2019_COMPATIBILITY
#	endif
#endif

#if defined(__CYGWIN__)
#	define DEATH_TARGET_CYGWIN
#elif defined(__MINGW32__)
#	define DEATH_TARGET_MINGW
#endif

// First checking the GCC/Clang builtin, if available. As a fallback do an architecture-based check, which is mirrored
// from SDL_endian.h. Doing this *properly* would mean we can't decide this at compile time as some architectures allow
// switching endianness at runtime (and worse, have per-page endianness). So let's pretend we never saw this article:
// https://en.wikipedia.org/wiki/Endianness#Bi-endianness
// For extra safety this gets runtime-tested in TargetTest, so when porting to a new platform, make sure you run that test.
#if defined(__BYTE_ORDER__)
#	if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#		define DEATH_TARGET_BIG_ENDIAN
#	elif __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#		error What kind of endianness is this?
#	endif
#elif defined(__hppa__) || \
	defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
	(defined(__MIPS__) && defined(__MIPSEB__)) || \
	defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
	defined(__sparc__)
#	define DEATH_TARGET_BIG_ENDIAN
#endif

// Compile-time CPU feature detection
#if defined(DEATH_TARGET_X86)

// SSE on GCC: https://stackoverflow.com/a/28939692
#if defined(DEATH_TARGET_GCC)
#	if defined(__SSE2__)
#		define DEATH_TARGET_SSE2
#	endif
#	if defined(__SSE3__)
#		define DEATH_TARGET_SSE3
#	endif
#	if defined(__SSSE3__)
#		define DEATH_TARGET_SSSE3
#	endif
#	if defined(__SSE4_1__)
#		define DEATH_TARGET_SSE41
#	endif
#	if defined(__SSE4_2__)
#		define DEATH_TARGET_SSE42
#	endif

// On MSVC: https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros
#elif defined(DEATH_TARGET_MSVC)
// _M_IX86_FP is defined only on 32bit, 64bit has SSE2 always (so we need to detect 64bit instead: https://stackoverflow.com/a/18570487)
#	if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_AMD64) || defined(_M_X64)
#		define DEATH_TARGET_SSE2
#	endif
// On MSVC there's no way to detect SSE3 and newer, these are only implied by AVX as far as I can tell
#	if defined(__AVX__)
#		define DEATH_TARGET_SSE3
#		define DEATH_TARGET_SSSE3
#		define DEATH_TARGET_SSE41
#		define DEATH_TARGET_SSE42
#	endif
#endif

// Both GCC and MSVC have the same macros for AVX, AVX2 and AVX512F
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_MSVC)
#	if defined(__AVX__)
#		define DEATH_TARGET_AVX
#	endif
#	if defined(__AVX2__)
#		define DEATH_TARGET_AVX2
#	endif
#	if defined(__AVX512F__)
#		define DEATH_TARGET_AVX512F
#	endif
#endif

// POPCNT, LZCNT and BMI1 on GCC, queried wth `gcc -mpopcnt -dM -E - | grep POPCNT`, and equivalent for others.
#if defined(DEATH_TARGET_GCC)
#	if defined(__POPCNT__)
#		define DEATH_TARGET_POPCNT
#	endif
#	if defined(__LZCNT__)
#		define DEATH_TARGET_LZCNT
#	endif
#	if defined(__BMI__)
#		define DEATH_TARGET_BMI1
#	endif
#	if defined(__BMI2__)
#		define DEATH_TARGET_BMI2
#	endif

// There doesn't seem to be any equivalent on MSVC, https://github.com/kimwalisch/libpopcnt assumes POPCNT is on x86 MSVC
// always, and LZCNT has encoding compatible with BSR so if not available it'll not crash but produce wrong results, sometimes.
// Enabling them always feels a bit too much, so instead going with what clang-cl uses. There /arch:AVX matches -march=sandybridge:
// https://github.com/llvm/llvm-project/blob/6542cb55a3eb115b1c3592514590a19987ffc498/clang/lib/Driver/ToolChains/Arch/X86.cpp#L46-L58
// and `echo | clang -march=sandybridge -dM -E -` lists only POPCNT, while /arch:AVX2 matches -march=haswell, which lists also LZCNT, BMI and BMI2.
#elif defined(DEATH_TARGET_MSVC)
// For extra robustness on clang-cl check the macros explicitly -- as with other AVX+ intrinsics, these are only included
// if the corresponding macro is defined as well. Failing to do so would mean the DEATH_ENABLE_AVX_{POPCNT,LZCNT,BMI1} macros
// are defined always, incorrectly implying presence of these intrinsics.
#	if defined(__AVX__)
#		if !defined(DEATH_TARGET_CLANG_CL) || defined(__POPCNT__)
#			define DEATH_TARGET_POPCNT
#		endif
#	endif
#	if defined(__AVX2__)
#		if !defined(DEATH_TARGET_CLANG_CL) || defined(__LZCNT__)
#			define DEATH_TARGET_LZCNT
#		endif
#		if !defined(DEATH_TARGET_CLANG_CL) || defined(__BMI__)
#			define DEATH_TARGET_BMI1
#		endif
#		if !defined(DEATH_TARGET_CLANG_CL) || defined(__BMI2__)
#			define DEATH_TARGET_BMI2
#		endif
#	endif
#endif

// On GCC, F16C and FMA have its own define, on MSVC FMA is implied by /arch:AVX2 (https://docs.microsoft.com/en-us/cpp/build/reference/arch-x86),
// no mention of F16C but https://walbourn.github.io/directxmath-f16c-and-fma/ says it's like that so I'll believe that.
// However, comments below https://stackoverflow.com/a/50829580 say there is a Via processor with AVX2 but no FMA, so then
// the __AVX2__ check isn't really bulletproof. Use runtime detection where possible, please.
#if defined(DEATH_TARGET_GCC)
#	if defined(__F16C__)
#		define DEATH_TARGET_AVX_F16C
#	endif
#	if defined(__FMA__)
#		define DEATH_TARGET_AVX_FMA
#	endif
#elif defined(DEATH_TARGET_MSVC) && defined(__AVX2__)
// On clang-cl /arch:AVX2 matches -march=haswell:
// https://github.com/llvm/llvm-project/blob/6542cb55a3eb115b1c3592514590a19987ffc498/clang/lib/Driver/ToolChains/Arch/X86.cpp#L46-L58
// And `echo | clang -march=haswell -dM -E -` lists both F16C and FMA. However, for robustness, check the macros
// explicitly -- as with other AVX+ intrinsics on clang-cl, these are only included if the corresponding macro is defined
// as well. Failing to do so would mean the DEATH_ENABLE_AVX_{16C,FMA} macros are defined always, incorrectly implying
// presence of these intrinsics.
#	if !defined(DEATH_TARGET_CLANG_CL) || defined(__F16C__)
#		define DEATH_TARGET_AVX_F16C
#	endif
#	if !defined(DEATH_TARGET_CLANG_CL) || defined(__FMA__)
#		define DEATH_TARGET_AVX_FMA
#	endif
#endif

// https://stackoverflow.com/a/37056771, confirmed on Android NDK Clang that __ARM_NEON is indeed still set.
// For MSVC, according to https://docs.microsoft.com/en-us/cpp/intrinsics/arm-intrinsics I would assume that since they
// use a standard header, they also expose the standard macro name, even though not listed among their predefined macros?
// Needs testing, though.
#elif defined(DEATH_TARGET_ARM)
#	if defined(__ARM_NEON)
#		define DEATH_TARGET_NEON
// NEON FMA is available only if __ARM_FEATURE_FMA is defined and some bits of __ARM_NEON_FP as well
// (ARM C Language Extensions 1.1, §6.5.5: https://developer.arm.com/documentation/ihi0053/b/). On ARM64 NEON is
// implicitly supported and __ARM_NEON_FP might not be defined (Android Clang defines it but GCC 9 on Ubuntu ARM64 not),
// so check for __aarch64__ as well.
#		if defined(__ARM_FEATURE_FMA) && (__ARM_NEON_FP || defined(__aarch64__))
#			define DEATH_TARGET_NEON_FMA
#		endif
// There's no linkable documentation for anything and the PDF is stupid. But, given the FP16 instructions implemented
// in GCC and Clang are guarded by this macro, it should be alright: https://gcc.gnu.org/legacy-ml/gcc-patches/2016-06/msg00460.html
#		if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC)
#			define DEATH_TARGET_NEON_FP16
#		endif
#	endif

// Undocumented, checked via `echo | em++ -x c++ -dM -E - -msimd128`. Restricting to the finalized SIMD variant,
// which is since Clang 13: https://github.com/llvm/llvm-project/commit/502f54049d17f5a107f833596fb2c31297a99773
// Emscripten 2.0.13 sets Clang 13 as the minimum, however it doesn't imply that emsdk 2.0.13 actually contains the final
// Clang 13. That's only since 2.0.18, thus to avoid nasty issues we have to check Emscripten version as well :(
// https://github.com/emscripten-core/emscripten/commit/deab7783df407b260f46352ffad2a77ca8fb0a4c
#elif defined(DEATH_TARGET_WASM)
#	if defined(__wasm_simd128__) && __clang_major__ >= 13 && __EMSCRIPTEN_major__*10000 + __EMSCRIPTEN_minor__*100 + __EMSCRIPTEN_tiny__ >= 20018
#		define DEATH_TARGET_SIMD128
#	endif
#endif

// Kill switch for when presence of a sanitizer is detected and DEATH_CPU_USE_IFUNC is enabled. Unfortunately in our case the
// __attribute__((no_sanitize_address)) workaround as described on https://github.com/google/sanitizers/issues/342 doesn't
// work / can't be used because it would mean marking basically everything including the actual implementation that's being dispatched to.
#if defined(DEATH_CPU_USE_IFUNC)
#	if defined(__has_feature)
#		if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#			define _DEATH_SANITIZER_IFUNC_DETECTED
#		endif
#	elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#		define _DEATH_SANITIZER_IFUNC_DETECTED
#	endif
#	if defined(_DEATH_SANITIZER_IFUNC_DETECTED)
#		 error The library was built with DEATH_CPU_USE_IFUNC, which is incompatible with sanitizers. Rebuild without this option or disable sanitizers.
#	endif
#endif

/**
	@brief Whether `long double` has the same precision as `double`

	Defined on platforms where the @cpp long double @ce type has a 64-bit precision instead of 80-bit, thus same as @cpp double @ce.
	It's the case for @ref DEATH_TARGET_MSVC "MSVC" ([source](https://docs.microsoft.com/en-us/previous-versions/9cx8xs15(v=vs.140))),
	32-bit @ref DEATH_TARGET_ANDROID "Android" (no reliable source found, sorry), @ref DEATH_TARGET_EMSCRIPTEN "Emscripten"
	and @ref DEATH_TARGET_APPLE "Mac" (but not @ref DEATH_TARGET_IOS "iOS") with @ref DEATH_TARGET_ARM "ARM" processors.
	Emscripten is a bit special because it's @cpp long double @ce is *sometimes* 80-bit, but its precision differs from the 80-bit
	representation elsewhere, so it's always treated as 64-bit. Note that even though the type size and precision may be the same,
	these are still two distinct types, similarly to how @cpp int @ce and @cpp signed int @ce behave the same but are treated as different types.
*/
#if defined(DEATH_TARGET_MSVC) || (defined(DEATH_TARGET_ANDROID) && !__LP64__) || defined(DEATH_TARGET_EMSCRIPTEN) || (defined(DEATH_TARGET_APPLE) && !defined(DEATH_TARGET_IOS) && defined(DEATH_TARGET_ARM))
#	define DEATH_LONG_DOUBLE_SAME_AS_DOUBLE
#endif

/**
	@brief Whether source location built-ins are supported

	Defined if compiler-specific builtins used to implement the C++20 std::source_location feature are available.
	Defined on GCC at least since version 4.8, Clang 9+ and MSVC 2019 16.6 and newer; on all three they're present also in the C++11 mode.
*/
#if (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG)) || (defined(DEATH_TARGET_CLANG) && ((defined(__apple_build_version__) && __clang_major__ >= 12) || (!defined(__apple_build_version__) && __clang_major__ >= 9))) || (defined(DEATH_TARGET_MSVC) && _MSC_VER >= 1926)
#	define DEATH_SOURCE_LOCATION_BUILTINS_SUPPORTED
#endif

/** @brief Deprecation mark */
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
#	define DEATH_DEPRECATED(message) __attribute((deprecated(message)))
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_DEPRECATED(message) __declspec(deprecated(message))
#else
#	define DEATH_DEPRECATED(message)
#endif

/** @brief Unused variable mark */
#if defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 10
#	define DEATH_UNUSED [[maybe_unused]]
#elif defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_UNUSED __attribute__((__unused__))
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_UNUSED __pragma(warning(suppress:4100))
#else
#	define DEATH_UNUSED
#endif

/** @brief Switch case fall-through */
#if (defined(DEATH_TARGET_MSVC) && _MSC_VER >= 1926 && DEATH_CXX_STANDARD >= 201703) || (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 7)
#	define DEATH_FALLTHROUGH [[fallthrough]];
#elif defined(DEATH_TARGET_CLANG)
// Clang unfortunately warns that [[fallthrough]] is a C++17 extension, so we use this instead of attempting to suppress the warning
#	define DEATH_FALLTHROUGH [[clang::fallthrough]];
#else
#	define DEATH_FALLTHROUGH
#endif

/** @brief Thread-local annotation */
#if defined(DEATH_TARGET_APPLE) && defined(__has_feature)
#	if !__has_feature(cxx_thread_local) /* Apple Clang 7.3 says false here */
#		define DEATH_THREAD_LOCAL __thread
#	endif
#endif
#if !defined(DEATH_THREAD_LOCAL) /* Assume it's supported otherwise */
#	define DEATH_THREAD_LOCAL thread_local
#endif

/** @brief C++14 constexpr annotation (if C++14 or newer standard is enabled and the compiler implements C++14 relaxed constexpr rules) */
#if DEATH_CXX_STANDARD >= 201402 && !defined(DEATH_MSVC2015_COMPATIBILITY)
#	define DEATH_CONSTEXPR14 constexpr
#else
#	define DEATH_CONSTEXPR14
#endif

/** @brief C++20 constexpr annotation (if C++20 or newer standard is enabled and the compiler implements all C++20 constexpr additions) */
#if __cpp_constexpr >= 201907
#	define DEATH_CONSTEXPR20 constexpr
#else
#	define DEATH_CONSTEXPR20
#endif

/** @brief Always inline a function */
#if defined(DEATH_TARGET_GCC)
#	define DEATH_ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ALWAYS_INLINE __forceinline
#else
#	define DEATH_ALWAYS_INLINE inline
#endif

/** @brief Never inline a function */
#if defined(DEATH_TARGET_GCC)
#	define DEATH_NEVER_INLINE __attribute__((noinline))
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_NEVER_INLINE __declspec(noinline)
#else
#	define DEATH_NEVER_INLINE
#endif

/** @brief Hint for compiler that for the lifetime of the pointer, no other pointer will be used to access the object it points to */
#if defined(__cplusplus)
#	define DEATH_RESTRICT __restrict
#else
#	define DEATH_RESTRICT restrict
#endif

/** @brief Hint for compiler to assume a condition */
#if !defined(DEATH_ASSUME)
#	if defined(DEATH_TARGET_CLANG)
#		define DEATH_ASSUME(condition) __builtin_assume(condition)
#	elif defined(DEATH_TARGET_MSVC)
#		define DEATH_ASSUME(condition) __assume(condition)
#	elif defined(DEATH_TARGET_GCC)
#		if __GNUC__ >= 13
#			define DEATH_ASSUME(condition) __attribute__((assume(condition)))
#		else
#			define DEATH_ASSUME(condition)							\
				do {												\
					if(!(condition)) __builtin_unreachable();		\
				} while(false)
#		endif
#	else
#		define DEATH_ASSUME(condition) do {} while(false)
#	endif
#endif

/** @brief Mark an if condition as likely to happen */
#if (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 10) || (DEATH_CXX_STANDARD > 201703 && ((defined(DEATH_TARGET_CLANG) && !defined(DEATH_TARGET_APPLE_CLANG) && __clang_major__ >= 12) || (defined(DEATH_TARGET_MSVC) && _MSC_VER >= 1926)))
#	define DEATH_LIKELY(...) (__VA_ARGS__) [[likely]]
#elif defined(DEATH_TARGET_GCC)
#	define DEATH_LIKELY(...) (__builtin_expect((__VA_ARGS__), 1))
#else
#	define DEATH_LIKELY(...) (__VA_ARGS__)
#endif

/** @brief Mark an if condition as unlikely to happen */
#if (defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG) && __GNUC__ >= 10) || (DEATH_CXX_STANDARD > 201703 && ((defined(DEATH_TARGET_CLANG) && !defined(DEATH_TARGET_APPLE_CLANG) && __clang_major__ >= 12) || (defined(DEATH_TARGET_MSVC) && _MSC_VER >= 1926)))
#	define DEATH_UNLIKELY(...) (__VA_ARGS__) [[unlikely]]
#elif defined(DEATH_TARGET_GCC)
#	define DEATH_UNLIKELY(...) (__builtin_expect((__VA_ARGS__), 0))
#else
#	define DEATH_UNLIKELY(...) (__VA_ARGS__)
#endif

/** @brief Passthrough */
#define DEATH_PASSTHROUGH(...) __VA_ARGS__
/** @brief No-op */
#define DEATH_NOOP(...)

// Internal macro implementation
#define __DEATH_PASTE(a, b) a ## b
#define __DEATH_HELPER_STR(x) #x
#define __DEATH_LINE_STRING_IMPLEMENTATION(...) __DEATH_HELPER_STR(__VA_ARGS__)

/** @brief Paste two tokens together */
#define DEATH_PASTE(a, b) __DEATH_PASTE(a, b)

/** @brief Line number as a string */
#define DEATH_LINE_STRING __DEATH_LINE_STRING_IMPLEMENTATION(__LINE__)
