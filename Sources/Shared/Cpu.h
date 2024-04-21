// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
// Copyright © 2023 Robert Clausecker <fuz@FreeBSD.org>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#pragma once

#include <type_traits>

#include "Common.h"

// Because can't use inline assembly when targeting 64bit on MSVC, and because <intrin.h> and <immintrin.h> is just
// too damn heavy to be included in a header. Declarations copied verbatim. Clang-cl doesn't like this (undefined
// reference to __cpuidex), so using the GCC/Clang codepath on it instead.
#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL) && defined(DEATH_TARGET_X86)
extern "C" {
	void __cpuidex(int[4], int, int);
	unsigned __int64 __cdecl _xgetbv(unsigned int);
}
#endif

/**
	@brief Compile-time and runtime CPU instruction set detection and dispatch

	This namespace provides *tags* for x86, ARM and WebAssembly instruction sets,
	which can be used for either system introspection or for choosing a particular
	implementation based on the available instruction set. These tags build on top
	of the @ref DEATH_TARGET_SSE2, @ref DEATH_TARGET_SSE3 etc. preprocessor
	macros and provide a runtime feature detection as well.

	@section Cpu-usage Usage

	The @ref Cpu namespace contains tags such as @ref Cpu::Avx2, @ref Cpu::Sse2,
	@ref Cpu::Neon or @ref Cpu::Simd128. These tags behave similarly to enum values
	and their combination result in @ref Cpu::Features.

	The most advanced base CPU instruction set enabled at compile time is then
	exposed through the @ref Cpu::DefaultBase variable, which is an alias to one of
	those tags, and it matches the architecture-specific @ref DEATH_TARGET_SSE2
	etc. macros. Since it's a @cpp constexpr @ce variable, it's usable in a
	compile-time context.

	@m_class{m-note m-info}

	@par
		If you're writing multiplatform code targeting multiple architectures, you
		still need to partially rely on the preprocessor when using the
		architecture-specific tags, as those are defined only on the architecture
		they apply to. The above would need to be wrapped in
		@cpp #ifdef DEATH_TARGET_X86 @ce; if you would be checking for
		@ref Cpu::Neon instead, then you'd need to wrap it in a
		@ref DEATH_TARGET_ARM check. On the other hand, the per-architecture tags
		are available on given architecture always --- so for example
		@ref Cpu::Avx512f is present even on a compiler that doesn't even recognize
		AVX-512 yet.

	@subsection Cpu-usage-dispatch-compile-time Dispatching on available CPU instruction set at compile time

	The main purpose of these tags, however, is to provide means for a compile-time
	overload resolution. In other words, picking the best candidate among a set of
	functions implemented with various instruction sets. As an example, let's say
	you have three different implementations of a certain algorithm transforming
	numeric data. One is using AVX2 instructions, another is a slower variant using
	just SSE 4.2 and as a fallback there's one with just regular scalar code. To
	distinguish them, the functions have the same name, but use a different *tag
	type*.

	Then you can either call a particular implementation directly --- for example
	to test it --- or you can pass @ref Cpu::DefaultBase, and it'll pick the best
	overload candidate for the set of CPU instruction features enabled at compile
	time:

	-   If the user code was compiled with AVX2 or higher enabled, the
		@ref Cpu::Avx2 overload will be picked.
	-   Otherwise, if just AVX, SSE 4.2 or anything else that includes SSE 4.2 was
		enabled, the @ref Cpu::Sse42 overload will be picked.
	-   Otherwise (for example when compiling for generic x86-64 that has just the
		SSE2 feature set), the @ref Cpu::Scalar overload will be picked. If you
		wouldn't provide this overload, the compilation would fail for such a
		target --- which is useful for example to enforce a certain CPU feature set
		to be enabled in order to use a certain API.

	@m_class{m-block m-warning}

	@par SSE3, SSSE3, SSE4.1/SSE4.2, POPCNT, LZCNT, BMI1, BMI2, AVX F16C and AVX FMA on MSVC
		A special case worth mentioning are SSE3 and newer instructions on Windows.
		MSVC only provides a very coarse `/arch:SSE2`, `/arch:AVX` and `/arch:AVX2`
		for either @ref Sse2, @ref Avx or @ref Avx2, but nothing in between. That
		means it's impossible to rely just on compile-time detection to use the
		later SSE features on machines that don't support AVX yet (or the various
		AVX additions on machines without AVX2), you have to use runtime dispatch
		there, as shown below.

	@subsection Cpu-usage-dispatch-runtime Runtime detection and manual dispatch

	So far that was all compile-time detection, which has use mainly when a binary
	can be optimized directly for the machine it will run on. But such approach is
	not practical when shipping to a heterogenous set of devices. Instead, the
	usual workflow is that the majority of code uses the lowest common
	denominator (such as SSE2 on x86), with the most demanding functions having
	alternative implementations --- picked at runtime --- that make use of more
	advanced instructions for better performance.

	Runtime detection is exposed through @ref Cpu::runtimeFeatures(). It will
	detect CPU features on platforms that support it, and fall back to
	@ref Cpu::compiledFeatures() on platforms that don't. You can then match the
	returned @ref Cpu::Features against particular tags to decide which variant to use.

	While such approach gives you the most control, manually managing the dispatch
	branches is error prone and the argument passthrough may also add nontrivial
	overhead. See below for an
	@ref Cpu-usage-automatic-runtime-dispatch "efficient automatic runtime dispatch".

	@section Cpu-usage-extra Usage with extra instruction sets

	Besides the base instruction set, which on x86 is @ref Sse2 through
	@ref Avx512f, with each tag being a superset of the previous one, there are
	* *extra* instruction sets such as @ref Popcnt or @ref AvxFma.

	The process of defining and dispatching to function variants that include extra
	instruction sets gets moderately more complex, however. As shown on the diagram
	below, those are instruction sets that neither fit into the hierarchy nor are
	unambiguously included in a later instruction set. For example, some CPUs are
	known to have @ref Avx and just @ref AvxFma, some @ref Avx and just
	@ref AvxF16c and there are even CPUs with @ref Avx2 but no @ref AvxFma.

	While there's no possibility of having a total ordering between all possible
	combinations for dispatching, the following approach is chosen:

	1.  The base instruction set has the main priority. For example, if both an
		@ref Avx2 and a @ref Sse2 variant are viable candidates, the @ref Avx2
		variant gets picked, even if the @ref Sse2 variant uses extra
		instruction sets that the @ref Avx2 doesn't.
	2.  After that, the variant with the most extra instruction sets is chosen. For
		example, an @ref Avx + @ref AvxFma variant is chosen over plain @ref Avx.

	On the declaration side, the desired base instruction set gets ORed with as
	many extra instruction sets as needed, and then wrapped in a
	@ref DEATH_CPU_DECLARE() macro. For example, a lookup algorithm may have a
	@ref Sse41 implementation which however also relies on @ref Popcnt and
	@ref Lzcnt, and a fallback @ref Sse2 implementation that uses neither.

	And a concrete overload gets picked at compile-time by passing a desired
	combination of CPU tags as well --- or @ref Default for the set of features
	enabled at compile time --- this time wrapped in a @ref DEATH_CPU_SELECT().

	@section Cpu-usage-target-attributes Enabling instruction sets for particular functions

	On GCC and Clang, a machine target has to be enabled in order to use a
	particular CPU instruction set or its intrinsics. While it's possible to do
	that for the whole compilation unit by passing for example `-mavx2` to the
	compiler, it would force you to create dedicated files for every architecture
	variant you want to support. Instead, it's possible to equip particular
	functions with *target attributes* defined by @ref DEATH_ENABLE_SSE2 and
	related macros, which then makes a particular instruction set enabled for given
	function.

	In contrast, MSVC doesn't restrict intrinsics usage in any way, so you can
	freely call e.g. AVX2 intrinsics even if the whole file is compiled with just
	SSE2 enabled. The @ref DEATH_ENABLE_SSE2 and related macros are thus defined
	to be empty on this compiler.

	@m_class{m-note m-warning}

	@par
		On the other hand, on MSVC, using just the baseline target on the file
		level means the compiler will not be able to use any advanced instructions
		apart from what you call explicitly via intrinsics. You can try extracting
		all AVX+ variants into a dedicated file with `/arch:AVX` enabled and see
		if it makes any performance difference.

	For developer convenience, the @ref DEATH_ENABLE_SSE2 etc. macros are defined
	only on matching architectures, and generally only if the compiler itself has
	given feature set implemented and usable. Which means you can easily use them
	to @cpp #ifdef @ce your variants to be compiled only where it makes sense, or
	even guard intrinsics includes with them to avoid including potentially heavy
	headers you won't use anyway. In comparison, using the @ref DEATH_TARGET_SSE2
	etc. macros would only make the variant available if the whole compilation unit
	has a corresponding `-m` or `/arch:` option passed to the compiler.

	Finally, the @ref DEATH_ENABLE() function allows multiple instruction sets to
	be enabled at the same time in a more concise way and consistently on both GCC
	and Clang.

	Definitions of the `lookup()` function variants from above would then look like
	below with the target attributes added. The extra instruction sets get
	explicitly enabled as well, in contrast a scalar variant would have no
	target-specific annotations at all.

	@section Cpu-usage-automatic-runtime-dispatch Automatic runtime dispatch

	Similarly to how the best-matching function variant can be picked at compile
	time, there's a possibility to do the same at runtime without maintaining a
	custom dispatch code for each case
	@ref Cpu-usage-dispatch-runtime "as was shown above". To avoid having to
	dispatch on every call and to remove the argument passthrough overhead, all
	variants need to have the same function signature, separate from the CPU tags.
	That's achievable by putting them into lambdas with a common signature, and
	returning that lambda from a wrapper function that contains the CPU tag. After
	that, a runtime dispatcher function that is created with the
	@ref DEATH_CPU_DISPATCHER_BASE() macro.

	The macro creates an overload of the same name, but taking @ref Features
	instead, and internally dispatches to one of the overloads using the same rules
	as in the compile-time dispatch. Which means you can now call it with e.g.
	@ref runtimeFeatures(), get a function pointer back and then call it with the
	actual arguments.

	@m_class{m-block m-danger }

	@par Instruction enabling macros and lambdas
		An important difference with the @ref DEATH_ENABLE_SSE2 "DEATH_ENABLE_*"
		macros is that they now have to go also directly next to the lambda as GCC
		[currently doesn't propagate the attributes](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80439)
		from the wrapper function to the nested lambda. To make matters worse,
		older versions of Clang suffer from the inverse problem and ignore lambda
		attributes, so you have to specify them on both the lambda and the wrapper
		function. GCC 9.1 to 9.3 also has a [bug where it can't parse attributes on lambdas with a trailing return type](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=90333).
		The preferrable solution is to not use a trailing return type at the cost
		of potentially more verbose @cpp return @ce statements. Alternatively you
		can require version 8, 9.4 or 10 instead, but note that 9.3 is the default
		compiler on Ubuntu 20.04.

	@subsection Cpu-usage-automatic-runtime-dispatch-extra Automatic runtime dispach with extra instruction sets

	If the variants are tagged with extra instruction sets instead of just the
	base instruction set like in the @cpp lookup() @ce case
	@ref Cpu-usage-extra "shown above", you'll use the @ref _DEATH_CPU_DISPATCHER()
	macro instead. There, to avoid a combinatorial explosion of cases to check,
	you're expected to list the actual extra tags the overloads use.

	If some extra instruction sets are always used together (like it is above with
	@ref Popcnt and @ref Lzcnt), you can reduce the amount of tested combinations
	by specifying them as a single ORed argument instead.
	On the call side, there's no difference compared to using just the base
	instruction sets. The created dispatcher function takes @ref Features as well.

	@section Cpu-usage-automatic-cached-dispatch Automatic cached dispatch

	Ultimately, the dispatch can be performed implicitly, exposing only the final
	function or a function pointer, with no additional steps needed from the user
	side. There's three possible scenarios with varying performance tradeoffs.
	Continuing from the @cpp lookupImplementation() @ce example above:

	<ul>
	<li>
	On Linux and Android with API 30+ it's possible to use the
	[GNU IFUNC](https://sourceware.org/glibc/wiki/GNU_IFUNC) mechanism, where the
	dynamic linker performs a dispatch during the early startup. This is the
	fastest variant of runtime dispatch, as it results in an equivalent of a
	regular dynamic library function call. Assuming a dispatcher was created using
	either @ref DEATH_CPU_DISPATCHER() or @ref DEATH_CPU_DISPATCHER_BASE(),
	it's implemented using the @ref DEATH_CPU_DISPATCHED_IFUNC() macro.
	</li>
	<li>
	On platforms where IFUNC isn't available, a function pointer can be used
	for runtime dispatch instead. It's one additional indirection, which may have a
	visible effect if the dispatched-to code is relatively tiny and is called from
	within a tight loop. Assuming a dispatcher was created using
	either @ref DEATH_CPU_DISPATCHER() or @ref DEATH_CPU_DISPATCHER_BASE(),
	it's implemented using the @ref DEATH_CPU_DISPATCHED_POINTER() macro.
	</li>
	<li>
	For the least amount of overhead, the compile-time dispatch can be used, with
	arguments passed through by hand. Similarly to IFUNC, this will also result in
	a regular function, but without the indirect overhead. Furthermore, since it's
	a direct call to the lambda inside, compiler optimizations will fully inline
	its contents, removing any remaining overhead and allowing LTO and other
	inter-procedural optimizations that wouldn't be possible with the indirect
	calls. This option is best suited for scenarios where it's possible to build
	and optimize code for a single target platform. In this case it calls directly
	to the original variants, so no macro is needed and
	@ref DEATH_CPU_DISPATCHER() / @ref DEATH_CPU_DISPATCHER_BASE() is not
	needed either.

	</li>
	</ul>

	With all three cases, you end up with either a function or a function pointer.
	The macro signatures are deliberately similar to each other and to the direct
	function declaration to make it possible to unify them under a single wrapper
	macro in case a practical use case needs to handle more than one variant.
*/
namespace Death { namespace Cpu {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Traits class for CPU detection tag types

		Useful for detecting tag properties at compile time without the need for
		repeated code such as method overloading, cascaded ifs or template
		specializations for all tag types. All tag types in the @ref Cpu namespace
		have this class implemented.
		@see @ref tag(), @ref features()
	*/
	template<class> struct TypeTraits;

	namespace Implementation
	{
		// A common type used in all tag constructors to avoid ambiguous calls when using { }
		struct InitT { };
		constexpr InitT Init{ };

		enum: unsigned int { ExtraTagBitOffset = 16 };
	}

