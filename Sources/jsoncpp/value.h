// Copyright 2007-2010 Baptiste Lepilleur and The JsonCpp Authors
// Distributed under MIT license, or public domain if desired and
// recognized in your jurisdiction.
// See file LICENSE for detail or copy at http://jsoncpp.sourceforge.net/LICENSE

#ifndef JSON_VALUE_H_INCLUDED
#define JSON_VALUE_H_INCLUDED

#include "forwards.h"

// Conditional NORETURN attribute on the throw functions would:
// a) suppress false positives from static code analysis
// b) possibly improve optimization opportunities.
#if !defined(JSONCPP_NORETURN)
#	if defined(_MSC_VER) && _MSC_VER == 1800
#		define JSONCPP_NORETURN __declspec(noreturn)
#	else
#		define JSONCPP_NORETURN [[noreturn]]
#	endif
#endif

// Support for '= delete' with template declarations was a late addition
// to the c++11 standard and is rejected by clang 3.8 and Apple clang 8.2
// even though these declare themselves to be c++11 compilers.
#if !defined(JSONCPP_TEMPLATE_DELETE)
#	if defined(__clang__) && defined(__apple_build_version__)
#		if __apple_build_version__ <= 8000042
#			define JSONCPP_TEMPLATE_DELETE
#		endif
#	elif defined(__clang__)
#		if __clang_major__ == 3 && __clang_minor__ <= 8
#			define JSONCPP_TEMPLATE_DELETE
#		endif
#	endif
#	if !defined(JSONCPP_TEMPLATE_DELETE)
#		define JSONCPP_TEMPLATE_DELETE = delete
#	endif
#endif

#if JSONCPP_CPLUSPLUS >= 201703L
#	define JSONCPP_HAS_STRING_VIEW 1
#endif

#include <array>
#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef JSONCPP_HAS_STRING_VIEW
#	include <string_view>
#endif

// Disable warning C4251: <data member>: <type> needs to have dll-interface to
// be used by...
#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#pragma pack(push)
#pragma pack()

/** @brief JSON (JavaScript Object Notation).
 */
namespace Json
{
	enum ErrorCode {
		SUCCESS = 0,                ///< No error
		CAPACITY,                   ///< This parser can't support a document that big
		MEMALLOC,                   ///< Error allocating memory, most likely out of memory
		TAPE_ERROR,                 ///< Something went wrong, this is a generic error
		DEPTH_ERROR,                ///< Your document exceeds the user-specified depth limitation
		STRING_ERROR,               ///< Problem while parsing a string
		T_ATOM_ERROR,               ///< Problem while parsing an atom starting with the letter 't'
		F_ATOM_ERROR,               ///< Problem while parsing an atom starting with the letter 'f'
		N_ATOM_ERROR,               ///< Problem while parsing an atom starting with the letter 'n'
		NUMBER_ERROR,               ///< Problem while parsing a number
		BIGINT_ERROR,               ///< The integer value exceeds 64 bits
		UTF8_ERROR,                 ///< the input is not valid UTF-8
		UNINITIALIZED,              ///< unknown error, or uninitialized document
		EMPTY,                      ///< no structural element found
		UNESCAPED_CHARS,            ///< found unescaped characters in a string.
		UNCLOSED_STRING,            ///< missing quote at the end
		UNSUPPORTED_ARCHITECTURE,   ///< unsupported architecture
		INCORRECT_TYPE,             ///< JSON element has a different type than user expected
		NUMBER_OUT_OF_RANGE,        ///< JSON number does not fit in 64 bits
		INDEX_OUT_OF_BOUNDS,        ///< JSON array index too large
		NO_SUCH_FIELD,              ///< JSON field not found in object
		IO_ERROR,                   ///< Error reading a file
		INVALID_JSON_POINTER,       ///< Invalid JSON pointer syntax
		INVALID_URI_FRAGMENT,       ///< Invalid URI fragment
		UNEXPECTED_ERROR,           ///< indicative of a bug in simdjson
		PARSER_IN_USE,              ///< parser is already in use.
		OUT_OF_ORDER_ITERATION,     ///< tried to iterate an array or object out of order (checked when SIMDJSON_DEVELOPMENT_CHECKS=1)
		INSUFFICIENT_PADDING,       ///< The JSON doesn't have enough padding for simdjson to safely parse it.
		INCOMPLETE_ARRAY_OR_OBJECT, ///< The document ends early.
		SCALAR_DOCUMENT_AS_VALUE,   ///< A scalar document is treated as a value.
		OUT_OF_BOUNDS,              ///< Attempted to access location outside of document.
		TRAILING_CONTENT,           ///< Unexpected trailing content in the JSON input
		NUM_ERROR_CODES
	};

#if JSON_USE_EXCEPTION
	/** Base class for all exceptions we throw.
	 *
	 * We use nothing but these internally. Of course, STL can throw others.
	 */
	class JSON_API Exception : public std::exception {
	public:
		Exception(String msg);
		~Exception() noexcept override;
		char const* what() const noexcept override;

