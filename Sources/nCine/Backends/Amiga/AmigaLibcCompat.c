/*
	Fills the few libc gaps the rest of the game trips over on libnix (AmigaOS 3.x):

	- exp2/exp2f/log2f: libnix's libm predates C99's base-2 set except the double log2. libxmp's
	  period computation calls exp2() from plain C, so the C++-side shim in
	  cmake/toolchains/amiga-libstdc++-c99.h cannot cover it - these are the real out-of-line
	  definitions, derived the standard way (exp2(x) == exp(x * ln 2)).

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

/*
	round/roundf: libnix SHIPS these symbols, but they are broken - they return garbage rather than a
	rounded value (measured on m68k-amigaos: roundf(0.0f) -> 0xFFFFFFFF i.e. NaN, roundf(2.5f) ->
	0x00200000, roundf(112.26f) -> 0x00010000, and the double form is equally wrong, while floorf,
	ceilf, truncf, fabsf, sqrtf, sinf and fmodf from the same library are all correct). Defining them
	here overrides the archive members, because the linker only pulls a libnix object in for a symbol
	that is still undefined.

	This mattered far more than it looks: the UI rounds every glyph and sprite position to whole
	pixels through std::round, so a broken round turned every menu item's coordinates into NaN and the
	whole menu drew off-screen.

	Half-way cases round away from zero, as C99 specifies. Adding 0.5 and flooring is NOT used because
	it rounds 0.49999997f up; taking the truncated part and comparing the exact remainder does not.
*/
/*
	trunc/truncf: broken the same way, though less spectacularly - for 0 < |x| < 1 libnix returns tiny
	denormals (measured: truncf(0.5f) -> 0x0000007E, truncf(-0.9f) -> 0x0000007E) instead of a clean
	zero. floorf and ceilf ARE correct, so they are what these are built from; roundf below then
	inherits a trustworthy truncation.
*/
float truncf(float x)
{
	return (x >= 0.0f ? floorf(x) : ceilf(x));
}

double trunc(double x)
{
	return (x >= 0.0 ? floor(x) : ceil(x));
}

float roundf(float x)
{
	float t = truncf(x);
	float d = x - t;
	if (d >= 0.5f) {
		t += 1.0f;
	} else if (d <= -0.5f) {
		t -= 1.0f;
	}
	return t;
}

double round(double x)
{
	double t = trunc(x);
	double d = x - t;
	if (d >= 0.5) {
		t += 1.0;
	} else if (d <= -0.5) {
		t -= 1.0;
	}
	return t;
}

/*
	fmin/fmax (and the float forms): also broken in libnix - fminf(0.5f, 2.5f) came back as 0x00000004
	rather than 0.5f. Plain comparisons are the whole content of these functions anyway; NaN operands
	are detected by bit pattern rather than with isnan(), which folds to a constant false under
	-ffast-math (DEATH_USE_FAST_MATH) and would defeat the check.
*/
static int __amiga_isnanf(float x)
{
	unsigned long bits;
	memcpy(&bits, &x, sizeof(bits));
	return ((bits & 0x7F800000UL) == 0x7F800000UL) && ((bits & 0x007FFFFFUL) != 0UL);
}

float fminf(float x, float y)
{
	if (__amiga_isnanf(x)) return y;
	if (__amiga_isnanf(y)) return x;
	return (x < y ? x : y);
}

float fmaxf(float x, float y)
{
	if (__amiga_isnanf(x)) return y;
	if (__amiga_isnanf(y)) return x;
	return (x > y ? x : y);
}

double fmin(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	return (x < y ? x : y);
}

double fmax(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	return (x > y ? x : y);
}

/*
	rint/rintf: libnix's rint returned its argument unchanged (rint(2.5) -> 2.5). It is written here
	from floor() rather than delegating to nearbyint(), even though nearbyint measured correct: that
	one is implemented in terms of rint inside libnix, so calling it from here recurses until the
	stack dies. Round-half-to-even, which is what both functions mean in the default rounding mode.
*/
static double __amiga_rint(double x)
{
	double t = floor(x);
	double d = x - t;
	if (d > 0.5) {
		return t + 1.0;
	}
	if (d < 0.5) {
		return t;
	}
	/* Exactly halfway: pick the even neighbour. Evenness is tested with floor() rather than fmod(),
	   because libnix's double fmod is not trustworthy either (fmod(2.0, 2.0) did not compare equal
	   to zero here, which sent 2.5 to 3.0 instead of 2.0). */
	const double half = t * 0.5;
	return (floor(half) == half ? t : t + 1.0);
}

double rint(double x)
{
	return __amiga_rint(x);
}

float rintf(float x)
{
	return (float)__amiga_rint((double)x);
}

double nearbyint(double x)
{
	return __amiga_rint(x);
}

float nearbyintf(float x)
{
	return (float)__amiga_rint((double)x);
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
