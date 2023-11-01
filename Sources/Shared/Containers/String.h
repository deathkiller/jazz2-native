// Copyright © 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016,
//             2017, 2018, 2019, 2020, 2021, 2022, 2023
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

#include "../CommonBase.h"
#include "StringView.h" /* needs to be included for comparison operators */

#include <cstddef>
#include <type_traits>
#include <string>

namespace Death::Containers
{
	template<class, class> class Pair;

	namespace Implementation
	{
		enum : std::size_t {
			SmallStringBit = 0x40,
			SmallStringSize = sizeof(std::size_t) * 3 - 1
		};

		template<class> struct StringConverter;

		template<> struct StringConverter<std::string> {
			static String from(const std::string& other);
			static std::string to(const String& other);
		};

		template<> struct StringViewConverter<const char, std::string> {
			static StringView from(const std::string& other);
			static std::string to(StringView other);
		};

		template<> struct StringViewConverter<char, std::string> {
			static MutableStringView from(std::string& other);
			static std::string to(MutableStringView other);
		};
	}

	/**
		@brief Allocated initialization tag type

		Used to distinguish @ref String construction that bypasses small string optimization.
	*/
	struct AllocatedInitT {
		struct Init {};
		// Explicit constructor to avoid ambiguous calls when using {}
		constexpr explicit AllocatedInitT(Init) {}
	};

	/**
		@brief Allocated initialization tag

		Use for @ref String construction that bypasses small string optimization.
	*/
	constexpr AllocatedInitT AllocatedInit { AllocatedInitT::Init{} };

	/**
		@brief String

		A lightweight non-templated alternative to @ref std::string with support for custom deleters. A non-owning version
		of this container is a @ref StringView and a @ref MutableStringView, implemented using a generic @ref BasicStringView.
	*/
	class String
	{
	public:
		typedef void(*Deleter)(char*, std::size_t); /**< @brief Deleter type */

		/**
		 * @brief Turn a view into a null-terminated string
		 *
		 * If the view is @ref StringViewFlags::NullTerminated, returns a
		 * non-owning reference to it without any extra allocations or copies
		 * involved, propagating also @ref StringViewFlag::Global to
		 * @ref viewFlags() if present. Otherwise creates a null-terminated
		 * owning copy using @ref String(StringView).
		 *
		 * This function is primarily meant for efficiently passing
		 * @ref BasicStringView "StringView" instances to APIs that expect
		 * null-terminated @cpp const char* @ce. Mutating the result in any way
		 * is undefined behavior.
		 */
		static String nullTerminatedView(StringView view);

		/**
		 * @brief Turn a view into a null-terminated string, bypassing SSO
		 *
		 * Compared to @ref nullTerminatedView(StringView) the null-terminated
		 * copy is always allocated.
		 */
		static String nullTerminatedView(AllocatedInitT, StringView view);

		/**
		 * @brief Turn a view into a null-terminated global string
		 *
		 * If the view is both @ref StringViewFlags::NullTerminated and
		 * @ref StringViewFlags::Global, returns a non-owning reference to it
		 * without any extra allocations or copies involved, propagating also
		 * @ref StringViewFlag::Global to @ref viewFlags(). Otherwise creates
		 * a null-terminated owning copy using @ref String(StringView).
		 *
		 * This function is primarily meant for efficiently storing
		 * @ref BasicStringView "StringView" instances, ensuring the
		 * memory stays in scope and then passing them to APIs that expect
		 * null-terminated @cpp const char* @ce. Mutating the result in any way
		 * is undefined behavior.
		 */
		static String nullTerminatedGlobalView(StringView view);

		/**
		 * @brief Turn a view into a null-terminated global string, bypassing SSO
		 *
		 * Compared to @ref nullTerminatedGlobalView(StringView) the
		 * null-terminated copy is always allocated.
		 */
		static String nullTerminatedGlobalView(AllocatedInitT, StringView view);

		/**
		 * @brief Default constructor
		 *
		 * Creates an empty string.
		 */
		/*implicit*/ String() noexcept;

		/**
		 * @brief Construct from a string view
		 *
		 * Creates a null-terminated owning copy of @p view. Contrary to the
		 * behavior of @ref std::string, @p view is allowed to be
		 * @cpp nullptr @ce, but only if it's size is zero. Depending on the
		 * size, it's either stored allocated or in a SSO.
		 */
		/*implicit*/ String(StringView view);
		/*implicit*/ String(ArrayView<const char> view);
		// Without these there's ambiguity between StringView / ArrayView and char*
		/*implicit*/ String(MutableStringView view);
		/*implicit*/ String(ArrayView<char> view);

