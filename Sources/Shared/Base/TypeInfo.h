#pragma once

#include "../Common.h"

// If `DEATH_NO_RUNTIME_CAST` is defined, standard dynamic_cast<T>() is used instead of optimized runtime_cast<T>()
#if !defined(DEATH_NO_RUNTIME_CAST)

#include <memory>
#include <type_traits>
#if DEATH_CXX_STANDARD < 201402
#	include <cstring>	// for std::strcmp()
#endif

#if defined(__has_builtin)
#	if __has_builtin(__builtin_constant_p)
#		define DEATH_HAS_BUILTIN_CONSTANT(x) __builtin_constant_p(x)
#	endif
#	if __has_builtin(__builtin_strcmp)
#		define DEATH_HAS_BUILTIN_STRCMP(x, y) __builtin_strcmp(x, y)
#	endif
#elif defined(DEATH_TARGET_GCC)
#	define DEATH_HAS_BUILTIN_CONSTANT(x) __builtin_constant_p(x)
#	define DEATH_HAS_BUILTIN_STRCMP(x, y) __builtin_strcmp(x, y)
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

	struct TypeInfoSkip {
		std::size_t SizeAtBegin;
		std::size_t SizeAtEnd;
		const char* UntilRuntime;
		std::size_t UntilRuntimeLength;
	};

	template<std::size_t N>
	constexpr TypeInfoSkip CreateTypeInfoSkip(std::size_t sizeAtBegin, std::size_t sizeAtEnd, bool moreAtRuntime, const char(&untilRuntime)[N]) {
		return TypeInfoSkip{sizeAtBegin, sizeAtEnd, untilRuntime, moreAtRuntime ? N - 1 : 0};
	}

	template<std::size_t N>
	constexpr TypeInfoSkip CreateTypeInfoSkip(std::size_t sizeAtBegin, std::size_t sizeAtEnd, const char(&untilRuntime)[N]) {
		return TypeInfoSkip{sizeAtBegin, sizeAtEnd, untilRuntime, N - 1};
	}

#if defined(DEATH_TARGET_MSVC) && !defined(DEATH_TARGET_CLANG_CL)
	// const char *__cdecl __ti<A>::n(void) noexcept
	constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(25, 19, "");
	}
#elif defined(DEATH_TARGET_CLANG)
	// static const char *__ti<A>::n() [T = A]
	constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(24, 1, "T = ");
	}
#elif defined(DEATH_TARGET_GCC) && DEATH_CXX_STANDARD >= 201402
	// static constexpr const char* __ti<T>::n() [with T = A]
	constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(52, 1, "");
	}
#elif defined(DEATH_TARGET_GCC)
	// static const char* __ti<T>::n() [with T = A]
	constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(42, 1, "");
	}
#else
	constexpr TypeInfoSkip skip() noexcept {
		return CreateTypeInfoSkip(0, 0, "");
	}
#endif

#if defined(DEATH_HAS_BUILTIN_CONSTANT)
	DEATH_CONSTEXPR14 DEATH_ALWAYS_INLINE bool IsConstantString(const char* str) noexcept {
		while (DEATH_HAS_BUILTIN_CONSTANT(*str)) {
			if (*str == '\0') {
				return true;
			}
			str++;
		}
		return false;
	}
#endif

	template<class ForwardIterator1, class ForwardIterator2>
	DEATH_CONSTEXPR14 inline ForwardIterator1 constexpr_search(ForwardIterator1 first1, ForwardIterator1 last1, ForwardIterator2 first2, ForwardIterator2 last2) noexcept {
		if (first2 == last2) {
			return first1;  // Specified in C++11
		}

		while (first1 != last1) {
			ForwardIterator1 it1 = first1;
			ForwardIterator2 it2 = first2;
			while (*it1 == *it2) {
				++it1; ++it2;
				if (it2 == last2) return first1;
				if (it1 == last1) return last1;
			}
			++first1;
		}
		return last1;
	}

	DEATH_CONSTEXPR14 inline std::int32_t constexpr_strcmp_loop(const char* v1, const char* v2) noexcept {
		while (*v1 != '\0' && *v1 == *v2) {
			++v1; ++v2;
		}
		return static_cast<std::int32_t>(*v1) - *v2;
	}

	DEATH_CONSTEXPR14 inline std::int32_t constexpr_strcmp(const char* v1, const char* v2) noexcept {
#if DEATH_CXX_STANDARD >= 201402 && defined(DEATH_HAS_BUILTIN_CONSTANT) && defined(DEATH_HAS_BUILTIN_STRCMP)
		if (IsConstantString(v1) && IsConstantString(v2)) {
			return constexpr_strcmp_loop(v1, v2);
		}
		return DEATH_HAS_BUILTIN_STRCMP(v1, v2);
#elif DEATH_CXX_STANDARD >= 201402
		return constexpr_strcmp_loop(v1, v2);
#else
		return std::strcmp(v1, v2);
#endif
	}

	template<std::size_t ArrayLength>
	DEATH_CONSTEXPR14 inline const char* SkipBeginningRuntime(const char* begin) noexcept {
		const char* const it = constexpr_search(
			begin, begin + ArrayLength,
			skip().UntilRuntime, skip().UntilRuntime + skip().UntilRuntimeLength);
		return (it == begin + ArrayLength ? begin : it + skip().UntilRuntimeLength);
	}

	template<std::size_t ArrayLength>
	DEATH_CONSTEXPR14 inline const char* SkipBeginning(const char* begin) noexcept {
		static_assert(ArrayLength > skip().SizeAtBegin + skip().SizeAtEnd, "runtime_cast<T>() is misconfigured for your compiler");

		return (skip().UntilRuntimeLength
			? SkipBeginningRuntime<ArrayLength - skip().SizeAtBegin>(begin + skip().SizeAtBegin)
			: begin + skip().SizeAtBegin);
	}
}}}

