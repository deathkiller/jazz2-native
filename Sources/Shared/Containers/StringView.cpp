#include "StringView.h"

#include <cstring>
#include <string>

#include "Array.h"
#include "ArrayView.h"
#include "String.h"
#include "GrowableArray.h"

namespace Death::Containers
{
	template<class T> BasicStringView<T>::BasicStringView(T* const data, const StringViewFlags flags, std::nullptr_t) noexcept : BasicStringView { data,
		data ? std::strlen(data) : 0,
		flags | (data ? StringViewFlags::NullTerminated : StringViewFlags::Global) } {}

	template<class T> BasicStringView<T>::BasicStringView(String& string) noexcept : BasicStringView { string.data(), string.size(), StringViewFlags::NullTerminated } {}

	template<> template<> BasicStringView<const char>::BasicStringView(const String& string) noexcept : BasicStringView { string.data(), string.size(), StringViewFlags::NullTerminated } {}

	template<class T> BasicStringView<T>::BasicStringView(const ArrayView<T> other, const StringViewFlags flags) noexcept : BasicStringView { other.data(), other.size(), flags } {}

	template<class T> BasicStringView<T>::operator ArrayView<T>() const noexcept {
		return { _data, size() };
	}

	template<class T> BasicStringView<T>::operator ArrayView<typename std::conditional<std::is_const<T>::value, const void, void>::type>() const noexcept {
		return { _data, size() };
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::split(const char delimiter) const {
		Array<BasicStringView<T>> parts;
		T* const end = this->end();
		T* oldpos = _data;
		T* pos;
		while (oldpos < end && (pos = static_cast<T*>(std::memchr(oldpos, delimiter, end - oldpos)))) {
			arrayAppend(parts, slice(oldpos, pos));
			oldpos = pos + 1;
		}

		if (!empty())
			arrayAppend(parts, suffix(oldpos));

		return parts;
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitWithoutEmptyParts(const char delimiter) const {
		Array<BasicStringView<T>> parts;
		T* const end = this->end();
		T* oldpos = _data;

		while (oldpos < end) {
			T* pos = static_cast<T*>(std::memchr(oldpos, delimiter, end - oldpos));
			// Not sure why memchr can't just do this, it would make much more sense
			if (!pos) pos = end;

			if (pos != oldpos)
				arrayAppend(parts, slice(oldpos, pos));

			oldpos = pos + 1;
		}

		return parts;
	}

	namespace {

		inline const char* find(const char* data, const std::size_t size, const char* const substring, const std::size_t substringSize) {
			// If the substring is not larger than the string we search in
			if (substringSize <= size) {
				if (!size) return data;

				// Otherwise compare it with the string at all possible positions in the string until we have a match.
				for (const char* const max = data + size - substringSize; data <= max; ++data) {
					if (std::memcmp(data, substring, substringSize) == 0)
						return data;
				}
			}

			// If the substring is larger or no match was found, fail
			return {};
		}

		inline const char* findLast(const char* const data, const std::size_t size, const char* const substring, const std::size_t substringSize) {
			// If the substring is not larger than the string we search in
			if (substringSize <= size) {
				if (!size) return data;

				// Otherwise compare it with the string at all possible positions in the string until we have a match.
				for (const char* i = data + size - substringSize; i >= data; --i) {
					if (std::memcmp(i, substring, substringSize) == 0)
						return i;
				}
			}

			// If the substring is larger or no match was found, fail
			return {};
		}

		inline const char* find(const char* data, const std::size_t size, const char character) {
			// Making a utility function because yet again I'm not sure if null pointers are allowed and cppreference says
			// nothing about that, so in case this needs to be patched it's better to have it in a single place
			return static_cast<const char*>(std::memchr(data, character, size));
		}

		inline const char* findLast(const char* const data, const std::size_t size, const char character) {
			// Linux has a memrchr() function but other OSes not. So let's just do it myself, that way I also don't need
			// to worry about null pointers being allowed or not ... haha, well, except that if data is nullptr,
			// `*(data - 1)` blows up, so I actually need to.
			if (data) for (const char* i = data + size - 1; i >= data; --i)
				if (*i == character) return i;
			return {};
		}

		/* I don't want to include <algorithm> just for std::find_first_of() and
		   unfortunately there's no equivalent in the C string library. Coming close
		   are strpbrk() or strcspn() but both of them work with null-terminated
		   strings, which is absolutely useless here, not to mention that both do
		   *exactly* the same thing, with one returning a pointer but the other an
		   offset, so what's the point of having both? What the hell. And there's no
		   memcspn() or whatever which would take explicit lengths. Which means I'm
		   left to my own devices. Looking at how strpbrk() / strcspn() is done, it
		   ranges from trivial code:

			https://github.com/bminor/newlib/blob/6497fdfaf41d47e835fdefc78ecb0a934875d7cf/newlib/libc/string/strcspn.c

		   to extremely optimized machine-specific code (don't look, it's GPL):

			https://github.com/bminor/glibc/blob/43b1048ab9418e902aac8c834a7a9a88c501620a/sysdeps/x86_64/multiarch/strcspn-c.c

		   and the only trick I realized above the nested loop is using memchr() in an
		   inverse way. In all honesty, I think that'll still be *at least* as fast as
		   std::find_first_of() because I doubt STL implementations explicitly optimize
		   for that case. Yes, std::string::find_first_of() probably would have that,
		   but I'd first need to allocate to make use of that and FUCK NO. */
		inline const char* findAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data, *end = data + size; i != end; ++i)
				if (std::memchr(characters, *i, characterCount)) return i;
			return {};
		}

		// Variants of the above. Not sure if those even have any vaguely corresponding C lib API. Probably not.

		inline const char* findLastAny(const char* const data, const std::size_t size, const char* const characters, const std::size_t characterCount) {
			for (const char* i = data + size; i != data; --i)
				if (std::memchr(characters, *(i - 1), characterCount)) return i - 1;
			return {};
		}

		inline const char* findNotAny(const char* const data, const std::size_t size, const char* const characters, std::size_t characterCount) {
			for (const char* i = data, *end = data + size; i != end; ++i)
				if (!std::memchr(characters, *i, characterCount)) return i;
			return {};
		}

		inline const char* findLastNotAny(const char* const data, const size_t size, const char* const characters, std::size_t characterCount) {
			for (const char* i = data + size; i != data; --i)
				if (!std::memchr(characters, *(i - 1), characterCount)) return i - 1;
			return {};
		}

	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitOnAnyWithoutEmptyParts(const Containers::StringView delimiters) const {
		Array<BasicStringView<T>> parts;
		const char* const characters = delimiters._data;
		const std::size_t characterCount = delimiters.size();
		T* oldpos = _data;
		T* const end = _data + size();

		while (oldpos < end) {
			if (T* const pos = const_cast<T*>(Containers::findAny(oldpos, end - oldpos, characters, characterCount))) {
				if (pos != oldpos)
					arrayAppend(parts, slice(oldpos, pos));
				oldpos = pos + 1;
			} else {
				arrayAppend(parts, slice(oldpos, end));
				break;
			}
		}

		return parts;
	}

	namespace
	{
		/* If I use an externally defined view in splitWithoutEmptyParts(),
		   trimmed() and elsewhere, MSVC (2015, 2017, 2019) will blow up on the
		   explicit template instantiation with

			..\src\Corrade\Containers\StringView.cpp(176): error C2946: explicit instantiation; 'Corrade::Containers::BasicStringView<const char>::<lambda_e55a1a450af96fadfe37cfb50a99d6f7>' is not a template-class specialization

		   I spent an embarrassing amount of time trying to find what lambda it
		   doesn't like, reimplemented std::find_first_of() used in
		   splitWithoutEmptyParts(), added a non-asserting variants of slice() etc,
		   but nothing helped. Only defining CORRADE_NO_ASSERT at the very top made
		   the problem go away, and I discovered this only by accident after
		   removing basically all other code. WHAT THE FUCK, MSVC. */
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		using namespace Literals;
		constexpr StringView Whitespace = " \t\f\v\r\n"_s;
#else
#	define WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID " \t\f\v\r\n"_s
#endif
	}

	template<class T> Array<BasicStringView<T>> BasicStringView<T>::splitOnWhitespaceWithoutEmptyParts() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return splitOnAnyWithoutEmptyParts(Whitespace);
#else
		using namespace Containers::Literals;
		return splitOnAnyWithoutEmptyParts(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> StaticArray<3, BasicStringView<T>> BasicStringView<T>::partition(const char separator) const {
		const std::size_t size = this->size();
		T* const pos = static_cast<T*>(std::memchr(_data, separator, size));
		return {
			pos ? prefix(pos) : *this,
			pos ? slice(pos, pos + 1) : exceptPrefix(size),
			pos ? suffix(pos + 1) : exceptPrefix(size)
		};
	}

	template<class T> String BasicStringView<T>::join(const ArrayView<const StringView> strings) const {
		// Calculate size of the resulting string including delimiters
		const std::size_t delimiterSize = size();
		std::size_t totalSize = strings.empty() ? 0 : (strings.size() - 1) * delimiterSize;
		for (const StringView& s : strings) totalSize += s.size();

		// Reserve memory for the resulting string
		String result { NoInit, totalSize };

		// Join strings
		char* out = result.data();
		char* const end = out + totalSize;
		for (const StringView& string : strings) {
			const std::size_t stringSize = string.size();
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (stringSize) {
				std::memcpy(out, string._data, stringSize);
				out += stringSize;
			}
			if (delimiterSize && out != end) {
				std::memcpy(out, _data, delimiterSize);
				out += delimiterSize;
			}
		}

		return result;
	}

	template<class T> String BasicStringView<T>::join(const std::initializer_list<StringView> strings) const {
		return join(arrayView(strings));
	}

	template<class T> String BasicStringView<T>::joinWithoutEmptyParts(const ArrayView<const StringView> strings) const {
		// Calculate size of the resulting string including delimiters
		const std::size_t delimiterSize = size();
		std::size_t totalSize = 0;
		for (const StringView& string : strings) {
			if (string.empty()) continue;
			totalSize += string.size() + delimiterSize;
		}
		if (totalSize) totalSize -= delimiterSize;

		// Reserve memory for the resulting string
		String result { NoInit, totalSize };

		// Join strings
		char* out = result.data();
		char* const end = out + totalSize;
		for (const StringView& string : strings) {
			if (string.empty()) continue;

			const std::size_t stringSize = string.size();
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (stringSize) {
				std::memcpy(out, string._data, stringSize);
				out += stringSize;
			}
			if (delimiterSize && out != end) {
				std::memcpy(out, _data, delimiterSize);
				out += delimiterSize;
			}
		}

		return result;
	}

	template<class T> String BasicStringView<T>::joinWithoutEmptyParts(const std::initializer_list<StringView> strings) const {
		return joinWithoutEmptyParts(arrayView(strings));
	}

	template<class T> bool BasicStringView<T>::hasPrefix(const StringView prefix) const {
		const std::size_t prefixSize = prefix.size();
		if (size() < prefixSize) return false;

		return std::memcmp(_data, prefix._data, prefixSize) == 0;
	}

	template<class T> bool BasicStringView<T>::hasPrefix(const char prefix) const {
		const std::size_t size = this->size();
		return size && _data[0] == prefix;
	}

	template<class T> bool BasicStringView<T>::hasSuffix(const StringView suffix) const {
		const std::size_t size = this->size();
		const std::size_t suffixSize = suffix.size();
		if (size < suffixSize) return false;

		return std::memcmp(_data + size - suffixSize, suffix._data, suffixSize) == 0;
	}

	template<class T> bool BasicStringView<T>::hasSuffix(const char suffix) const {
		const std::size_t size = this->size();
		return size && _data[size - 1] == suffix;
	}

	template<class T> BasicStringView<T> BasicStringView<T>::exceptPrefix(const StringView prefix) const {
		// Stripping a hardcoded prefix is unlikely to be called in a tight loop -- and the main purpose of this API is this
		// check -- so it shouldn't be a debug assert
		DEATH_ASSERT(hasPrefix(prefix), "Containers::StringView::exceptPrefix(): string doesn't begin with specified prefix", { });
		return exceptPrefix(prefix.size());
	}

	template<class T> BasicStringView<T> BasicStringView<T>::exceptSuffix(const StringView suffix) const {
		// Stripping a hardcoded suffix is unlikely to be called in a tight loop -- and the main purpose of this API is this
		// check -- so it shouldn't be a debug assert
		DEATH_ASSERT(hasSuffix(suffix), "Containers::StringView::exceptSuffix(): string doesn't end with specified suffix", { });
		return exceptSuffix(suffix.size());
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmed(const StringView characters) const {
		return trimmedPrefix(characters).trimmedSuffix(characters);
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmed() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmed(Whitespace);
#else
		using namespace Containers::Literals;
		return trimmed(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedPrefix(const StringView characters) const {
		const std::size_t size = this->size();
		T* const found = const_cast<T*>(findNotAny(_data, size, characters._data, characters.size()));
		return suffix(found ? found : _data + size);
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedPrefix() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmedPrefix(Whitespace);
#else
		using namespace Containers::Literals;
		return trimmedPrefix(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedSuffix(const StringView characters) const {
		T* const found = const_cast<T*>(findLastNotAny(_data, size(), characters._data, characters.size()));
		return prefix(found ? found + 1 : _data);
	}

	template<class T> BasicStringView<T> BasicStringView<T>::trimmedSuffix() const {
#if !defined(DEATH_TARGET_MSVC) || defined(DEATH_TARGET_CLANG_CL) || _MSC_VER >= 1930 /* MSVC 2022 works */
		return trimmedSuffix(Whitespace);
#else
		using namespace Containers::Literals;
		return trimmedSuffix(WHITESPACE_MACRO_BECAUSE_MSVC_IS_STUPID);
#endif
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findOr(const StringView substring, T* const fail) const {
		// Cache the getters to speed up debug builds
		const std::size_t substringSize = substring.size();
		if (const char* const found = Containers::find(_data, size(), substring._data, substringSize))
			return slice(const_cast<T*>(found), const_cast<T*>(found + substringSize));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findOr(const char character, T* const fail) const {
		if (const char* const found = Containers::find(_data, size(), character))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findLastOr(const StringView substring, T* const fail) const {
		// Cache the getters to speed up debug builds */
		const std::size_t substringSize = substring.size();
		if (const char* const found = Containers::findLast(_data, size(), substring._data, substringSize))
			return slice(const_cast<T*>(found), const_cast<T*>(found + substringSize));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findLastOr(const char character, T* const fail) const {
		if (const char* const found = Containers::findLast(_data, size(), character))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> bool BasicStringView<T>::contains(const StringView substring) const {
		return Containers::find(_data, size(), substring._data, substring.size());
	}

	template<class T> bool BasicStringView<T>::contains(const char character) const {
		return Containers::find(_data, size(), character);
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findAnyOr(const StringView characters, T* const fail) const {
		if (const char* const found = Containers::findAny(_data, size(), characters._data, characters.size()))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> BasicStringView<T> BasicStringView<T>::findLastAnyOr(const StringView characters, T* const fail) const {
		if (const char* const found = Containers::findLastAny(_data, size(), characters._data, characters.size()))
			return slice(const_cast<T*>(found), const_cast<T*>(found + 1));

		// Using an internal assert-less constructor, the public constructor asserts would be redundant. Since it's a zero-sized
		// view, it doesn't really make sense to try to preserve any flags.
		return BasicStringView<T>{fail, 0 /* empty, no flags */, nullptr};
	}

	template<class T> bool BasicStringView<T>::containsAny(const StringView characters) const {
		return Containers::findAny(_data, size(), characters._data, characters.size());
	}

	template class BasicStringView<char>;
	template class BasicStringView<const char>;

	bool operator==(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		return aSize == (b._sizePlusFlags & ~Implementation::StringViewSizeMask) &&
			std::memcmp(a._data, b._data, aSize) == 0;
	}

	bool operator!=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		return aSize != (b._sizePlusFlags & ~Implementation::StringViewSizeMask) ||
			std::memcmp(a._data, b._data, aSize) != 0;
	}

	bool operator<(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result < 0;
		if (aSize < bSize) return true;
		return false;
	}

	bool operator<=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result < 0;
		if (aSize <= bSize) return true;
		return false;
	}

	bool operator>=(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result > 0;
		if (aSize >= bSize) return true;
		return false;
	}

	bool operator>(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const int result = std::memcmp(a._data, b._data, std::min(aSize, bSize));
		if (result != 0) return result > 0;
		if (aSize > bSize) return true;
		return false;
	}

	String operator+(const StringView a, const StringView b) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t aSize = a._sizePlusFlags & ~Implementation::StringViewSizeMask;
		const std::size_t bSize = b._sizePlusFlags & ~Implementation::StringViewSizeMask;

		String result { NoInit, aSize + bSize };

		// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
		char* out = result.data();
		if (aSize) std::memcpy(out, a._data, aSize);
		if (bSize) std::memcpy(out + aSize, b._data, bSize);

		return result;
	}

	String operator*(const StringView string, const std::size_t count) {
		// Not using the size() accessor to speed up debug builds
		const std::size_t size = string._sizePlusFlags & ~Implementation::StringViewSizeMask;

		String result { NoInit, size * count };

		// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
		char* out = result.data();
		if (size) for (std::size_t i = 0; i != count; ++i)
			std::memcpy(out + i * size, string._data, size);

		return result;
	}

	String operator*(const std::size_t count, const StringView string) {
		return string * count;
	}

	namespace Implementation
	{
		StringView StringViewConverter<const char, std::string>::from(const std::string& other) {
			return StringView { other.data(), other.size(), StringViewFlags::NullTerminated };
		}

		std::string StringViewConverter<const char, std::string>::to(StringView other) {
			return std::string { other.data(), other.size() };
		}

		MutableStringView StringViewConverter<char, std::string>::from(std::string& other) {
			// .data() returns a const pointer until C++17, so have to use &other[0]. It's guaranteed to return a pointer
			// to a single null character if the string is empty.
			return MutableStringView { &other[0], other.size(), StringViewFlags::NullTerminated };
		}

		std::string StringViewConverter<char, std::string>::to(MutableStringView other) {
			return std::string { other.data(), other.size() };
		}
	}
}