	protected:
		String msg_;
	};

	/** Exceptions which the user cannot easily avoid.
	 *
	 * E.g. out-of-memory (when we use malloc), stack-overflow, malicious input
	 *
	 * @remark derived from Json::Exception
	 */
	class JSON_API RuntimeError : public Exception {
	public:
		RuntimeError(String const& msg);
	};

	/** Exceptions thrown by JSON_ASSERT/JSON_FAIL macros.
	 *
	 * These are precondition-violations (user bugs) and internal errors (our bugs).
	 *
	 * @remark derived from Json::Exception
	 */
	class JSON_API LogicError : public Exception {
	public:
		LogicError(String const& msg);
	};
#endif

	/// used internally
	JSONCPP_NORETURN void throwRuntimeError(StringContainer const& msg);
	/// used internally
	JSONCPP_NORETURN void throwLogicError(StringContainer const& msg);

	/** @brief Type of the value held by a Value object */
	enum ValueType {
		nullValue = 0, ///< 'null' value
		intValue,      ///< signed integer value
		uintValue,     ///< unsigned integer value
		realValue,     ///< double value
		stringValue,   ///< UTF-8 string value
		booleanValue,  ///< bool value
		arrayValue,    ///< array value (ordered list)
		objectValue    ///< object value (collection of name/value pairs).
	};

	enum CommentPlacement {
		commentBefore = 0,      ///< a comment placed on the line before a value
		commentAfterOnSameLine, ///< a comment just after a value on the same line
		commentAfter, ///< a comment on the line after a value (only make sense for root value)
		numberOfCommentPlacement
	};

	/** @brief Type of precision for formatting of real values.
	 */
	enum PrecisionType {
		significantDigits = 0, ///< we set max number of significant digits in string
		decimalPlaces          ///< we set max number of digits after "." in string
	};

	/** @brief Lightweight wrapper to tag static string.
	 *
	 * Value constructor and objectValue member assignment takes advantage of the
	 * StaticString and avoid the cost of string duplication when storing the
	 * string or the member name.
	 *
	 * Example of usage:
	 * @cpp
	 * Json::Value aValue(StaticString("some text"));
	 * Json::Value object;
	 * static const StaticString code("code");
	 * object[code] = 1234;
	 * @ce
	 */
	class JSON_API StaticString {
	public:
		explicit StaticString(const char* czstring) : c_str_(czstring) {
		}

		operator const char* () const {
			return c_str_;
		}

		const char* c_str() const {
			return c_str_;
		}

	private:
		const char* c_str_;
	};

	/** @brief Represents a <a HREF="http://www.json.org">JSON</a> value.
	 *
	 * This class is a discriminated union wrapper that can represents a:
	 * - signed integer [range: Value::minInt - Value::maxInt]
	 * - unsigned integer (range: 0 - Value::maxUInt)
	 * - double
	 * - UTF-8 string
	 * - boolean
	 * - 'null'
	 * - an ordered list of Values
	 * - collection of name/value pairs (javascript object)
	 *
	 * The type of the held value is represented by a #ValueType and
	 * can be obtained using type().
	 *
	 * Values of an #objectValue or #arrayValue can be accessed using operator[]()
	 * methods.
	 * Non-const methods will automatically create the a #nullValue element
	 * if it does not exist.
	 * The sequence of an #arrayValue will be automatically resized and initialized
	 * with #nullValue. resize() can be used to enlarge or truncate an #arrayValue.
	 *
	 * The get() methods can be used to obtain default value in the case the
	 * required element does not exist.
	 *
	 * It is possible to iterate over the list of member keys of an object using
	 * the getMemberNames() method.
	 *
	 * @note #Value string-length fit in size_t, but keys must be < 2^30.
	 * (The reason is an implementation detail.) A #CharReader will raise an
	 * exception if a bound is exceeded to avoid security holes in your app,
	 * but the Value API does *not* check bounds. That is the responsibility
	 * of the caller.
	 */
	class JSON_API Value {
		friend class ValueIteratorBase;

