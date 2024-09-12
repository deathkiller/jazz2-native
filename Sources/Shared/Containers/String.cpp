#include "String.h"
#include "Array.h"
#include "Pair.h"
#include "StaticArray.h"

#include <cstring>

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace
	{
		enum : std::size_t {
			SmallSizeMask = 0xc0,
			LargeSizeMask = SmallSizeMask << (sizeof(std::size_t) - 1) * 8
		};
	}

	static_assert(std::size_t(LargeSizeMask) == Implementation::StringViewSizeMask,
		"Reserved bits should be the same in String and StringView");
	static_assert(std::size_t(LargeSizeMask) == (std::size_t(StringViewFlags::Global) | (std::size_t(Implementation::SmallStringBit) << (sizeof(std::size_t) - 1) * 8)),
		"Small string and global view bits should cover both reserved bits");

	String String::nullTerminatedView(StringView view) {
		if ((view.flags() & StringViewFlags::NullTerminated) == StringViewFlags::NullTerminated) {
			String out { view.data(), view.size(), [](char*, std::size_t) { } };
			out._large.size |= std::size_t(view.flags() & StringViewFlags::Global);
			return out;
		}
		return String{view};
	}

	String String::nullTerminatedView(AllocatedInitT, StringView view) {
		if ((view.flags() & StringViewFlags::NullTerminated) == StringViewFlags::NullTerminated) {
			String out { view.data(), view.size(), [](char*, std::size_t) { } };
			out._large.size |= std::size_t(view.flags() & StringViewFlags::Global);
			return out;
		}
		return String{AllocatedInit, view};
	}

	String String::nullTerminatedGlobalView(StringView view) {
		if ((view.flags() & (StringViewFlags::NullTerminated | StringViewFlags::Global)) == (StringViewFlags::NullTerminated | StringViewFlags::Global)) {
			String out { view.data(), view.size(), [](char*, std::size_t) { } };
			out._large.size |= std::size_t(StringViewFlags::Global);
			return out;
		}
		return String{view};
	}

	String String::nullTerminatedGlobalView(AllocatedInitT, StringView view) {
		if ((view.flags() & (StringViewFlags::NullTerminated | StringViewFlags::Global)) == (StringViewFlags::NullTerminated | StringViewFlags::Global)) {
			String out { view.data(), view.size(), [](char*, std::size_t) { } };
			out._large.size |= std::size_t(StringViewFlags::Global);
			return out;
		}
		return String{AllocatedInit, view};
	}

	inline void String::construct(NoInitT, const std::size_t size) {
		if (size < Implementation::SmallStringSize) {
			_small.data[size] = '\0';
			_small.size = (unsigned char)(size | Implementation::SmallStringBit);
		} else {
			_large.data = new char[size + 1];
			_large.data[size] = '\0';
			_large.size = size;
			_large.deleter = nullptr;
		}
	}

	inline void String::construct(const char* const data, const std::size_t size) {
		construct(NoInit, size);

		// If the size is small enough for SSO, use that. Not using <= because we need to store the null terminator as well.
		if (size < Implementation::SmallStringSize) {
			// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
			if (size != 0) std::memcpy(_small.data, data, size);
		} else {
			// Otherwise allocate. Assuming the size is small enough -- this should have been checked in the caller already.
			std::memcpy(_large.data, data, size);
		}
	}

	inline void String::destruct() {
		// If not SSO, delete the data
		if (_small.size & Implementation::SmallStringBit) return;
		// Instances created with a custom deleter either don't the Global bit set at all, or have it set but the deleter
		// is a no-op passed from nullTerminatedView() / nullTerminatedGlobalView(). Thus *technically* it's not needed to clear
		// the LargeSizeMask (which implies there's also no way to test that it got cleared), but do it for consistency.
		if (_large.deleter) _large.deleter(_large.data, _large.size & ~LargeSizeMask);
		else delete[] _large.data;
	}

	inline Pair<const char*, std::size_t> String::dataInternal() const {
		if (_small.size & Implementation::SmallStringBit)
			return { _small.data, _small.size & ~SmallSizeMask };
		return { _large.data, _large.size & ~LargeSizeMask };
	}

	String::String() noexcept {
		// Create a zero-size small string to fullfil the guarantee of data() being always non-null and null-terminated
		_small.data[0] = '\0';
		_small.size = Implementation::SmallStringBit;
	}

	String::String(const StringView view) : String{view._data, view._sizePlusFlags & ~Implementation::StringViewSizeMask} {}

	String::String(const MutableStringView view) : String{view._data, view._sizePlusFlags & ~Implementation::StringViewSizeMask} {}

	String::String(const ArrayView<const char> view) : String{view.data(), view.size()} {}

	String::String(const ArrayView<char> view) : String{view.data(), view.size()} {}

	String::String(std::nullptr_t, std::nullptr_t, std::nullptr_t, const char* const data) : String{data, data ? std::strlen(data) : 0} {}

	String::String(const char* const data, const std::size_t size)
		: _large{}
	{
#if defined(DEATH_TARGET_32BIT)
		// Compared to StringView construction which happens a lot this shouldn't, and the chance of strings > 1 GB on 32-bit
		// is rare but possible and thus worth checking even in release
		DEATH_ASSERT(size < std::size_t{1} << (sizeof(std::size_t) * 8 - 2), ("String expected to be smaller than 2^%zu bytes, got %zu", sizeof(std::size_t) * 8 - 2, size), );
#endif
		DEATH_ASSERT(data != nullptr || size == 0, ("Received a null string of size %zu", size), );

		construct(data, size);
	}

	String::String(AllocatedInitT, const StringView view) : String{AllocatedInit, view._data, view._sizePlusFlags & ~Implementation::StringViewSizeMask} {}

	String::String(AllocatedInitT, const MutableStringView view) : String{AllocatedInit, view._data, view._sizePlusFlags & ~Implementation::StringViewSizeMask} {}

	String::String(AllocatedInitT, const ArrayView<const char> view) : String{AllocatedInit, view.data(), view.size()} {}

	String::String(AllocatedInitT, const ArrayView<char> view) : String{AllocatedInit, view.data(), view.size()} {}

	String::String(AllocatedInitT, const char* const data) : String{AllocatedInit, data, data ? std::strlen(data) : 0} {}

	String::String(AllocatedInitT, const char* const data, const std::size_t size)
		: _large{}
	{
		// Compared to StringView construction which happens a lot this shouldn't, and the chance of strings > 1 GB on 32-bit
		// is rare but possible and thus worth checking even in release
		DEATH_ASSERT(size < std::size_t{1} << (sizeof(std::size_t) * 8 - 2), ("String expected to be smaller than 2^%zu bytes, got %zu", sizeof(std::size_t) * 8 - 2, size), );
		DEATH_ASSERT(data != nullptr || size == 0, ("Received a null string of size %zu", size), );

		_large.data = new char[size + 1];
		// Apparently memcpy() can't be called with null pointers, even if size is zero. I call that bullying.
		if (size != 0) std::memcpy(_large.data, data, size);
		_large.data[size] = '\0';
		_large.size = size;
		_large.deleter = nullptr;
	}

	String::String(AllocatedInitT, String&& other)
	{
		// Allocate a copy if the other is a SSO
		if (other.isSmall()) {
			const std::size_t sizePlusOne = (other._small.size & ~SmallSizeMask) + 1;
			_large.data = new char[sizePlusOne];
			// Copies also the null terminator
			std::memcpy(_large.data, other._small.data, sizePlusOne);
			_large.size = other._small.size & ~SmallSizeMask;
			_large.deleter = nullptr;
		} else {
			// Otherwise take over the data
			_large.data = other._large.data;
			_large.size = other._large.size; // including the potential Global bit
			_large.deleter = other._large.deleter;
		}

		// Move-out the other instance in both cases
		other._large.data = nullptr;
		other._large.size = 0;
		other._large.deleter = nullptr;
	}

	String::String(AllocatedInitT, const String& other)
	{
		const Pair<const char*, std::size_t> data = other.dataInternal();
		const std::size_t sizePlusOne = data.second() + 1;
		_large.size = data.second();
		_large.data = new char[sizePlusOne];
		// Copies also the null terminator
		std::memcpy(_large.data, data.first(), sizePlusOne);
		_large.deleter = nullptr;
	}

	String::String(char* const data, const std::size_t size, void(*deleter)(char*, std::size_t)) noexcept
		: _large{}
	{
		// Compared to StringView construction which happens a lot this shouldn't, the chance of strings > 1 GB on 32-bit
		// is rare but possible and thus worth checking even in release; but most importantly checking for null
		// termination outweighs potential speed issues
		DEATH_ASSERT(size < std::size_t{1} << (sizeof(std::size_t) * 8 - 2), ("String expected to be smaller than 2^%zu bytes, got %zu", sizeof(std::size_t) * 8 - 2, size), );
		DEATH_ASSERT(data != nullptr && !data[size], "Can only take ownership of a non-null null-terminated array", );

		_large.data = data;
		_large.size = size;
		_large.deleter = deleter;
	}

	String::String(void(*deleter)(char*, std::size_t), std::nullptr_t, char* const data) noexcept : String{
		data,
		// If data is null, strlen() would crash before reaching our assert inside the delegated-to constructor
		data ? std::strlen(data) : 0,
		deleter
	} {}

	String::String(ValueInitT, const std::size_t size)
		: _large{}
	{
		// Compared to StringView construction which happens a lot this shouldn't, and the chance of strings > 1 GB on 32-bit
		// is rare but possible and thus  worth checking even in release
		DEATH_ASSERT(size < std::size_t{1} << (sizeof(std::size_t) * 8 - 2), ("String expected to be smaller than 2^%zu bytes, got %zu", sizeof(std::size_t) * 8 - 2, size), );

		if (size < Implementation::SmallStringSize) {
			// Everything already zero-init'd in the constructor init list
			_small.size = (unsigned char)(size | Implementation::SmallStringBit);
		} else {
			_large.data = new char[size + 1] {};
			_large.size = size;
			_large.deleter = nullptr;
		}
	}

	String::String(NoInitT, const std::size_t size)
	{
		// Compared to StringView construction which happens a lot this shouldn't, and the chance of strings > 1 GB on 32-bit
		// is rare but possible and thus worth checking even in release
		DEATH_ASSERT(size < std::size_t{1} << (sizeof(std::size_t) * 8 - 2), ("String expected to be smaller than 2^%zu bytes, got %zu", sizeof(std::size_t) * 8 - 2, size), );

		construct(NoInit, size);
	}

	String::String(DirectInitT, const std::size_t size, const char c) : String{NoInit, size} {
		std::memset(size < Implementation::SmallStringSize ? _small.data : _large.data, c, size);
	}

	String::~String() {
		destruct();
	}

	inline void String::copyConstruct(const String& other) {
		// For predictability, in particular preserving the AllocatedInit aspect of a string in copies and not just in moves,
		// if the original string is allocated, the copied one is as well, independently of the actual size.
		if (other.isSmall()) {
			std::memcpy(_small.data, other._small.data, Implementation::SmallStringSize);
			_small.size = other._small.size;
		} else {
			// Excluding the potential Global bit
			const std::size_t size = other._large.size & ~LargeSizeMask;
			_large.data = new char[size + 1];
			// Copies also the null terminator
			std::memcpy(_large.data, other._large.data, size + 1);
			_large.size = size;
			_large.deleter = nullptr;
		}
	}

	String::String(const String& other) {
		copyConstruct(other);
	}

	String::String(String&& other) noexcept {
		// Similarly as in operator=(String&&), the following works also in case of SSO, as for small string we would be
		// doing a copy of _small.data and then also a copy of _small.size *including* the two highest bits
		_large.data = other._large.data;
		_large.size = other._large.size; // including the potential Global bit
		_large.deleter = other._large.deleter;
		other._large.data = nullptr;
		other._large.size = 0;
		other._large.deleter = nullptr;
	}

	String& String::operator=(const String& other) {
		destruct();
		copyConstruct(other);
		return *this;
	}

	String& String::operator=(String&& other) noexcept {
		using std::swap;
		swap(other._large.data, _large.data);
		swap(other._large.size, _large.size); // including the potential Global bit
		swap(other._large.deleter, _large.deleter);
		return *this;
	}

	String::operator ArrayView<const char>() const noexcept {
		const Pair<const char*, std::size_t> data = dataInternal();
		return { data.first(), data.second() };
	}

	String::operator ArrayView<const void>() const noexcept {
		const Pair<const char*, std::size_t> data = dataInternal();
		return { data.first(), data.second() };
	}

	String::operator ArrayView<char>() noexcept {
		const Pair<const char*, std::size_t> data = dataInternal();
		return { const_cast<char*>(data.first()), data.second() };
	}

	String::operator ArrayView<void>() noexcept {
		const Pair<const char*, std::size_t> data = dataInternal();
		return { const_cast<char*>(data.first()), data.second() };
	}

	String::operator Array<char>() && {
		Array<char> out;
		if (_small.size & Implementation::SmallStringBit) {
			const std::size_t size = _small.size & ~SmallSizeMask;
			// Allocate the output including a null terminator at the end, but don't include it in the size
			out = Array<char>{Array<char>{NoInit, size + 1}.release(), size};
			out[size] = '\0';
			std::memcpy(out.data(), _small.data, size);
		} else {
			out = Array<char>{_large.data, _large.size & ~LargeSizeMask, deleter()};
		}

		// Same as in release(). Create a zero-size small string to fullfil the guarantee of data() being always non-null
		// and null-terminated. Since this makes the string switch to SSO, we also clear the deleter this way.
		_small.data[0] = '\0';
		_small.size = Implementation::SmallStringBit;

		return out;
	}

	String::operator bool() const {
		// The data pointer is guaranteed to be non-null, so no need to check it
		if (_small.size & Implementation::SmallStringBit)
			return _small.size & ~SmallSizeMask;
		return _large.size & ~LargeSizeMask;
	}

	StringViewFlags String::viewFlags() const {
		return StringViewFlags(_large.size & std::size_t(StringViewFlags::Global)) | StringViewFlags::NullTerminated;
	}

	const char* String::data() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data;
		return _large.data;
	}

	char* String::data() {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data;
		return _large.data;
	}

	bool String::empty() const {
		if (_small.size & Implementation::SmallStringBit)
			return !(_small.size & ~SmallSizeMask);
		return !(_large.size & ~LargeSizeMask);
	}

	auto String::deleter() const -> Deleter {
		DEATH_DEBUG_ASSERT(!(_small.size & Implementation::SmallStringBit),
			"Can't call on a SSO instance", {});
		return _large.deleter;
	}

	std::size_t String::size() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.size & ~SmallSizeMask;
		return _large.size & ~LargeSizeMask;
	}

	char* String::begin() {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data;
		return _large.data;
	}

	const char* String::begin() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data;
		return _large.data;
	}

	const char* String::cbegin() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data;
		return _large.data;
	}

	char* String::end() {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data + (_small.size & ~SmallSizeMask);
		return _large.data + (_large.size & ~LargeSizeMask);
	}

	const char* String::end() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data + (_small.size & ~SmallSizeMask);
		return _large.data + (_large.size & ~LargeSizeMask);
	}

	const char* String::cend() const {
		if (_small.size & Implementation::SmallStringBit)
			return _small.data + (_small.size & ~SmallSizeMask);
		return _large.data + (_large.size & ~LargeSizeMask);
	}

	char& String::front() {
		DEATH_DEBUG_ASSERT(size(), "String is empty", *begin());
		return *begin();
	}

	char String::front() const {
		return const_cast<String&>(*this).front();
	}

	char& String::back() {
		DEATH_DEBUG_ASSERT(size(), "String is empty", *(end() - 1));
		return *(end() - 1);
	}

	char String::back() const {
		return const_cast<String&>(*this).back();
	}

	char& String::operator[](std::size_t i) {
		// Accessing the null terminator is fine
		DEATH_DEBUG_ASSERT(i < size() + 1, ("Index %zu out of range for %zu null-terminated bytes", i, size()), _small.data[0]);
		if (_small.size & Implementation::SmallStringBit)
			return _small.data[i];
		return _large.data[i];
	}

	char String::operator[](std::size_t i) const {
		// Accessing the null terminator is fine
		DEATH_DEBUG_ASSERT(i < size() + 1, ("Index %zu out of range for %zu null-terminated bytes", i, size()), _small.data[0]);
		if (_small.size & Implementation::SmallStringBit)
			return _small.data[i];
		return _large.data[i];
	}

	String String::operator+=(const StringView& other)
	{
		std::size_t otherSize = other.size();
		if (otherSize == 0) {
			return *this;
		}

		std::size_t currentSize = size();
		std::size_t newSize = currentSize + otherSize;

		if (newSize < Implementation::SmallStringSize) {
			std::memcpy(_small.data + currentSize, other._data, otherSize);
			_small.data[newSize] = '\0';
			_small.size = (unsigned char)(newSize | Implementation::SmallStringBit);
		} else if (isSmall()) {
			char tmp[Implementation::SmallStringSize];
			std::memcpy(tmp, _small.data, currentSize);
			
			construct(NoInit, newSize);

			std::memcpy(_large.data, tmp, currentSize);
			std::memcpy(_large.data + currentSize, other._data, otherSize);
		} else {
			char* oldData = _large.data;
			auto oldDeleter = _large.deleter;

			construct(NoInit, newSize);

			std::memcpy(_large.data, oldData, currentSize);
			std::memcpy(_large.data + currentSize, other._data, otherSize);

			if (oldDeleter) oldDeleter(oldData, currentSize);
			else delete[] oldData;
		}

		return *this;
	}

	MutableStringView String::slice(char* const begin, char* const end) {
		return MutableStringView{*this}.slice(begin, end);
	}

	StringView String::slice(const char* const begin, const char* const end) const {
		return StringView{*this}.slice(begin, end);
	}

	MutableStringView String::slice(const std::size_t begin, const std::size_t end) {
		return MutableStringView{*this}.slice(begin, end);
	}

	StringView String::slice(const std::size_t begin, const std::size_t end) const {
		return StringView{*this}.slice(begin, end);
	}

	MutableStringView String::sliceSizePointerInternal(char* const begin, const std::size_t size) {
		return MutableStringView{*this}.sliceSize(begin, size);
	}

	StringView String::sliceSizePointerInternal(const char* const begin, const std::size_t size) const {
		return StringView{*this}.sliceSize(begin, size);
	}

	MutableStringView String::sliceSize(const std::size_t begin, const std::size_t size) {
		return MutableStringView{*this}.sliceSize(begin, size);
	}

	StringView String::sliceSize(const std::size_t begin, const std::size_t size) const {
		return StringView{*this}.sliceSize(begin, size);
	}

	MutableStringView String::prefixPointerInternal(char* const end) {
		return MutableStringView{*this}.prefix(end);
	}

	StringView String::prefixPointerInternal(const char* const end) const {
		return StringView{*this}.prefix(end);
	}

	MutableStringView String::suffix(char* const begin) {
		return MutableStringView{*this}.suffix(begin);
	}

	StringView String::suffix(const char* const begin) const {
		return StringView{*this}.suffix(begin);
	}

	MutableStringView String::prefix(const std::size_t count) {
		return MutableStringView{*this}.prefix(count);
	}

	StringView String::prefix(const std::size_t count) const {
		return StringView{*this}.prefix(count);
	}

	MutableStringView String::exceptPrefix(const std::size_t count) {
		return MutableStringView{*this}.exceptPrefix(count);
	}

	StringView String::exceptPrefix(const std::size_t count) const {
		return StringView{*this}.exceptPrefix(count);
	}

	MutableStringView String::exceptSuffix(const std::size_t count) {
		return MutableStringView{*this}.exceptSuffix(count);
	}

	StringView String::exceptSuffix(const std::size_t count) const {
		return StringView{*this}.exceptSuffix(count);
	}

	Array<MutableStringView> String::split(const char delimiter) {
		return MutableStringView{*this}.split(delimiter);
	}

	Array<StringView> String::split(const char delimiter) const {
		return StringView{*this}.split(delimiter);
	}

	Array<MutableStringView> String::split(const StringView delimiter) {
		return MutableStringView{*this}.split(delimiter);
	}

	Array<StringView> String::split(const StringView delimiter) const {
		return StringView{*this}.split(delimiter);
	}

	Array<MutableStringView> String::splitWithoutEmptyParts(const char delimiter) {
		return MutableStringView{*this}.splitWithoutEmptyParts(delimiter);
	}

	Array<StringView> String::splitWithoutEmptyParts(const char delimiter) const {
		return StringView{*this}.splitWithoutEmptyParts(delimiter);
	}

	Array<MutableStringView> String::splitOnAnyWithoutEmptyParts(const StringView delimiters) {
		return MutableStringView{*this}.splitOnAnyWithoutEmptyParts(delimiters);
	}

	Array<StringView> String::splitOnAnyWithoutEmptyParts(const StringView delimiters) const {
		return StringView{*this}.splitOnAnyWithoutEmptyParts(delimiters);
	}

	Array<MutableStringView> String::splitOnWhitespaceWithoutEmptyParts() {
		return MutableStringView{*this}.splitOnWhitespaceWithoutEmptyParts();
	}

	Array<StringView> String::splitOnWhitespaceWithoutEmptyParts() const {
		return StringView{*this}.splitOnWhitespaceWithoutEmptyParts();
	}

	StaticArray<3, MutableStringView> String::partition(const char separator) {
		return MutableStringView{*this}.partition(separator);
	}

	StaticArray<3, StringView> String::partition(const char separator) const {
		return StringView{*this}.partition(separator);
	}

	StaticArray<3, MutableStringView> String::partition(const StringView separator) {
		return MutableStringView{*this}.partition(separator);
	}

	StaticArray<3, StringView> String::partition(const StringView separator) const {
		return StringView{*this}.partition(separator);
	}

	String String::join(const ArrayView<const StringView> strings) const {
		return StringView{*this}.join(strings);
	}

	String String::join(const std::initializer_list<StringView> strings) const {
		// Doing it this way instead of calling directly into StringView to have the above overload implicitly covered
		return join(arrayView(strings));
	}

	String String::joinWithoutEmptyParts(const ArrayView<const StringView> strings) const {
		return StringView{*this}.joinWithoutEmptyParts(strings);
	}

	String String::joinWithoutEmptyParts(const std::initializer_list<StringView> strings) const {
		// Doing it this way instead of calling directly into StringView to have the above overload implicitly covered
		return joinWithoutEmptyParts(arrayView(strings));
	}

	bool String::hasPrefix(const StringView prefix) const {
		return StringView{*this}.hasPrefix(prefix);
	}

	bool String::hasPrefix(const char prefix) const {
		return StringView{*this}.hasPrefix(prefix);
	}

	bool String::hasSuffix(const StringView suffix) const {
		return StringView{*this}.hasSuffix(suffix);
	}

	bool String::hasSuffix(const char suffix) const {
		return StringView{*this}.hasSuffix(suffix);
	}

	MutableStringView String::exceptPrefix(const StringView prefix) {
		return MutableStringView{*this}.exceptPrefix(prefix);
	}

	StringView String::exceptPrefix(const StringView prefix) const {
		return StringView{*this}.exceptPrefix(prefix);
	}

	MutableStringView String::exceptSuffix(const StringView suffix) {
		return MutableStringView{*this}.exceptSuffix(suffix);
	}

	StringView String::exceptSuffix(const StringView suffix) const {
		return StringView{*this}.exceptSuffix(suffix);
	}

	MutableStringView String::trimmed(const StringView characters) {
		return MutableStringView{*this}.trimmed(characters);
	}

	StringView String::trimmed(const StringView characters) const {
		return StringView{*this}.trimmed(characters);
	}

	MutableStringView String::trimmed() {
		return MutableStringView{*this}.trimmed();
	}

	StringView String::trimmed() const {
		return StringView{*this}.trimmed();
	}

	MutableStringView String::trimmedPrefix(const StringView characters) {
		return MutableStringView{*this}.trimmedPrefix(characters);
	}

	StringView String::trimmedPrefix(const StringView characters) const {
		return StringView{*this}.trimmedPrefix(characters);
	}

	MutableStringView String::trimmedPrefix() {
		return MutableStringView{*this}.trimmedPrefix();
	}

	StringView String::trimmedPrefix() const {
		return StringView{*this}.trimmedPrefix();
	}

	MutableStringView String::trimmedSuffix(const StringView characters) {
		return MutableStringView{*this}.trimmedSuffix(characters);
	}

	StringView String::trimmedSuffix(const StringView characters) const {
		return StringView{*this}.trimmedSuffix(characters);
	}

	MutableStringView String::trimmedSuffix() {
		return MutableStringView{*this}.trimmedSuffix();
	}

	StringView String::trimmedSuffix() const {
		return StringView{*this}.trimmedSuffix();
	}

	MutableStringView String::find(const StringView substring) {
		// Calling straight into the concrete implementation to reduce call stack depth
		return MutableStringView{*this}.findOr(substring, nullptr);
	}

	StringView String::find(const StringView substring) const {
		// Calling straight into the concrete implementation to reduce call stack depth
		return StringView{*this}.findOr(substring, nullptr);
	}

	MutableStringView String::find(const char character) {
		// Calling straight into the concrete implementation to reduce call stack depth
		return MutableStringView{*this}.findOr(character, nullptr);
	}

	StringView String::find(const char character) const {
		// Calling straight into the concrete implementation to reduce call stack depth
		return StringView{*this}.findOr(character, nullptr);
	}

	MutableStringView String::findOr(const StringView substring, char* const fail) {
		return MutableStringView{*this}.findOr(substring, fail);
	}

	StringView String::findOr(const StringView substring, const char* const fail) const {
		return StringView{*this}.findOr(substring, fail);
	}

	MutableStringView String::findOr(const char character, char* const fail) {
		return MutableStringView{*this}.findOr(character, fail);
	}

	StringView String::findOr(const char character, const char* const fail) const {
		return StringView{*this}.findOr(character, fail);
	}

	MutableStringView String::findLast(const StringView substring) {
		// Calling straight into the concrete implementation to reduce call stack depth
		return MutableStringView{*this}.findLastOr(substring, nullptr);
	}

	StringView String::findLast(const StringView substring) const {
		// Calling straight into the concrete implementation to reduce call stack depth
		return StringView{*this}.findLastOr(substring, nullptr);
	}

	MutableStringView String::findLast(const char character) {
		// Calling straight into the concrete implementation to reduce call stack depth
		return MutableStringView{*this}.findLastOr(character, nullptr);
	}

	StringView String::findLast(const char character) const {
		/* Calling straight into the concrete implementation to reduce call stack depth */
		return StringView{*this}.findLastOr(character, nullptr);
	}

	MutableStringView String::findLastOr(const StringView substring, char* const fail) {
		return MutableStringView{*this}.findLastOr(substring, fail);
	}

	StringView String::findLastOr(const StringView substring, const char* const fail) const {
		return StringView{*this}.findLastOr(substring, fail);
	}

	MutableStringView String::findLastOr(const char character, char* const fail) {
		return MutableStringView{*this}.findLastOr(character, fail);
	}

	StringView String::findLastOr(const char character, const char* const fail) const {
		return StringView{*this}.findLastOr(character, fail);
	}

	bool String::contains(const StringView substring) const {
		return StringView{*this}.contains(substring);
	}

	bool String::contains(const char character) const {
		return StringView{*this}.contains(character);
	}

	MutableStringView String::findAny(const StringView characters) {
		return MutableStringView{*this}.findAny(characters);
	}

	StringView String::findAny(const StringView characters) const {
		return StringView{*this}.findAny(characters);
	}

	MutableStringView String::findAnyOr(const StringView characters, char* fail) {
		return MutableStringView{*this}.findAnyOr(characters, fail);
	}

	StringView String::findAnyOr(const StringView characters, const char* fail) const {
		return StringView{*this}.findAnyOr(characters, fail);
	}

	MutableStringView String::findLastAny(const StringView characters) {
		return MutableStringView{*this}.findLastAny(characters);
	}

	StringView String::findLastAny(const StringView characters) const {
		return StringView{*this}.findLastAny(characters);
	}

	MutableStringView String::findLastAnyOr(const StringView characters, char* fail) {
		return MutableStringView{*this}.findLastAnyOr(characters, fail);
	}

	StringView String::findLastAnyOr(const StringView characters, const char* fail) const {
		return StringView{*this}.findLastAnyOr(characters, fail);
	}

	bool String::containsAny(const StringView substring) const {
		return StringView{*this}.containsAny(substring);
	}

	std::size_t String::count(const char character) const {
		return StringView{*this}.count(character);
	}

	char* String::release() {
		DEATH_DEBUG_ASSERT(!(_small.size & Implementation::SmallStringBit),
			"Can't call on a SSO instance", {});
		char* data = _large.data;

		// Create a zero-size small string to fullfil the guarantee of data() being always non-null and null-terminated.
		// Since this makes the string switch to SSO, we also clear the deleter this way.
		_small.data[0] = '\0';
		_small.size = Implementation::SmallStringBit;
		return data;
	}

}}