	/**
		@brief Scalar tag type

		See the @ref Cpu namespace and the @ref Scalar tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct ScalarT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit ScalarT(Implementation::InitT) { }
	};

	// Scalar code is when nothing else is available, thus no bits set
	template<> struct TypeTraits<ScalarT> {
		enum: unsigned int { Index = 0 };
		static const char* name() { return "Scalar"; }
	};

#if defined(DEATH_TARGET_X86)
	/**
		@brief SSE2 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Sse2 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Sse2T: ScalarT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Sse2T(Implementation::InitT): ScalarT{Implementation::Init} { }
	};

	/**
		@brief SSE3 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Sse3 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Sse3T: Sse2T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Sse3T(Implementation::InitT): Sse2T{Implementation::Init} { }
	};

	/**
		@brief SSSE3 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Ssse3 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Ssse3T: Sse3T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Ssse3T(Implementation::InitT): Sse3T{Implementation::Init} { }
	};

	/**
		@brief SSE4.1 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Sse41T tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Sse41T: Ssse3T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Sse41T(Implementation::InitT): Ssse3T{Implementation::Init} { }
	};

	/**
		@brief SSE4.2 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Sse42T tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Sse42T: Sse41T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Sse42T(Implementation::InitT): Sse41T{Implementation::Init} { }
	};

	/**
		@brief POPCNT tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Popcnt tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct PopcntT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit PopcntT(Implementation::InitT) { }
	};

	/**
		@brief LZCNT tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Lzcnt tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct LzcntT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit LzcntT(Implementation::InitT) { }
	};

	/**
		@brief BMI1 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Bmi1 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Bmi1T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Bmi1T(Implementation::InitT) { }
	};

	/**
		@brief BMI2 tag type
		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Bmi2 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Bmi2T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Bmi2T(Implementation::InitT) { }
	};

	/**
		@brief AVX tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Avx tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct AvxT: Sse42T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit AvxT(Implementation::InitT): Sse42T{Implementation::Init} { }
	};

	/**
		@brief AVX F16C tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref AvxF16c tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct AvxF16cT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit AvxF16cT(Implementation::InitT) { }
	};

	/**
		@brief AVX FMA tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref AvxFma tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct AvxFmaT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit AvxFmaT(Implementation::InitT) { }
	};

	/**
		@brief AVX2 tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Avx2 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Avx2T: AvxT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Avx2T(Implementation::InitT): AvxT{Implementation::Init} { }
	};

	/**
		@brief AVX-512 Foundation tag type

		Available only on @ref DEATH_TARGET_X86 "x86". See the @ref Cpu namespace
		and the @ref Avx512f tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Avx512fT: Avx2T {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Avx512fT(Implementation::InitT): Avx2T{Implementation::Init} { }
	};

	// Features earlier in the hierarchy should have lower bits set
	template<> struct TypeTraits<Sse2T> {
		enum: unsigned int { Index = 1 << 0 };
		static const char* name() { return "Sse2"; }
	};
	template<> struct TypeTraits<Sse3T> {
		enum: unsigned int { Index = 1 << 1 };
		static const char* name() { return "Sse3"; }
	};
	template<> struct TypeTraits<Ssse3T> {
		enum: unsigned int { Index = 1 << 2 };
		static const char* name() { return "Ssse3"; }
	};
	template<> struct TypeTraits<Sse41T> {
		enum: unsigned int { Index = 1 << 3 };
		static const char* name() { return "Sse41"; }
	};
	template<> struct TypeTraits<Sse42T> {
		enum: unsigned int { Index = 1 << 4 };
		static const char* name() { return "Sse42"; }
	};
	template<> struct TypeTraits<AvxT> {
		enum: unsigned int { Index = 1 << 5 };
		static const char* name() { return "Avx"; }
	};
	template<> struct TypeTraits<Avx2T> {
		enum: unsigned int { Index = 1 << 6 };
		static const char* name() { return "Avx2"; }
	};
	template<> struct TypeTraits<Avx512fT> {
		enum: unsigned int { Index = 1 << 7 };
		static const char* name() { return "Avx512f"; }
	};

	// The total bit range should not be larger than ExtraTagCount
	template<> struct TypeTraits<PopcntT> {
		enum: unsigned int { Index = 1 << (0 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "Popcnt"; }
	};
	template<> struct TypeTraits<LzcntT> {
		enum: unsigned int { Index = 1 << (1 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "Lzcnt"; }
	};
	template<> struct TypeTraits<Bmi1T> {
		enum: unsigned int { Index = 1 << (2 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "Bmi1"; }
	};
	template<> struct TypeTraits<Bmi2T> {
		enum: unsigned int { Index = 1 << (3 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "Bmi2"; }
	};
	template<> struct TypeTraits<AvxF16cT> {
		enum: unsigned int { Index = 1 << (4 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "AvxF16c"; }
	};
	template<> struct TypeTraits<AvxFmaT> {
		enum: unsigned int { Index = 1 << (5 + Implementation::ExtraTagBitOffset) };
		static const char* name() { return "AvxFma"; }
	};
#endif

#if defined(DEATH_TARGET_ARM)
	/**
		@brief NEON tag type

		Available only on @ref DEATH_TARGET_ARM "ARM". See the @ref Cpu namespace
		and the @ref Neon tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct NeonT: ScalarT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit NeonT(Implementation::InitT): ScalarT{Implementation::Init} { }
	};

	/**
		@brief NEON FMA tag type

		Available only on @ref DEATH_TARGET_ARM "ARM". See the @ref Cpu namespace
		and the @ref NeonFma tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct NeonFmaT: NeonT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit NeonFmaT(Implementation::InitT): NeonT{Implementation::Init} { }
	};

	/**
		@brief NEON FP16 tag type

		Available only on @ref DEATH_TARGET_ARM "ARM". See the @ref Cpu namespace
		and the @ref NeonFp16 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct NeonFp16T: NeonFmaT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit NeonFp16T(Implementation::InitT): NeonFmaT{Implementation::Init} { }
	};

	template<> struct TypeTraits<NeonT> {
		enum: unsigned int { Index = 1 << 0 };
		static const char* name() { return "Neon"; }
	};
	template<> struct TypeTraits<NeonFmaT> {
		enum: unsigned int { Index = 1 << 1 };
		static const char* name() { return "NeonFma"; }
	};
	template<> struct TypeTraits<NeonFp16T> {
		enum: unsigned int { Index = 1 << 2 };
		static const char* name() { return "NeonFp16"; }
	};
#endif

#if defined(DEATH_TARGET_WASM)
	/**
		@brief SIMD128 tag type

		Available only on @ref DEATH_TARGET_WASM "WebAssembly". See the @ref Cpu
		namespace and the @ref Simd128 tag for more information.
		@see @ref tag(), @ref features()
	*/
	struct Simd128T: ScalarT {
		// Explicit constructor to avoid ambiguous calls when using { }
		constexpr explicit Simd128T(Implementation::InitT): ScalarT{Implementation::Init} { }
	};

	template<> struct TypeTraits<Simd128T> {
		enum: unsigned int { Index = 1 << 0 };
		static const char* name() { return "Simd128"; }
	};
#endif

	/**
		@brief Scalar tag

		Code that isn't explicitly optimized with any advanced CPU instruction set.
		Fallback if no other CPU instruction set is chosen or available. The next most
		widely supported instruction sets are @ref Sse2 on x86, @ref Neon on ARM and
		@ref Simd128 on WebAssembly.
	*/
	constexpr ScalarT Scalar { Implementation::Init };

#if defined(DEATH_TARGET_X86)
	/**
		@brief SSE2 tag

		[Streaming SIMD Extensions 2](https://en.wikipedia.org/wiki/SSE2). Available
		only on @ref DEATH_TARGET_X86 "x86", supported by all 64-bit x86 processors
		and is present on majority of contemporary 32-bit x86 processors as well.
		Superset of @ref Scalar, implied by @ref Sse3.
		@see @ref DEATH_TARGET_SSE2, @ref DEATH_ENABLE_SSE2
	*/
	constexpr Sse2T Sse2 { Implementation::Init };

	/**
		@brief SSE3 tag

		[Streaming SIMD Extensions 3](https://en.wikipedia.org/wiki/SSE3). Available
		only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Sse2, implied by
		@ref Ssse3.
		@see @ref DEATH_TARGET_SSE3, @ref DEATH_ENABLE_SSE3
	*/
	constexpr Sse3T Sse3 { Implementation::Init };

	/**
		@brief SSSE3 tag

		[Supplemental Streaming SIMD Extensions 3](https://en.wikipedia.org/wiki/SSSE3).
		Available only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Sse3, implied
		by @ref Sse41.

		Note that certain older AMD processors have [SSE4a](https://en.wikipedia.org/wiki/SSE4#SSE4a)
		but neither SSSE3 nor SSE4.1. Both can be however treated as a subset of SSE4.1
		to a large extent, and it's recommended to use @ref Sse41 to handle those.
		@see @ref DEATH_TARGET_SSSE3, @ref DEATH_ENABLE_SSSE3
	*/
	constexpr Ssse3T Ssse3 { Implementation::Init };

	/**
		@brief SSE4.1 tag

		[Streaming SIMD Extensions 4.1](https://en.wikipedia.org/wiki/SSE4#SSE4.1).
		Available only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Ssse3,
		implied by @ref Sse42.

		Note that certain older AMD processors have [SSE4a](https://en.wikipedia.org/wiki/SSE4#SSE4a)
		but neither SSSE3 nor SSE4.1. Both can be however treated as a subset of SSE4.1
		to a large extent, and it's recommended to use @ref Sse41 to handle those.
		@see @ref DEATH_TARGET_SSE41, @ref DEATH_ENABLE_SSE41
	*/
	constexpr Sse41T Sse41 { Implementation::Init };

	/**
		@brief SSE4.2 tag

		[Streaming SIMD Extensions 4.2](https://en.wikipedia.org/wiki/SSE4#SSE4.2).
		Available only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Sse41,
		implied by @ref Avx.
		@see @ref DEATH_TARGET_SSE42, @ref DEATH_ENABLE_SSE42
	*/
	constexpr Sse42T Sse42 { Implementation::Init };

	/**
		@brief POPCNT tag

		[POPCNT](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation))
		instructions. Available only on @ref DEATH_TARGET_X86 "x86". This instruction
		set is treated as an *extra*, i.e. is neither a superset of nor implied by any
		other instruction set. See @ref Cpu-usage-extra for more information.
		@see @ref Lzcnt, @ref Bmi1, @ref Bmi2, @ref DEATH_TARGET_POPCNT, @ref DEATH_ENABLE_POPCNT
	*/
	constexpr PopcntT Popcnt { Implementation::Init };

	/**
		@brief LZCNT tag

		[LZCNT](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation))
		instructions. Available only on @ref DEATH_TARGET_X86 "x86". This instruction
		set is treated as an *extra*, i.e. is neither a superset of nor implied by any
		other instruction set. See @ref Cpu-usage-extra for more information.

		Note that this instruction has encoding compatible with an earlier `BSR`
		instruction which has a slightly different behavior. To avoid wrong results if
		it isn't available, prefer to always detect its presence with
		@ref runtimeFeatures() instead of a compile-time check.
		@see @ref Popcnt, @ref Bmi1, @ref Bmi2, @ref DEATH_TARGET_LZCNT, @ref DEATH_ENABLE_LZCNT
	*/
	constexpr LzcntT Lzcnt { Implementation::Init };

	/**
		@brief BMI1 tag

		[BMI1](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#BMI1_(Bit_Manipulation_Instruction_Set_1))
		instructions, including `TZCNT`. Available only on
		@ref DEATH_TARGET_X86 "x86". This instruction set is treated as an *extra*,
		i.e. is neither a superset of nor implied by any other instruction set. See
		@ref Cpu-usage-extra for more information.

		Note that the `TZCNT` instruction has encoding compatible with an earlier `BSF`
		instruction which has a slightly different behavior. To avoid wrong results if
		it isn't available, prefer to always detect its presence with
		@ref runtimeFeatures() instead of a compile-time check.
		@see @ref Popcnt, @ref Lzcnt, @ref Bmi2, @ref DEATH_TARGET_BMI1, @ref DEATH_ENABLE_BMI1
	*/
	constexpr Bmi1T Bmi1 { Implementation::Init };

	/**
		@brief BMI2 tag
		[BMI2](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#BMI2_(Bit_Manipulation_Instruction_Set_2))
		instructions. Available only on @ref DEATH_TARGET_X86 "x86". This instruction
		set is treated as an *extra*, i.e. is neither a superset of nor implied by any
		other instruction set. See @ref Cpu-usage-extra for more information.
		@see @ref Popcnt, @ref Lzcnt, @ref Bmi1, @ref DEATH_TARGET_BMI2, @ref DEATH_ENABLE_BMI2
	*/
	constexpr Bmi2T Bmi2 { Implementation::Init };

	/**
		@brief AVX tag

		[Advanced Vector Extensions](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions).
		Available only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Sse42,
		implied by @ref Avx2.
		@see @ref DEATH_TARGET_AVX, @ref DEATH_ENABLE_AVX
	*/
	constexpr AvxT Avx { Implementation::Init };

	/**
		@brief AVX F16C tag

		[F16C](https://en.wikipedia.org/wiki/F16C) instructions. Available only on
		@ref DEATH_TARGET_X86 "x86". This instruction set is treated as an *extra*,
		i.e. is neither a superset of nor implied by any other instruction set. See
		@ref Cpu-usage-extra for more information.
		@see @ref DEATH_TARGET_AVX_F16C, @ref DEATH_ENABLE_AVX_F16C
	*/
	constexpr AvxF16cT AvxF16c { Implementation::Init };

	/**
		@brief AVX FMA tag

		[FMA3 instruction set](https://en.wikipedia.org/wiki/FMA_instruction_set).
		Available only on @ref DEATH_TARGET_X86 "x86". This instruction set is
		treated as an *extra*, i.e. is neither a superset of nor implied by any other
		instruction set. See @ref Cpu-usage-extra for more information.
		@see @ref DEATH_TARGET_AVX_FMA, @ref DEATH_ENABLE_AVX_FMA
	*/
	constexpr AvxFmaT AvxFma { Implementation::Init };

	/**
		@brief AVX2 tag

		[Advanced Vector Extensions 2](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions#Advanced_Vector_Extensions_2).
		Available only on @ref DEATH_TARGET_X86 "x86". Superset of @ref Avx,
		implied by @ref Avx512f.
		@see @ref DEATH_TARGET_AVX2, @ref DEATH_ENABLE_AVX2
	*/
	constexpr Avx2T Avx2 { Implementation::Init };

	/**
		@brief AVX-512 Foundation tag

		[AVX-512](https://en.wikipedia.org/wiki/AVX-512) Foundation. Available only on
		@ref DEATH_TARGET_X86 "x86". Superset of @ref Avx2.
		@see @ref DEATH_TARGET_AVX512F, @ref DEATH_ENABLE_AVX512F
	*/
	constexpr Avx512fT Avx512f { Implementation::Init };
#endif

#if defined(DEATH_TARGET_ARM)
	/**
		@brief NEON tag type

		[ARM NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon)).
		Available only on @ref DEATH_TARGET_ARM "ARM". Superset of @ref Scalar,
		implied by @ref NeonFp16.
		@see @ref DEATH_TARGET_NEON, @ref DEATH_ENABLE_NEON
	*/
	constexpr NeonT Neon { Implementation::Init };