	public:
		using Members = std::vector<StringContainer>;
		using iterator = ValueIterator;
		using const_iterator = ValueConstIterator;
		using UInt = Json::UInt;
		using Int = Json::Int;
#if defined(JSON_HAS_INT64)
		using UInt64 = Json::UInt64;
		using Int64 = Json::Int64;
#endif // defined(JSON_HAS_INT64)
		using LargestInt = Json::LargestInt;
		using LargestUInt = Json::LargestUInt;
		using ArrayIndex = Json::ArrayIndex;

		// Required for boost integration, e.g. BOOST_TEST
		using value_type = std::string;

		// null and nullRef are deprecated, use this instead.
		static Value const& nullSingleton();

		/// Minimum signed integer value that can be stored in a Json::Value.
		static constexpr LargestInt minLargestInt = LargestInt(~(LargestUInt(-1) / 2));
		/// Maximum signed integer value that can be stored in a Json::Value.
		static constexpr LargestInt maxLargestInt = LargestInt(LargestUInt(-1) / 2);
		/// Maximum unsigned integer value that can be stored in a Json::Value.
		static constexpr LargestUInt maxLargestUInt = LargestUInt(-1);

		/// Minimum signed int value that can be stored in a Json::Value.
		static constexpr Int minInt = Int(~(UInt(-1) / 2));
		/// Maximum signed int value that can be stored in a Json::Value.
		static constexpr Int maxInt = Int(UInt(-1) / 2);
		/// Maximum unsigned int value that can be stored in a Json::Value.
		static constexpr UInt maxUInt = UInt(-1);

#if defined(JSON_HAS_INT64)
		/// Minimum signed 64 bits int value that can be stored in a Json::Value.
		static constexpr Int64 minInt64 = Int64(~(UInt64(-1) / 2));
		/// Maximum signed 64 bits int value that can be stored in a Json::Value.
		static constexpr Int64 maxInt64 = Int64(UInt64(-1) / 2);
		/// Maximum unsigned 64 bits int value that can be stored in a Json::Value.
		static constexpr UInt64 maxUInt64 = UInt64(-1);
#endif // defined(JSON_HAS_INT64)
		/// Default precision for real value for string representation.
		static constexpr UInt defaultRealPrecision = 17;
		// The constant is hard-coded because some compiler have trouble
		// converting Value::maxUInt64 to a double correctly (AIX/xlC).
		// Assumes that UInt64 is a 64 bits integer.
		static constexpr double maxUInt64AsDouble = 18446744073709551615.0;
		// Workaround for bug in the NVIDIAs CUDA 9.1 nvcc compiler
		// when using gcc and clang backend compilers.  CZString
		// cannot be defined as private. See issue #486
#ifdef __NVCC__
	public:
#else
	private:
#endif
#ifndef DOXYGEN_GENERATING_OUTPUT
		class CZString {
		public:
			enum DuplicationPolicy {
				noDuplication = 0, duplicate, duplicateOnCopy
			};
			CZString(ArrayIndex index);
			CZString(char const* str, unsigned length, DuplicationPolicy allocate);
			CZString(CZString const& other);
			CZString(CZString&& other) noexcept;
			~CZString();
			CZString& operator=(const CZString& other);
			CZString& operator=(CZString&& other) noexcept;

			bool operator<(CZString const& other) const;
			bool operator==(CZString const& other) const;
			ArrayIndex index() const;
			// const char* c_str() const; ///< \deprecated
			char const* data() const;
			unsigned length() const;
			bool isStaticString() const;

		private:
			void swap(CZString& other);

			struct StringStorage {
				unsigned policy_ : 2;
				unsigned length_ : 30; // 1GB max
			};

			char const* cstr_; // actually, a prefixed string, unless policy is noDup
			union {
				ArrayIndex index_;
				StringStorage storage_;
			};
		};

	public:
		typedef std::map<CZString, Value> ObjectValues;
#endif // ifndef DOXYGEN_GENERATING_OUTPUT

