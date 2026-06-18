#pragma once

#include "../Containers/StringView.h"

#include <memory>

// If `DEATH_SUPPRESS_RUNTIME_CAST` is defined, standard dynamic_cast<T>() is used instead of optimized runtime_cast<T>()
#if !defined(DEATH_SUPPRESS_RUNTIME_CAST) && !defined(DOXYGEN_GENERATING_OUTPUT)

#include <type_traits>
#if DEATH_CXX_STANDARD < 201402
#	include <cstring>	// for std::strcmp()
#endif

#if defined(__has_builtin)
#	if __has_builtin(__builtin_constant_p)
#		define __DEATH_HAS_BUILTIN_CONSTANT(x) __builtin_constant_p(x)
#	endif
#	if __has_builtin(__builtin_strcmp)
#		define __DEATH_HAS_BUILTIN_STRCMP(x, y) __builtin_strcmp(x, y)
#	endif
#elif defined(DEATH_TARGET_GCC)
#	define __DEATH_HAS_BUILTIN_CONSTANT(x) __builtin_constant_p(x)
#	define __DEATH_HAS_BUILTIN_STRCMP(x, y) __builtin_strcmp(x, y)
#endif

#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
#	define __DEATH_WARNING_PUSH __pragma(warning(push))
#	define __DEATH_WARNING_POP __pragma(warning(pop))
#	define __DEATH_NO_OVERRIDE_WARNING __pragma(warning(disable: 26433))
#elif defined(DEATH_TARGET_CLANG)
#	define __DEATH_WARNING_PUSH _Pragma("clang diagnostic push")
#	define __DEATH_WARNING_POP _Pragma("clang diagnostic pop")
#	if (__clang_major__*100 + __clang_minor__ >= 1100)
#		define __DEATH_NO_OVERRIDE_WARNING _Pragma("clang diagnostic ignored \"-Winconsistent-missing-override\"") _Pragma("clang diagnostic ignored \"-Wsuggest-override\"")
#	elif (__clang_major__*100 + __clang_minor__ >= 306)
#		define __DEATH_NO_OVERRIDE_WARNING _Pragma("clang diagnostic ignored \"-Winconsistent-missing-override\"")
#	endif
#elif defined(DEATH_TARGET_GCC) && (__GNUC__*100 + __GNUC_MINOR__ >= 501)
#	define __DEATH_WARNING_PUSH _Pragma("GCC diagnostic push")
#	define __DEATH_WARNING_POP _Pragma("GCC diagnostic pop")
#	define __DEATH_NO_OVERRIDE_WARNING _Pragma("GCC diagnostic ignored \"-Wsuggest-override\"")
#else
#	define __DEATH_WARNING_PUSH
#	define __DEATH_WARNING_POP
#	define __DEATH_NO_OVERRIDE_WARNING
#endif

namespace Death { namespace TypeInfo { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	template<std::uint16_t N>
	class StaticStringBuffer {
	public:
		constexpr explicit StaticStringBuffer(const char* str) noexcept
			: StaticStringBuffer{str, std::make_integer_sequence<std::uint16_t, N>{}} {}

		constexpr explicit StaticStringBuffer(Containers::StringView str) noexcept
			: StaticStringBuffer{str.data(), std::make_integer_sequence<std::uint16_t, N>{}} {
			DEATH_DEBUG_ASSERT(str.size() == N);
		}

		constexpr const char* data() const noexcept {
			return _chars;
		}

		constexpr std::uint16_t size() const noexcept {
			return N;
		}

		constexpr operator Containers::StringView() const noexcept {
			// It's used in constexpr context, so it should be always global
			return { data(), size(), Containers::StringViewFlags::Global | Containers::StringViewFlags::NullTerminated };
		}

	private:
		template<std::uint16_t... I>
		constexpr StaticStringBuffer(const char* str, std::integer_sequence<std::uint16_t, I...>) noexcept : _chars{static_cast<char>(str[I])..., static_cast<char>('\0')} {}