		/**
		 * @brief Construct from a null-terminated C string
		 *
		 * Creates a null-terminated owning copy of @p data. Contrary to the
		 * behavior of @ref std::string, @p data is allowed to be
		 * @cpp nullptr @ce --- in that case an empty string is constructed.
		 * Depending on the size, it's either stored allocated or in a SSO.
		 */
		/* To avoid ambiguity in certain cases of passing 0 to overloads that take either a String or std::size_t */
		template<class T, class = typename std::enable_if<std::is_convertible<T, const char*>::value && !std::is_convertible<T, std::size_t>::value>::type> /*implicit*/ String(T data) : String{nullptr, nullptr, nullptr, data} {}

		/**
		 * @brief Construct from a sized C string
		 *
		 * Creates a null-terminated owning copy of @p data. Contrary to the
		 * behavior of @ref std::string, @p data is allowed to be
		 * @cpp nullptr @ce, but only if @p size is zero. Depending on the
		 * size, it's either stored allocated or in a SSO.
		 */
		/*implicit*/ String(const char* data, std::size_t size);

		/**
		 * @brief Construct from a string view, bypassing SSO
		 *
		 * Compared to @ref String(StringView) the data is always allocated.
		 */
		explicit String(AllocatedInitT, StringView view);
		explicit String(AllocatedInitT, ArrayView<const char> view);
		// Without these there's ambiguity between StringView / ArrayView and char*
		explicit String(AllocatedInitT, MutableStringView view);
		explicit String(AllocatedInitT, ArrayView<char> view);

		/**
		 * @brief Create a string instance bypassing SSO
		 *
		 * If @p other already has allocated data, the data ownership is
		 * transferred. Otherwise a copy is allocated.
		 */
		explicit String(AllocatedInitT, String&& other);
		explicit String(AllocatedInitT, const String& other);

		/**
		 * @brief Construct from a null-terminated C string, bypassing SSO
		 *
		 * Compared to @ref String(const char*) the data is always allocated.
		 */
		explicit String(AllocatedInitT, const char* data);

		/**
		 * @brief Construct from a sized C string
		 *
		 * Compared to @ref String(const char*, std::size_t) the data is always
		 * allocated.
		 */
		explicit String(AllocatedInitT, const char* data, std::size_t size);

		/**
		 * @brief Take ownership of an external data array
		 * @param data      String. Can't be @cpp nullptr @ce.
		 * @param size      Size of the string, excluding the null terminator
		 * @param deleter   Deleter. Use @cpp nullptr @ce for the standard
		 *      @cpp delete[] @ce.
		 *
		 * Since the @ref String class provides a guarantee of null-terminated
		 * strings, the @p data array is expected to be null-terminated (which
		 * implies @p data *can't* be @cpp nullptr @ce), but the null
		 * terminator not being included in @p size. For consistency and
		 * interoperability with @ref Array (i.e., an empty string turning to a
		 * zero-sized array) this in turn means the size passed to @p deleter
		 * is one byte less than the actual memory size, and if the deleter
		 * does sized deallocation, it has to account for that.
		 *
		 * The @p deleter will be *unconditionally* called on destruction with
		 * @p data and @p size as an argument. In particular, it will be also
		 * called if @p size is @cpp 0 @ce (@p data isn't allowed to be
		 * @cpp nullptr @ce).
		 *
		 * In case of a moved-out instance, the deleter gets reset to a
		 * default-constructed value alongside the array pointer and size. It
		 * effectively means @cpp delete[] nullptr @ce gets called when
		 * destructing a moved-out instance (which is a no-op).
		 */
		explicit String(char* data, std::size_t size, Deleter deleter) noexcept;

		/**
		 * @brief Take ownership of an external data array with implicit size
		 *
		 * Calculates the size using @ref std::strlen() and calls
		 * @ref String(char*, std::size_t, Deleter).
		 */
		// Gets ambigous when calling String{ptr, 0}. FFS, zero as null pointerwas deprecated in C++11 already, why is this still a problem?!
		template<class T> String(typename std::enable_if<std::is_convertible<T, Deleter>::value && !std::is_convertible<T, std::size_t>::value, char*>::type data, T deleter) noexcept : String{deleter, nullptr, data} {}