	public:
		/**
		 * @brief Create a default Value of the given type.
		 *
		 * This is a very useful constructor.
		 * To create an empty array, pass arrayValue.
		 * To create an empty object, pass objectValue.
		 * Another Value can then be set to this one by assignment.
		 * This is useful since clear() and resize() will not alter types.
		 *
		 * Examples:
		 *   \code
		 *   Json::Value null_value; // null
		 *   Json::Value arr_value(Json::arrayValue); // []
		 *   Json::Value obj_value(Json::objectValue); // {}
		 *   \endcode
		 */
		Value(ValueType type = nullValue);
		Value(Int value);
		Value(UInt value);
#if defined(JSON_HAS_INT64)
		Value(Int64 value);
		Value(UInt64 value);
#endif // if defined(JSON_HAS_INT64)
		Value(double value);
		Value(const char* value); ///< Copy til first 0. (NULL causes to seg-fault.)
		Value(const char* begin, const char* end); ///< Copy all, incl zeroes.
		/**
		 * @brief Constructs a value from a static string.
		 *
		 * Like other value string constructor but do not duplicate the string for
		 * internal storage. The given string must remain alive after the call to
		 * this constructor.
		 *
		 * @note This works only for null-terminated strings. (We cannot change the
		 * size of this class, so we have nowhere to store the length, which might be
		 * computed later for various operations.)
		 *
		 * Example of usage:
		 *   \code
		 *   static StaticString foo("some text");
		 *   Json::Value aValue(foo);
		 *   \endcode
		 */
		Value(const StaticString& value);
		Value(const StringContainer& value);
#ifdef JSONCPP_HAS_STRING_VIEW
		Value(std::string_view value);
#endif
		Value(bool value);
		Value(std::nullptr_t ptr) = delete;
		Value(const Value& other);
		Value(Value&& other) noexcept;
		~Value();

		/// @note Overwrite existing comments. To preserve comments, use
		/// #swapPayload().
		Value& operator=(const Value& other);
		Value& operator=(Value&& other) noexcept;

		/// Swap everything.
		void swap(Value& other);
		/// Swap values but leave comments and source offsets in place.
		void swapPayload(Value& other);

		/// copy everything.
		void copy(const Value& other);
		/// copy values but leave comments and source offsets in place.
		void copyPayload(const Value& other);

		ValueType type() const;

		/// Compare payload only, not comments etc.
		bool operator<(const Value& other) const;
		bool operator<=(const Value& other) const;
		bool operator>=(const Value& other) const;
		bool operator>(const Value& other) const;
		bool operator==(const Value& other) const;
		bool operator!=(const Value& other) const;
		int compare(const Value& other) const;

		const char* asCString() const; ///< Embedded zeroes could cause you trouble!
#if JSONCPP_USE_SECURE_MEMORY
		unsigned getCStringLength() const; // Allows you to understand the length of the CString
#endif
		StringContainer asString() const; ///< Embedded zeroes are possible.
		/** Get raw char* of string-value.
		 *  \return false if !string. (Seg-fault if str or end are NULL.)
		 */
		bool getString(char const** begin, char const** end) const;
#ifdef JSONCPP_HAS_STRING_VIEW
		/** Get string_view of string-value.
		 *  \return false if !string. (Seg-fault if str is NULL.)
		 */
		ErrorCode get(std::string_view& str) const;
#endif
		Int asInt() const;
		UInt asUInt() const;
#if defined(JSON_HAS_INT64)
		ErrorCode get(Value::Int64& value) const;
		Int64 asInt64() const;
		UInt64 asUInt64() const;
#endif // if defined(JSON_HAS_INT64)
		LargestInt asLargestInt() const;
		LargestUInt asLargestUInt() const;
		ErrorCode get(double& value) const;
		double asDouble() const;
		float asFloat() const;
		ErrorCode get(bool& value) const;
		bool asBool() const;

		bool isNull() const;
		bool isBool() const;
		bool isInt() const;
		bool isInt64() const;
		bool isUInt() const;
		bool isUInt64() const;
		bool isIntegral() const;
		bool isDouble() const;
		bool isNumeric() const;
		bool isString() const;
		bool isArray() const;
		bool isObject() const;

		/// The `as<T>` and `is<T>` member function templates and specializations.
		template<typename T> T as() const JSONCPP_TEMPLATE_DELETE;
		template<typename T> bool is() const JSONCPP_TEMPLATE_DELETE;

		bool isConvertibleTo(ValueType other) const;

		/// Number of values in array or object
		ArrayIndex size() const;

		/// @brief Return true if empty array, empty object, or null;
		/// otherwise, false.
		bool empty() const;

		/// Return !isNull()
		explicit operator bool() const;

		/// Remove all object members and array elements.
		/// \pre type() is arrayValue, objectValue, or nullValue
		/// \post type() is unchanged
		void clear();

		/// Resize the array to newSize elements.
		/// New elements are initialized to null.
		/// May only be called on nullValue or arrayValue.
		/// \pre type() is arrayValue or nullValue
		/// \post type() is arrayValue
		void resize(ArrayIndex newSize);

		///@{
		/// Access an array element (zero based index). If the array contains less
		/// than index element, then null value are inserted in the array so that
		/// its size is index+1.
		/// (You may need to say 'value[0u]' to get your compiler to distinguish
		/// this from the operator[] which takes a string.)
		Value& operator[](ArrayIndex index);
		Value& operator[](int index);
		///@}