	/**
		@brief NEON FMA tag type

		[ARM NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon))
		with FMA instructions. Available only on @ref DEATH_TARGET_ARM "ARM".
		Superset of @ref Neon, implied by @ref NeonFp16.
		@see @ref DEATH_TARGET_NEON_FMA, @ref DEATH_ENABLE_NEON_FMA
	*/
	constexpr NeonFmaT NeonFma { Implementation::Init };

	/**
		@brief NEON FP16 tag type

		[ARM NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon))
		with ARMv8.2-a FP16 vector arithmetic. Available only on
		@ref DEATH_TARGET_ARM "ARM". Superset of @ref NeonFma.
		@see @ref DEATH_TARGET_NEON_FP16, @ref DEATH_ENABLE_NEON_FP16
	*/
	constexpr NeonFp16T NeonFp16 { Implementation::Init };
#endif

#if defined(DEATH_TARGET_WASM)
	/**
		@brief SIMD128 tag type

		[128-bit WebAssembly SIMD](https://github.com/webassembly/simd). Available only
		on @ref DEATH_TARGET_WASM "WebAssembly". Superset of @ref Scalar.
		@see @ref DEATH_TARGET_SIMD128, @ref DEATH_ENABLE_SIMD128
	*/
	constexpr Simd128T Simd128 { Implementation::Init };
#endif

	namespace Implementation
	{
		template<unsigned i> struct Priority : Priority<i - 1> { };
		template<> struct Priority<0> { };

		// Count of "extra" tags that are not in the hierarchy. Should not be larger than strictly
		// necessary as it deepens inheritance hierarchy when picking best overload candidate.
		enum : unsigned int {
			BaseTagMask = (1 << ExtraTagBitOffset) - 1,
			ExtraTagMask = 0xffffffffu & ~BaseTagMask,
#if defined(DEATH_TARGET_X86)
			ExtraTagCount = 6,
#else
			ExtraTagCount = 0,
#endif
		};

		// On sane compilers (and on MSVC with /permissive-), these two could be directly in the Tag constructor enable_if expressions.
		// But MSVC chokes hard on those ("C2988: unrecognizable template declaration/definition", haha), so they have to be extracted
		// outside. I could also #ifdef around the workaround and extract it only for MSVC, but I don't think the code duplication would
		// be worth it.
		template<unsigned int value, unsigned int otherValue> struct IsTagConversionAllowed {
			enum : bool {
				Value =
				// There should be at most one base tag set in both, if there's none then it's Cpu::Scalar
				!((value & BaseTagMask) & ((value & BaseTagMask) - 1)) &&
				!((otherValue & BaseTagMask) & ((otherValue & BaseTagMask) - 1)) &&
				// The other base tag should be the same or derived (i.e, having same or larger value)
				(otherValue & BaseTagMask) >= (value & BaseTagMask) &&
				// The other extra bits should be a superset of this
				((otherValue & value) & ExtraTagMask) == (value & ExtraTagMask)
			};
		};
		template<unsigned int value, unsigned int otherIndex> struct IsSingleTagConversionAllowed {
			enum : bool {
				Value =
				// There should be at most one base tag set in this one, the other satisfies that implicitly as it's constrained by TypeTraits
				!((value & BaseTagMask) & ((value & BaseTagMask) - 1)) &&
				// The other base tag should be the same or derived (i.e, having same or larger value)
				(otherIndex & BaseTagMask) >= (value & BaseTagMask) &&
				// The other extra bits should be a superset of this. Since a single tag can be only one bit, this condition gets satisfied
				// only either if we're Cpu::Scalar or if we have no extra bits.
				((otherIndex & value) & ExtraTagMask) == (value & ExtraTagMask)
			};
		};

		// Holds a compile-time combination of tags. Kept private, since it isn't really directly
		// needed in user code and it would only lead to confusion.
		template<unsigned int value> struct Tags {
			enum : unsigned int {
				Value = value
			};

			// Empty initialization. Should not be needed by public code.
			constexpr explicit Tags(InitT) { }

			// Conversion from other tag combination, allowed only if the other is not a subset
			template<unsigned int otherValue> constexpr Tags(Tags<otherValue>, typename std::enable_if<IsTagConversionAllowed<value, otherValue>::Value>::type* = { }) { }

			// Conversion from a single tag, allowed only if we're a single bit and the other is not a subset
			template<class T> constexpr Tags(T, typename std::enable_if<IsSingleTagConversionAllowed<Value, TypeTraits<T>::Index>::Value>::type* = { }) { }

			//A subset of operators on Features, excluding the assignment ones -- since they modify the type, they make no sense here
			template<unsigned int otherValue> constexpr Tags<value | otherValue> operator|(Tags<otherValue>) const {
				return Tags<value | otherValue>{Init};
			}
			template<class U> constexpr Tags<value | TypeTraits<U>::Index> operator|(U) const {
				return Tags<value | TypeTraits<U>::Index>{Init};
			}
			template<unsigned int otherValue> constexpr Tags<value& otherValue> operator&(Tags<otherValue>) const {
				return Tags<value& otherValue>{Init};
			}
			template<class U> constexpr Tags<value& TypeTraits<U>::Index> operator&(U) const {
				return Tags<value& TypeTraits<U>::Index>{Init};
			}
			template<unsigned int otherValue> constexpr Tags<value^ otherValue> operator^(Tags<otherValue>) const {
				return Tags<value^ otherValue>{Init};
			}
			template<class U> constexpr Tags<value^ TypeTraits<U>::Index> operator^(U) const {
				return Tags<value^ TypeTraits<U>::Index>{Init};
			}
			constexpr Tags<~value> operator~() const {
				return Tags<~value>{Init};
			}
			constexpr explicit operator bool() const {
				// An explicit cast to avoid "C4305: 'return': truncation from 'unsigned int' to 'bool'" on MSVC
				return bool(value);
			}
			constexpr operator unsigned int() const {
				return value;
			}
		};

		template<class T> constexpr Tags<TypeTraits<T>::Index> tags(T) {
			return Tags<TypeTraits<T>::Index>{Init};
		}
		template<unsigned int value> constexpr Tags<value> tags(Tags<value> tags) {
			return tags;
		}

		// Base-2 log, "plus one" (returns 0 for A == 0). For a base tag, which is always just one bit, returns position of that bit.
		// Used for calculating distance between two tags in order to calculate overload priority. But since there's also
		// the Scalar tag, which is 0, it's plus one.
		template<unsigned short A> struct BitIndex {
			enum : unsigned short {
				Value = 1 + BitIndex<(A >> 1)>::Value
			};
		};
		template<> struct BitIndex<0> {
			enum : unsigned short {
				Value = 0
			};
		};

		// Popcount, but constexpr. No, not related to Cpu::Popcnt, in any way. Used for calculating a difference between
		// two extra tag sets in order to calculate overload priority.
		template<unsigned short A> struct BitCount {
			enum : unsigned short {
				Bits1 = 0x5555, /* 0b0101010101010101 */
				Bits2 = 0x3333, /* 0b0011001100110011 */
				Bits4 = 0x0f0f, /* 0b0000111100001111 */
				Bits8 = 0x00ff, /* 0b0000000011111111 */

				B0 = (A >> 0) & Bits1,
				B1 = (A >> 1) & Bits1,
				C = B0 + B1,
				D0 = (C >> 0) & Bits2,
				D2 = (C >> 2) & Bits2,
				E = D0 + D2,
				F0 = (E >> 0) & Bits4,
				F4 = (E >> 4) & Bits4,
				G = F0 + F4,
				H0 = (G >> 0) & Bits8,
				H8 = (G >> 8) & Bits8,
				Value = H0 + H8
			};
		};

		// Calculates an absolute priority index for given tag, which is either a base-2 log "plus one" of its index
		// if it's a base tag, or is 1 if it's an extra tag.
		//
		// MSVC 2015 and 2017 need the extra () inside Priority<>, otherwise they demand that a typename is used
		// for TypeTraits<T>::Index. Heh. Clang 14 warns if I don't cast because bitwise operations between
		// different enums are deprecated in C++20.
		template<class T> Priority<(static_cast<unsigned int>(TypeTraits<T>::Index)& ExtraTagMask ? 1 : BitIndex<static_cast<unsigned int>(TypeTraits<T>::Index)& BaseTagMask>::Value * (ExtraTagCount + 1))> constexpr priority(T) {
			return { };
		}
		template<unsigned int value> Priority<(BitIndex<value& BaseTagMask>::Value* (ExtraTagCount + 1) + BitCount<((value& ExtraTagMask) >> ExtraTagBitOffset)>::Value)> constexpr priority(Tags<value>) {
			static_assert(!((value & BaseTagMask) & ((value & BaseTagMask) - 1)), "more than one base tag used");
			// GCC 4.8 loudly complains about enum comparison if I don't cast, sigh
			static_assert(((value & ExtraTagMask) >> ExtraTagBitOffset) < (1 << static_cast<unsigned int>(ExtraTagCount)), "extra tag out of expected bounds");
			return { };
		}
	}

	/**
		@brief Default base tag type

		See the @ref DefaultBase tag for more information.
	*/
	typedef
#if defined(DEATH_TARGET_X86)
#	if defined(DEATH_TARGET_AVX512F)
		Avx512fT
#	elif defined(DEATH_TARGET_AVX2)
		Avx2T
#	elif defined(DEATH_TARGET_AVX)
		AvxT
#	elif defined(DEATH_TARGET_SSE42)
		Sse42T
#	elif defined(DEATH_TARGET_SSE41)
		Sse41T
#	elif defined(DEATH_TARGET_SSSE3)
		Ssse3T
#	elif defined(DEATH_TARGET_SSE3)
		Sse3T
#		elif defined(DEATH_TARGET_SSE2)
		Sse2T
#else
		ScalarT
#	endif
#elif defined(DEATH_TARGET_ARM)
#	if defined(DEATH_TARGET_NEON_FP16)
		NeonFp16T
#	elif defined(DEATH_TARGET_NEON_FMA)
		NeonFmaT
#	elif defined(DEATH_TARGET_NEON)
		NeonT
#	else
		ScalarT
#	endif
#elif defined(DEATH_TARGET_WASM)
#	if defined(DEATH_TARGET_SIMD128)
		Simd128T
#	else
		ScalarT
#	endif
#else
		ScalarT
#endif
		DefaultBaseT;

	/**
		@brief Default extra tag type

		See the @ref DefaultExtra tag for more information.
	*/
	// Clang 14 warns if zero (an int) isn't first because bitwise operations between different enums are deprecated in C++20.
	typedef Implementation::Tags<0
#if defined(DEATH_TARGET_X86)
#	if defined(DEATH_TARGET_POPCNT)
		| TypeTraits<PopcntT>::Index
#	endif
#	if defined(DEATH_TARGET_LZCNT)
		| TypeTraits<LzcntT>::Index
#	endif
#	if defined(DEATH_TARGET_BMI1)
		| TypeTraits<Bmi1T>::Index
#	endif
#	if defined(DEATH_TARGET_BMI2)
		| TypeTraits<Bmi2T>::Index
#	endif
#	if defined(DEATH_TARGET_AVX_FMA)
		| TypeTraits<AvxFmaT>::Index
#	endif
#	if defined(DEATH_TARGET_AVX_F16C)
		| TypeTraits<AvxF16cT>::Index
#	endif
#endif
	> DefaultExtraT;

	/**
		@brief Default tag type

		See the @ref Default tag for more information.
	*/
	// Clang 14 warns if I don't cast because bitwise operations between different enums are deprecated in C++20
	typedef Implementation::Tags<static_cast<unsigned int>(TypeTraits<DefaultBaseT>::Index) | DefaultExtraT::Value> DefaultT;

	/**
		@brief Default base tag

		Highest base instruction set available on given architecture with current
		compiler flags. Ordered by priority, on @ref DEATH_TARGET_X86 it's one of these:

		-   @ref Avx512f if @ref DEATH_TARGET_AVX512F is defined
		-   @ref Avx2 if @ref DEATH_TARGET_AVX2 is defined
		-   @ref Avx if @ref DEATH_TARGET_AVX is defined
		-   @ref Sse42 if @ref DEATH_TARGET_SSE42 is defined
		-   @ref Sse41 if @ref DEATH_TARGET_SSE41 is defined
		-   @ref Ssse3 if @ref DEATH_TARGET_SSSE3 is defined
		-   @ref Sse3 if @ref DEATH_TARGET_SSE3 is defined
		-   @ref Sse2 if @ref DEATH_TARGET_SSE2 is defined
		-   @ref Scalar otherwise

		On @ref DEATH_TARGET_ARM it's one of these:

		-   @ref NeonFp16 if @ref DEATH_TARGET_NEON_FP16 is defined
		-   @ref NeonFma if @ref DEATH_TARGET_NEON_FMA is defined
		-   @ref Neon if @ref DEATH_TARGET_NEON is defined
		-   @ref Scalar otherwise

		On @ref DEATH_TARGET_WASM it's one of these:

		-   @ref Simd128 if @ref DEATH_TARGET_SIMD128 is defined
		-   @ref Scalar otherwise

		In addition to the above, @ref DefaultExtra contains a combination of extra
		instruction sets available together with the base instruction set, and
		@ref Default is a combination of both. See also @ref compiledFeatures() which
		returns a *combination* of base tags instead of just the highest available,
		together with the extra instruction sets, and @ref runtimeFeatures() which is
		capable of detecting the available CPU feature set at runtime.
	*/
	constexpr DefaultBaseT DefaultBase { Implementation::Init };

	/**
		@brief Default extra tags

		Instruction sets available in addition to @ref DefaultBase on
		given architecture with current compiler flags. On @ref DEATH_TARGET_X86 it's
		a combination of these:

		-   @ref Popcnt if @ref DEATH_TARGET_POPCNT is defined
		-   @ref Lzcnt if @ref DEATH_TARGET_LZCNT is defined
		-   @ref Bmi1 if @ref DEATH_TARGET_BMI1 is defined
		-   @ref Bmi2 if @ref DEATH_TARGET_BMI2 is defined
		-   @ref AvxFma if @ref DEATH_TARGET_AVX_FMA is defined
		-   @ref AvxF16c if @ref DEATH_TARGET_AVX_F16C is defined

		No extra instruction sets are currently defined for @ref DEATH_TARGET_ARM or
		@ref DEATH_TARGET_WASM.

		In addition to the above, @ref Default is a combination of both
		@ref DefaultBase and the extra instruction sets. See also
		@ref compiledFeatures() which returns these together with a combination of all
		base instruction sets available, and @ref runtimeFeatures() which is capable of
		detecting the available CPU feature set at runtime.
	*/
	constexpr DefaultExtraT DefaultExtra { Implementation::Init };

	/**
		@brief Default tags

		A combination of @ref DefaultBase and @ref DefaultExtra, see their
		documentation for more information.
	*/
	constexpr DefaultT Default { Implementation::Init };

	/**
		@brief Tag for a tag type

		Returns a tag corresponding to tag type @p T.

		@see @ref features()
	*/
	template<class T> constexpr T tag() {
		return T { Implementation::Init };
	}

#if defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(__FreeBSD__))
	namespace Implementation
	{
		// Needed for a friend declaration, implementation is at the very end of the header
		Features runtimeFeatures(unsigned long caps);
	}
#endif

