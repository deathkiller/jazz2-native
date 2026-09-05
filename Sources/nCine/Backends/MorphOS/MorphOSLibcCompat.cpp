/*
	Fills the two gaps the rest of the game trips over on MorphOS, in the same spirit as
	AmigaLibcCompat.c does for AmigaOS 3.x - except that both of these are C++ rather than libc:

	- wcstoul: the C library declares the wide-character set and implements almost none of it. The
	  game's formatted-text parser measures numeric attribute values with this one.

	- __throw_bad_array_new_length: GCC 11 emits a call to it for every `new T[n]` whose length it
	  cannot prove valid, but the libstdc++ this SDK ships predates the function, so the reference
	  goes unresolved at link time. The definition here matches what libstdc++ does - the array-new
	  length check has failed and there is nothing to return to.

	- std::to_chars for double: this libstdc++ implements <charconv> for integers only, which is how it
	  was shipped before the floating-point half landed. The engine's string formatter calls it for every
	  float it prints.

	- realpath: <stdlib.h> declares it, and only the ixemul flavour of the C library defines it - this
	  port builds -noixemul, where the reference is left undefined at link time. AmigaLibcCompat.c fills
	  the same gap for AmigaOS 3.x, and for the same reason: it is the one libc function
	  `FileSystem::GetAbsolutePath()` cannot do without.
*/

#if defined(DEATH_TARGET_MORPHOS)

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <charconv>
#include <exception>

#include <errno.h>
#include <limits.h>
#include <unistd.h>

extern "C" unsigned long wcstoul(const wchar_t* nptr, wchar_t** endptr, int base)
{
	const wchar_t* p = nptr;
	while (*p == L' ' || *p == L'\t') {
		p++;
	}
	int negative = 0;
	if (*p == L'+' || *p == L'-') {
		negative = (*p == L'-');
		p++;
	}
	if ((base == 0 || base == 16) && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) {
		p += 2;
		base = 16;
	} else if (base == 0) {
		base = (p[0] == L'0' ? 8 : 10);
	}
	unsigned long value = 0;
	const wchar_t* start = p;
	for (;;) {
		wchar_t c = *p;
		int digit;
		if (c >= L'0' && c <= L'9') {
			digit = (int)(c - L'0');
		} else if (c >= L'a' && c <= L'z') {
			digit = (int)(c - L'a') + 10;
		} else if (c >= L'A' && c <= L'Z') {
			digit = (int)(c - L'A') + 10;
		} else {
			break;
		}
		if (digit >= base) {
			break;
		}
		value = value * (unsigned long)base + (unsigned long)digit;
		p++;
	}
	if (endptr != nullptr) {
		*endptr = (wchar_t*)(p != start ? p : nptr);
	}
	return (negative ? (unsigned long)(-(long)value) : value);
}

/*
	No symlinks exist on the volumes this runs from, and AmigaDOS paths need no normalization: a path is
	absolute when it names a volume, device or assign ("Work:Games/MyGame" - the colon IS the root, there
	is no separator after it), and anything else is relative to the current directory, which is what joining
	it onto getcwd() means. What AmigaDOS spells with a leading or doubled slash - the parent directory - is
	deliberately left alone rather than resolved: the filesystem understands it, and rewriting it here would
	only be a chance to get it wrong.
*/
extern "C" char* realpath(const char* path, char* resolved)
{
	if (path == nullptr || resolved == nullptr) {
		return nullptr;
	}
	// The contract is that the caller's buffer holds at least PATH_MAX bytes
	const std::size_t capacity = PATH_MAX;
	if (std::strchr(path, ':') != nullptr) {
		const std::size_t length = std::strlen(path);
		if (length >= capacity) {
			errno = ENAMETOOLONG;
			return nullptr;
		}
		std::memcpy(resolved, path, length + 1);
		return resolved;
	}
	if (::getcwd(resolved, capacity) == nullptr) {
		return nullptr;
	}
	std::size_t length = std::strlen(resolved);
	// AmigaDOS separates a directory from its parent with '/', except right after the volume's colon
	if (length > 0 && resolved[length - 1] != '/' && resolved[length - 1] != ':') {
		if (length + 1 >= capacity) {
			errno = ENAMETOOLONG;
			return nullptr;
		}
		resolved[length++] = '/';
	}
	const std::size_t pathLength = std::strlen(path);
	if (length + pathLength >= capacity) {
		errno = ENAMETOOLONG;
		return nullptr;
	}
	std::memcpy(resolved + length, path, pathLength + 1);
	return resolved;
}

namespace std
{
	/*
		Shortest representation that reads back as the same value, which is what the standard asks of the
		no-precision overload: %g is tried from one significant digit upwards until strtod() round-trips.
		Seventeen digits always round-trip for an IEEE double, so the loop is bounded.
	*/
	to_chars_result to_chars(char* first, char* last, double value)
	{
		char buffer[64];
		int length = 0;
		for (int precision = 1; precision <= 17; precision++) {
			length = std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
			if (length <= 0) {
				return to_chars_result{last, std::errc::value_too_large};
			}
			if (std::strtod(buffer, nullptr) == value) {
				break;
			}
		}
		if (length > (int)(last - first)) {
			return to_chars_result{last, std::errc::value_too_large};
		}
		std::memcpy(first, buffer, (std::size_t)length);
		return to_chars_result{first + length, std::errc()};
	}

	void __throw_bad_array_new_length()
	{
		// The game is built without exceptions, so this ends the process the same way an uncaught
		// bad_array_new_length would
		std::abort();
	}
}

#endif