		/**
		 * @brief Take ownership of an immutable external data array
		 *
		 * Casts away the @cpp const @ce and delegates to
		 * @ref String(char*, std::size_t, Deleter). This constructor is
		 * provided mainly to allow a @ref String instance to reference global
		 * immutable data (such as C string literals) without having to make a
		 * copy, it's the user responsibility to avoid mutating the data in any
		 * way.
		 */
		explicit String(const char* data, std::size_t size, Deleter deleter) noexcept : String{const_cast<char*>(data), size, deleter} {}

		/**
		 * @brief Take ownership of an external data array with implicit size
		 *
		 * Calculates the size using @ref std::strlen() and calls
		 * @ref String(const char*, std::size_t, Deleter).
		 */
		// Gets ambigous when calling String{ptr, 0}. FFS, zero as null pointer was deprecated in C++11 already, why is this still a problem?!
		template<class T, class = typename std::enable_if<std::is_convertible<T, Deleter>::value && !std::is_convertible<T, std::size_t>::value, const char*>::type> String(const char* data, T deleter) noexcept : String{deleter, nullptr, const_cast<char*>(data)} {}

		/**
		 * @brief Taking ownership of a null pointer is not allowed
		 *
		 * Since the @ref String class provides a guarantee of null-terminated
		 * strings, @p data *can't* be @cpp nullptr @ce.
		 */
		explicit String(std::nullptr_t, std::size_t size, Deleter deleter) = delete;

		/**
		 * @brief Taking ownership of a null pointer is not allowed
		 *
		 * Since the @ref String class provides a guarantee of null-terminated
		 * strings, @p data *can't* be @cpp nullptr @ce.
		 */
		// Gets ambigous when calling String{nullptr, 0}. FFS, zero as null pointer was deprecated in C++11 already, why is this still a problem?!
		template<class T> String(typename std::enable_if<std::is_convertible<T, Deleter>::value && !std::is_convertible<T, std::size_t>::value, std::nullptr_t>::type, T) noexcept = delete;

		/**
		 * @brief Create a zero-initialized string
		 * @param size      Size excluding the null terminator
		 */
		explicit String(ValueInitT, std::size_t size);

		/**
		 * @brief Create an uninitialized string
		 * @param size      Size excluding the null terminator
		 *
		 * While the string contents are left untouched, the null terminator
		 * *does* get initialized to @cpp '\0' @ce. Useful if you're going to
		 * overwrite the contents anyway.
		 */
		explicit String(NoInitT, std::size_t size);

		/**
		 * @brief Create a string initialized to a particular character
		 * @param size      Size excluding the null terminator
		 * @param c         Character value
		 */
		explicit String(DirectInitT, std::size_t size, char c);

		/**
		 * @brief Construct a view on an external type / from an external representation
		 */
		// There's no restriction that would disallow creating StringView from e.g. std::string<T>&& because that would break uses like
		// `consume(foo());`, where `consume()` expects a view but `foo()` returns a std::vector. Besides that, to simplify the implementation,
		// there's no const-adding conversion. Instead, the implementer is supposed to add an ArrayViewConverter variant for that.
		template<class T, class = decltype(Implementation::StringConverter<typename std::decay<T&&>::type>::from(std::declval<T&&>()))> /*implicit*/ String(T&& other) noexcept : String{Implementation::StringConverter<typename std::decay<T&&>::type>::from(std::forward<T>(other))} {}

		/**
		 * @brief Destructor
		 *
		 * Calls @ref deleter() on the owned @ref data(); in case of a SSO does nothing.
		 */
		~String();

		/** @brief Copy constructor */
		String(const String& other);

		/** @brief Move constructor */
		String(String&& other) noexcept;

		/** @brief Copy assignment */
		String& operator=(const String& other);

		/** @brief Move assignment */
		String& operator=(String&& other) noexcept;

		/**
		 * @brief Convert to a const @ref ArrayView
		 *
		 * The resulting view has the same size as this string @ref size() ---
		 * the null terminator is not counted into it.
		 */
		/*implicit*/ operator ArrayView<const char>() const noexcept;
		/*implicit*/ operator ArrayView<const void>() const noexcept;