	/**
		@brief Feature set

		Provides storage and comparison as well as runtime detection of CPU instruction
		set. Provides an interface similar to an @ref Containers::EnumSet, with values
		being the @ref Sse2, @ref Sse3 etc. tags.

		See the @ref Cpu namespace for an overview and usage examples.
		@see @ref compiledFeatures(), @ref runtimeFeatures()
	*/
	class Features {
	public:
		/**
		 * @brief Default constructor
		 *
		 * Equivalent to @ref Scalar.
		 */
		constexpr explicit Features() noexcept : _data { } { }

		/**
		 * @brief Construct from a tag
		 *
		 * @see @ref features()
		 */
		template<class T, class = decltype(TypeTraits<T>::Index)> constexpr /*implicit*/ Features(T) noexcept : _data { TypeTraits<T>::Index } {
			// GCC 4.8 loudly complains about enum comparison if I don't cast; Clang 14 warns if I don't cast
			// because bitwise operations between different enums are deprecated in C++20
			static_assert(((static_cast<unsigned int>(TypeTraits<T>::Index) & Implementation::ExtraTagMask) >> Implementation::ExtraTagBitOffset) < (1 << static_cast<unsigned int>(Implementation::ExtraTagCount)),
				"extra tag out of expected bounds");
		}

		// The compile-time Tags<> is an implementation detail, don't show that in the docs
		template<unsigned int value> constexpr /*implicit*/ Features(Implementation::Tags<value>) noexcept : _data { value } { }

		/** @brief Equality comparison */
		constexpr bool operator==(Features other) const {
			return _data == other._data;
		}

		/** @brief Non-equality comparison */
		constexpr bool operator!=(Features other) const {
			return _data != other._data;
		}

		/**
		 * @brief Whether @p other is a subset of this (@f$ a \supseteq o @f$)
		 *
		 * Equivalent to @cpp (a & other) == other @ce.
		 */
		constexpr bool operator>=(Features other) const {
			return (_data & other._data) == other._data;
		}

		/**
		 * @brief Whether @p other is a superset of this (@f$ a \subseteq o @f$)
		 *
		 * Equivalent to @cpp (a & other) == a @ce.
		 */
		constexpr bool operator<=(Features other) const {
			return (_data & other._data) == _data;
		}

		/** @brief Union of two feature sets */
		constexpr Features operator|(Features other) const {
			return Features { _data | other._data };
		}

		/** @brief Union two feature sets and assign */
		Features& operator|=(Features other) {
			_data |= other._data;
			return *this;
		}

		/** @brief Intersection of two feature sets */
		constexpr Features operator&(Features other) const {
			return Features { _data & other._data };
		}

		/** @brief Intersect two feature sets and assign */
		Features& operator&=(Features other) {
			_data &= other._data;
			return *this;
		}

		/** @brief XOR of two feature sets */
		constexpr Features operator^(Features other) const {
			return Features { _data ^ other._data };
		}

		/** @brief XOR two feature sets and assign */
		Features& operator^=(Features other) {
			_data ^= other._data;
			return *this;
		}

		/** @brief Feature set complement */
		constexpr Features operator~() const {
			return Features { ~_data };
		}

		/**
		 * @brief Boolean conversion
		 *
		 * Returns @cpp true @ce if at least one feature apart from @ref Scalar
		 * is present, @cpp false @ce otherwise.
		 */
		constexpr explicit operator bool() const {
			return _data;
		}

		/**
		 * @brief Integer representation
		 *
		 * For testing purposes. @ref Cpu::Scalar is always @cpp 0 @ce, values
		 * corresponding to other feature tags are unspecified.
		 */
		constexpr explicit operator unsigned int() const {
			return _data;
		}

	private:
		template<class> friend constexpr Features features();
		friend constexpr Features compiledFeatures();
#if (defined(DEATH_TARGET_X86) && (defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC))) || (defined(DEATH_TARGET_ARM) && defined(DEATH_TARGET_APPLE))
		friend Features runtimeFeatures();
#endif
#if defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(__FreeBSD__))
		friend Features Implementation::runtimeFeatures(unsigned long);
#endif

		constexpr explicit Features(unsigned int data) noexcept : _data { data } { }

		unsigned int _data;
	};

	/**
		@brief Feature set for a tag type

		Returns @ref Features with a tag corresponding to tag type @p T, avoiding a
		need to form the tag value in order to pass it to @ref Features::Features(T).

		@see @ref tag()
	*/
	template<class T> constexpr Features features() {
		return Features { TypeTraits<T>::Index };
	}

	/** @relates Features
		@brief Equality comparison of a tag and a feature set

		Same as @ref Features::operator==().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr bool operator==(T a, Features b) {
		return Features(a) == b;
	}

	/** @relates Features
		@brief Equality comparison of two tags
		Same as @ref Features::operator==(). Needs to be present to avoid ambiguity in C++20.
	*/
	template<class T, class U, class = decltype(TypeTraits<T>::Index), class = decltype(TypeTraits<U>::Index)> constexpr bool operator==(T, U) {
		// Need to cast because operations between different enums are deprecated in C++20.
		return static_cast<unsigned int>(TypeTraits<T>::Index) == TypeTraits<U>::Index;
	}

	/** @relates Features
		@brief Non-equality comparison of a tag and a feature set

		Same as @ref Features::operator!=().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr bool operator!=(T a, Features b) {
		return Features(a) != b;
	}

	/** @relates Features
		@brief Non-equality comparison of two tags
		Same as @ref Features::operator!=(). Needs to be present to avoid ambiguity in
		C++20.
	*/
	template<class T, class U, class = decltype(TypeTraits<T>::Index), class = decltype(TypeTraits<U>::Index)> constexpr bool operator!=(T, U) {
		// Need to cast because operations between different enums are deprecated in C++20.
		return static_cast<unsigned int>(TypeTraits<T>::Index) != TypeTraits<U>::Index;
	}

	/** @relates Features
		@brief Whether @p a is a superset of @p b (@f$ a \supseteq b @f$)

		Same as @ref Features::operator>=().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr bool operator>=(T a, Features b) {
		return Features(a) >= b;
	}

	/** @relates Features
		@brief Whether @p a is a subset of @p b (@f$ a \subseteq b @f$)

		Same as @ref Features::operator<=().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr bool operator<=(T a, Features b) {
		return Features(a) <= b;
	}

	/** @relates Features
		@brief Union of two feature sets

		Same as @ref Features::operator|().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr Features operator|(T a, Features b) {
		return b | a;
	}

	// Compared to the above, this produces a type that encodes the value instead of Features. Has to be in the same namespace
	// as the tags, but since Tags<> is an implementation detail, this is hidden from plain sight as well.
	//
	// Clang 14 warns if I don't cast because bitwise operations between different enums are deprecated in C++20.
	template<class T, class U> constexpr Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index) | TypeTraits<U>::Index> operator|(T, U) {
		return Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index) | TypeTraits<U>::Index>{Implementation::Init};
	}
	template<class T, unsigned int value> constexpr Implementation::Tags<TypeTraits<T>::Index | value> operator|(T, Implementation::Tags<value>) {
		return Implementation::Tags<TypeTraits<T>::Index | value>{Implementation::Init};
	}

	/** @relates Features
		@brief Intersection of two feature sets

		Same as @ref Features::operator&().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr Features operator&(T a, Features b) {
		return b & a;
	}

	// Compared to the above, this produces a type that encodes the value instead of Features. Has to be in the same namespace
	// as the tags, but since Tags<> is an implementation detail, this is hidden from plain sight as well.
	//
	// Clang 14 warns if I don't cast because bitwise operations between different enums are deprecated in C++20.
	template<class T, class U> constexpr Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index)& TypeTraits<U>::Index> operator&(T, U) {
		return Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index)& TypeTraits<U>::Index>{Implementation::Init};
	}
	template<class T, unsigned int value> constexpr Implementation::Tags<TypeTraits<T>::Index& value> operator&(T, Implementation::Tags<value>) {
		return Implementation::Tags<TypeTraits<T>::Index& value>{Implementation::Init};
	}

	/** @relates Features
		@brief XOR of two feature sets

		Same as @ref Features::operator^().
	*/
	template<class T, class = decltype(TypeTraits<T>::Index)> constexpr Features operator^(T a, Features b) {
		return b ^ a;
	}

	// Compared to the above, this produces a type that encodes the value instead of Features. Has to be in the same namespace
	// as the tags, but since Tags<> is an implementation detail, this is hidden from plain sight as well.
	//
	// Clang 14 warns if I don't cast because bitwise operations between different enums are deprecated in C++20.
	template<class T, class U> constexpr Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index) ^ TypeTraits<U>::Index> operator^(T, U) {
		return Implementation::Tags<static_cast<unsigned int>(TypeTraits<T>::Index) ^ TypeTraits<U>::Index>{Implementation::Init};
	}
	template<class T, unsigned int value> constexpr Implementation::Tags<TypeTraits<T>::Index^ value> operator^(T, Implementation::Tags<value>) {
		return Implementation::Tags<TypeTraits<T>::Index^ value>{Implementation::Init};
	}

	/** @relates Features
		@brief Feature set complement

		Same as @ref Features::operator~().
	*/
	// To avoid confusion, to the doc use it's shown that the operator produces a Features, but in fact it's a type with a compile-time-encoded value
	template<class T> constexpr Implementation::Tags<~TypeTraits<T>::Index> operator~(T) {
		return Implementation::Tags<~TypeTraits<T>::Index>{Implementation::Init};
	}

	/**
		@brief CPU instruction sets enabled at compile time

		On @ref DEATH_TARGET_X86 "x86" returns a combination of @ref Sse2, @ref Sse3,
		@ref Ssse3, @ref Sse41, @ref Sse42, @ref Popcnt, @ref Lzcnt, @ref Bmi1,
		@ref Bmi2, @ref Avx, @ref AvxF16c, @ref AvxFma, @ref Avx2 and @ref Avx512f
		based on what all @ref DEATH_TARGET_SSE2 etc. preprocessor variables are defined.

		On @ref DEATH_TARGET_ARM "ARM", returns a combination of @ref Neon,
		@ref NeonFma and @ref NeonFp16 based on what all @ref DEATH_TARGET_NEON etc.
		preprocessor variables are defined.

		On @ref DEATH_TARGET_WASM "WebAssembly", returns @ref Simd128 based on
		whether the @ref DEATH_TARGET_SIMD128 preprocessor variable is defined.

		On other platforms or if no known CPU instruction set is enabled, the returned
		value is equal to @ref Scalar, which in turn is equivalent to empty (or
		default-constructed) @ref Features.
	*/
	constexpr Features compiledFeatures() {
		// Clang 14 warns if zero (an int) isn't first because bitwise operations between different enums are deprecated in C++20.
		return Features { 0
#if defined(DEATH_TARGET_X86)
#	if defined(DEATH_TARGET_SSE2)
			| TypeTraits<Sse2T>::Index
#	endif
#	if defined(DEATH_TARGET_SSE3)
			| TypeTraits<Sse3T>::Index
#	endif
#	if defined(DEATH_TARGET_SSSE3)
			| TypeTraits<Ssse3T>::Index
#	endif
#	if defined(DEATH_TARGET_SSE41)
			| TypeTraits<Sse41T>::Index
#	endif
#	if defined(DEATH_TARGET_SSE42)
			| TypeTraits<Sse42T>::Index
#	endif
#	if defined(DEATH_TARGET_POPCNT)
			| TypeTraits<PopcntT>::Index
#	endif
#	if defined(DEATH_TARGET_LZCNT)
			| TypeTraits<LzcntT>::Index
#	endif
#	if defined(DEATH_TARGET_BMI1)
			| TypeTraits<Bmi1T>::Index
#	endif
#	if defined(DEATH_TARGET_BMI2)
			| TypeTraits<Bmi2T>::Index
#	endif
#	if defined(DEATH_TARGET_AVX)
			| TypeTraits<AvxT>::Index
#	endif
#	if defined(DEATH_TARGET_AVX_FMA)
			| TypeTraits<AvxFmaT>::Index
#	endif
#	if defined(DEATH_TARGET_AVX_F16C)
			| TypeTraits<AvxF16cT>::Index
#	endif
#	if defined(DEATH_TARGET_AVX2)
			| TypeTraits<Avx2T>::Index
#	endif
#	if defined(DEATH_TARGET_AVX512F)
			| TypeTraits<Avx512fT>::Index
#	endif
#elif defined(DEATH_TARGET_ARM)
#	if defined(DEATH_TARGET_NEON)
			| TypeTraits<NeonT>::Index
#	endif
#	if defined(DEATH_TARGET_NEON_FMA)
			| TypeTraits<NeonFmaT>::Index
#	endif
#	if defined(DEATH_TARGET_NEON_FP16)
			| TypeTraits<NeonFp16T>::Index
#	endif
#elif defined(DEATH_TARGET_WASM)
#	if defined(DEATH_TARGET_SIMD128)
			| TypeTraits<Simd128T>::Index
#	endif
#endif
		};
	}

	/**
		@brief Detect available CPU instruction sets at runtime

		On @ref DEATH_TARGET_X86 "x86" and GCC, Clang or MSVC uses the
		[CPUID](https://en.wikipedia.org/wiki/CPUID) builtin to check for the
		@ref Sse2, @ref Sse3, @ref Ssse3, @ref Sse41, @ref Sse42, @ref Popcnt,
		@ref Lzcnt, @ref Bmi1, @ref Bmi2, @ref Avx, @ref AvxF16c, @ref AvxFma,
		@ref Avx2 and @ref Avx512f runtime features. @ref Avx needs OS support as well,
		if it's not present, no following flags including @ref Bmi1 and @ref Bmi2 are
		checked either. On compilers other than GCC, Clang and MSVC the function is
		@cpp constexpr @ce and delegates into @ref compiledFeatures().

		On @ref DEATH_TARGET_ARM "ARM" and Linux or Android API level 18+ uses
		@m_class{m-doc-external} [getauxval()](https://man.archlinux.org/man/getauxval.3), or on ARM macOS and iOS uses @m_class{m-doc-external} [sysctlbyname()](https://developer.apple.com/documentation/kernel/1387446-sysctlbyname)
		to check for the @ref Neon, @ref NeonFma and @ref NeonFp16. @ref Neon and
		@ref NeonFma are implicitly supported on ARM64. On other platforms the function
		is @cpp constexpr @ce and delegates into @ref compiledFeatures().

		On @ref DEATH_TARGET_WASM "WebAssembly" an attempt to use SIMD instructions
		without runtime support results in a WebAssembly compilation error and thus
		runtime detection is largely meaningless. While this may change once the
		[feature detection proposal](https://github.com/WebAssembly/feature-detection/blob/main/proposals/feature-detection/Overview.md)
		is implemented, at the moment the function is @cpp constexpr @ce and delegates
		into @ref compiledFeatures().

		On other platforms or if no known CPU instruction set is detected, the returned
		value is equal to @ref Scalar, which in turn is equivalent to empty (or
		default-constructed) @ref Features.
	*/
#if (defined(DEATH_TARGET_X86) && (defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC))) || (defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(DEATH_TARGET_APPLE) || defined(__FreeBSD__)))
	Features runtimeFeatures();
#else
	constexpr Features runtimeFeatures() {
		return compiledFeatures();
	}
#endif