		template<std::uint16_t... I>
		constexpr StaticStringBuffer(Containers::StringView str, std::integer_sequence<std::uint16_t, I...>) noexcept : _chars{str[I]..., static_cast<char>('\0')} {}

		char _chars[static_cast<std::size_t>(N) + 1];
	};

	template<>
	class StaticStringBuffer<0> {
	public:
		constexpr explicit StaticStringBuffer() = default;
		constexpr explicit StaticStringBuffer(const char* str) noexcept {}
		constexpr explicit StaticStringBuffer(Containers::StringView) noexcept {}

		constexpr const char* data() const noexcept {
			return nullptr;
		}

		constexpr std::uint16_t size() const noexcept {
			return 0;
		}

		constexpr operator Containers::StringView() const noexcept {
			return {};
		}
	};

	struct TypeInfoSkip {
		std::size_t SizeAtBegin;
		std::size_t SizeAtEnd;
		const char* UntilRuntime;
		std::size_t UntilRuntimeLength;
	};

	template<std::size_t N>
	static constexpr TypeInfoSkip CreateTypeInfoSkip(std::size_t sizeAtBegin, std::size_t sizeAtEnd, bool moreAtRuntime, const char(&untilRuntime)[N]) {
		return TypeInfoSkip{sizeAtBegin, sizeAtEnd, untilRuntime, moreAtRuntime ? N - 1 : 0};
	}

	template<std::size_t N>
	static constexpr TypeInfoSkip CreateTypeInfoSkip(std::size_t sizeAtBegin, std::size_t sizeAtEnd, const char(&untilRuntime)[N]) {
		return TypeInfoSkip{sizeAtBegin, sizeAtEnd, untilRuntime, N - 1};
	}

#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
	// auto __cdecl __ti<class A>::n(void) noexcept
	static constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(24, 19, "");
	}
#elif defined(DEATH_TARGET_CLANG)
	// static auto __ti<A>::n() [T = A]
	static constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(17, 1, "T = ");
	}
#elif defined(DEATH_TARGET_GCC)
	// static constexpr auto __ti<T>::n() [with T = A]
	static constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(45, 1, "");
	}
#else
	static constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(0, 0, "");
	}
#endif

#if defined(__DEATH_HAS_BUILTIN_CONSTANT)
	static constexpr DEATH_ALWAYS_INLINE bool IsConstantString(const char* str) noexcept {
		while (__DEATH_HAS_BUILTIN_CONSTANT(*str)) {
			if (*str == '\0') {
				return true;
			}
			str++;
		}
		return false;
	}
#endif
	
	template<class ForwardIterator1, class ForwardIterator2>
	static constexpr ForwardIterator1 constexpr_search(ForwardIterator1 begin1, std::size_t length1, ForwardIterator2 begin2, std::size_t length2) noexcept {
		if (length2 == 0) {
			return begin1;  // Specified in C++11
		}

		for (std::size_t i = 0; i <= length1 - length2; i++) {
			ForwardIterator1 it1 = begin1;
			ForwardIterator2 it2 = begin2;
			std::size_t matched = 0;

			while (matched < length2 && *it1 == *it2) {
				it1++;
				it2++;
				matched++;
			}

			if (matched == length2) {
				return begin1;
			}

			begin1++;
		}

		return begin1 + length2;
	}

	static constexpr std::int32_t constexpr_strcmp_loop(const char* v1, const char* v2) noexcept {
		while (*v1 != '\0' && *v1 == *v2) {
			++v1; ++v2;
		}
		return static_cast<std::int32_t>(*v1) - *v2;
	}

	static constexpr std::int32_t constexpr_strcmp(const char* v1, const char* v2) noexcept {
#if DEATH_CXX_STANDARD >= 201402 && defined(__DEATH_HAS_BUILTIN_CONSTANT) && defined(__DEATH_HAS_BUILTIN_STRCMP)
		if (IsConstantString(v1) && IsConstantString(v2)) {
			return constexpr_strcmp_loop(v1, v2);
		}
		return __DEATH_HAS_BUILTIN_STRCMP(v1, v2);
#elif DEATH_CXX_STANDARD >= 201402
		return constexpr_strcmp_loop(v1, v2);
#else
		return std::strcmp(v1, v2);
#endif
	}

	template<std::size_t N>
	static constexpr const char* ExtractTypeNameInRuntime(const char* begin) noexcept {
		const char* const it = constexpr_search(
			begin, N,
			skip().UntilRuntime, skip().UntilRuntimeLength);
		return (it == begin + N ? begin : it + skip().UntilRuntimeLength);
	}

	template<std::size_t N>
	static constexpr Containers::StringView ExtractTypeName(const char* begin) noexcept {
		static_assert(N > skip().SizeAtBegin + skip().SizeAtEnd, "runtime_cast<T>() is misconfigured for your compiler");

		const char* const offset = (skip().UntilRuntimeLength
			? ExtractTypeNameInRuntime<N - skip().SizeAtBegin>(begin + skip().SizeAtBegin)
			: begin + skip().SizeAtBegin);
		// It's used in constexpr context, so it should be always global
		return { offset, N - 1 - (offset - begin) - skip().SizeAtEnd, Containers::StringViewFlags::Global };
	}

}}}