		/**
		 * @brief Convert to an @ref ArrayView
		 *
		 * The resulting view has the same size as this string @ref size() ---
		 * the null terminator is not counted into it. Note that with custom
		 * deleters the returned view is not guaranteed to be actually mutable.
		 */
		/*implicit*/ operator ArrayView<char>() noexcept;
		/*implicit*/ operator ArrayView<void>() noexcept;

		/**
		 * @brief Move-convert to an @ref Array
		 *
		 * The data and the corresponding @ref deleter() is transferred to the
		 * returned array. In case of a SSO, a copy of the string is allocated
		 * and a default deleter is used. The string then resets data pointer,
		 * size and deleter to be equivalent to a default-constructed instance.
		 * In both the allocated and the SSO case the returned array contains a
		 * sentinel null terminator (i.e., not counted into its size). Note
		 * that with custom deleters the array is not guaranteed to be actually
		 * mutable.
		 */
		/*implicit*/ operator Array<char>()&&;

		/**
		 * @brief Convert the string to external representation
		 */
		// To simplify the implementation, there's no const-adding conversion. Instead, the implementer is supposed to add an StringViewConverter variant for that.
		template<class T, class = decltype(Implementation::StringConverter<T>::to(std::declval<String>()))> /*implicit*/ operator T() const {
			return Implementation::StringConverter<T>::to(*this);
		}

		/**
		 * @brief Whether the string is non-empty
		 *
		 * Returns @cpp true @ce if the string is non-empty, @cpp false @ce
		 * otherwise. Compared to @ref BasicStringView::operator bool(), a
		 * @ref String can never be @cpp nullptr @ce, so the pointer value
		 * isn't taken into account here.
		 */
		explicit operator bool() const;

		/**
		 * @brief Whether the string is stored using small string optimization
		 *
		 * It's not allowed to call @ref deleter() or @ref release() on a SSO
		 * instance. See @ref Containers-String-usage-sso for more information.
		 */
		bool isSmall() const {
			return _small.size & Implementation::SmallStringBit;
		}

		/**
		 * @brief View flags
		 *
		 * A @ref BasicStringView "StringView" constructed from this instance
		 * will have these flags. @ref StringViewFlag::NullTerminated is
		 * present always, @ref StringViewFlag::Global if the string was
		 * originally created from a global null-terminated view with
		 * @ref nullTerminatedView() or @ref nullTerminatedGlobalView().
		 */
		StringViewFlags viewFlags() const;

		/**
		 * @brief String data
		 *
		 * The pointer is always guaranteed to be non-null and the data to be
		 * null-terminated, however note that the actual string might contain
		 * null bytes earlier than at the end.
		 */
		char* data();
		const char* data() const;

		/**
		 * @brief String deleter
		 *
		 * If set to @cpp nullptr @ce, the contents are deleted using standard
		 * @cpp operator delete[] @ce. Can be called only if the string is not
		 * stored using SSO --- see @ref Containers-String-usage-sso for more
		 * information.
		 */
		Deleter deleter() const;

		/**
		 * @brief Whether the string is empty
		 */
		bool empty() const;

		/**
		 * @brief String size
		 *
		 * Excludes the null terminator.
		 */
		std::size_t size() const;

		/**
		 * @brief Pointer to the first byte
		 */
		char* begin();
		const char* begin() const;
		const char* cbegin() const;

		/**
		 * @brief Pointer to (one item after) the last byte
		 */
		char* end();
		const char* end() const;
		const char* cend() const;

		/**
		 * @brief First byte
		 *
		 * Expects there is at least one byte.
		 */
		char& front();
		char front() const;

		/**
		 * @brief Last byte
		 *
		 * Expects there is at least one byte.
		 */
		char& back();
		char back() const;

		/** @brief Element access */
		char& operator[](std::size_t i);
		char operator[](std::size_t i) const;

		/**
		 * @brief String concatenation
		 *
		 * For joining more than one string prefer to use @ref StringView::join() to avoid
		 * needless temporary allocations.
		 */
		String operator+=(const StringView& other);

		/**
		 * @brief View on a slice
		 *
		 * Equivalent to @ref BasicStringView::slice(). Both arguments are
		 * expected to be in range. If @p end points to (one item after) the
		 * end of the original (null-terminated) string, the result has
		 * @ref StringViewFlags::NullTerminated set.
		 * @m_keywords{substr()}
		 */
		MutableStringView slice(char* begin, char* end);
		StringView slice(const char* begin, const char* end) const;
		MutableStringView slice(std::size_t begin, std::size_t end);
		StringView slice(std::size_t begin, std::size_t end) const;

