#pragma once

#include "GrowableArray.h"
#include "String.h"

#if defined(DEATH_TARGET_VITA)
#	include <string.h>
#endif

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		template<typename T> struct StringConcatenable;

		template<typename T>
		using StringConcatenableEx = StringConcatenable<std::remove_cv_t<std::remove_reference_t<T>>>;

		template<typename A, typename B> struct ConvertToTypeHelper
		{ typedef A ConvertTo; };
		template<typename T> struct ConvertToTypeHelper<T, String>
		{ typedef String ConvertTo; };
	}

	/** @brief Resulting base class of a deferred string concatenation */
	template<typename Builder, typename T>
	struct StringBuilderBase
	{
	protected:
		/** @brief Returns the resulting string */
		T resolved() const {
			return *static_cast<const Builder*>(this);
		}
	};

	/** @brief Resulting template class of a deferred string concatenation */
	template<typename A, typename B>
	class StringBuilder : public StringBuilderBase<StringBuilder<A, B>,
		typename Implementation::ConvertToTypeHelper<
		typename Implementation::StringConcatenableEx<A>::ConvertTo,
		typename Implementation::StringConcatenableEx<B>::ConvertTo
		>::ConvertTo
	>
	{
	public:
		StringBuilder(A&& a_, B&& b_) : a(Death::forward<A>(a_)), b(Death::forward<B>(b_)) {}

		StringBuilder(StringBuilder&&) = default;
		StringBuilder(const StringBuilder&) = default;
		~StringBuilder() = default;

	private:
		typedef Implementation::StringConcatenable<StringBuilder<A, B>> Concatenable;

		template<typename T>
		T convertTo() const
		{
			const std::size_t length = Concatenable::size(*this);
			T s{NoInit, length};
			auto d = s.data();
			Concatenable::appendTo(*this, d);
			return s;
		}

	public:
		typedef typename Concatenable::ConvertTo ConvertTo;

		operator ConvertTo() const {
			return convertTo<ConvertTo>();
		}

		/** @brief Returns total length in characters */
		std::size_t size() const {
			return Concatenable::size(*this);
		}

		/** @brief First part */
		A a;
		/** @brief Second part */
		B b;

	private:
		StringBuilder& operator=(StringBuilder&&) = delete;
		StringBuilder& operator=(const StringBuilder&) = delete;
	};

	namespace Implementation
	{
		template<>
		struct StringConcatenable<char>
		{
			typedef char type;
			typedef String ConvertTo;

			static std::size_t size(const char) {
				return 1;
			}
			static inline void appendTo(const char c, char*& out) {
				*out++ = c;
			}
		};

		template<>
		struct StringConcatenable<String>
		{
			typedef String type;
			typedef String ConvertTo;

			static std::size_t size(const String& a) {
				return a.size();
			}
			static inline void appendTo(const String& a, char*& out) {
				const std::size_t n = a.size();
				if (n != 0) {
					std::memcpy(out, a.data(), n);
				}
				out += n;
			}
		};

		template<>
		struct StringConcatenable<StringView>
		{
			typedef StringView type;
			typedef String ConvertTo;

			static std::size_t size(StringView a) {
				return a.size();
			}
			static inline void appendTo(StringView a, char*& out) {
				const std::size_t n = a.size();
				if (n != 0) {
					std::memcpy(out, a.data(), n);
				}
				out += n;
			}
		};

		template<>
		struct StringConcatenable<MutableStringView> : StringConcatenable<StringView>
		{
			typedef MutableStringView type;
		};

		template<std::size_t N>
		struct StringConcatenable<const char[N]>
		{
			typedef const char type[N];
			typedef String ConvertTo;

			static std::size_t size(const char a[N]) {
#if defined(DEATH_TARGET_VITA)
				for (std::size_t i = 0; i < N; i++) {
					if (a[i] == '\0') {
						return i;
					}
				}
				return N;
#else
				return strnlen(a, N);
#endif
			}
			static inline void appendTo(const char a[N], char*& out) {
				while (*a != '\0') {
					*out++ = *a++;
				}
			}
		};

		template<std::size_t N>
		struct StringConcatenable<char[N]> : StringConcatenable<const char[N]>
		{
			typedef char type[N];
		};

		template<>
		struct StringConcatenable<const char*>
		{
			typedef const char* type;
			typedef String ConvertTo;

			static std::size_t size(const char* a) {
				return std::strlen(a);
			}
			static inline void appendTo(const char* a, char*& out) {
				while (*a != '\0') {
					*out++ = *a++;
				}
			}
		};

		template<>
		struct StringConcatenable<char*> : StringConcatenable<const char*>
		{
			typedef char* type;
		};

		template<>
		struct StringConcatenable<ArrayView<const char>>
		{
			typedef ArrayView<const char> type;
			typedef String ConvertTo;

			static std::size_t size(const ArrayView<const char>& a) {
				return a.size();
			}
			static inline void appendTo(const ArrayView<const char>& a, char*& out) {
				const char* d = a.data();
				const char* const end = a.end();
				while (d != end) {
					*out++ = *d++;
				}
			}
		};

		template<typename A, typename B>
		struct StringConcatenable<StringBuilder<A, B>>
		{
			typedef StringBuilder<A, B> type;
			using ConvertTo = typename Implementation::ConvertToTypeHelper<
				typename StringConcatenableEx<A>::ConvertTo,
				typename StringConcatenableEx<B>::ConvertTo
			>::ConvertTo;

			static std::size_t size(const type& c) {
				return StringConcatenableEx<A>::size(c.a) + StringConcatenableEx<B>::size(c.b);
			}

			template<typename T>
			static inline void appendTo(const type& c, T*& out) {
				StringConcatenableEx<A>::appendTo(c.a, out);
				StringConcatenableEx<B>::appendTo(c.b, out);
			}
		};
	}

#ifndef DOXYGEN_GENERATING_OUTPUT
	template<typename A, typename B,
		typename = std::void_t<typename Implementation::StringConcatenableEx<A>::type, typename Implementation::StringConcatenableEx<B>::type>>
		auto operator+(A&& a, B&& b)
	{
		return StringBuilder<A, B>(Death::forward<A>(a), Death::forward<B>(b));
	}

	template<typename A, typename B>
	Array<char>& operator+=(Array<char>& a, const StringBuilder<A, B>& b)
	{
		std::size_t prevLength = a.size();
		std::size_t length = prevLength + Implementation::StringConcatenable<StringBuilder<A, B>>::size(b);
		std::size_t doubleCapacity = 2 * arrayCapacity(a);

		arrayReserve(a, length < doubleCapacity ? doubleCapacity : length);

		char* it = a.data() + prevLength;
		Implementation::StringConcatenable<StringBuilder<A, B>>::appendTo(b, it);

		arrayResize(a, NoInit, length);
		return a;
	}

	template<typename A, typename B>
	String& operator+=(String& a, const StringBuilder<A, B>& b)
	{
		std::size_t prevLength = a.size();
		std::size_t length = prevLength + Implementation::StringConcatenable<StringBuilder<A, B>>::size(b);

		String result{NoInit, length};
		char* it = result.data();

		if (prevLength != 0) {
			std::memcpy(it, a.data(), prevLength);
		}
		it += prevLength;
		Implementation::StringConcatenable<StringBuilder<A, B>>::appendTo(b, it);

		a = move(result);
		return a;
	}
#endif

}}