	/**
		@brief Declare a CPU tag for a compile-time dispatch

		Meant to be used to declare a function overload that uses given combination of
		CPU instruction sets. The @ref DEATH_CPU_SELECT() macro is a counterpart used
		to select among overloads declared with this macro. See @ref Cpu-usage-extra
		for more information and usage example.

		Internally, this macro expands to two function parameter declarations separated
		by a comma, one that ensures only an overload matching the desired instruction
		sets get picked, and one that assigns an absolute priority to this overload.
	*/
	#define DEATH_CPU_DECLARE(tag) decltype(Death::Cpu::Implementation::tags(tag)), decltype(Death::Cpu::Implementation::priority(tag))

	/**
		@brief Select a CPU tag for a compile-time dispatch

		Meant to be used to select among function overloads declared with
		@ref DEATH_CPU_DECLARE() that best matches given combination of CPU
		instruction sets. See @ref Cpu-usage-extra for more information and usage
		example.

		Internally, this macro expands to two function parameter values separated by a
		comma, one that contains the desired instruction sets to filter the overloads
		against and another that converts the sets to an absolute priority to pick the
		best viable overload.
	*/
	#define DEATH_CPU_SELECT(tag) tag, Death::Cpu::Implementation::priority(tag)

	// Called from _DEATH_CPU_DISPATCHER() and _DEATH_ENABLE_CONCATENATE() to pick a macro implementation based
	// on how many arguments were passed. Source: https://stackoverflow.com/a/11763277
	#define _DEATH_HELPER_PICK(_0, _1, _2, _3, _4, _5, _6, _7, macroName, ...) macroName

	/**
		@brief Create a function for a runtime dispatch on a base CPU instruction set

		Given a set of function overloads named @p function that accept a CPU tag as a
		parameter, all returning a function pointer of the same type, creates a
		function with signature @cpp function(Cpu::Features) @ce which will select
		among the overloads using a runtime-specified
		@relativeref{Death,Cpu::Features}, using the same rules as the compile-time
		overload selection. For this macro to work, at the very least there has to be
		an overload with a @relativeref{Death,Cpu::ScalarT} argument. See
		@ref Cpu-usage-automatic-runtime-dispatch for more information and an example.

		This function works with just a single base CPU instruction tag such as
		@relativeref{Death,Cpu::Avx2} or @relativeref{Death,Cpu::Neon}, but not the
		extra instruction sets like @relativeref{Death,Cpu::Lzcnt} or
		@relativeref{Death,Cpu::AvxFma}. For a dispatch that takes extra instruction
		sets into account as well use @ref _DEATH_CPU_DISPATCHER() instead.
	*/
	// Ideally this would reuse _DEATH_CPU_DISPATCHER_IMPLEMENTATION(), unfortunately due to MSVC not being able
	// to defer macro calls unless "the new preprocessor" is enabled it wouldn't be possible to get rid of the
	// DEATH_CPU_SELECT() macro from there.
#if defined(DEATH_TARGET_X86)
	#define _DEATH_CPU_DISPATCHER_BASE(function)								\
		decltype(function(Death::Cpu::Scalar)) function(Death::Cpu::Features features) {	\
			if(features & Death::Cpu::Avx512f)									\
				return function(Death::Cpu::Avx512f);							\
			if(features & Death::Cpu::Avx2)										\
				return function(Death::Cpu::Avx2);								\
			if(features & Death::Cpu::Avx)										\
				return function(Death::Cpu::Avx);								\
			if(features & Death::Cpu::Sse42)									\
				return function(Death::Cpu::Sse42);								\
			if(features & Death::Cpu::Sse41)									\
				return function(Death::Cpu::Sse41);								\
			if(features & Death::Cpu::Ssse3)									\
				return function(Death::Cpu::Ssse3);								\
			if(features & Death::Cpu::Sse3)										\
				return function(Death::Cpu::Sse3);								\
			if(features & Death::Cpu::Sse2)										\
				return function(Death::Cpu::Sse2);								\
			return function(Death::Cpu::Scalar);								\
		}
#elif defined(DEATH_TARGET_ARM)
	#define _DEATH_CPU_DISPATCHER_BASE(function)								\
		decltype(function(Death::Cpu::Scalar)) function(Death::Cpu::Features features) {	\
			if(features & Death::Cpu::NeonFp16)									\
				return function(Death::Cpu::NeonFp16);							\
			if(features & Death::Cpu::NeonFma)									\
				return function(Death::Cpu::NeonFma);							\
			if(features & Death::Cpu::Neon)										\
				return function(Death::Cpu::Neon);								\
			return function(Death::Cpu::Scalar);								\
		}
#elif defined(DEATH_TARGET_WASM)
	#define _DEATH_CPU_DISPATCHER_BASE(function)								\
		decltype(function(Death::Cpu::Scalar)) function(Death::Cpu::Features features) {	\
			if(features & Death::Cpu::Simd128)									\
				return function(Death::Cpu::Simd128);							\
			return function(Death::Cpu::Scalar);								\
		}
#else
	#define _DEATH_CPU_DISPATCHER_BASE(function)								\
		decltype(function(Death::Cpu::Scalar)) function(Death::Cpu::Features features) {	\
			return function(Death::Cpu::Scalar);								\
		}
#endif

#if defined(DEATH_TARGET_X86)
	#define _DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, extra)				\
		if(features >= (Death::Cpu::Avx512f extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Avx512f extra));		\
		if(features >= (Death::Cpu::Avx2 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Avx2 extra));			\
		if(features >= (Death::Cpu::Avx extra))									\
			return function(DEATH_CPU_SELECT(Death::Cpu::Avx extra));			\
		if(features >= (Death::Cpu::Sse42 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Sse42 extra));			\
		if(features >= (Death::Cpu::Sse41 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Sse41 extra));			\
		if(features >= (Death::Cpu::Ssse3 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Ssse3 extra));			\
		if(features >= (Death::Cpu::Sse3 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Sse3 extra));			\
		if(features >= (Death::Cpu::Sse2 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Sse2 extra));			\
		return function(DEATH_CPU_SELECT(Death::Cpu::Scalar extra));
#elif defined(DEATH_TARGET_ARM)
	#define _DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, extra)				\
		if(features >= (Death::Cpu::NeonFp16 extra))							\
			return function(DEATH_CPU_SELECT(Death::Cpu::NeonFp16 extra));		\
		if(features >= (Death::Cpu::NeonFma extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::NeonFma extra));		\
		if(features >= (Death::Cpu::Neon extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Neon extra));			\
		return function(DEATH_CPU_SELECT(Death::Cpu::Scalar extra));
#elif defined(DEATH_TARGET_WASM)
	#define _DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, extra)				\
		if(features >= (Death::Cpu::Simd128 extra))								\
			return function(DEATH_CPU_SELECT(Death::Cpu::Simd128 extra));		\
		return function(DEATH_CPU_SELECT(Death::Cpu::Scalar extra));
#else
	#define _DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, extra)				\
		return function(DEATH_CPU_SELECT(Death::Cpu::Scalar extra));
#endif

	// _DEATH_CPU_DISPATCHER() specialization for 0 extra instruction sets. Basically equivalent to DEATH_CPU_DISPATCHER_BASE()
	// except for the extra DEATH_CPU_SELECT() macro.
	#define _DEATH_CPU_DISPATCHER0(function)									\
		decltype(function(DEATH_CPU_SELECT(Death::Cpu::Scalar))) function(Death::Cpu::Features features) {	\
			_DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, )					\
		}

	// _DEATH_CPU_DISPATCHER() specialization for 1+ extra instruction sets. On Clang this still generates quite a reasonable code
	// for 2 extra sets compared to DEATH_CPU_DISPATCHER_BASE(), on GCC it's ~25% longer but also still reasonable.
	// I attempted adding an "unrolled" _DEATH_CPU_DISPATCHER2() but it made everything significantly worse on both compilers,
	// more than doubling the amount of generated code.
	#define _DEATH_CPU_DISPATCHERn(function, ...)								\
		template<unsigned int value> DEATH_ALWAYS_INLINE decltype(function(DEATH_CPU_SELECT(Death::Cpu::Scalar))) function ## Internal(Death::Cpu::Features features, Death::Cpu::Implementation::Tags<value>) { \
			_DEATH_CPU_DISPATCHER_IMPLEMENTATION(function, |Death::Cpu::Implementation::Tags<value>{Death::Cpu::Implementation::Init})	\
		}																		\
		template<unsigned int value, class First, class ...Next> DEATH_ALWAYS_INLINE decltype(function(DEATH_CPU_SELECT(Death::Cpu::Scalar))) function ## Internal(Death::Cpu::Features features, Death::Cpu::Implementation::Tags<value> extra, First first, Next... next) { \
			static_assert(!(static_cast<unsigned int>(Death::Cpu::Implementation::tags(First{Death::Cpu::Implementation::Init})) & Death::Cpu::Implementation::BaseTagMask),	\
				"only extra instruction set tags should be explicitly listed");	\
			if(features & first)												\
				return function ## Internal(features, extra|first, next...);	\
			else																\
				return function ## Internal(features, extra, next...);			\
		}																		\
		decltype(function(DEATH_CPU_SELECT(Death::Cpu::Scalar))) function(Death::Cpu::Features features) {	\
			return function ## Internal(features, Death::Cpu::Implementation::Tags<0>{Death::Cpu::Implementation::Init}, __VA_ARGS__);	\
		}

	/**
		@brief Create a function for a runtime dispatch on a base CPU instruction set and select extra instruction sets

		Given a set of function overloads named @p function that accept a CPU tag
		combination wrapped in @ref DEATH_CPU_DECLARE() as a parameter, all returning
		a function pointer of the same type, creates a function with signature
		@cpp function(Cpu::Features) @ce which will select among the overloads using a
		runtime-specified @relativeref{Death,Cpu::Features}, using the same rules as
		the compile-time overload selection. The extra instruction sets considered in
		the overload selection are specified as additional parameters to the macro,
		specifying none is valid as well. For this macro to work, at the very least
		there has to be an overload with a
		@ref Death::Cpu::Scalar "DEATH_CPU_DECLARE(Cpu::Scalar)" argument. See
		@ref Cpu-usage-automatic-runtime-dispatch for more information and an example.

		For a dispatch using just the base instruction set use
		@ref DEATH_CPU_DISPATCHER_BASE() instead.
	*/
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL)
	#define _DEATH_CPU_DISPATCHER(...)										\
		_DEATH_HELPER_PICK(__VA_ARGS__, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHER0, )(__VA_ARGS__)
#else
	// Workaround for MSVC not being able to expand __VA_ARGS__ correctly. Would work with /Zc:preprocessor or /experimental:preprocessor,
	// but I'm not enabling that globally yet. Source: https://stackoverflow.com/a/5134656
	#define _DEATH_CPU_DISPATCHER_FFS_MSVC_EXPAND_THIS(x) x
	#define _DEATH_CPU_DISPATCHER(...)										\
		_DEATH_CPU_DISPATCHER_FFS_MSVC_EXPAND_THIS(_DEATH_HELPER_PICK(__VA_ARGS__, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHERn, _DEATH_CPU_DISPATCHER0, )(__VA_ARGS__))
#endif

	/**
		@brief Create a runtime-dispatched function pointer

		Assuming a @p dispatcher was defined with either @ref DEATH_CPU_DISPATCHER()
		or @ref DEATH_CPU_DISPATCHER_BASE(), defines a function pointer variable with
		a signature specified in the second variadic argument. In a global constructor
		the variable is assigned a function pointer returned by @p dispatcher for
		@relativeref{Death,Cpu::runtimeFeatures()}.

		The pointer can be changed afterwards, such as for testing purposes, See also
		@ref DEATH_CPU_DISPATCHED_IFUNC() which avoids the overhead of function
		pointer indirection.

		See @ref Cpu-usage-automatic-cached-dispatch for more information, usage
		example and overhead comparison.
	*/
	#define DEATH_CPU_DISPATCHED_POINTER(dispatcher, ...)					\
		__VA_ARGS__ = dispatcher(Death::Cpu::runtimeFeatures());

	/**
		@brief Create a runtime-dispatched function via GNU IFUNC

		Available only if @ref DEATH_CPU_USE_IFUNC is enabled. Assuming a
		@p dispatcher was defined with either @ref DEATH_CPU_DISPATCHER() or
		@ref DEATH_CPU_DISPATCHER_BASE(), defines a function with a signature
		specified via the third variadic argument. The signature has to match @p type.
		The function uses the [GNU IFUNC](https://sourceware.org/glibc/wiki/GNU_IFUNC)
		mechanism, which causes the function call to be resolved to a function pointer
		returned by @p dispatcher for @relativeref{Death,Cpu::runtimeFeatures()}. The
		dispatch is performed by the dynamic linker during early startup and cannot be
		changed afterwards.

		If @ref DEATH_CPU_USE_IFUNC isn't available, is explicitly disabled or if
		you need to be able to subsequently change the dispatched-to function (such as
		for testing purposes), use @ref DEATH_CPU_DISPATCHED_POINTER() instead.

		See @ref Cpu-usage-automatic-cached-dispatch for more information, usage
		example and overhead comparison.
	*/
#if defined(DEATH_CPU_USE_IFUNC)
// On ARM we get CPU features through getauxval() but it can't be called from an ifunc resolver because it's too early at that point.
// Instead, AT_HWCAPS is passed to it from outside, so there we call an internal variant with the caps parameter. On x86 calling into CPUID
// from within an ifunc resolver is no problem.
//
// Although not specifically documented anywhere, the dispatcher has to have C++ mangling disabled in order to be found by __attribute__((ifunc)),
// on both GCC and Clang. That however means it's exported even if inside an anonymous namespace, which is undesirable. To fix that,
// it's marked as static...
#if defined(DEATH_TARGET_CLANG)
// ... unfortunately the static makes Clang not find the name again, so there we can't use it. But, to drown even deeper, not using the static causes
// the -Wmissing-prototypes macro to get fired (which is enabled globally because it has obvious benefits), so to avoid noise it has to be disabled here.
#if !defined(DEATH_TARGET_ARM)
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	_Pragma("GCC diagnostic push")											\
	_Pragma("GCC diagnostic ignored \"-Wmissing-prototypes\"")				\
	extern "C" decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher() {	\
		return dispatcher(Death::Cpu::runtimeFeatures());					\
	}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher)));						\
	_Pragma("GCC diagnostic pop")
#else
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	_Pragma("GCC diagnostic push")											\
	_Pragma("GCC diagnostic ignored \"-Wmissing-prototypes\"")				\
	extern "C" decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher(unsigned long caps) {	\
		return dispatcher(Death::Cpu::Implementation::runtimeFeatures(caps));	\
	}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher)));						\
	_Pragma("GCC diagnostic pop")