		/**
		 * @brief View on a slice of given size
		 *
		 * Equivalent to @ref BasicStringView::sliceSize(). Both arguments are
		 * expected to be in range. If `begin + size` points to (one item
		 * after) the end of the original (null-terminated) string, the result
		 * has @ref StringViewFlag::NullTerminated set.
		 * @m_keywords{substr()}
		 */
		template<class T, class = typename std::enable_if<std::is_convertible<T, char*>::value && !std::is_convertible<T, std::size_t>::value>::type> MutableStringView sliceSize(T begin, std::size_t size) {
			return sliceSizePointerInternal(begin, size);
		}
		template<class T, class = typename std::enable_if<std::is_convertible<T, const char*>::value && !std::is_convertible<T, std::size_t>::value>::type> StringView sliceSize(T begin, std::size_t size) const {
			return sliceSizePointerInternal(begin, size);
		}
		MutableStringView sliceSize(std::size_t begin, std::size_t size);
		StringView sliceSize(std::size_t begin, std::size_t size) const;

		/**
		 * @brief View on a prefix until a pointer
		 *
		 * Equivalent to @ref BasicStringView::prefix(T*) const. If @p end
		 * points to (one item after) the end of the original (null-terminated)
		 * string, the result has @ref StringViewFlags::NullTerminated set.
		 */
		template<class T, class = typename std::enable_if<std::is_convertible<T, char*>::value && !std::is_convertible<T, std::size_t>::value>::type> MutableStringView prefix(T end) {
			return prefixPointerInternal(end);
		}
		template<class T, class = typename std::enable_if<std::is_convertible<T, const char*>::value && !std::is_convertible<T, std::size_t>::value>::type> StringView prefix(T end) const {
			return prefixPointerInternal(end);
		}

		/**
		 * @brief View on a suffix after a pointer
		 *
		 * Equivalent to @ref BasicStringView::suffix(T*) const. The result has
		 * always @ref StringViewFlags::NullTerminated set.
		 */
		MutableStringView suffix(char* begin);
		StringView suffix(const char* begin) const;

		/**
		 * @brief View on the first @p size bytes
		 *
		 * Equivalent to @ref BasicStringView::prefix(std::size_t) const. If
		 * @p size is equal to @ref size(), the result has
		 * @ref StringViewFlags::NullTerminated set.
		 */
		MutableStringView prefix(std::size_t size);
		StringView prefix(std::size_t size) const;

		/**
		 * @brief View except the first @p size bytes
		 *
		 * Equivalent to @ref BasicStringView::exceptPrefix(). The result has
		 * always @ref StringViewFlags::NullTerminated set.
		 */
		MutableStringView exceptPrefix(std::size_t size);
		StringView exceptPrefix(std::size_t size) const;

		/**
		 * @brief View except the last @p size bytes
		 *
		 * Equivalent to @ref BasicStringView::exceptSuffix(). If
		 * @p size is @cpp 0 @ce, the result has
		 * @ref StringViewFlags::NullTerminated set.
		 */
		MutableStringView exceptSuffix(std::size_t size);
		StringView exceptSuffix(std::size_t size) const;

		/**
		 * @brief Split on given character
		 *
		 * Equivalent to @ref BasicStringView::split(char) const.
		 */
		Array<MutableStringView> split(char delimiter);
		Array<StringView> split(char delimiter) const;

		/**
		 * @brief Split on given substring
		 *
		 * Equivalent to @ref BasicStringView::split(StringView) const.
		 */
		Array<MutableStringView> split(StringView delimiter);
		Array<StringView> split(StringView delimiter) const;

		/**
		 * @brief Split on given character, removing empty parts
		 *
		 * Equivalent to @ref BasicStringView::splitWithoutEmptyParts(char) const.
		 */
		Array<MutableStringView> splitWithoutEmptyParts(char delimiter);
		Array<StringView> splitWithoutEmptyParts(char delimiter) const;

		/**
		 * @brief Split on any character from given set, removing empty parts
		 *
		 * Equivalent to @ref BasicStringView::splitOnAnyWithoutEmptyParts(StringView) const.
		 */
		Array<MutableStringView> splitOnAnyWithoutEmptyParts(StringView delimiters);
		Array<StringView> splitOnAnyWithoutEmptyParts(StringView delimiters) const;