// It is located in the root namespace to keep the resulting string as short as possible
template<class T>
struct __ti
{
	static DEATH_CONSTEXPR14 const char* n() noexcept {
#if defined(__FUNCSIG__)
		return Death::TypeInfo::Implementation::SkipBeginning<sizeof(__FUNCSIG__)>(__FUNCSIG__);
#elif defined(__PRETTY_FUNCTION__) \
		|| defined(DEATH_TARGET_GCC) \
		|| defined(DEATH_TARGET_CLANG) \
		|| (defined(__ICC) && (__ICC >= 600)) \
		|| (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) \
		|| (defined(__SUNPRO_CC) && (__SUNPRO_CC >= 0x5130))
		return Death::TypeInfo::Implementation::SkipBeginning<sizeof(__PRETTY_FUNCTION__)>(__PRETTY_FUNCTION__);
#else
		static_assert(sizeof(T) && false, "runtime_cast<T>() could not detect your compiler");
		return "";
#endif
	}
};

namespace Death { namespace TypeInfo { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	using TypeId = const char*;

	struct Helpers
	{
		template<class T>
		static DEATH_CONSTEXPR14 inline TypeId ConstructTypeId() noexcept {
			return __ti<T>::n();
		}

		template<class T>
		static constexpr TypeId GetTypeId(const T*) noexcept {
			return ConstructTypeId<T>();
		}

		/*template<class T>
		Containers::Pair<const char*, std::size_t> GetTypeName() noexcept {
			constexpr const char* name = __ti<T>::n();
			std::size_t length = std::strlen(name + skip().SizeAtEnd);
			while (name[length - 1] == ' ') length--; // MSVC sometimes adds trailing whitespaces
			return { name, length };
		}*/

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
				u->__FindInstance(ConstructTypeId<T>())
			));
		}

		template<class T, class U>
		static const T* RuntimeCast(const U* u, std::integral_constant<bool, false>) noexcept {
			if (u == nullptr)
				return nullptr;
			return static_cast<const T*>(u->__FindInstance(ConstructTypeId<T>()));
		}

		template<class Self>
		static constexpr const void* FindInstance(TypeId, const Self*) noexcept {
			return nullptr;
		}

		template<class Base, class ...OtherBases, class Self>
		static const void* FindInstance(TypeId t, const Self* self) noexcept {
			if (const void* ptr = self->Base::__FindInstance(t)) {
				return ptr;
			}
			return FindInstance<OtherBases...>(t, self);
		}
	};
}}}

/** @brief Class annotation to enable optimized @ref runtime_cast() functionality */
#define DEATH_RUNTIME_OBJECT(...)																		\
	__DEATH_WARNING_PUSH																				\
	__DEATH_NO_OVERRIDE_WARNING																			\
	friend struct Death::TypeInfo::Implementation::Helpers;												\
	virtual const void* __FindInstance(Death::TypeInfo::Implementation::TypeId t) const noexcept {		\
		if (t == Death::TypeInfo::Implementation::Helpers::GetTypeId(this))								\
			return this;																				\
		return Death::TypeInfo::Implementation::Helpers::FindInstance<__VA_ARGS__>(t, this);			\
	}																									\
	__DEATH_WARNING_POP

/** @brief Safely converts pointers to classes up, down, and sideways along the inheritance hierarchy of classes annotated by @ref DEATH_RUNTIME_OBJECT() */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(U* u) noexcept {
	typedef typename std::remove_pointer<T>::type Derived;
	return Death::TypeInfo::Implementation::Helpers::RuntimeCast<Derived>(u, std::is_base_of<T, U>());
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const U* u) noexcept {
	typedef typename std::remove_pointer<T>::type Derived;
	return Death::TypeInfo::Implementation::Helpers::RuntimeCast<Derived>(u, std::is_base_of<T, U>());
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const std::shared_ptr<U>& u) noexcept {
	typedef typename std::remove_pointer<T>::type Derived;
	return Death::TypeInfo::Implementation::Helpers::RuntimeCast<Derived>(u.get(), std::is_base_of<T, U>());
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const std::unique_ptr<U>& u) noexcept {
	typedef typename std::remove_pointer<T>::type Derived;
	return Death::TypeInfo::Implementation::Helpers::RuntimeCast<Derived>(u.get(), std::is_base_of<T, U>());
}

#else

/** @brief Class annotation to enable optimized @ref runtime_cast() functionality */
#define DEATH_RUNTIME_OBJECT(...)

/** @brief Safely converts pointers to classes up, down, and sideways along the inheritance hierarchy of classes annotated by @ref DEATH_RUNTIME_OBJECT() */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(U* u) noexcept {
	return dynamic_cast<T>(u);
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const U* u) noexcept {
	return dynamic_cast<T>(u);
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const std::shared_ptr<U>& u) noexcept {
	return dynamic_cast<T>(u.get());
}

/** @overload */
template<typename T, typename U>
DEATH_ALWAYS_INLINE T runtime_cast(const std::unique_ptr<U>& u) noexcept {
	return dynamic_cast<T>(u.get());
}

#endif
