/*
	Fills the few libc gaps the rest of the game trips over on libnix (AmigaOS 3.x):

	- exp2/exp2f/log2f: libnix's libm predates C99's base-2 set except the double log2. libxmp's
	  period computation calls exp2() from plain C, so the C++-side shim in
	  cmake/toolchains/amiga-libstdc++-c99.h cannot cover it - these are the real out-of-line
	  definitions, derived the standard way (exp2(x) == exp(x * ln 2)).

	  Nothing else from libm is replaced here. round, trunc, fmin, fmax, rint and nearbyint used to
	  be, because they returned garbage - that was never a library bug but the soft-float build the
	  toolchain file used to produce: libm returns floating point in fp0 and soft-float code reads it
	  from d0. With -m68881 (see cmake/toolchains/amiga.cmake) every one of them is correct.

	- __xpg_strerror_r: the m68k-amigaos libstdc++ was configured against a glibc-shaped
	  strerror_r and its system_error translation unit references the XPG entry point by name.
	  Nothing here is threaded (the whole port runs single-threaded), so routing it through plain
	  strerror() is exact enough.
*/

#if defined(__AMIGA__) || defined(AMIGA)

#include <math.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>

/* Our own prototypes: these are definitions the C library is missing, so nothing declares them */
size_t strnlen(const char* s, size_t maxlen);
int __xpg_strerror_r(int errnum, char* buf, size_t buflen);

double exp2(double x)
{
	return exp(x * 0.69314718055994530942);
}

float exp2f(float x)
{
	return expf(x * 0.6931471805599453f);
}

float log2f(float x)
{
	return (float)log2(x);
}

/* strnlen: absent from libnix entirely - neither declared nor implemented - while the engine uses it
   wherever it measures a fixed-size name buffer. The declaration for C++ callers is in
   cmake/toolchains/amiga-libstdc++-c99.h, which the toolchain force-includes. */
size_t strnlen(const char* s, size_t maxlen)
{
	const char* end = (const char*)memchr(s, '\0', maxlen);
	return (end != NULL ? (size_t)(end - s) : maxlen);
}

int __xpg_strerror_r(int errnum, char* buf, size_t buflen)
{
	const char* message = strerror(errnum);
	if (message == NULL) {
		return EINVAL;
	}
	if (buf == NULL || buflen == 0) {
		return ERANGE;
	}
	size_t length = strlen(message);
	if (length >= buflen) {
		length = buflen - 1;
	}
	memcpy(buf, message, length);
	buf[length] = '\0';
	return 0;
}

/* gethostname: the trace header stamps the machine name with it. AmigaOS has no host name
   without a TCP/IP stack, so a constant stands in. */
int gethostname(char* name, size_t namelen)
{
	static const char hostname[] = "amiga";
	if (name == NULL || namelen == 0) {
		return -1;
	}
	size_t length = sizeof(hostname) - 1;
	if (length >= namelen) {
		length = namelen - 1;
	}
	memcpy(name, hostname, length);
	name[length] = '\0';
	return 0;
}

/* flock: single-tasking use of the game's own files - AmigaOS has no advisory descriptor locks,
   and nothing else contends for them. */
int flock(int fd, int operation)
{
	(void)fd;
	(void)operation;
	return 0;
}

/* fchmod: libnix has chmod but no descriptor form; the copy path that calls it treats failure as
   non-fatal, and AmigaOS protection bits do not map onto POSIX modes anyway. */
int fchmod(int fd, mode_t mode)
{
	(void)fd;
	(void)mode;
	return 0;
}

/* getpwuid: no user database on AmigaOS; the caller (home-directory lookup) handles NULL. */
struct passwd* getpwuid(int uid)
{
	(void)uid;
	return NULL;
}

/* realpath: no symlinks to resolve on the volumes the game uses - absolute paths (anything with a
   volume/assign ':' or rooted for libnix's unix-ish view) pass through, relative ones are joined
   onto the current directory. */
char* realpath(const char* path, char* resolved)
{
	if (path == NULL || resolved == NULL) {
		return NULL;
	}
	if (strchr(path, ':') != NULL || path[0] == '/') {
		strcpy(resolved, path);
		return resolved;
	}
	if (getcwd(resolved, 512) == NULL) {
		return NULL;
	}
	size_t length = strlen(resolved);
	if (length > 0 && resolved[length - 1] != '/' && resolved[length - 1] != ':') {
		resolved[length++] = '/';
	}
	strcpy(resolved + length, path);
	return resolved;
}

/* wcstoul: libnix declares the wide-character set but ships no implementation of this one; the
   game's formatted-text parser uses it for numeric attribute values (base 10/16). */
unsigned long wcstoul(const wchar_t* nptr, wchar_t** endptr, int base)
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
	if (endptr != NULL) {
		*endptr = (wchar_t*)(p != start ? p : nptr);
	}
	return (negative ? (unsigned long)(-(long)value) : value);
}

#endif
