// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022
//           Vladimír Vondruš <mosra@centrum.cz> and contributors
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

#include <cstddef>
#include <initializer_list>
#include <type_traits>

#include "../Common.h"
#include "Array.h"

namespace Death::Containers
{
	class String;
	template<class> class BasicStringView;
	typedef BasicStringView<const char> StringView;

	namespace Implementation
	{
		template<class, class> struct StringViewConverter;
	}

	enum class StringViewFlags : std::size_t
	{
		/**
		 * The referenced string is global, i.e., with an unlimited lifetime. A
		 * string view with this flag set doesn't need to have a copy allocated in
		 * order to ensure it stays in scope.
		 */
		Global = std::size_t { 1 } << (sizeof(std::size_t) * 8 - 1),

		/**
		 * The referenced string is null-terminated. A string view with this flag
		 * set doesn't need to have a null-terminated copy allocated in order to
		 * pass to an API that expects only null-terminated strings.
		 */
		 NullTerminated = std::size_t { 1 } << (sizeof(std::size_t) * 8 - 2)
	};

	DEFINE_ENUM_OPERATORS(StringViewFlags);

	namespace Implementation
	{
		enum : std::size_t {
			StringViewSizeMask = std::size_t(StringViewFlags::NullTerminated) | std::size_t(StringViewFlags::Global)
		};
	}