		///@{
		/// Access an array element (zero based index).
		/// (You may need to say 'value[0u]' to get your compiler to distinguish
		/// this from the operator[] which takes a string.)
		const Value& operator[](ArrayIndex index) const;
		const Value& operator[](int index) const;
		///@}

		/// If the array contains at least index+1 elements, returns the element
		/// value, otherwise returns defaultValue.
		Value get(ArrayIndex index, const Value& defaultValue) const;
		/// Return true if index < size().
		bool isValidIndex(ArrayIndex index) const;
		/// @brief Append value to array at the end.
		///
		/// Equivalent to jsonvalue[jsonvalue.size()] = value;
		Value& append(const Value& value);
		Value& append(Value&& value);

		/// @brief Insert value in array at specific index
		bool insert(ArrayIndex index, const Value& newValue);
		bool insert(ArrayIndex index, Value&& newValue);

#ifdef JSONCPP_HAS_STRING_VIEW
		/// Access an object value by name, create a null member if it does not exist.
		/// \param key may contain embedded nulls.
		Value& operator[](std::string_view key);
		/// Access an object value by name, returns null if there is no member with
		/// that name.
		/// \param key may contain embedded nulls.
		const Value& operator[](std::string_view key) const;
#else
		/// Access an object value by name, create a null member if it does not exist.
		/// @note Because of our implementation, keys are limited to 2^30 -1 chars.
		/// Exceeding that will cause an exception.
		Value& operator[](const char* key);
		/// Access an object value by name, returns null if there is no member with
		/// that name.
		const Value& operator[](const char* key) const;
		/// Access an object value by name, create a null member if it does not exist.
		/// \param key may contain embedded nulls.
		Value& operator[](const StringContainer& key);
		/// Access an object value by name, returns null if there is no member with
		/// that name.
		/// \param key may contain embedded nulls.
		const Value& operator[](const StringContainer& key) const;
#endif
		/** @brief Access an object value by name, create a null member if it does not
		 * exist.
		 *
		 * If the object has no entry for that name, then the member name used to
		 * store the new entry is not duplicated.
		 * Example of use:
		 *   \code
		 *   Json::Value object;
		 *   static const StaticString code("code");
		 *   object[code] = 1234;
		 *   \endcode
		 */
		Value& operator[](const StaticString& key);
#ifdef JSONCPP_HAS_STRING_VIEW
		/// Return the member named key if it exist, defaultValue otherwise.
		/// @note deep copy
		Value get(std::string_view key, const Value& defaultValue) const;
#else
		/// Return the member named key if it exist, defaultValue otherwise.
		/// @note deep copy
		Value get(const char* key, const Value& defaultValue) const;
		/// Return the member named key if it exist, defaultValue otherwise.
		/// @note deep copy
		/// \param key may contain embedded nulls.
		Value get(const StringContainer& key, const Value& defaultValue) const;
#endif
		/// Return the member named key if it exist, defaultValue otherwise.
		/// @note deep copy
		/// @note key may contain embedded nulls.
		Value get(const char* begin, const char* end, const Value& defaultValue) const;
		/// Most general and efficient version of isMember()const, get()const, and operator[]const
		/// @note As stated elsewhere, behavior is undefined if (end-begin) >= 2^30
		Value const* find(char const* begin, char const* end) const;
		/// Most general and efficient version of isMember()const, get()const,
		/// and operator[]const
		Value const* find(const StringContainer& key) const;

		/// Calls find and only returns a valid pointer if the type is found
		template <typename T, bool (T::* TMemFn)() const>
		Value const* findValue(const StringContainer& key) const {
			Value const* found = find(key);
			if (!found || !(found->*TMemFn)())
				return nullptr;
			return found;
		}

		Value const* findNull(const StringContainer& key) const;
		Value const* findBool(const StringContainer& key) const;
		Value const* findInt(const StringContainer& key) const;
		Value const* findInt64(const StringContainer& key) const;
		Value const* findUInt(const StringContainer& key) const;
		Value const* findUInt64(const StringContainer& key) const;
		Value const* findIntegral(const StringContainer& key) const;
		Value const* findDouble(const StringContainer& key) const;
		Value const* findNumeric(const StringContainer& key) const;
		Value const* findString(const StringContainer& key) const;
		Value const* findArray(const StringContainer& key) const;
		Value const* findObject(const StringContainer& key) const;