#endif
#elif defined(DEATH_TARGET_GCC) && __GNUC__*100 + __GNUC_MINOR__ < 409
// Furthermore, due to https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58105 the resolver function won't work on GCC 4.8 unless it's marked with
// DEATH_NEVER_INLINE. That, however, causes GCC 4.8 to spit out a bogus warning that the dispatcher function is redeclared as noinline -- it thinks
// it's the same function as the always-inline lambda wrappers which have the same name. Despite the warning it works, but to avoid useless noise the
// ifunc dispatcher is named differently.
#if !defined(DEATH_TARGET_ARM)
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	extern "C" { DEATH_NEVER_INLINE static decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher ## Ifunc() {	\
		return dispatcher(Death::Cpu::runtimeFeatures());					\
	}}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher "Ifunc")));
#else
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	extern "C" { DEATH_NEVER_INLINE static decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher ## Ifunc(unsigned long caps) {	\
		return dispatcher(Death::Cpu::Implementation::runtimeFeatures(caps));	\
	}}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher "Ifunc")));
#endif
#else
// Only GCC 4.9+ has the implementation in the most minimal form.
#if !defined(DEATH_TARGET_ARM)
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	extern "C" { static decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher() {	\
		return dispatcher(Death::Cpu::runtimeFeatures());					\
	}}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher)));