// It is located in the root namespace to keep the resulting string as short as possible
template<class T>
struct __ti
{
	static constexpr auto n() noexcept {
		using namespace Death::TypeInfo::Implementation;
#if defined(__FUNCSIG__)
		constexpr auto name = ExtractTypeName<sizeof(__FUNCSIG__)>(__FUNCSIG__);
		return StaticStringBuffer<name.size()>{name.data()};
#elif defined(__PRETTY_FUNCTION__) \
		|| defined(DEATH_TARGET_GCC) \
		|| defined(DEATH_TARGET_CLANG) \
		|| (defined(__ICC) && (__ICC >= 600)) \
		|| (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) \
		|| (defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5130))
		constexpr auto name = ExtractTypeName<sizeof(__PRETTY_FUNCTION__)>(__PRETTY_FUNCTION__);
		return StaticStringBuffer<name.size()>{name.data()};
#else
		static_assert(sizeof(T) && false, "runtime_cast<T>() could not detect your compiler");
		return StaticStringBuffer<0>{};
#endif
	}
};

namespace Death { namespace TypeInfo { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	// 64-bit FNV-1a of the extracted type name, evaluated at compile-time and used as the type identity.
	// It's stable across translation units and shared-library (DLL/.so) boundaries (unlike a raw pointer
	// to the name), so it's emitted as an immediate and the name string itself never reaches the binary.
	static constexpr std::uint64_t HashTypeName(const char* data, std::size_t size) noexcept {
		std::uint64_t hash = 0xcbf29ce484222325ull;
		for (std::size_t i = 0; i < size; i++) {
			hash = (hash ^ static_cast<std::uint8_t>(data[i])) * 0x00000100000001b3ull;
		}
		return hash;
	}

	// The name is built into a local (never a static member), so it stays a compile-time-only value
	template<class T>
	static constexpr std::uint64_t ComputeNameHash() noexcept {
		constexpr auto name = __ti<T>::n();
		return HashTypeName(name.data(), name.size());
	}

	template<class T>
	struct TypeName {
		static constexpr std::uint64_t hash = ComputeNameHash<T>();
	};

	// Debug-only registry that verifies no two distinct types share a 64-bit hash (which would make
	// runtime_cast<T>() occasionally return a wrong, non-null pointer). Each type registers (hash + name)
	// through ForceTypeRegistration(), which its __FindInstance() ODR-uses, so the registrar's static
	// initializer runs at startup and checks the new type against all already-registered ones. None of
	// this - including the type-name strings - is compiled into release builds.
#if defined(DEATH_DEBUG)
	template<class T>
	struct TypeNameString {
		static constexpr auto value = __ti<T>::n();
	};

	struct TypeRegistryNode {
		std::uint64_t Hash;
		const char* Name;
		TypeRegistryNode* Next;
	};

	// Meyers singleton, so the list head is alive before any registrar runs, regardless of init order
	inline TypeRegistryNode*& GetTypeRegistryHead() noexcept {
		static TypeRegistryNode* head = nullptr;
		return head;
	}