		/// Most general and efficient version of object-mutators.
		/// @note As stated elsewhere, behavior is undefined if (end-begin) >= 2^30
		/// \return non-zero, but JSON_ASSERT if this is neither object nor nullValue.
		Value* demand(char const* begin, char const* end);
		/// @brief Remove and return the named member.
		///
		/// Do nothing if it did not exist.
		/// \pre type() is objectValue or nullValue
		/// \post type() is unchanged
#if JSONCPP_HAS_STRING_VIEW
		void removeMember(std::string_view key);
#else
		void removeMember(const char* key);
		/// Same as removeMember(const char*)
		/// \param key may contain embedded nulls.
		void removeMember(const StringContainer& key);
#endif
		/** @brief Remove the named map member.
		 *
		 *  Update 'removed' iff removed.
		 *  \param key may contain embedded nulls.
		 *  \return true iff removed (no exceptions)
		 */
#if JSONCPP_HAS_STRING_VIEW
		bool removeMember(std::string_view key, Value* removed);
#else
		bool removeMember(String const& key, Value* removed);
		/// Same as removeMember(const char* begin, const char* end, Value* removed),
		/// but 'key' is null-terminated.
		bool removeMember(const char* key, Value* removed);
#endif
		/// Same as removeMember(String const& key, Value* removed)
		bool removeMember(const char* begin, const char* end, Value* removed);
		/** @brief Remove the indexed array element.
		 *
		 *  O(n) expensive operations.
		 *  Update 'removed' iff removed.
		 *  \return true if removed (no exceptions)
		 */
		bool removeIndex(ArrayIndex index, Value* removed);

#ifdef JSONCPP_HAS_STRING_VIEW
		/// Return true if the object has a member named key.
		/// \param key may contain embedded nulls.
		bool isMember(std::string_view key) const;
#else
		/// Return true if the object has a member named key.
		/// @note 'key' must be null-terminated.
		bool isMember(const char* key) const;
		/// Return true if the object has a member named key.
		/// \param key may contain embedded nulls.
		bool isMember(const StringContainer& key) const;
#endif
		/// Same as isMember(String const& key)const
		bool isMember(const char* begin, const char* end) const;

		/// @brief Return a list of the member names.
		///
		/// If null, return an empty list.
		/// \pre type() is objectValue or nullValue
		/// \post if type() was nullValue, it remains nullValue
		Members getMemberNames() const;

		size_t getMemberCount() const;

		/// Comments must be //... or /* ... */
		void setComment(const char* comment, size_t len, CommentPlacement placement) {
			setComment(StringContainer(comment, len), placement);
		}
		/// Comments must be //... or /* ... */
		void setComment(StringContainer comment, CommentPlacement placement);
		bool hasComment() const;
		bool hasComment(CommentPlacement placement) const;
		/// Include delimiters and embedded newlines.
		StringContainer getComment(CommentPlacement placement) const;

		StringContainer toStyledString() const;

		const_iterator begin() const;
		const_iterator end() const;

		iterator begin();
		iterator end();

		/// @brief Returns a reference to the first element in the `Value`.
		/// Requires that this value holds an array or json object, with at least one
		/// element.
		const Value& front() const;

		/// @brief Returns a reference to the first element in the `Value`.
		/// Requires that this value holds an array or json object, with at least one
		/// element.
		Value& front();

		/// @brief Returns a reference to the last element in the `Value`.
		/// Requires that value holds an array or json object, with at least one
		/// element.
		const Value& back() const;

		/// @brief Returns a reference to the last element in the `Value`.
		/// Requires that this value holds an array or json object, with at least one
		/// element.
		Value& back();

		// Accessors for the [start, limit) range of bytes within the JSON text from
		// which this value was parsed, if any.
		void setOffsetStart(ptrdiff_t start);
		void setOffsetLimit(ptrdiff_t limit);
		ptrdiff_t getOffsetStart() const;
		ptrdiff_t getOffsetLimit() const;

	private:
		void setType(ValueType v) {
			bits_.value_type_ = static_cast<unsigned char>(v);
		}
		bool isAllocated() const {
			return bits_.allocated_;
		}
		void setIsAllocated(bool v) {
			bits_.allocated_ = v;
		}

		void initBasic(ValueType type, bool allocated = false);
		void dupPayload(const Value& other);
		void releasePayload();
		void dupMeta(const Value& other);

		Value& resolveReference(const char* key);
		Value& resolveReference(const char* key, const char* end);

		// struct MemberNamesTransform
		//{
		//   typedef const char *result_type;
		//   const char *operator()( const CZString &name ) const
		//   {
		//      return name.c_str();
		//   }
		//};

		union ValueHolder {
			LargestInt int_;
			LargestUInt uint_;
			double real_;
			bool bool_;
			char* string_; // if allocated_, ptr to { unsigned, char[] }.
			ObjectValues* map_;
		} value_;