#else
#define DEATH_CPU_DISPATCHED_IFUNC(dispatcher, ...)							\
	extern "C" { static decltype(dispatcher(std::declval<Death::Cpu::Features>())) dispatcher(unsigned long caps) {	\
		return dispatcher(Death::Cpu::Implementation::runtimeFeatures(caps));	\
	}}																		\
	__VA_ARGS__ __attribute__((ifunc(#dispatcher)));
#endif
#endif
#endif

#if defined(DEATH_TARGET_X86)
	/**
		@brief Enable SSE2 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang and @ref DEATH_TARGET_CLANG_CL "clang-cl"
		expands to @cpp __attribute__((__target__("sse2"))) @ce, allowing use of
		[SSE2](https://en.wikipedia.org/wiki/SSE2) and earlier SSE instructions inside
		a function annotated with this macro without having to specify `-msse2` for the
		whole compilation unit. On x86 MSVC expands to nothing, as the compiler doesn't
		restrict use of intrinsics in any way. Not defined on other compilers or
		architectures.

		As a special case, if @ref DEATH_TARGET_SSE2 is present (meaning SSE2 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Implied by @ref DEATH_ENABLE_SSE3. See @ref Cpu-usage-target-attributes for
		more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsSse2.h instead of
			@cpp #include <emmintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Sse2}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_SSE2)
#	define DEATH_ENABLE_SSE2
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE2
#	endif
#elif defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_SSE2 __attribute__((__target__("sse2")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE2 "sse2",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_SSE2
#endif

	/**
		@brief Enable SSE3 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("sse3"))) @ce, allowing use of
		[SSE3](https://en.wikipedia.org/wiki/SSE3) and earlier SSE intrinsics inside a
		function annotated with this macro without having to specify `-msse3` for the
		whole compilation unit. On x86 MSVC expands to nothing, as the compiler doesn't
		restrict use of intrinsics in any way. Not defined on other compilers or
		architectures.

		As a special case, if @ref DEATH_TARGET_SSE3 is present (meaning SSE3 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Superset of @ref DEATH_ENABLE_SSE2, implied by @ref DEATH_ENABLE_SSSE3. See
		@ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsSse3.h instead of
			@cpp #include <pmmintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Sse3}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_SSE3)
#	define DEATH_ENABLE_SSE3
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE3
#	endif
#elif defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
// The -msse3 option implies -msse2 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -msse3`)
#	define DEATH_ENABLE_SSE3 __attribute__((__target__("sse3")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE3 "sse3",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_SSE3
#endif

	/**
		@brief Enable SSSE3 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang and @ref DEATH_TARGET_CLANG_CL "clang-cl"
		expands to @cpp __attribute__((__target__("ssse3"))) @ce, allowing use of
		[SSSE3](https://en.wikipedia.org/wiki/SSSE3) and earlier SSE instructions
		inside a function annotated with this macro without having to specify `-mssse3`
		for the whole compilation unit. On x86 MSVC expands to nothing, as the compiler
		doesn't restrict use of intrinsics in any way. Not defined on other compilers
		or architectures.

		As a special case, if @ref DEATH_TARGET_SSSE3 is present (meaning SSSE3 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Superset of @ref DEATH_ENABLE_SSE3, implied by @ref DEATH_ENABLE_SSE41. See
		@ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsSsse3.h instead of
			@cpp #include <tmmintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Ssse3}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_SSSE3)
#	define DEATH_ENABLE_SSSE3
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSSE3
#	endif
#elif defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
// The -mssse3 option implies -msse2 -msse3 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -mssse3`)
#	define DEATH_ENABLE_SSSE3 __attribute__((__target__("ssse3")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSSE3 "ssse3",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_SSSE3
#endif

	/**
		@brief Enable SSE4.1 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang and @ref DEATH_TARGET_CLANG_CL "clang-cl"
		expands to @cpp __attribute__((__target__("sse4.1"))) @ce, allowing use of
		[SSE4.1](https://en.wikipedia.org/wiki/SSE4#SSE4.1) and earlier SSE
		instructions inside a function annotated with this macro without having to
		specify `-msse4.1` for the whole compilation unit. On x86 MSVC expands to
		nothing, as the compiler doesn't restrict use of intrinsics in any way. Not
		defined on other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_SSE41 is present (meaning SSE4.1 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Superset of @ref DEATH_ENABLE_SSSE3, implied by @ref DEATH_ENABLE_SSE42.
		See @ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-danger}

		@par
			Unless @ref DEATH_TARGET_SSE41 is present, this macro is not defined on
			GCC 4.8, as SSE4.1 intrinsics only work if `-msse4.2` is specified as well
			due to both SSE4.1 and 4.2 intrinsics living in the same header. You can
			only use @ref DEATH_ENABLE_SSE42 in this case.

		@see @relativeref{Death,Cpu::Sse41}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_SSE41)
#	define DEATH_ENABLE_SSE41
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE41
#	endif
#elif (defined(DEATH_TARGET_GCC) && __GNUC__*100 + __GNUC_MINOR__ >= 409) || defined(DEATH_TARGET_CLANG) /* also matches clang-cl */
// The -msse4.1 option implies -msse2 -msse3 -mssse3 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -msse4.1`)
#	define DEATH_ENABLE_SSE41 __attribute__((__target__("sse4.1")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE41 "sse4.1",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_SSE41
#endif

/**
	@brief Enable SSE4.2 for given function

	On @ref DEATH_TARGET_X86 "x86" GCC, Clang and @ref DEATH_TARGET_CLANG_CL "clang-cl"
	expands to @cpp __attribute__((__target__("sse4.2"))) @ce, allowing use of
	[SSE4.2](https://en.wikipedia.org/wiki/SSE4#SSE4.2) and earlier SSE
	instructions inside a function annotated with this macro without having to
	specify `-msse4.2` for the whole compilation unit. On x86 MSVC expands to
	nothing, as the compiler doesn't restrict use of intrinsics in any way. Not
	defined on other compilers or architectures.

	As a special case, if @ref DEATH_TARGET_SSE42 is defined (meaning SSE4.2 is
	enabled for the whole compilation unit), this macro is defined as empty on all
	compilers.

	Superset of @ref DEATH_ENABLE_SSE41, implied by @ref DEATH_ENABLE_AVX. See
	@ref Cpu-usage-target-attributes for more information and usage example.

	@m_class{m-note m-info}

	@par
		If you target GCC 4.8, you may also want to use @ref IntrinsicsSse4.h instead of
		@cpp #include <smmintrin.h> @ce and @cpp #include <nmmintrin.h> @ce to be
		able to access the intrinsics on this compiler.

	@see @relativeref{Death,Cpu::Sse42}, @ref DEATH_ENABLE()
*/
#if defined(DEATH_TARGET_SSE42)
#	define DEATH_ENABLE_SSE42
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE42
#	endif
#elif defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
// The -msse4.2 option implies -msse2 -msse3 -mssse3 -msse4.1 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -msse4.2`)
#	define DEATH_ENABLE_SSE42 __attribute__((__target__("sse4.2")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SSE42 "sse4.2",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_SSE42
#endif

	/**
		@brief Enable POPCNT for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang and @ref DEATH_TARGET_CLANG_CL "clang-cl"
		expands to @cpp __attribute__((__target__("popcnt"))) @ce, allowing use of the
		[POPCNT](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation))
		instructions inside a function annotated with this macro without having to
		specify `-mpopcnt` for the whole compilation unit. On x86 MSVC expands to
		nothing, as the compiler doesn't restrict use of intrinsics in any way. Not
		defined on GCC 4.8, as there it's not generally possible to enable it alongside
		other instruction sets without running into linker errors. Not defined on other
		compilers or architectures.

		As a special case, if @ref DEATH_TARGET_POPCNT is defined (meaning POCNT is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Neither a superset nor implied by any other `DEATH_ENABLE_*` macro, so you
		may need to specify it together with others. See
		@ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8 or Clang < 7, you may also want to use @ref IntrinsicsSse4.h instead of
			@cpp #include <nmmintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Popcnt}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_POPCNT)
#	define DEATH_ENABLE_POPCNT
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_POPCNT
#	endif
#elif (defined(DEATH_TARGET_GCC) && __GNUC__*100 + __GNUC_MINOR__ >= 409) || defined(DEATH_TARGET_CLANG) /* matches clang-cl */
#	define DEATH_ENABLE_POPCNT __attribute__((__target__("popcnt")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_POPCNT "popcnt",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_POPCNT
#endif

	/**
		@brief Enable LZCNT for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("lzcnt"))) @ce, allowing use of the
		[LZCNT](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation))
		instructions inside a function annotated with this macro without having to
		specify `-mlzcnt` for the whole compilation unit. On x86 MSVC expands to
		nothing, as the compiler doesn't restrict use of intrinsics in any way. Unlike
		the SSE variants and POPCNT this macro is not defined on
		@ref DEATH_TARGET_CLANG_CL "clang-cl", as there LZCNT, BMI1, BMI2, AVX and
		newer intrinsics are provided only if enabled on compiler command line. Not
		defined on GCC 4.8, as there it's not generally possible to enable it alongside
		unrelated instruction sets without running into linker errors. Not defined on
		other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_LZCNT is defined (meaning LZCNT is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Neither a superset nor implied by any other `DEATH_ENABLE_*` macro (not even
		@ref DEATH_TARGET_BMI2, although the name would suggest that), so you may
		need to specify it together with others.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Lzcnt}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_LZCNT)
#	define DEATH_ENABLE_LZCNT
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_LZCNT
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
#	define DEATH_ENABLE_LZCNT __attribute__((__target__("lzcnt")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_LZCNT "lzcnt",
#	endif
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __LZCNT__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#	define DEATH_ENABLE_LZCNT
#endif

	/**
		@brief Enable BMI1 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang expands to
		@cpp __attribute__((__target__("bmi"))) @ce, allowing use of the
		[BMI1](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#BMI1_(Bit_Manipulation_Instruction_Set_1))
		instructions inside a function annotated with this macro without having to
		specify `-mbmi` for the whole compilation unit. On x86 MSVC expands to nothing,
		as the compiler doesn't restrict use of intrinsics in any way. Unlike the SSE
		variants and POPCNT this macro is not defined on
		@ref DEATH_TARGET_CLANG_CL "clang-cl", as there LZCNT, BMI1, AVX and newer
		intrinsics are provided only if enabled on compiler command line. Not defined
		on GCC 4.8, as there it's not generally possible to enable it alongside
		unrelated instruction sets without running into linker errors. Not defined on
		other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_BMI1 is defined (meaning BMI1 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Neither a superset nor implied by any other `DEATH_ENABLE_*` macro, so you
		may need to specify it together with others. See
		@ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Bmi1}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_BMI1)
#	define DEATH_ENABLE_BMI1
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_BMI1
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
#	define DEATH_ENABLE_BMI1 __attribute__((__target__("bmi")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_BMI1 "bmi",
#	endif
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __BMI__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#	define DEATH_ENABLE_BMI1
#endif

	/**
		@brief Enable BMI2 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC, Clang expands to
		@cpp __attribute__((__target__("bmi2"))) @ce, allowing use of the
		[BMI2](https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#BMI2_(Bit_Manipulation_Instruction_Set_2))
		instructions inside a function annotated with this macro without having to
		specify `-mbmi2` for the whole compilation unit. On x86 MSVC expands to
		nothing, as the compiler doesn't restrict use of intrinsics in any way. Unlike
		the SSE variants and POPCNT this macro is not defined on
		@ref DEATH_TARGET_CLANG_CL "clang-cl", as there LZCNT, BMI1, BMI2, AVX and
		newer intrinsics are provided only if enabled on compiler command line. Not
		defined on GCC 4.8, as there it's not generally possible to enable it alongside
		unrelated instruction sets without running into linker errors. Not defined on
		other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_BMI2 is defined (meaning BMI2 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Neither a superset nor implied by any other `DEATH_ENABLE_*` macro (not even
		@ref DEATH_TARGET_BMI1, although the name would suggest that), so you may
		need to specify it together with others.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Bmi2}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_BMI2)
#	define DEATH_ENABLE_BMI2
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_BMI2
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
#	define DEATH_ENABLE_BMI2 __attribute__((__target__("bmi2")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_BMI2 "bmi2",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __BMI2__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_BMI2
#endif

	/**
		@brief Enable AVX for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("avx"))) @ce, allowing use of
		[AVX](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions) and all earlier
		SSE instructions inside a function annotated with this macro without having to
		specify `-mavx` for the whole compilation unit. On x86 MSVC expands to nothing,
		as the compiler doesn't restrict use of intrinsics in any way. Unlike the SSE
		variants this macro is not defined on @ref DEATH_TARGET_CLANG_CL "clang-cl",
		as there AVX and newer intrinsics are provided only if enabled on compiler
		command line. Not defined on other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_AVX is present (meaning AVX is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Superset of @ref DEATH_ENABLE_SSE42, implied by @ref DEATH_ENABLE_AVX2. See
		@ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Avx}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_AVX)
#	define DEATH_ENABLE_AVX
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX
#	endif
#elif defined(DEATH_TARGET_GCC) /* does not match clang-cl */
// The -mavx option implies -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -mavx`)
#	define DEATH_ENABLE_AVX __attribute__((__target__("avx")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX "avx",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __AVX__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_AVX
#endif

	/**
		@brief Enable AVX F16C for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("f16c"))) @ce, allowing use of
		[F16C](https://en.wikipedia.org/wiki/F16C) instructions inside a function
		annotated with this macro without having to specify `-mf16c` for the whole
		compilation unit. On x86 MSVC expands to nothing, as the compiler doesn't
		restrict use of intrinsics in any way. Unlike the SSE variants this macro is
		not defined on @ref DEATH_TARGET_CLANG_CL "clang-cl", as there AVX and newer
		intrinsics are provided only if enabled on compiler command line. Not defined
		on GCC 4.8, as there it's not generally possible to enable it alongside other
		instruction sets without running into linker errors. Not defined on other
		compilers or architectures.

		As a special case, if @ref DEATH_TARGET_AVX_F16C is present (meaning AVX F16C
		is enabled for the whole compilation unit), this macro is defined as empty on
		all compilers.

		Superset of @ref DEATH_ENABLE_AVX on both GCC and Clang. However not
		portably implied by any other `DEATH_ENABLE_*` macro so you may need to
		specify it together with others. See @ref Cpu-usage-target-attributes for more
		information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::AvxF16c}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_AVX_F16C)
#	define DEATH_ENABLE_AVX_F16C
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX_F16C
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
// The -mf16c option implies -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx on both GCC and Clang (verified with `echo | gcc -dM -E - -mf16c`)
#	define DEATH_ENABLE_AVX_F16C __attribute__((__target__("f16c")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX_F16C "f16c",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __F16C__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_AVX_F16C
#endif

	/**
		@brief Enable AVX FMA for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("fma"))) @ce, allowing use of
		[FMA](https://en.wikipedia.org/wiki/FMA_instruction_set) instructions inside a
		function annotated with this macro without having to specify `-mfma` for the
		whole compilation unit. On x86 MSVC expands to nothing, as the compiler doesn't
		restrict use of intrinsics in any way. Unlike the SSE variants this macro is
		not defined on @ref DEATH_TARGET_CLANG_CL "clang-cl", as there AVX and newer
		intrinsics are provided only if enabled on compiler command line. Not defined
		on GCC 4.8, as there it's not generally possible to enable it alongside other
		instruction sets without running into linker errors. Not defined on other
		compilers or architectures.

		As a special case, if @ref DEATH_TARGET_AVX_FMA is present (meaning AVX with
		FMA is enabled for the whole compilation unit), this macro is defined as empty
		on all compilers.

		Superset of @ref DEATH_ENABLE_AVX on both GCC and Clang. However not
		portably implied by any other `DEATH_ENABLE_*` macro so you may need to
		specify it together with others. See @ref Cpu-usage-target-attributes for more
		information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::AvxFma}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_AVX_FMA)
#	define DEATH_ENABLE_AVX_FMA
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX_FMA
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
// The -mfma option implies -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx on both GCC and Clang (verified with `echo | gcc -dM -E - -mf16c`)
#	define DEATH_ENABLE_AVX_FMA __attribute__((__target__("fma")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX_FMA "fma",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __FMA__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_AVX_FMA
#endif

	/**
		@brief Enable AVX2 for given function

		On @ref DEATH_TARGET_X86 "x86" GCC and Clang expands to
		@cpp __attribute__((__target__("avx2"))) @ce, allowing use of
		[AVX2](https://en.wikipedia.org/wiki/Advanced_Vector_Extensions#Advanced_Vector_Extensions_2),
		FMA, F16C, AVX and all earlier SSE instructions inside a function annotated
		with this macro without having to specify `-mavx2` for the whole compilation
		unit. On x86 MSVC expands to nothing, as the compiler doesn't restrict use of
		intrinsics in any way. Unlike the SSE variants this macro is not defined on
		@ref DEATH_TARGET_CLANG_CL "clang-cl", as there AVX and newer intrinsics are
		provided only if enabled on compiler command line. Not defined on other
		compilers  or architectures.

		As a special case, if @ref DEATH_TARGET_AVX2 is present (meaning AVX2 is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers.

		Superset of @ref DEATH_ENABLE_AVX, implied by @ref DEATH_ENABLE_AVX512F.
		See @ref Cpu-usage-target-attributes for more information and usage example.

		@m_class{m-note m-info}

		@par
			If you target GCC 4.8, you may also want to use @ref IntrinsicsAvx.h instead of
			@cpp #include <immintrin.h> @ce to be able to access the intrinsics on this
			compiler.

		@see @relativeref{Death,Cpu::Avx2}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_AVX2)
#	define DEATH_ENABLE_AVX2
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX2
#	endif
#elif defined(DEATH_TARGET_GCC) /* does not match clang-cl */
// The -mavx2 option implies -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -mavx2`)
#	define DEATH_ENABLE_AVX2 __attribute__((__target__("avx2")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX2 "avx2",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __AVX2__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_AVX2
#endif

	/**
		@brief Enable AVX-512 Foundation for given function

		On @ref DEATH_TARGET_X86 "x86" GCC 4.9+ and Clang expands to
		@cpp __attribute__((__target__("avx512f"))) @ce, allowing use of
		[AVX-512](https://en.wikipedia.org/wiki/AVX-512) Foundation and all earlier AVX
		and SSE instructions inside a function annotated with this macro without having
		to specify `-mavx512f` for the whole compilation unit. On x86 MSVC 2017 15.3+
		expands to nothing, as the compiler doesn't restrict use of intrinsics in any
		way. Unlike the SSE variants this macro is not defined on
		@ref DEATH_TARGET_CLANG_CL "clang-cl", as there AVX and newer intrinsics are
		provided only if enabled on compiler command line. Not defined on other
		compilers, earlier compiler versions without AVX-512 support or other
		architectures.

		As a special case, if @ref DEATH_TARGET_AVX512F is present (meaning AVX-512
		Foundation is enabled for the whole compilation unit), this macro is defined as
		empty on all compilers.

		Superset of @ref DEATH_ENABLE_AVX2. See @ref Cpu-usage-target-attributes for
		more information and usage example.
		@see @relativeref{Death,Cpu::Avx512f}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_AVX512F)
#	define DEATH_ENABLE_AVX512F
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX512F
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 409 || defined(DEATH_TARGET_CLANG)) /* does not match clang-cl */
// The -mavx512 option implies -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx -mavx2 on both GCC and Clang, so no need to specify those as well (verified with `echo | gcc -dM -E - -mavx512f`)
#	define DEATH_ENABLE_AVX512F __attribute__((__target__("avx512f")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_AVX512F "avx512f",
#	endif
// https://github.com/llvm/llvm-project/commit/379a1952b37247975d2df8d23498675c9c8cc730,
// still present in Jul 2022, meaning we can only use these if __AVX512F__ is defined. Funnily enough the older headers don't have
// this on their own, only <immintrin.h>. Also I don't think "Actually using intrinsics on Windows already requires
// the right /arch: settings" is correct.
#elif defined(DEATH_TARGET_MSVC) && _MSC_VER >= 1911 && !defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE_AVX512F
#endif
#endif

#if defined(DEATH_TARGET_ARM)
	/**
		@brief Enable NEON for given function

		On 32-bit @ref DEATH_TARGET_ARM "ARM" GCC expands to
		@cpp __attribute__((__target__("fpu=neon"))) @ce, allowing use of
		[NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon))
		instructions inside a function annotated with this macro without having to
		specify `-mfpu=neon` for the whole compilation unit. On ARM MSVC expands to
		nothing, as the compiler doesn't restrict use of intrinsics in any way. In
		contrast to GCC, this macro is not defined on Clang, as it makes the NEON
		intrinsics available only if enabled on compiler command line. Not defined on
		other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_NEON is present (meaning NEON is
		enabled for the whole compilation unit), this macro is defined as empty on all
		compilers. This is also the case for ARM64, where NEON support is implicit
		(and where `-mfpu=neon` is unrecognized).

		Implied by @ref DEATH_ENABLE_NEON_FMA. See @ref Cpu-usage-target-attributes
		for more information and usage example.
		@see @relativeref{Death,Cpu::Neon}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_NEON)
#	define DEATH_ENABLE_NEON
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON
#	endif
// https://github.com/android/ndk/issues/1066 is the only reported (and ignored) issue I found, feels strange that people would
// just not use ifunc or target attributes on Android at all and instead put everything in separate files. Needs further
// investigation. Too bad most ARM platforms ditched GCC, where this works properly.
#elif defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG)
#	define DEATH_ENABLE_NEON __attribute__((__target__("fpu=neon")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON "fpu=neon",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_NEON
#endif

	/**
		@brief Enable NEON FMA for given function

		On 32-bit @ref DEATH_TARGET_ARM "ARM" GCC expands to
		@cpp __attribute__((__target__("fpu=neon-vfpv4"))) @ce, allowing use of
		[NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon)) FMA
		instructions inside a function annotated with this macro without having to
		specify `-mfpu=neon-vfpv4` for the whole compilation unit. On ARM MSVC expands
		to nothing, as the compiler doesn't restrict use of intrinsics in any way. In
		contrast to GCC, this macro is not defined on Clang, as it makes the NEON FMA
		intrinsics available only if enabled on compiler command line. Not defined on
		other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_NEON_FMA is present (meaning NEON FMA
		is enabled for the whole compilation unit), this macro is defined as empty on
		all compilers. This is also the case for ARM64, where NEON support is implicit
		(and where `-mfpu=neon-vfpv4` is unrecognized).

		Superset of @ref DEATH_ENABLE_NEON, implied by @ref DEATH_ENABLE_NEON_FP16.
		See @ref Cpu-usage-target-attributes for more information and usage example.
		@see @relativeref{Death,Cpu::NeonFma}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_NEON_FMA)
#	define DEATH_ENABLE_NEON_FMA
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON_FMA
#	endif
// See DEATH_ENABLE_NEON above for details about Clang
#elif defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG)
#	define DEATH_ENABLE_NEON_FMA __attribute__((__target__("fpu=neon-vfpv4")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON_FMA "fpu=neon-vfpv4",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_NEON_FMA
#endif

	/**
		@brief Enable NEON FP16 for given function

		On @ref DEATH_TARGET_ARM "ARM" GCC expands to
		@cpp __attribute__((__target__("arch=armv8.2-a+fp16"))) @ce, allowing use of
		ARMv8.2-a [NEON](https://en.wikipedia.org/wiki/ARM_architecture#Advanced_SIMD_(Neon))
		FP16 vector arithmetic inside a function annotated with this macro without
		having to specify `-march=armv8.2-a+fp16` for the whole compilation unit. On
		ARM MSVC expands to nothing, as the compiler doesn't restrict use of intrinsics
		in any way. In contrast to GCC, this macro is not defined on Clang, as it makes
		the NEON FP16 intrinsics available only if enabled on compiler command line.
		Not defined on other compilers or architectures.

		As a special case, if @ref DEATH_TARGET_NEON_FP16 is present (meaning NEON
		FP16 is enabled for the whole compilation unit), this macro is defined as empty
		on all compilers.

		Superset of @ref DEATH_ENABLE_NEON_FMA. See @ref Cpu-usage-target-attributes
		for more information and usage example.
		@see @relativeref{Death,Cpu::NeonFp16}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_NEON_FP16)
#	define DEATH_ENABLE_NEON_FP16
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON_FP16
#	endif
// See DEATH_ENABLE_NEON above for details about Clang
#elif defined(DEATH_TARGET_GCC) && !defined(DEATH_TARGET_CLANG)
#	define DEATH_ENABLE_NEON_FP16 __attribute__((__target__("arch=armv8.2-a+fp16")))
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_NEON_FP16 "arch=armv8.2-a+fp16",
#	endif
#elif defined(DEATH_TARGET_MSVC)
#	define DEATH_ENABLE_NEON_FP16
#endif
#endif

#if defined(DEATH_TARGET_WASM)
	/**
		@brief Enable SIMD128 for given function

		Given that it's currently not possible to selectively use
		[128-bit SIMD](https://github.com/webassembly/simd) in a WebAssembly module
		without causing a compilation error on runtimes that don't support it, this
		macro is only defined if @ref DEATH_TARGET_SIMD128 is present (meaning
		SIMD128 is explicitly enabled for the whole compilation unit), and is always
		empty, as @cpp __attribute__((__target__("simd128"))) @ce would be redundant
		if `-msimd128` is passed on the command line.

		The situation may change once the
		[feature detection proposal](https://github.com/WebAssembly/feature-detection/blob/main/proposals/feature-detection/Overview.md)
		is implemented, but likely only for instruction sets building on top of this
		one.

		See @ref Cpu-usage-target-attributes for more information and usage example.
		@see @relativeref{Death,Cpu::Simd128}, @ref DEATH_ENABLE()
	*/
#if defined(DEATH_TARGET_SIMD128)
#	define DEATH_ENABLE_SIMD128
#	if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#		define _DEATH_ENABLE_SIMD128
#	endif
#endif
#endif

// GCC before version 12 and Clang before version 8 treat DEATH_ENABLE_ macros after each other. Clang since version 8 then treats
// that as if only "foo" was specified, which is different but also wrong (I didn't bother finding a commit backing this) and it's
// still broken in Clang 15. Instead, the only accepted form is __attribute__((target("foo,bar"))). Fortunately, string literal
// concatenation works here, thus with some extra macro trickery we can produce __attribute__((target("foo" "," "bar"))).
// The pieces are _DEATH_ENABLE_* variants defined if and only if a corresponding DEATH_ENABLE_* macro is defined. These can
// however be empty, thus it's not possible to just join them all with "," in between. Instead, the macros themselves have a trailing
// comma after the string literal (thus "foo",), which causes the empty macros be filtered out when passed one after another
// (without commas) from _DEATH_ENABLEn to _DEATH_ENABLE_CONCATENATE().
#if (defined(DEATH_TARGET_GCC) && __GNUC__ < 12) || defined(DEATH_TARGET_CLANG)
#define _DEATH_ENABLE_CONCATENATE0(unused)
#define _DEATH_ENABLE_CONCATENATE1(v0, unused)								\
	__attribute__((__target__(v0)))
#define _DEATH_ENABLE_CONCATENATE2(v0, v1, unused)							\
	__attribute__((__target__(v0 "," v1)))
#define _DEATH_ENABLE_CONCATENATE3(v0, v1, v2, unused)						\
	__attribute__((__target__(v0 "," v1 "," v2)))
#define _DEATH_ENABLE_CONCATENATE4(v0, v1, v2, v3, unused)					\
	__attribute__((__target__(v0 "," v1 "," v2 "," v3)))
#define _DEATH_ENABLE_CONCATENATE5(v0, v1, v2, v3, v4, unused)				\
	__attribute__((__target__(v0 "," v1 "," v2 "," v3 "," v4)))
#define _DEATH_ENABLE_CONCATENATE6(v0, v1, v2, v3, v4, v5, unused)			\
	__attribute__((__target__(v0 "," v1 "," v2 "," v3 "," v4 "," v5)))
#define _DEATH_ENABLE_CONCATENATE7(v0, v1, v2, v3, v4, v5, v6, unused)		\
	__attribute__((__target__(v0 "," v1 "," v2 "," v3 "," v4 "," v5 "," v6)))
#define _DEATH_ENABLE_CONCATENATE(...)										\
	_DEATH_HELPER_PICK(__VA_ARGS__, _DEATH_ENABLE_CONCATENATE7, _DEATH_ENABLE_CONCATENATE6, _DEATH_ENABLE_CONCATENATE5, _DEATH_ENABLE_CONCATENATE4, _DEATH_ENABLE_CONCATENATE3, _DEATH_ENABLE_CONCATENATE2, _DEATH_ENABLE_CONCATENATE1, _DEATH_ENABLE_CONCATENATE0, )(__VA_ARGS__)
// No _DEATH_HELPER_PASTE() needed here, as there's enough other indirections to make that work
#define _DEATH_ENABLE1(v0)													\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
	)
#define _DEATH_ENABLE2(v0, v1)												\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
	)
#define _DEATH_ENABLE3(v0, v1, v2)											\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
		_DEATH_ENABLE_ ## v2												\
	)