	struct TypeRegistrar {
		TypeRegistryNode Node;
		TypeRegistrar(std::uint64_t hash, const char* name) noexcept {
			Node.Hash = hash;
			Node.Name = name;
			// A shared hash with a different name is a real collision; the same name reached from two
			// translation units is the same type and benign. The strcmp() runs only on a hash match.
			for (const TypeRegistryNode* n = GetTypeRegistryHead(); n != nullptr; n = n->Next) {
				DEATH_DEBUG_ASSERT(n->Hash != hash || constexpr_strcmp(n->Name, name) == 0,
					("runtime_cast<T>() hash collision: \"{}\" and \"{}\" both hash to {}", n->Name, name, hash));
			}
			Node.Next = GetTypeRegistryHead();
			GetTypeRegistryHead() = &Node;
		}
	};

	template<class T>
	struct TypeRegistryEntry {
		static TypeRegistrar Instance;
	};

	template<class T>
	TypeRegistrar TypeRegistryEntry<T>::Instance{TypeName<T>::hash, TypeNameString<T>::value.data()};

	template<class T>
	DEATH_ALWAYS_INLINE void ForceTypeRegistration() noexcept {
		// Just ODR-uses the per-type registrar so its static initializer registers the type at startup
		(void)&TypeRegistryEntry<T>::Instance;
	}
#else
	template<class T>
	DEATH_ALWAYS_INLINE void ForceTypeRegistration() noexcept {}
#endif

	// A single 64-bit value, so it's passed in a register and compared in one instruction. A wider
	// (16-byte) handle would be passed via memory on the Windows x64 ABI, which slows the cast walk
	// considerably. A false match needs two distinct type names to share a 64-bit hash (~2^-64).
	using TypeHandle = std::uint64_t;

	struct Helpers
	{
		template<class T>
		static constexpr TypeHandle GetTypeHandle() noexcept {
			return TypeName<T>::hash;
		}

		template<class T>
		static constexpr TypeHandle GetTypeHandle(const T*) noexcept {
			return GetTypeHandle<T>();
		}

		template<class T, class U>
		static T* RuntimeCast(U* u, std::integral_constant<bool, true>) noexcept {
			return u;
		}

		template<class T, class U>
		static const T* RuntimeCast(const U* u, std::integral_constant<bool, true>) noexcept {
			return u;
		}

		template<class T, class U>
		static T* RuntimeCast(U* u, std::integral_constant<bool, false>) noexcept {
			if (u == nullptr)
				return nullptr;
			return const_cast<T*>(static_cast<const T*>(
				u->__FindInstance(GetTypeHandle<T>())
			));
		}

		template<class T, class U>
		static const T* RuntimeCast(const U* u, std::integral_constant<bool, false>) noexcept {
			if (u == nullptr)
				return nullptr;
			return static_cast<const T*>(u->__FindInstance(GetTypeHandle<T>()));
		}

		template<class Self>
		static constexpr const void* FindInstance(TypeHandle, const Self*) noexcept {
			return nullptr;
		}

		template<class Base, class ...OtherBases, class Self>
		static const void* FindInstance(TypeHandle t, const Self* self) noexcept {
			if (const void* ptr = self->Base::__FindInstance(t)) {
				return ptr;
			}
			return FindInstance<OtherBases...>(t, self);
		}
	};

}}}