		struct {
			// Really a ValueType, but types should agree for bitfield packing.
			unsigned int value_type_ : 8;
			// Unless allocated_, string_ must be null-terminated.
			unsigned int allocated_ : 1;
		} bits_;

		class Comments {
		public:
			Comments() = default;
			Comments(const Comments& that);
			Comments(Comments&& that) noexcept;
			Comments& operator=(const Comments& that);
			Comments& operator=(Comments&& that) noexcept;
			bool has(CommentPlacement slot) const;
			bool hasAny() const;
			StringContainer get(CommentPlacement slot) const;
			void set(CommentPlacement slot, StringContainer comment);

		private:
			using Array = std::array<StringContainer, numberOfCommentPlacement>;
			std::unique_ptr<Array> ptr_;
		};
		Comments comments_;

		// [start, limit) byte offsets in the source JSON text from which this Value
		// was extracted.
		ptrdiff_t start_;
		ptrdiff_t limit_;
	};

	template<> inline bool Value::as<bool>() const {
		return asBool();
	}
	template<> inline bool Value::is<bool>() const {
		return isBool();
	}

	template<> inline Int Value::as<Int>() const {
		return asInt();
	}
	template<> inline bool Value::is<Int>() const {
		return isInt();
	}

	template<> inline UInt Value::as<UInt>() const {
		return asUInt();
	}
	template<> inline bool Value::is<UInt>() const {
		return isUInt();
	}

#if defined(JSON_HAS_INT64)
	template<> inline Int64 Value::as<Int64>() const {
		return asInt64();
	}
	template<> inline bool Value::is<Int64>() const {
		return isInt64();
	}

	template<> inline UInt64 Value::as<UInt64>() const {
		return asUInt64();
	}
	template<> inline bool Value::is<UInt64>() const {
		return isUInt64();
	}
#endif

	template<> inline double Value::as<double>() const {
		return asDouble();
	}
	template<> inline bool Value::is<double>() const {
		return isDouble();
	}

	template<> inline StringContainer Value::as<StringContainer>() const {
		return asString();
	}
	template<> inline bool Value::is<StringContainer>() const {
		return isString();
	}

	/// These `as` specializations are type conversions, and do not have a
	/// corresponding `is`.
	template<> inline float Value::as<float>() const {
		return asFloat();
	}
	template<> inline const char* Value::as<const char*>() const {
		return asCString();
	}

	/** @brief Experimental and untested: represents an element of the "path" to
	 * access a node.
	 */
	class JSON_API PathArgument {
	public:
		friend class Path;

		PathArgument();
		PathArgument(ArrayIndex index);
		PathArgument(const char* key);
		PathArgument(StringContainer key);

	private:
		enum Kind {
			kindNone = 0, kindIndex, kindKey
		};
		StringContainer key_;
		ArrayIndex index_{};
		Kind kind_{kindNone};
	};

	/** @brief Experimental and untested: represents a "path" to access a node.
	 *
	 * Syntax:
	 * - "." => root node
	 * - ".[n]" => elements at index 'n' of root node (an array value)
	 * - ".name" => member named 'name' of root node (an object value)
	 * - ".name1.name2.name3"
	 * - ".[0][1][2].name1[3]"
	 * - ".%" => member name is provided as parameter
	 * - ".[%]" => index is provided as parameter
	 */
	class JSON_API Path {
	public:
		Path(const StringContainer& path, const PathArgument& a1 = {},
			 const PathArgument& a2 = {},
			 const PathArgument& a3 = {},
			 const PathArgument& a4 = {},
			 const PathArgument& a5 = {});

		const Value& resolve(const Value& root) const;
		Value resolve(const Value& root, const Value& defaultValue) const;
		/// Creates the "path" to access the specified node and returns a reference on the node.
		Value& make(Value& root) const;

	private:
		using InArgs = std::vector<const PathArgument*>;
		using Args = std::vector<PathArgument>;

		void makePath(const StringContainer& path, const InArgs& in);
		void addPathInArg(const StringContainer& path, const InArgs& in, InArgs::const_iterator& itInArg, PathArgument::Kind kind);
		static void invalidPath(const StringContainer& path, int location);

		Args args_;
	};

	/** @brief base class for Value iterators. */
	class JSON_API ValueIteratorBase {
	public:
		using iterator_category = std::bidirectional_iterator_tag;
		using size_t = unsigned int;
		using difference_type = int;
		using SelfType = ValueIteratorBase;

		bool operator==(const SelfType& other) const {
			return isEqual(other);
		}

		bool operator!=(const SelfType& other) const {
			return !isEqual(other);
		}