		/**
		 * @brief Split on whitespace, removing empty parts
		 *
		 * Equivalent to @ref BasicStringView::splitOnWhitespaceWithoutEmptyParts() const.
		 */
		Array<MutableStringView> splitOnWhitespaceWithoutEmptyParts();
		Array<StringView> splitOnWhitespaceWithoutEmptyParts() const;

		/**
		 * @brief Partition
		 *
		 * Equivalent to @ref BasicStringView::partition(char) const. The last returned
		 * value has always @ref StringViewFlags::NullTerminated set.
		 */
		StaticArray<3, MutableStringView> partition(char separator);
		StaticArray<3, StringView> partition(char separator) const;

		/**
		 * @brief Partition
		 *
		 * Equivalent to @ref BasicStringView::partition(StringView) const. The
		 * last returned value has always @ref StringViewFlag::NullTerminated
		 * set.
		 */
		StaticArray<3, MutableStringView> partition(StringView separator);
		StaticArray<3, StringView> partition(StringView separator) const;

		/**
		 * @brief Join strings with this view as the delimiter
		 *
		 * Equivalent to @ref BasicStringView::join().
		 */
		String join(ArrayView<const StringView> strings) const;
		/** @overload */
		String join(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Join strings with this view as the delimiter, skipping empty parts
		 *
		 * Equivalent to @ref BasicStringView::joinWithoutEmptyParts().
		 */
		String joinWithoutEmptyParts(ArrayView<const StringView> strings) const;
		String joinWithoutEmptyParts(std::initializer_list<StringView> strings) const;

		/**
		 * @brief Whether the string begins with given prefix
		 *
		 * Equivalent to @ref BasicStringView::hasPrefix().
		 */
		bool hasPrefix(StringView prefix) const;
		bool hasPrefix(char prefix) const;

		/**
		 * @brief Whether the string ends with given suffix
		 *
		 * Equivalent to @ref BasicStringView::hasSuffix().
		 */
		bool hasSuffix(StringView suffix) const;
		bool hasSuffix(char suffix) const;

		/**
		 * @brief View with given prefix stripped
		 *
		 * Equivalent to @ref BasicStringView::exceptPrefix().
		 */
		MutableStringView exceptPrefix(StringView prefix);
		StringView exceptPrefix(StringView prefix) const;

		/**
		 * @brief Using char literals for prefix stripping is not allowed
		 *
		 * To avoid accidentally interpreting a @cpp char @ce literal as a size
		 * and calling @ref exceptPrefix(std::size_t) instead, or vice versa,
		 * you have to always use a string literal to call this function.
		 */
		template<class T, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> MutableStringView exceptPrefix(T&& prefix) = delete;
		template<class T, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> StringView exceptPrefix(T&& prefix) const = delete;

		/**
		 * @brief View with given suffix stripped
		 *
		 * Equivalent to @ref BasicStringView::exceptSuffix().
		 */
		MutableStringView exceptSuffix(StringView suffix);
		StringView exceptSuffix(StringView suffix) const;

		/**
		 * @brief Using char literals for suffix stripping is not allowed
		 *
		 * To avoid accidentally interpreting a @cpp char @ce literal as a size
		 * and calling @ref exceptSuffix(std::size_t) instead, or vice versa,
		 * you have to always use a string literal to call this function.
		 */
		template<class T, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> MutableStringView exceptSuffix(T&& suffix) = delete;
		template<class T, class = typename std::enable_if<std::is_same<typename std::decay<T>::type, char>::value>::type> StringView exceptSuffix(T&& suffix) const = delete;

		/**
		 * @brief View with given characters trimmed from prefix and suffix
		 *
		 * Equivalent to @ref BasicStringView::trimmed(StringView) const.
		 */
		MutableStringView trimmed(StringView characters);
		StringView trimmed(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from prefix and suffix
		 *
		 * Equivalent to @ref BasicStringView::trimmed() const.
		 */
		MutableStringView trimmed();
		StringView trimmed() const;

		/**
		 * @brief View with given characters trimmed from prefix
		 *
		 * Equivalent to @ref BasicStringView::trimmedPrefix(StringView) const.
		 */
		MutableStringView trimmedPrefix(StringView characters);
		StringView trimmedPrefix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from prefix
		 *
		 * Equivalent to @ref BasicStringView::trimmedPrefix() const.
		 */
		MutableStringView trimmedPrefix();
		StringView trimmedPrefix() const;

		/**
		 * @brief View with given characters trimmed from suffix
		 *
		 * Equivalent to @ref BasicStringView::trimmedSuffix(StringView) const.
		 */
		MutableStringView trimmedSuffix(StringView characters);
		StringView trimmedSuffix(StringView characters) const;

		/**
		 * @brief View with whitespace trimmed from suffix
		 *
		 * Equivalent to @ref BasicStringView::trimmedSuffix() const.
		 */
		MutableStringView trimmedSuffix();
		StringView trimmedSuffix() const;

		/**
		 * @brief Find a substring
		 *
		 * Equivalent to @ref BasicStringView::find(StringView) const.
		 */
		MutableStringView find(StringView substring);
		StringView find(StringView substring) const;

		/**
		 * @brief Find a substring
		 *
		 * Equivalent to @ref BasicStringView::find(char) const, which in turn
		 * is a specialization of @ref BasicStringView::find(StringView) const.
		 */
		MutableStringView find(char character);
		StringView find(char character) const;

		/**
		 * @brief Find a substring with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findOr(StringView, T*) const.
		 */
		MutableStringView findOr(StringView substring, char* fail);
		StringView findOr(StringView substring, const char* fail) const;

		/**
		 * @brief Find a substring with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findOr(char, T*) const, which in
		 * turn is a specialization of @ref BasicStringView::findOr(StringView, T*) const.
		 */
		MutableStringView findOr(char character, char* fail);
		StringView findOr(char character, const char* fail) const;

		/**
		 * @brief Find the last occurence of a substring
		 *
		 * Equivalent to @ref BasicStringView::findLast(StringView) const.
		 */
		MutableStringView findLast(StringView substring);
		StringView findLast(StringView substring) const;

		/**
		 * @brief Find the last occurence of a substring
		 *
		 * Equivalent to @ref BasicStringView::findLast(char) const, which in
		 * turn is a specialization of @ref BasicStringView::findLast(StringView) const.
		 */
		MutableStringView findLast(char character);
		StringView findLast(char character) const;

		/**
		 * @brief Find the last occurence of a substring with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findLastOr(StringView, T*) const.
		 */
		MutableStringView findLastOr(StringView substring, char* fail);
		StringView findLastOr(StringView substring, const char* fail) const;

		/**
		 * @brief Find the last occurence of a substring with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findLastOr(char, T*) const,
		 * which in turn is a specialization of @ref BasicStringView::findLastOr(StringView, T*) const.
		 */
		MutableStringView findLastOr(char character, char* fail);
		StringView findLastOr(char character, const char* fail) const;

		/**
		 * @brief Whether the string contains a substring
		 *
		 * Equivalent to @ref BasicStringView::contains(StringView) const.
		 */
		bool contains(StringView substring) const;

		/**
		 * @brief Whether the string contains a character
		 *
		 * Equivalent to @ref BasicStringView::contains(char) const.
		 */
		bool contains(char character) const;

		/**
		 * @brief Find any character from given set
		 *
		 * Equivalent to @ref BasicStringView::findAny().
		 */
		MutableStringView findAny(StringView characters);
		StringView findAny(StringView characters) const;

		/**
		 * @brief Find any character from given set with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findAnyOr().
		 */
		MutableStringView findAnyOr(StringView characters, char* fail);
		StringView findAnyOr(StringView characters, const char* fail) const;

		/**
		 * @brief Find the last occurence of any character from given set
		 *
		 * Equivalent to @ref BasicStringView::findLastAny().
		 */
		MutableStringView findLastAny(StringView characters);
		StringView findLastAny(StringView characters) const;

		/**
		 * @brief Find the last occurence of any character from given set with a custom failure pointer
		 *
		 * Equivalent to @ref BasicStringView::findLastAnyOr().
		 */
		MutableStringView findLastAnyOr(StringView characters, char* fail);
		StringView findLastAnyOr(StringView characters, const char* fail) const;

		/**
		 * @brief Whether the string contains any character from given set
		 *
		 * Equivalent to @ref BasicStringView::containsAny().
		 */
		bool containsAny(StringView substring) const;

		/**
		 * @brief Release data storage
		 *
		 * Returns the data pointer and resets data pointer, size and deleter
		 * to be equivalent to a default-constructed instance. Can be called
		 * only if the string is not stored using SSO --- see
		 * @ref Containers-String-usage-sso for more information. Deleting the
		 * returned array is user responsibility --- note the string might have
		 * a custom @ref deleter() and so @cpp delete[] @ce might not be always
		 * appropriate. Note also that with custom deleters the returned
		 * pointer is not guaranteed to be actually mutable.
		 */
		char* release();

	private:
		// Delegated to from the (templated) String(const char*). THREE extra nullptr arguments to avoid accidental ambiguous overloads.
		explicit String(std::nullptr_t, std::nullptr_t, std::nullptr_t, const char* data);
		// Delegated to from the (templated) String(char*, Deleter). Argument order shuffled together with a null parameter to avoid accidental ambiguous overloads.
		explicit String(Deleter deleter, std::nullptr_t, char* data) noexcept;

		void construct(NoInitT, std::size_t size);
		void construct(const char* data, std::size_t size);
		void destruct();
		Pair<const char*, std::size_t> dataInternal() const;

		MutableStringView sliceSizePointerInternal(char* begin, std::size_t size);
		StringView sliceSizePointerInternal(const char* begin, std::size_t size) const;
		MutableStringView prefixPointerInternal(char* end);
		StringView prefixPointerInternal(const char* end) const;

		/* Small string optimization. Following size restrictions from
		   StringView (which uses the top two bits for marking global and
		   null-terminated views), we can use the second highest bit of the
		   size to denote a small string. The highest bit, marked as G in the
		   below diagram, is used to preserve StringViewFlag::Global in case of
		   a nullTerminatedGlobalView() and a subsequent conversion back to a
		   StringView. In case of a large string, there's size, data pointer
		   and deleter pointer, either 24 (or 12) bytes in total. In case of a
		   small string, we can store the size only in one byte out of 8 (or
		   4), which then gives us 23 (or 11) bytes for storing the actual
		   data, excluding the null terminator that's at most 22 / 10 ASCII
		   chars. With the two topmost bits reserved, it's still 6 bits left
		   for the size, which is more than enough in this case.

		   On LE the layout is as follows (bits inside a byte are flipped for
		   clarity as well). A useful property of this layout is that the SSO
		   data are pointer-aligned as well:

			+-------------------------------+---------+
			|             string            | si | 1G |
			|              data             | ze |    |
			|             23B/11B           | 6b | 2b |
			+-------------------+-----+++++++---------+
								| LSB |||||||   MSB   |
			+---------+---------+-----+++++++---------+
			|  data   |  data   |      size      | 0G |
			| pointer | deleter |                |    |
			|  8B/4B  |  8B/4B  |  56b/24b  | 6b | 2b |
			+---------+---------+-----------+---------+

		   On BE it's like this. In this case it's however not possible to
		   have both the global/SSO bits in the same positions *and* the SSO
		   data aligned. Having consistent access to the G0/G1 bits made more
		   sense from the implementation perspective, so that won.

			+---------+-------------------------------+
			| G1 | si |             string            |
			|    | ze |              data             |
			| 2b | 6b |             23B/11B           |
			+---------+++++++-----+-------------------+
			|   MSB   ||||||| LSB |
			+---------+++++++-----+---------+---------+
			| G0 |     size       |  data   |  data   |
			|    |                | pointer | deleter |
			| 2b | 6b |  56b/24b  |  8B/4B  |  8B/4B  |
			+---------+-----------+---------+---------+

		   I originally tried storing the "small string" bit in the lowest bit
		   of the deleter pointer, but function pointers apparently can have
		   odd addresses on some platforms as well:

			http://lists.llvm.org/pipermail/llvm-dev/2018-March/121953.html

		   The above approach is consistent with StringView, which is the
		   preferrable solution after all. */
		struct Small {
#if defined(DEATH_TARGET_BIG_ENDIAN)
			unsigned char size;
			char data[Implementation::SmallStringSize];
#else
			char data[Implementation::SmallStringSize];
			unsigned char size;
#endif
		};
		struct Large {
#if defined(DEATH_TARGET_BIG_ENDIAN)
			std::size_t size;
			char* data;
			void(*deleter)(char*, std::size_t);
#else
			char* data;
			void(*deleter)(char*, std::size_t);
			std::size_t size;
#endif
		};
		union {
			Small _small;
			Large _large;
		};
	};
}