#define _DEATH_ENABLE4(v0, v1, v2, v3)										\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
		_DEATH_ENABLE_ ## v2												\
		_DEATH_ENABLE_ ## v3												\
	)
#define _DEATH_ENABLE5(v0, v1, v2, v3, v4)									\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
		_DEATH_ENABLE_ ## v2												\
		_DEATH_ENABLE_ ## v3												\
		_DEATH_ENABLE_ ## v4												\
	)
#define _DEATH_ENABLE6(v0, v1, v2, v3, v4, v5)								\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
		_DEATH_ENABLE_ ## v2												\
		_DEATH_ENABLE_ ## v3												\
		_DEATH_ENABLE_ ## v4												\
		_DEATH_ENABLE_ ## v5												\
	)
#define _DEATH_ENABLE7(v0, v1, v2, v3, v4, v5, v6)							\
	_DEATH_ENABLE_CONCATENATE(												\
		_DEATH_ENABLE_ ## v0												\
		_DEATH_ENABLE_ ## v1												\
		_DEATH_ENABLE_ ## v2												\
		_DEATH_ENABLE_ ## v3												\
		_DEATH_ENABLE_ ## v4												\
		_DEATH_ENABLE_ ## v5												\
		_DEATH_ENABLE_ ## v6												\
	)
// None of this is needed for GCC 12+, fortunately, so here the whole thing expands to just DEATH_ENABLE_FOO DEATH_ENABLE_BAR.
// I hope Clang eventually fixes this as well, thus keeping both variants so I can drop the nasty one in the future.
// As another future-proof this also gets used for any compilers other than MSVC. MSVC's preprocessor isn't able to perform
// the delayed expansion without / Zc:preprocessor, fortunately DEATH_ENABLE() isn't needed there at all.
#elif defined(DEATH_TARGET_GCC) || !defined(DEATH_TARGET_MSVC)
// Using __DEATH_PASTE() instead of DEATH_PASTE() here, as that's enough to make that work and it's less
// work for the preprocessor. Concatenating directly doesn't work, unlike in the above case for GCC.
#define _DEATH_ENABLE1(v0)													\
	__DEATH_PASTE(DEATH_ENABLE_, v0)
#define _DEATH_ENABLE2(v0, v1)												\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)
#define _DEATH_ENABLE3(v0, v1, v2)											\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)
#define _DEATH_ENABLE4(v0, v1, v2, v3)										\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)										\
	__DEATH_PASTE(DEATH_ENABLE_, v3)
#define _DEATH_ENABLE5(v0, v1, v2, v3, v4)									\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)										\
	__DEATH_PASTE(DEATH_ENABLE_, v3)										\
	__DEATH_PASTE(DEATH_ENABLE_, v4)
#define _DEATH_ENABLE6(v0, v1, v2, v3, v4, v5)								\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)										\
	__DEATH_PASTE(DEATH_ENABLE_, v3)										\
	__DEATH_PASTE(DEATH_ENABLE_, v4)										\
	__DEATH_PASTE(DEATH_ENABLE_, v5)
#define _DEATH_ENABLE7(v0, v1, v2, v3, v4, v5, v6)							\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)										\
	__DEATH_PASTE(DEATH_ENABLE_, v3)										\
	__DEATH_PASTE(DEATH_ENABLE_, v4)										\
	__DEATH_PASTE(DEATH_ENABLE_, v5)										\
	__DEATH_PASTE(DEATH_ENABLE_, v6)
#define _DEATH_ENABLE8(v0, v1, v2, v3, v4, v5, v6, v7)						\
	__DEATH_PASTE(DEATH_ENABLE_, v0)										\
	__DEATH_PASTE(DEATH_ENABLE_, v1)										\
	__DEATH_PASTE(DEATH_ENABLE_, v2)										\
	__DEATH_PASTE(DEATH_ENABLE_, v3)										\
	__DEATH_PASTE(DEATH_ENABLE_, v4)										\
	__DEATH_PASTE(DEATH_ENABLE_, v5)										\
	__DEATH_PASTE(DEATH_ENABLE_, v6)										\
	__DEATH_PASTE(DEATH_ENABLE_, v7)
#endif

	/**
		@brief Enable multiple targets for given function

		Accepts a comma-separated list of `DEATH_ENABLE_*` macro suffixes,
		effectively enabling given combination. For the macro to work, all
		`DEATH_ENABLE_*` macros corresponding to the arguments have to be defined,
		the common usage pattern is thus in combination with an @cpp #ifdef @ce. See
		@ref Cpu-usage-target-attributes for more information and an example.

		When multiple `DEATH_ENABLE_*` macros are specified one after another, Clang
		8+ would pick only the first specified, and Clang before version 8 and GCC
		before version 12 only the last specified, ignoring the others. There the macro
		expands into a single combined @cpp __attribute__((__target__(...))) @ce
		attribute. For GCC 12+ and other compilers except MSVC it's just a shorthand
		for multiple `DEATH_ENABLE_*` macros one after another. On MSVC expands to
		nothing --- there the functions aren't annotated in anyway and moreover the
		default preprocessor behavior would make this extremely tricky to implement.

		@attention Due to the way the attributes are combined Clang and GCC before
			version 12, in certain cases the macro may silently accept even arguments
			that don't have a corresponding `DEATH_ENABLE_*` macro defined. To
			prevent portability issues, pay extra attention to have a matching
			@cpp #ifdef @ce guard.
	*/
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL)
#	define DEATH_ENABLE(...) _DEATH_HELPER_PICK(__VA_ARGS__, _DEATH_ENABLE8, _DEATH_ENABLE7, _DEATH_ENABLE6, _DEATH_ENABLE5, _DEATH_ENABLE4, _DEATH_ENABLE3, _DEATH_ENABLE2, _DEATH_ENABLE1, )(__VA_ARGS__)
#else
#	define DEATH_ENABLE(...)
#endif

// x86 CPUID implementation on GCC/Clang/MSVC. Has to be inlined in the header because otherwise in the IFUNC scenario it may result
// in a cross-SO call that's unsupported on Clang and older GCC, causing a crash in the dynamic loader during early startup because
// it calls into a place that's not there yet.
//
// Because casually including <immintrin.h> leads to 37+ kLOC (!!!), I go an extra way and use inline assembly instead. Which,
// compared to using the intrinsics --- funnily enough --- reduces the amount of cursing and lengthy comments explaining compiler
// bugs and differences to an absolute minimum. If any of the following misbehaves, please check Git history for the original implementation.
#if defined(DEATH_TARGET_X86) && (defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_GCC))
	namespace Implementation {
		inline void cpuid(int data[4], int leaf, int count) {
			// What's in GCC's / Clang's cpuid.h. Clang-cl as well, as it doesn't seem to know the __cpuidex() intrinsics.
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
#	if defined(DEATH_TARGET_32BIT)
			asm("cpuid":       \
				"=a"(data[0]), "=b"(data[1]), "=c"(data[2]), "=d"(data[3]) : \
				"0"(leaf), "2"(count));
#	else
			// Clang says "x86-64 uses %rbx as the base register", GCC says "%rbx may be the PIC register",
			// so probably important to preserve it or some such?
			asm("xchgq %%rbx,%q1\n" \
				"cpuid\n" \
				"xchgq %%rbx,%q1": \
				"=a"(data[0]), "=b"(data[1]), "=c"(data[2]), "=d"(data[3]) : \
				"0"(leaf), "2"(count));
#	endif
#elif defined(DEATH_TARGET_MSVC)
			// Declared at the top of the file
			__cpuidex(data, leaf, count);
#else
#	error
#endif
		}
	}

	inline Features runtimeFeatures() {
		union {
			struct {
				unsigned int ax, bx, cx, dx;
			} e;
			int data[4];
		} cpuid { };

		Implementation::cpuid(cpuid.data, 1, 0);

		// https://en.wikipedia.org/wiki/CPUID#EAX=1:_Processor_Info_and_Feature_Bits
		unsigned int out = 0;
		if (cpuid.e.dx & (1 << 26)) out |= TypeTraits<Sse2T>::Index;
		if (cpuid.e.cx & (1 << 0)) out |= TypeTraits<Sse3T>::Index;
		if (cpuid.e.cx & (1 << 9)) out |= TypeTraits<Ssse3T>::Index;
		if (cpuid.e.cx & (1 << 19)) out |= TypeTraits<Sse41T>::Index;
		if (cpuid.e.cx & (1 << 20)) out |= TypeTraits<Sse42T>::Index;

		// https://en.wikipedia.org/wiki/CPUID#EAX=80000001h:_Extended_Processor_Info_and_Feature_Bits,
		// bit 5 says "ABM (lzcnt and popcnt)", but
		// https://en.wikipedia.org/wiki/X86_Bit_manipulation_instruction_set#ABM_(Advanced_Bit_Manipulation)
		// says that while LZCNT is advertised in the ABM CPUID bit, POPCNT is a separate CPUID flag. Get POPCNT first, ABM later.
		if (cpuid.e.cx & (1 << 23)) out |= TypeTraits<PopcntT>::Index;

		// AVX needs OS support checked, as the OS needs to be capable of saving and restoring the expanded registers when switching contexts:
		// https://en.wikipedia.org/wiki/Advanced_Vector_Extensions#Operating_system_support
		if ((cpuid.e.cx & (1 << 27)) && /* XSAVE/XRESTORE CPU support */
		   (cpuid.e.cx & (1 << 28)))   /* AVX CPU support */
		{
			// XGETBV indicates that the registers will be properly saved and restored by the OS: https://stackoverflow.com/a/22521619.

			// https://github.com/vectorclass/version2/blob/ff7450acfad9d3a7c6825d92cfb782a42ccfa71f/instrset_detect.cpp#L30-L32
			// Clang-cl as well, as it doesn't seem to know the MSVC intrinsics.
#if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG_CL)
			unsigned int a, d;
			__asm("xgetbv": "=a"(a), "=d"(d) : "c"(0) : );
			const unsigned long long xgetbv = a | (static_cast<unsigned long long>(d) << 32);

			// Declared at the top of the file
#elif defined(DEATH_TARGET_MSVC)
			const unsigned long long xgetbv = _xgetbv(0);
#else
#	error
#endif

			// If AVX is not supported, we don't check any following flags either
			if ((xgetbv & 0x06) == 0x06 /* XSTATE_SSE|XSTATE_YMM */) {
				out |= TypeTraits<AvxT>::Index;

				if (cpuid.e.cx & (1 << 29)) out |= TypeTraits<AvxF16cT>::Index;
				if (cpuid.e.cx & (1 << 12)) out |= TypeTraits<AvxFmaT>::Index;

				// https://en.wikipedia.org/wiki/CPUID#EAX=7,_ECX=0:_Extended_Features
				Implementation::cpuid(cpuid.data, 7, 0);
				if (cpuid.e.bx & (1 << 3)) out |= TypeTraits<Bmi1T>::Index;
				if (cpuid.e.bx & (1 << 8)) out |= TypeTraits<Bmi2T>::Index;
				if (cpuid.e.bx & (1 << 5)) out |= TypeTraits<Avx2T>::Index;
			}

			// AVX-512 needs additional state saving support
			// https://patchwork.ozlabs.org/project/gcc/patch/20180329124309.GA12667@intel.com/#1884915
			// https://github.com/google/highway/blob/master/hwy/targets.cc#L279
			if ((cpuid.e.bx & (1 << 16)) &&
			   // XSTATE_SSE|XSTATE_YMM|XSTATE_OPMASK|XSTATE_ZMM|XSTATE_HI_ZMM
			   (xgetbv & 0xe6) == 0xe6) {
				out |= TypeTraits<Avx512fT>::Index;
			}
		}

		// And now the LZCNT bit, finally
		// https://en.wikipedia.org/wiki/CPUID#EAX=80000001h:_Extended_Processor_Info_and_Feature_Bits
		Implementation::cpuid(cpuid.data, 0x80000001, 0);
		if (cpuid.e.cx & (1 << 5)) out |= TypeTraits<LzcntT>::Index;

		return Features { out };
	}
#endif

	// ARM implementation on Linux and Android, inlined for the same reason as the x86 variant above -- to make IFUNC work.
	// As getauxval() can't be called from within an ifunc resolver because there it's too early for an external call,
	// the value of AT_HWCAP is instead passed to it from outside, on glibc 2.13+ and on Android API 30+:
	// https://github.com/bminor/glibc/commit/7520ff8c744a704ca39741c165a2360d63a4f47a
	// https://android.googlesource.com/platform/bionic/+/e949195f6489653ee3771535951ed06973246c3e/libc/include/sys/ifunc.h
	// Which means we need a variant of runtimeFeatures() that is able to operate with a value fed from outside, which is
	// then used inside such resolvers. A nice consequence of that is that we don't need any other headers.

	// The public Cpu::runtimeFeatures() is deinlined, calls getauxval() and passes it into this function.
#if defined(DEATH_TARGET_ARM) && ((defined(__linux__) && !(defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ < 18)) || defined(__FreeBSD__))
	namespace Implementation {
		inline Features runtimeFeatures(const unsigned long caps) {
			unsigned int out = 0;
#	if defined(DEATH_TARGET_32BIT)
			if (caps & (1 << 12) /*HWCAP_NEON*/) out |= TypeTraits<NeonT>::Index;
			// Since FMA is enabled by passing -mfpu=neon-vfpv4, I assume this is the flag that corresponds to it.
			if (caps & (1 << 16) /*HWCAP_VFPv4*/) out |= TypeTraits<NeonFmaT>::Index;
#	else
			// On ARM64 NEON and NEON FMA is implicit. For extra security make use of the DEATH_TARGET_ defines (which should be always there).
			out |=
#		if defined(DEATH_TARGET_NEON)
				TypeTraits<NeonT>::Index |
#		endif
#		if defined(DEATH_TARGET_NEON_FMA)
				TypeTraits<NeonFmaT>::Index |
#		endif
				0;
			// The HWCAP flags are extremely cryptic. The only vague confirmation is in a *commit message* to the kernel hwcaps file, FFS.
			// The HWCAP_FPHP seems to correspond to scalar FP16, so the other should be the vector one?
			// https://github.com/torvalds/linux/blame/master/arch/arm64/include/uapi/asm/hwcap.h
			// This one also isn't present on 32-bit, so I assume it's ARM64-only?
			if (caps & (1 << 10) /*HWCAP_ASIMDHP*/) out |= TypeTraits<NeonFp16T>::Index;
#	endif
			return Features { out };
		}
	}
#endif
}}