		difference_type operator-(const SelfType& other) const {
			return other.computeDistance(*this);
		}

		/// Return either the index or the member name of the referenced value as a
		/// Value.
		Value key() const;

		/// Return the index of the referenced Value, or -1 if it is not an
		/// arrayValue.
		UInt index() const;

		/// Return the member name of the referenced Value, or "" if it is not an objectValue.
		/// @note Avoid `c_str()` on result, as embedded zeroes are possible.
#ifdef JSONCPP_HAS_STRING_VIEW
		std::string_view name() const;
#else
		StringContainer name() const;
#endif

#ifdef JSONCPP_HAS_STRING_VIEW
		std::string_view memberName() const;
#endif

		/// Return the member name of the referenced Value, or NULL if it is not an
		/// objectValue.
		/// @note Better version than memberName(). Allows embedded nulls.
		char const* memberName(char const** end) const;

	protected:
		/*! Internal utility functions to assist with implementing
		 *   other iterator functions. The const and non-const versions
		 *   of the "deref" protected methods expose the protected
		 *   current_ member variable in a way that can often be
		 *   optimized away by the compiler.
		 */
		const Value& deref() const;
		Value& deref();

		void increment();

		void decrement();

		difference_type computeDistance(const SelfType& other) const;

		bool isEqual(const SelfType& other) const;

		void copy(const SelfType& other);

	private:
		Value::ObjectValues::iterator current_;
		// Indicates that iterator is for a null value.
		bool isNull_{true};

	public:
		// For some reason, BORLAND needs these at the end, rather
		// than earlier. No idea why.
		ValueIteratorBase();
		explicit ValueIteratorBase(const Value::ObjectValues::iterator& current);
	};

	/** @brief const iterator for object and array value.
	 *
	 */
	class JSON_API ValueConstIterator : public ValueIteratorBase {
		friend class Value;

	public:
		using value_type = const Value;
		// typedef unsigned int size_t;
		// typedef int difference_type;
		using reference = const Value&;
		using pointer = const Value*;
		using SelfType = ValueConstIterator;

		ValueConstIterator();
		ValueConstIterator(ValueIterator const& other);

	private:
		/*! \internal Use by Value to create an iterator.
		 */
		explicit ValueConstIterator(const Value::ObjectValues::iterator& current);

	public:
		SelfType& operator=(const ValueIteratorBase& other);

		SelfType operator++(int) {
			SelfType temp(*this);
			++*this;
			return temp;
		}

		SelfType operator--(int) {
			SelfType temp(*this);
			--*this;
			return temp;
		}

		SelfType& operator--() {
			decrement();
			return *this;
		}

		SelfType& operator++() {
			increment();
			return *this;
		}

		reference operator*() const {
			return deref();
		}

		pointer operator->() const {
			return &deref();
		}
	};

	/** @brief Iterator for object and array value.
	 */
	class JSON_API ValueIterator : public ValueIteratorBase {
		friend class Value;

	public:
		using value_type = Value;
		using size_t = unsigned int;
		using difference_type = int;
		using reference = Value&;
		using pointer = Value*;
		using SelfType = ValueIterator;

		ValueIterator();
		explicit ValueIterator(const ValueConstIterator& other);
		ValueIterator(const ValueIterator& other);

	private:
		/*! \internal Use by Value to create an iterator.
		 */
		explicit ValueIterator(const Value::ObjectValues::iterator& current);

	public:
		SelfType& operator=(const SelfType& other);

		SelfType operator++(int) {
			SelfType temp(*this);
			++*this;
			return temp;
		}

		SelfType operator--(int) {
			SelfType temp(*this);
			--*this;
			return temp;
		}

		SelfType& operator--() {
			decrement();
			return *this;
		}

		SelfType& operator++() {
			increment();
			return *this;
		}

		/*! The return value of non-const iterators can be
		 *  changed, so the these functions are not const
		 *  because the returned references/pointers can be used
		 *  to change state of the base class.
		 */
		reference operator*() const {
			return const_cast<reference>(deref());
		}
		pointer operator->() const {
			return const_cast<pointer>(&deref());
		}
	};

	inline void swap(Value& a, Value& b) {
		a.swap(b);
	}

	inline const Value& Value::front() const {
		return *begin();
	}

	inline Value& Value::front() {
		return *begin();
	}

	inline const Value& Value::back() const {
		return *(--end());
	}

	inline Value& Value::back() {
		return *(--end());
	}

} // namespace Json

#pragma pack(pop)

#if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)
#pragma warning(pop)
#endif // if defined(JSONCPP_DISABLE_DLL_INTERFACE_WARNING)

#endif // JSON_H_INCLUDED