	/**
		A lighter alternative to C++17 @ref std::string_view that has also a mutable
		variant and additional optimizations for reducing unnecessary copies and
		allocations. An owning version of this container is a @ref String.
	*/
	template<class T> class BasicStringView
	{
	public:
		/**
		 * @brief Default constructor
		 *
		 * A default-constructed instance has @ref StringViewFlags::Global set.
		 */
		constexpr /*implicit*/ BasicStringView(std::nullptr_t = nullptr) noexcept : _data {}, _sizePlusFlags { std::size_t(StringViewFlags::Global) } {}

		/**
		 * @brief Construct from a C string of known size
		 * @param data      C string
		 * @param size      Size of the C string, excluding the null terminator
		 * @param flags     Flags describing additional string properties
		 *
		 * If @ref StringViewFlags::Global is set, the data pointer is assumed
		 * to never go out of scope, which can avoid copies and allocations in
		 * code using the instance. If @ref StringViewFlags::NullTerminated is
		 * set, it's expected that `data` is not @cpp nullptr @ce and
		 * @cpp data[size] == '\0' @ce. That can avoid copies and allocations
		 * in code that passes such string to APIs that expect null-terminated
		 * strings (such as @ref std::fopen()).
		 *
		 * If you're unsure about data origin, the safe bet is to keep flags at
		 * their default. On the other hand, C string literals are always
		 * global and null-terminated --- for those, the recommended way is to
		 * use the @link operator""_s() @endlink literal instead.
		 */
		constexpr /*implicit*/ BasicStringView(T* data, std::size_t size, StringViewFlags flags = {}) noexcept : _data { data }, _sizePlusFlags {
			// This ends up being called from BasicStringView(T*, Flags), so basically on every implicit conversion
			// from a C string, thus the release build perf aspect wins over safety
			(size | (std::size_t(flags) & Implementation::StringViewSizeMask)) } { }

		/**
		 * @brief Construct from a @ref String
		 *
		 * The resulting view has @ref StringViewFlags::NullTerminated set.
		 */
		/*implicit*/ BasicStringView(String& data) noexcept;

		/**
		 * @brief Construct from a const @ref String
		 *
		 * Enabled only if the view is not mutable. The resulting view has
		 * @ref StringViewFlags::NullTerminated set.
		 */
		template<class U = T, class = typename std::enable_if<std::is_const<U>::value>::type> /*implicit*/ BasicStringView(const String& data) noexcept;

		/**
		 * @brief Construct from an @ref ArrayView
		 *
		 * The resulting view has the same size as @p data, by default no
		 * null-termination is assumed.
		 */
		/*implicit*/ BasicStringView(ArrayView<T> data, StringViewFlags flags = { }) noexcept;

		/** @brief Construct a @ref StringView from a @ref MutableStringView */
		template<class U, class = typename std::enable_if<std::is_same<const U, T>::value>::type> constexpr /*implicit*/ BasicStringView(BasicStringView<U> mutable_) noexcept : _data { mutable_._data }, _sizePlusFlags { mutable_._sizePlusFlags } {}

		/**
		 * @brief Construct from a null-terminated C string
		 *
		 * Contrary to the behavior of @ref std::string, @p data is allowed to
		 * be @cpp nullptr @ce --- in that case an empty view is constructed.
		 *
		 * Calls @ref BasicStringView(T*, std::size_t, StringViewFlags) with
		 * @p size set to @ref std::strlen() of @p data if @p data is not
		 * @cpp nullptr @ce. If @p data is @cpp nullptr @ce, @p size is set to
		 * @cpp 0 @ce. In addition to @p extraFlags, if @p data is not
		 * @cpp nullptr @ce, @ref StringViewFlags::NullTerminated is set,
		 * otherwise @ref StringViewFlags::Global is set.
		 *
		 * The @ref BasicStringView(std::nullptr_t) overload (which is a
		 * default constructor) is additionally @cpp constexpr @ce.
		 */
		// The template has to be here in order to avoid ambiguity when creating a StringView from something that's not exactly
		// ArrayView<T>, leading to both the ArrayView<T> and T* constructors being picked
		template<class U, class = typename std::enable_if<std::is_same<typename std::decay<U>::type, typename std::remove_const<T>::type*>::value || std::is_same<typename std::decay<U>::type, T*>::value>::type> /*implicit*/ BasicStringView(U&& data, StringViewFlags extraFlags = {}) noexcept : BasicStringView { data, extraFlags, nullptr } {}

		/**
		 * @brief Construct a view on an external type / from an external representation
		 */
		// There's no restriction that would disallow creating StringView from e.g. std::string<T>&& because that would break uses like
		// `consume(foo());`, where `consume()` expects a view but `foo()` returns a std::vector. Besides that, to simplify the implementation,
		// there's no const-adding conversion. Instead, the implementer is supposed to add an ArrayViewConverter variant for that.
		template<class U, class = decltype(Implementation::StringViewConverter<T, typename std::decay<U&&>::type>::from(std::declval<U&&>()))> constexpr /*implicit*/ BasicStringView(U&& other) noexcept : BasicStringView { Implementation::StringViewConverter<T, typename std::decay<U&&>::type>::from(std::forward<U>(other)) } {}

		/**
		 * @brief Convert to an @ref ArrayView
		 *
		 * The resulting view has the same size as this string @ref size() ---
		 * the null terminator, if any, is not counted into it.
		 */
		/*implicit*/ operator ArrayView<T>() const noexcept;
		/*implicit*/ operator ArrayView<typename std::conditional<std::is_const<T>::value, const void, void>::type>() const noexcept;

		/**
		* @brief Convert the view to external representation
		*/
		// To simplify the implementation, there's no const-adding conversion. Instead, the implementer is supposed to add an StringViewConverter variant for that.
		template<class U, class = decltype(Implementation::StringViewConverter<T, U>::to(std::declval<BasicStringView<T>>()))> constexpr /*implicit*/ operator U() const {
			return Implementation::StringViewConverter<T, U>::to(*this);
		}

		/**
		 * @brief Whether the string is non-empty and non-null
		 *
		 * Returns @cpp true @ce if the string is non-empty *and* the pointer
		 * is not @cpp nullptr @ce, @cpp false @ce otherwise. If you rely on
		 * just one of these conditions, use @ref empty() and @ref data()
		 * instead.
		 */
		constexpr explicit operator bool() const {
			return _data && (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/** @brief Flags */
		constexpr StringViewFlags flags() const {
			return StringViewFlags(_sizePlusFlags & Implementation::StringViewSizeMask);
		}

		/**
		 * @brief String data
		 *
		 * The pointer is not guaranteed to be null-terminated, use
		 * @ref flags() and @ref StringViewFlags::NullTerminated to check for
		 * the presence of a null terminator.
		 */
		constexpr T* data() const {
			return _data;
		}

		/**
		 * @brief String size
		 *
		 * Excludes the null terminator.
		 */
		constexpr std::size_t size() const {
			return (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief Whether the string is empty
		 */
		constexpr bool empty() const {
			return !(_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief Pointer to the first byte
		 */
		constexpr T* begin() const {
			return _data;
		}
		constexpr T* cbegin() const {
			return _data;
		}

		/**
		 * @brief Pointer to (one item after) the last byte
		 */
		constexpr T* end() const {
			return _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}
		constexpr T* cend() const {
			return _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief First byte
		 *
		 * Expects there is at least one byte.
		 */
		constexpr T& front() const;

		/**
		 * @brief Last byte
		 *
		 * Expects there is at least one byte.
		 */
		constexpr T& back() const;

		/** @brief Element access */
		constexpr T& operator[](std::size_t i) const {
			return _data[i];
		}

		/**
		 * @brief View slice
		 *
		 * Both arguments are expected to be in range. Propagates the
		 * @ref StringViewFlags::Global flag and if @p end points to (one item
		 * after) the end of the original null-terminated string, the result
		 * has @ref StringViewFlags::NullTerminated also.
		 */
		constexpr BasicStringView<T> slice(T* begin, T* end) const;

		/** @overload */
		constexpr BasicStringView<T> slice(std::size_t begin, std::size_t end) const;

		/**
		 * @brief View prefix until a pointer
		 *
		 * Equivalent to @cpp string.slice(string.begin(), end) @ce. If @p end
		 * is @cpp nullptr @ce, returns zero-sized @cpp nullptr @ce view.
		 */
		constexpr BasicStringView<T> prefix(T* end) const {
			return end ? slice(_data, end) : BasicStringView<T> {};
		}

		/**
		 * @brief View suffix after a pointer
		 *
		 * Equivalent to @cpp string.slice(begin, string.end()) @ce. If
		 * @p begin is @cpp nullptr @ce and the original view isn't, returns a
		 * zero-sized @cpp nullptr @ce view.
		 */
		constexpr BasicStringView<T> suffix(T* begin) const {
			return _data && !begin ? BasicStringView<T>{} : slice(begin, _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask));
		}

		/**
		 * @brief View on the first @p count bytes
		 *
		 * Equivalent to @cpp string.slice(0, count) @ce.
		 */
		constexpr BasicStringView<T> prefix(std::size_t count) const {
			return slice(0, count);
		}

		// Here will be suffix(std::size_t count), view on the last count bytes, once the deprecated suffix(std::size_t begin)
		// is gone and enough time passes to not cause silent breakages in existing code.

		/**
		* @brief View except the first @p count bytes
		*
		* Equivalent to @cpp string.slice(count, string.size()) @ce.
		*/
		constexpr BasicStringView<T> exceptPrefix(std::size_t count) const {
			return slice(count, _sizePlusFlags & ~Implementation::StringViewSizeMask);
		}

		/**
		 * @brief View except the last @p count bytes
		 *
		 * Equivalent to @cpp string.slice(0, string.size() - count) @ce.
		 */
		constexpr BasicStringView<T> exceptSuffix(std::size_t count) const {
			return slice(0, (_sizePlusFlags & ~Implementation::StringViewSizeMask) - count);
		}

		/**
		 * @brief Split on given character
		 *
		 * If @p delimiter is not found, returns a single-item array containing
		 * the full input string. If the string is empty, returns an empty
		 * array. The function uses @ref slice() internally, meaning it
		 * propagates the @ref flags() as appropriate.
		 */
		Array<BasicStringView<T>> split(char delimiter) const;

		/**
		 * @brief Split on given character, removing empty parts
		 *
		 * If @p delimiter is not found, returns a single-item array containing
		 * the full input string. If the string is empty or consists just of
		 * @p delimiter characters, returns an empty array. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate.
		 */
		Array<BasicStringView<T>> splitWithoutEmptyParts(char delimiter) const;

		/**
		 * @brief Split on any character from given set, removing empty parts
		 *
		 * If no characters from @p delimiters are found, returns a single-item
		 * array containing the full input string. If the string is empty or
		 * consists just of characters from @p delimiters, returns an empty
		 * array. The function uses @ref slice() internally, meaning it
		 * propagates the @ref flags() as appropriate.
		 */
		Array<BasicStringView<T>> splitOnAnyWithoutEmptyParts(StringView delimiters) const;

		/**
		 * @brief Split on whitespace, removing empty parts
		 *
		 * Equivalent to calling @ref splitOnAnyWithoutEmptyParts(StringView) const
		 * with @cpp " \t\f\v\r\n" @ce passed to @p delimiters.
		 */
		Array<BasicStringView<T>> splitOnWhitespaceWithoutEmptyParts() const;

		/**
		 * @brief Partition
		 *
		 * Equivalent to Python's @m_class{m-doc-external} [str.partition()](https://docs.python.org/3/library/stdtypes.html#str.partition).
		 * Splits @p string at the first occurrence of @p separator. First
		 * returned value is the part before the separator, second the
		 * separator, third a part after the separator. If the separator is not
		 * found, returns the input string followed by two empty strings.
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting views are
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		StaticArray<3, BasicStringView<T>> partition(char separator) const;

		/**
		 * @brief Join strings with this view as the delimiter
		 *
		 * Similar in usage to Python's @m_class{m-doc-external} [str.join()](https://docs.python.org/3/library/stdtypes.html#str.join)
		 */
		String join(ArrayView<const StringView> strings) const;

		/** @overload */
		String join(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Join strings with this view as the delimiter, skipping empty parts
		 *
		 * Like @ref join(), but empty views in @p strings are skipped instead
		 * of causing multiple repeated delimiters in the output.
		 */
		String joinWithoutEmptyParts(ArrayView<const StringView> strings) const;

		/** @overload */
		String joinWithoutEmptyParts(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Whether the string begins with given prefix
		 *
		 * For an empty string returns @cpp true @ce only if @p prefix is empty
		 * as well.
		 */
		bool hasPrefix(StringView prefix) const;
		bool hasPrefix(char prefix) const;

		/**
		 * @brief Whether the string ends with given suffix
		 *
		 * For an empty string returns @cpp true @ce only if @p suffix is empty
		 * as well.
		 */
		bool hasSuffix(StringView suffix) const;
		bool hasSuffix(char suffix) const;

		/**
		 * @brief View with given prefix stripped
		 *
		 * Expects that the string actually begins with given prefix. The
		 * function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> exceptPrefix(StringView prefix) const;

		template<class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> BasicStringView<T> exceptPrefix(T&& prefix) const = delete;

		/**
		 * @brief View with given suffix stripped
		 *
		 * Expects that the string actually ends with given suffix. The
		 * function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> exceptSuffix(StringView suffix) const;

		template<class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> BasicStringView<T> exceptSuffix(T&& suffix) const = delete;

		/**
		 * @brief View with given characters trimmed from prefix and suffix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmed(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from prefix and suffix
		 *
		 * Equivalent to calling @ref trimmed(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmed() const;

		/**
		 * @brief View with given characters trimmed from prefix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmedPrefix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from prefix
		 *
		 * Equivalent to calling @ref trimmedPrefix(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmedPrefix() const;

		/**
		 * @brief View with given characters trimmed from suffix
		 *
		 * The function uses @ref slice() internally, meaning it propagates the
		 * @ref flags() as appropriate. Additionally, the resulting view is
		 * @cpp nullptr @ce only if the input is @cpp nullptr @ce, otherwise
		 * the view always points to existing memory.
		 */
		BasicStringView<T> trimmedSuffix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from suffix
		 *
		 * Equivalent to calling @ref trimmedSuffix(StringView) const with
		 * @cpp " \t\f\v\r\n" @ce passed to @p characters.
		 */
		BasicStringView<T> trimmedSuffix() const;

		/**
		 * @brief Find a substring
		 *
		 * Returns a view pointing to the first found substring. If not found,
		 * an empty @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * Note that the function operates with a @f$ \mathcal{O}(nm) @f$
		 * complexity and as such is meant mainly for one-time searches in
		 * non-performance-critical code. For repeated searches or searches of
		 * large substrings it's recommended to use the @ref std::search()
		 * algorithms, especially @ref std::boyer_moore_searcher and its
		 * variants. Those algorithms on the other hand have to perform certain
		 * preprocessing of the input and keep extra state and due to that
		 * overhead aren't generally suited for one-time searches. Consider
		 * using @ref find(char) const instead for single-byte substrings.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		// Technically it would be enough to have just one overload with a default value for the fail parameter. But then `find(foo, pointer)`
		// would imply "find foo after pointer", because that's what the second parameter does in most APIs. On the other hand, naming this
		// findOr() and documenting the custom failure handling would add extra congitive load for people looking for find() and nothing else.
		BasicStringView<T> find(StringView substring) const {
			return findOr(substring, nullptr);
		}

		/**
		 * @brief Find a character
		 *
		 * Faster than @ref find(StringView) const if the string has just one
		 * byte.
		 */
		// Technically it would be enough to have just one overload with a default value for the fail parameter, see above why it's not
		BasicStringView<T> find(char character) const {
			return findOr(character, nullptr);
		}

		/**
		 * @brief Find a substring with a custom failure pointer
		 *
		 * Like @ref find(StringView) const, but returns an empty view pointing
		 * to the @p fail value instead of @cpp nullptr @ce, which is useful to
		 * avoid explicit handling of cases where the substring wasn't found.
		 *
		 * The @p fail value can be @cpp nullptr @ce or any other pointer, but
		 * commonly it's set to either @ref begin() or @ref end().
		 * 
		 * Consider using @ref findOr(char, T*) const for single-byte
		 * substrings.
		 */
		BasicStringView<T> findOr(StringView substring, T* fail) const;

		/**
		 * @brief Find a character with a custom failure pointer
		 *
		 * Faster than @ref findOr(StringView, T*) const if the string has just
		 * one byte.
		 */
		BasicStringView<T> findOr(char character, T* fail) const;

		/**
		 * @brief Find the last occurence of a substring
		 *
		 * Returns a view pointing to the last found substring. If not found,
		 * an empty @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * Similarly as with @ref find(), note that the function operates with
		 * a @f$ \mathcal{O}(nm) @f$ complexity and as such is meant mainly for
		 * one-time searches in non-performance-critical code. See the
		 * documentation of @ref find() for further information and suggested
		 * alternatives. Consider using @ref findLast(char) const instead for
		 * single-byte substrings.
		 *
		 * This function is equivalent to calling @relativeref{std::string,rfind()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		// Technically it would be enough to have just one overload with a default value for the fail parameter, see above why it's not
		BasicStringView<T> findLast(StringView substring) const {
			return findLastOr(substring, nullptr);
		}

		/**
		 * @brief Find the last occurence of a character
		 *
		 * Faster than @ref findLast(StringView) const if the string has just
		 * one byte.
		 */
		// Technically it would be enough to have just one overload with a default value for the fail parameter, see above why it's not
		BasicStringView<T> findLast(char character) const {
			return findLastOr(character, nullptr);
		}

		/**
		 * @brief Find the last occurence a substring with a custom failure pointer
		 *
		 * Like @ref findLast(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where the substring
		 * wasn't found. See @ref findOr() for an example use case.
		 */
		BasicStringView<T> findLastOr(StringView substring, T* fail) const;

		/**
		 * @brief Find the last occurence of a character with a custom failure pointer
		 *
		 * Faster than @ref findLastOr(StringView, T*) const if the string has
		 * just one byte.
		 */
		BasicStringView<T> findLastOr(char character, T* fail) const;

		/**
		 * @brief Whether the view contains a substring
		 *
		 * A slightly lighter variant of @ref find() useful when you only want
		 * to know if a substring was found or not. Consider using
		 * @ref contains(char) const for single-byte substrings.
		 */
		bool contains(StringView substring) const;

		/**
		 * @brief Whether the view contains a character
		 *
		 * Faster than @ref contains(StringView) const if the string has just
		 * one byte.
		 */
		bool contains(char character) const;

		/**
		 * @brief Find any character from given set
		 *
		 * Returns a view pointing to the first found character from the set.
		 * If no characters from @p characters are found, an empty
		 * @cpp nullptr @ce view is returned. The function uses @ref slice()
		 * internally, meaning it propagates the @ref flags() as appropriate,
		 * except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find_first_of()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		BasicStringView<T> findAny(StringView characters) const {
			return findAnyOr(characters, nullptr);
		}

		/**
		 * @brief Find any character from given set with a custom failure pointer
		 *
		 * Like @ref findAny(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where no character was
		 * found.
		 *
		 * The @p fail value can be @cpp nullptr @ce or any other pointer, but
		 * commonly it's set to either @ref begin() or @ref end().
		 */
		BasicStringView<T> findAnyOr(StringView characters, T* fail) const;

		/**
		 * @brief Find the last occurence of any character from given set
		 *
		 * Returns a view pointing to the last found character from the set.
		 * If no characters from @p characters are found, an empty
		 * @cpp nullptr @ce view is returned. The function uses
		 * @ref slice() internally, meaning it propagates the @ref flags() as
		 * appropriate, except in case of a failure, where it always returns no
		 * @ref StringViewFlags.
		 *
		 * This function is equivalent to calling @relativeref{std::string,find_last_of()}
		 * on a @ref std::string or a @ref std::string_view.
		 */
		BasicStringView<T> findLastAny(StringView characters) const {
			return findLastAnyOr(characters, nullptr);
		}

		/**
		 * @brief Find the last occurence of any character from given set with a custom failure pointer
		 *
		 * Like @ref findLastAny(StringView) const, but returns an empty view
		 * pointing to the @p fail value instead of @cpp nullptr @ce, which is
		 * useful to avoid explicit handling of cases where the substring
		 * wasn't found.
		 */
		BasicStringView<T> findLastAnyOr(StringView characters, T* fail) const;

		/**
		 * @brief Whether the view contains any character from given set
		 *
		 * A slightly lighter variant of @ref findAny() useful when you only
		 * want to know if a character was found or not.
		 */
		bool containsAny(StringView substring) const;

	private:
		// Needed for mutable/immutable conversion
		template<class> friend class BasicStringView;
		friend String;

		// MSVC demands the export macro to be here as well
		friend bool operator==(StringView, StringView);
		friend bool operator!=(StringView, StringView);
		friend bool operator<(StringView, StringView);
		friend bool operator<=(StringView, StringView);
		friend bool operator>=(StringView, StringView);
		friend bool operator>(StringView, StringView);
		friend String operator+(StringView, StringView);
		friend String operator*(StringView, std::size_t);

		// Used by the char* constructor, delinlined because it calls into std::strlen()
		explicit BasicStringView(T* data, StringViewFlags flags, std::nullptr_t) noexcept;

		// Used by slice() to skip unneeded checks in the public constexpr constructor
		constexpr explicit BasicStringView(T* data, std::size_t sizePlusFlags, std::nullptr_t) noexcept : _data { data }, _sizePlusFlags { sizePlusFlags } {}

		T* _data;
		std::size_t _sizePlusFlags;
	};

	typedef BasicStringView<const char> StringView;
	typedef BasicStringView<char> MutableStringView;

	bool operator==(StringView a, StringView b);

	bool operator!=(StringView a, StringView b);

	bool operator<(StringView a, StringView b);

	bool operator<=(StringView a, StringView b);

	bool operator>=(StringView a, StringView b);

	bool operator>(StringView a, StringView b);

	/**
		@brief String concatenation

		For joining more than one string prefer to use @ref StringView::join() to avoid
		needless temporary allocations.
	*/
	String operator+(StringView a, StringView b);

	/**
		@brief String multiplication

		Equivalent to string multiplication in Python, returns @p string repeated
		@p count times.
	*/
	String operator*(StringView string, std::size_t count);

	String operator*(std::size_t count, StringView string);

	namespace Literals
	{
		/** @relatesalso Corrade::Containers::BasicStringView
			@brief String view literal

			The returned instance has both @ref StringViewFlags::Global and
			@ref StringViewFlags::NullTerminated set. See
			@ref Containers-BasicStringView-usage for more information.
		*/
		constexpr StringView operator"" _s(const char* data, std::size_t size) {
			/* Using plain bit ops instead of EnumSet to speed up debug builds */
			return StringView { data, size, StringViewFlags(std::size_t(StringViewFlags::Global) | std::size_t(StringViewFlags::NullTerminated)) };
		}

	}

	template<class T> constexpr T& BasicStringView<T>::front() const {
		return _data[0];
	}

	template<class T> constexpr T& BasicStringView<T>::back() const {
		return _data[size() - 1];
	}

	template<class T> constexpr BasicStringView<T> BasicStringView<T>::slice(T* const begin, T* const end) const {
		return BasicStringView<T>{begin, std::size_t(end - begin) |
			// Propagate the global flag always
			(_sizePlusFlags & std::size_t(StringViewFlags::Global)) |
			// The null termination flag only if the original is null-terminated and end points to the original end
			((_sizePlusFlags & std::size_t(StringViewFlags::NullTerminated)) * (end == _data + (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
			// Using an internal assert-less constructor, the public constructor asserts would be redundant
			nullptr};
	}

	template<class T> constexpr BasicStringView<T> BasicStringView<T>::slice(const std::size_t begin, const std::size_t end) const {
		return BasicStringView<T>{_data + begin, (end - begin) |
			// Propagate the global flag always */
			(_sizePlusFlags & std::size_t(StringViewFlags::Global)) |
			// The null termination flag only if the original is null-terminated and end points to the original end
			((_sizePlusFlags & std::size_t(StringViewFlags::NullTerminated)) * (end == (_sizePlusFlags & ~Implementation::StringViewSizeMask))),
			// Using an internal assert-less constructor, the public constructor asserts would be redundant
			nullptr};
	}
}