/** @brief Class annotation to enable optimized `runtime_cast<T>()` functionality */
#define DEATH_RUNTIME_OBJECT(...)																		\
	__DEATH_WARNING_PUSH																				\
	__DEATH_NO_OVERRIDE_WARNING																			\
	friend struct Death::TypeInfo::Implementation::Helpers;												\
	virtual const void* __FindInstance(Death::TypeInfo::Implementation::TypeHandle t) const noexcept {	\
		Death::TypeInfo::Implementation::ForceTypeRegistration<											\
			typename std::remove_cv<typename std::remove_pointer<decltype(this)>::type>::type>();		\
		if (t == Death::TypeInfo::Implementation::Helpers::GetTypeHandle(this))							\
			return this;																				\
		return Death::TypeInfo::Implementation::Helpers::FindInstance<__VA_ARGS__>(t, this);			\
	}																									\
	__DEATH_WARNING_POP

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Casts a pointer to another type
		
		Safely converts pointers to classes up, down, and sideways along the inheritance
		hierarchy of classes annotated by `DEATH_RUNTIME_OBJECT()` in an optimized way.
		Additionally, it can perform downcast at no performance cost.
		
		If @cpp DEATH_SUPPRESS_RUNTIME_CAST @ce is defined, the optimized implementation is suppressed
		and the standard @cpp dynamic_cast<T>() @ce is used to cast the pointer instead.
	*/
	template<typename T, typename U>
	DEATH_ALWAYS_INLINE T* runtime_cast(U* u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		return Death::TypeInfo::Implementation::Helpers::RuntimeCast<T>(u, std::is_base_of<T, U>());
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/** @overload */
	template<typename T, typename U>
	DEATH_ALWAYS_INLINE const T* runtime_cast(const U* u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		return Death::TypeInfo::Implementation::Helpers::RuntimeCast<T>(u, std::is_base_of<T, U>());
	}
#endif

	/** @overload */
	template<class T, class U>
	DEATH_ALWAYS_INLINE std::shared_ptr<T> runtime_cast(const std::shared_ptr<U>& u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		auto ptr = Death::TypeInfo::Implementation::Helpers::RuntimeCast<T>(u.get(), std::is_base_of<T, U>());
		if (ptr) {
			return std::shared_ptr<T>(u, ptr);
		}
		return {};
	}

	/** @overload */
	template<class T, class U>
	DEATH_ALWAYS_INLINE std::shared_ptr<T> runtime_cast(std::shared_ptr<U>&& u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		auto ptr = Death::TypeInfo::Implementation::Helpers::RuntimeCast<T>(u.get(), std::is_base_of<T, U>());
		if (ptr) {
			return std::shared_ptr<T>(Death::move(u), ptr);
		}
		return {};
	}

}

#else

/** @brief Class annotation to enable optimized `runtime_cast<T>()` functionality */
#define DEATH_RUNTIME_OBJECT(...)

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Casts a pointer to another type
		
		Safely converts pointers to classes up, down, and sideways along the inheritance
		hierarchy of classes annotated by `DEATH_RUNTIME_OBJECT()` in an optimized way.
		Additionally, it can perform downcast at no performance cost.
		
		If @cpp DEATH_SUPPRESS_RUNTIME_CAST @ce is defined, the optimized implementation is suppressed
		and the standard @cpp dynamic_cast<T>() @ce is used to cast the pointer instead.
	*/
	template<typename T, typename U>
	DEATH_ALWAYS_INLINE T* runtime_cast(U* u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		return dynamic_cast<T*>(u);
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/** @overload */
	template<typename T, typename U>
	DEATH_ALWAYS_INLINE const T* runtime_cast(const U* u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		return dynamic_cast<const T*>(u);
	}
#endif

	/** @overload */
	template<class T, class U>
	DEATH_ALWAYS_INLINE std::shared_ptr<T> runtime_cast(const std::shared_ptr<U>& u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		auto ptr = dynamic_cast<T*>(u.get());
		if (ptr) {
			return std::shared_ptr<T>(u, ptr);
		}
		return {};
	}

	/** @overload */
	template<class T, class U>
	DEATH_ALWAYS_INLINE std::shared_ptr<T> runtime_cast(std::shared_ptr<U>&& u) noexcept {
		static_assert(!std::is_pointer<T>::value && !std::is_reference<T>::value,
			"T must be a plain value type - pointers (T*) and references (T&, T&&) are not allowed");

		auto ptr = dynamic_cast<T*>(u.get());
		if (ptr) {
			return std::shared_ptr<T>(Death::move(u), ptr);
		}
		return {};
	}

}

#endif

// Allow to use runtime_cast without namespace
using Death::runtime_cast;