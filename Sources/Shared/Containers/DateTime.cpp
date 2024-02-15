#include "DateTime.h"
#include "../CommonWindows.h"
#include "../Asserts.h"

#include <cstring>
#include <sys/types.h>

#if !defined(DEATH_USE_GMTOFF_IN_TM) && defined(DEATH_TARGET_APPLE)
#	define DEATH_USE_GMTOFF_IN_TM
#endif

#if defined(DEATH_USE_GMTOFF_IN_TM)
#	include <atomic>
#endif

#if defined(DEATH_TARGET_SWITCH) || (defined(__MINGW32_TOOLCHAIN__) && defined(__STRICT_ANSI__))
// These two are excluded from headers for some reason, define them here
extern "C" void tzset(void);
extern long _timezone;
#endif

#define MONTHS_IN_YEAR 12
#define SEC_PER_MIN 60
#define MIN_PER_HOUR 60
#define SECONDS_PER_DAY 86400
#define MILLISECONDS_PER_DAY 86400000
#define DST_OFFSET 3600

#define EPOCH_JDN 2440587l
#define JDN_OFFSET 32046l
#define DAYS_PER_5_MONTHS 153l
#define DAYS_PER_4_YEARS 1461l
#define DAYS_PER_400_YEARS 146097l

namespace Death { namespace Containers {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace Implementation
	{
		// The first line is for normal years, the second one is for the leap ones
		static const std::int32_t DaysInMonth[2][MONTHS_IN_YEAR] = {
			{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
			{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
		};

		static bool IsLeapYear(std::int32_t year)
		{
			if (year == -1) {
				year = DateTime::Now().GetYear();
			}

			return (year % 4 == 0 && ((year % 100) != 0 || (year % 400) == 0));
		}

		static std::int32_t GetNumberOfDaysInMonth(std::int32_t month, std::int32_t year)
		{
			return DaysInMonth[IsLeapYear(year)][month];
		}

		static std::int32_t GetTruncatedJDN(std::int32_t day, std::int32_t mon, std::int32_t year)
		{
			// Make the year positive to avoid problems with negative numbers division
			year += 4800;

			// Months are counted from March here
			std::int32_t month;
			if (mon >= 2 /*March*/) {
				month = mon - 2;
			} else {
				month = mon + 10;
				year--;
			}

			return ((year / 100) * DAYS_PER_400_YEARS) / 4 + ((year % 100) * DAYS_PER_4_YEARS) / 4
						+ (month * DAYS_PER_5_MONTHS + 2) / 5 + day - JDN_OFFSET;
		}

		static struct tm* GetLocalTm(const time_t* ticks, struct tm* temp)
		{
#if defined(DEATH_TARGET_WINDOWS)
			return (localtime_s(temp, ticks) == 0 ? temp : nullptr);
#else
			// TODO: This function is not thread-safe on Unix - use localtime_r instead
			const tm* const result = localtime(ticks);
			if (result == nullptr) {
				return nullptr;
			}

			std::memcpy(temp, result, sizeof(struct tm));
			return temp;
#endif
		}

		static bool IsDST(const DateTime date)
		{
			time_t timet = date.GetTicks();
			if (timet == (time_t)-1) {
				return false;
			}

			struct tm temp;
			tm* tm = GetLocalTm(&timet, &temp);
			return (tm != nullptr && tm->tm_isdst == 1);
		}

		static std::int32_t GetTimeZone()
		{
			// Struct tm doesn't always have the tm_gmtoff field, define this if it does
#if defined(DEATH_USE_GMTOFF_IN_TM)
			static std::atomic<std::int32_t> _gmtoffset { INT32_MAX }; // Invalid timezone
			if (_gmtoffset == INT32_MAX) {
				time_t t = time(nullptr);
				struct tm tm;
				GetLocalTm(&t, &tm);

				std::int32_t gmtoffset = static_cast<std::int32_t>(-tm.tm_gmtoff);

				// This function is supposed to return the same value whether DST is enabled or not, so we need to use
				// an additional offset if DST is on as tm_gmtoff already does include it
				if (tm.tm_isdst) {
					gmtoffset += 3600;
				}

				_gmtoffset = gmtoffset;
			}
			return _gmtoffset;
#elif defined(__WINE__)
			struct timeb tb;
			ftime(&tb);
			return tb.timezone * 60;
#elif defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_WINDOWS_RT)
			// In any case we must initialize the time zone before using it, try to do it only once
			static bool _tzSet = (_tzset(), true);
			(void)_tzSet;
#	endif

			long t;
			_get_timezone(&t);
			return t;
#else
			// In any case we must initialize the time zone before using it, try to do it only once
			static bool _tzSet = (tzset(), true);
			(void)_tzSet;

#	if defined(DEATH_TARGET_MINGW) || defined(DEATH_TARGET_SWITCH)
			return _timezone;
#	else // Unknown platform - assume it has timezone variable
			return timezone;
#	endif
#endif
		}

		static const tm* TryGetTm(time_t t, const DateTime::TimeZone tz, struct tm* temp)
		{
			if (tz.IsLocal()) {
				return Implementation::GetLocalTm(&t, temp);
			}

			t += (time_t)tz.GetOffset();
#if !defined(__VMS__) // Time is unsigned so avoid warning
			if (t < 0) {
				return nullptr;
			}
#endif

#if defined(DEATH_TARGET_WINDOWS)
			return (gmtime_s(temp, &t) == 0 ? temp : nullptr);
#else
			// TODO: This function is not thread-safe on Unix - use gmtime_r instead
			const tm* const result = gmtime(&t);
			if (result == nullptr) {
				return nullptr;
			}

			std::memcpy(temp, result, sizeof(struct tm));
			return temp;
#endif
		}

		template<class T>
		static bool GetNumericToken(const T* string, std::size_t length, std::size_t& i, std::size_t width, std::int32_t* number, std::size_t* scannedDigitsCount = nullptr)
		{
			std::size_t n = 1;
			*number = 0;
			while (i < length && string[i] >= '0' && string[i] <= '9') {
				*number *= 10;
				*number += string[i] - '0';

				i++;
				n++;

				if (width > 0 && n > width) {
					break;
				}
			}

			if (scannedDigitsCount != nullptr) {
				*scannedDigitsCount = n - 1;
			}

			return (n > 1);
		}

		template<class T, class U>
		std::size_t TranslateFromUnicodeFormat(T* dest, std::size_t destLength, const U* src, std::size_t srcLength)
		{
			static const char* formatchars =
				"dghHmMsSy"
#if defined(DEATH_TARGET_WINDOWS)
				"t"
#else
				"EcLawD"
#endif
				;

			U chLast = '\0';
			std::size_t lastCount = 0;
			std::size_t j = 0;

			for (std::size_t i = 0; /* Handled inside the loop */; i++) {
				if (i < srcLength) {
					if (src[i] == chLast) {
						lastCount++;
						continue;
					}

					if (src[i] >= 'A' && src[i] <= 'z' && strchr(formatchars, (char)src[i])) {
						chLast = src[i];
						lastCount = 1;
						continue;
					}
				}

				if (lastCount > 0) {
					switch (chLast) {
						case 'd':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'd';
									break;
#if defined(DEATH_TARGET_WINDOWS)
								case 3:
									dest[j++] = '%'; dest[j++] = 'a';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'A';
									break;
#endif
							}
							break;
#if !defined(DEATH_TARGET_WINDOWS)
						case 'D':
							switch (lastCount) {
								case 1:
								case 2:
								case 3:
									dest[j++] = '%'; dest[j++] = 'j';
									break;
							}
							break;
						case 'w':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'W';
									break;
							}
							break;
						case 'E':
							switch (lastCount) {
								case 1:
								case 2:
								case 3:
									dest[j++] = '%'; dest[j++] = 'a';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'A';
									break;
								case 5:
								case 6:
									// No "narrow form" in strftime(), use abbreviation instead
									dest[j++] = '%'; dest[j++] = 'a';
									break;
							}
							break;
						case 'c':
							switch (lastCount) {
								case 1:
									// First day of week as numeric value
									dest[j++] = '1';
									break;
								case 3:
									dest[j++] = '%'; dest[j++] = 'a';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'A';
									break;
								case 5:
									// No "narrow form" in strftime(), use abbreviation instead
									dest[j++] = '%'; dest[j++] = 'a';
									break;
							}
							break;
						case 'L':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'm';
									break;
								case 3:
									dest[j++] = '%'; dest[j++] = 'b';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'B';
									break;
								case 5:
									//No "narrow form" in strftime(), use abbreviation instead
									dest[j++] = '%'; dest[j++] = 'b';
									break;
							}
							break;
#endif
						case 'M':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'm';
									break;
								case 3:
									dest[j++] = '%'; dest[j++] = 'n';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'B';
									break;
								case 5:
									// No "narrow form" in strftime(), use abbreviation instead
									dest[j++] = '%'; dest[j++] = 'b';
									break;
							}
							break;
						case 'y':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'y';
									break;
								case 4:
									dest[j++] = '%'; dest[j++] = 'Y';
									break;
							}
							break;
						case 'H':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'H';
									break;
							}
							break;
						case 'h':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'I';
									break;
							}
							break;
						case 'm':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'M';
									break;
							}
							break;
						case 's':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'S';
									break;
							}
							break;
						case 'g':
							// This format is not supported in strftime(), ignore it
							break;
#if !defined(DEATH_TARGET_WINDOWS)
						case 'a':
							dest[j++] = '%'; dest[j++] = 'p';
							break;
#else
						case 't':
							switch (lastCount) {
								case 1:
								case 2:
									dest[j++] = '%'; dest[j++] = 'p';
									break;
							}
							break;
#endif
					}

					chLast = '\0';
					lastCount = 0;
				}

				if (i >= srcLength) {
					break;
				}

				if (i + 1 < srcLength && src[i] == '\'' && src[i + 1] == '\'') {
					dest[j++] = '\'';
					i++;
					continue;
				}

				bool isEndQuote = false;
				if (src[i] == '\'') {
					i++;
					while (i < srcLength) {
						if (i + 1 < srcLength && src[i] == '\'' && src[i + 1] == '\'') {
							dest[j++] = '\'';
							i += 2;
							continue;
						}

						if (src[i] == '\'') {
							isEndQuote = true;
							break;
						}

						dest[j++] = (T)src[i];
						i++;
					}
				}

				if (i >= srcLength) {
					break;
				}

				if (!isEndQuote) {
					if (src[i] == '%') {
						dest[j++] = '%';
					}

					dest[j++] = (T)src[i];
				}
			}

			return j;
		}

		template<class T>
		static bool TryParseFormat(DateTime& result, const T* input, std::size_t inputLength, const T* format, std::size_t formatLength, std::size_t* endIndex)
		{
			std::int32_t msec = 0, sec = 0, min = 0, hour = 0, wday = -1, yday = 0, day = 0, mon = -1, year = 0, timeZone = 0, num;
			bool haveWDay = false, haveYDay = false, haveDay = false, haveMon = false, haveYear = false, haveHour = false,
				haveMin = false, haveSec = false, haveMsec = false, hourIsIn12hFormat = false, isPM = false, haveTimeZone = false;

			std::size_t j = 0;
			for (std::size_t i = 0; i < formatLength; i++) {
				if (format[i] != '%') {
					if (format[i] == ' ') {
						// Whitespace in the format string matches zero or more whitespaces in the input
						while (j < inputLength && input[j] == ' ') {
							j++;
						}
					} else {
						// Any other character (except whitespace and '%') must be matched by itself in the input
						if (j >= inputLength || input[j++] != format[i]) {
							return false;
						}
					}
					continue;
				}

				// Start of a format specification
				i++;

				// Parse the optional width
				std::size_t width = 0;
				while (format[i] >= '0' && format[i] <= '9') {
					width *= 10;
					width += format[i] - '0';
					i++;
				}

				if (width == 0) {
					switch (format[i]) {
						case 'Y':			// Year has 4 digits
							width = 4;
							break;
						case 'j':			// Day of year has 3 digits
						case 'l':			// Milliseconds have 3 digits
							width = 3;
							break;
						case 'w':			// Week day as number has only one
							width = 1;
							break;
						default:
							width = 2;		// Default for all other fields
							break;
					}
				}

				switch (format[i]) {
					case 'c': {		// Locale default date and time representation
						DateTime dt;
						T format2[64];
						bool success2 = false;
#if defined(DEATH_TARGET_WINDOWS)
						wchar_t buffer[64];
						std::int32_t bufferLength = ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, buffer, countof(buffer));
						if (bufferLength > 0) {
							if (buffer[bufferLength - 1] == '\0') {
								bufferLength--;
							}
							buffer[bufferLength++] = ' ';
							std::int32_t timeLength = ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, buffer + bufferLength, countof(buffer) - bufferLength);
							if (timeLength > 0) {
								bufferLength += timeLength;
								if (buffer[bufferLength - 1] == '\0') {
									bufferLength--;
								}
								std::size_t format2Length = TranslateFromUnicodeFormat(format2, countof(format2), buffer, bufferLength);
								if (format2Length > 0) {
									success2 = TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex);
								}
							}
						}
#endif
						if (!success2) {
							std::size_t format2Length = 0;	// yyyy/mm/dd HH:MM:SS
							format2[format2Length++] = '%'; format2[format2Length++] = 'Y'; format2[format2Length++] = '/';
							format2[format2Length++] = '%'; format2[format2Length++] = 'm'; format2[format2Length++] = '/';
							format2[format2Length++] = '%'; format2[format2Length++] = 'd'; format2[format2Length++] = ' ';
							format2[format2Length++] = '%'; format2[format2Length++] = 'H'; format2[format2Length++] = ':';
							format2[format2Length++] = '%'; format2[format2Length++] = 'M'; format2[format2Length++] = ':';
							format2[format2Length++] = '%'; format2[format2Length++] = 'S';
							if (!TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex)) {
								return false;
							}
						}

						j = *endIndex;

						const DateTime::Tm tm = dt.Partitioned();

						haveDay = haveMon = haveYear = haveHour = haveMin = haveSec = true;

						hour = tm.Hour;
						min = tm.Minute;
						sec = tm.Second;

						year = tm.Year;
						mon = tm.Month;
						day = tm.Day;
						break;
					}
					case 'd':		// Day of a month (01-31)
					case 'e': {		// Day of a month (1-31) (GNU extension)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num < 1 || num > 31) {
							return false;
						}

						haveDay = true;
						day = num;
						break;
					}
					case 'H': {		// Hour in 24h format (00-23)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num > 23) {
							return false;
						}

						haveHour = true;
						hour = num;
						break;
					}
					case 'I': {		// Hour in 12h format (01-12)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num == 0 || num > 12) {
							return false;
						}

						haveHour = true;
						hourIsIn12hFormat = true;
						hour = (num % 12);
						break;
					}
					case 'j': {		// Day of the year
						if (!GetNumericToken(input, inputLength, j, width, &num) || num == 0 || num > 366) {
							return false;
						}

						haveYDay = true;
						yday = num;
						break;
					}
					case 'l': {		// Milliseconds (0-999)
						if (!GetNumericToken(input, inputLength, j, width, &num)) {
							return false;
						}

						haveMsec = true;
						msec = num;
						break;
					}
					case 'm': {		// Month as a number (01-12)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num == 0 || num > 12) {
							return false;
						}

						haveMon = true;
						mon = (num - 1);
						break;
					}
					case 'M': {		// Minute as a decimal number (00-59)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num > 59) {
							return false;
						}

						haveMin = true;
						min = num;
						break;
					}
					case 'p': {		// AM or PM string
						if (j + 1 >= inputLength || input[j + 1] != 'M') {
							return false;
						}
						switch (input[j]) {
							case 'P': isPM = true; break;
							case 'A': break;
							default: return false;
						}
						j += 2;
						break;
					}
					case 'S': {		// Second as a decimal number (00-61)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num > 61) {
							return false;
						}

						haveSec = true;
						sec = num;
						break;
					}
					case 'w': {		// Weekday as a number (0-6), Sunday = 0
						if (!GetNumericToken(input, inputLength, j, width, &num) || wday > 6) {
							return false;
						}

						haveWDay = true;
						wday = num;
						break;
					}
					case 'x': {		// Locale default date representation
						DateTime dt;
						T format2[64];
						bool success2 = false;
#if defined(DEATH_TARGET_WINDOWS)
						wchar_t buffer[64];
						std::int32_t bufferLength = ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, buffer, countof(buffer));
						if (bufferLength > 0) {
							if (buffer[bufferLength - 1] == '\0') {
								bufferLength--;
							}
							std::size_t format2Length = TranslateFromUnicodeFormat(format2, countof(format2), buffer, bufferLength);
							if (format2Length > 0) {
								success2 = TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex);
								if (!success2) {
									std::int32_t bufferLength = ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SLONGDATE, buffer, countof(buffer));
									if (bufferLength > 0) {
										if (buffer[bufferLength - 1] == '\0') {
											bufferLength--;
										}
										format2Length = TranslateFromUnicodeFormat(format2, countof(format2), buffer, bufferLength);
										if (format2Length > 0) {
											success2 = TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex);
										}
									}
								}
							}
						}
#endif
						if (!success2) {
							std::size_t format2Length = 0;	// yyyy/mm/dd
							format2[format2Length++] = '%'; format2[format2Length++] = 'Y'; format2[format2Length++] = '/';
							format2[format2Length++] = '%'; format2[format2Length++] = 'm'; format2[format2Length++] = '/';
							format2[format2Length++] = '%'; format2[format2Length++] = 'd';
							if (!TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex)) {
								return false;
							}
						}

						j = *endIndex;

						const DateTime::Tm tm = dt.Partitioned();

						haveDay = haveMon = haveYear = true;

						year = tm.Year;
						mon = tm.Month;
						day = tm.Day;
						break;
					}
					case 'X': {		// Locale default time representation
						DateTime dt;
						T format2[64];
						bool success2 = false;
#if defined(DEATH_TARGET_WINDOWS)
						wchar_t buffer[64];
						std::int32_t bufferLength = ::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, buffer, countof(buffer));
						if (bufferLength > 0) {
							if (buffer[bufferLength - 1] == '\0') {
								bufferLength--;
							}
							std::size_t format2Length = TranslateFromUnicodeFormat(format2, countof(format2), buffer, bufferLength);
							if (format2Length > 0) {
								success2 = TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex);
							}
						}
#endif
						if (!success2) {
							std::size_t format2Length = 0;	// HH:MM:SS
							format2[format2Length++] = '%'; format2[format2Length++] = 'H'; format2[format2Length++] = ':';
							format2[format2Length++] = '%'; format2[format2Length++] = 'M'; format2[format2Length++] = ':';
							format2[format2Length++] = '%'; format2[format2Length++] = 'S';
							if (!TryParseFormat(dt, input + j, inputLength - j, format2, format2Length, endIndex)) {
								return false;
							}
						}

						j = *endIndex;

						const DateTime::Tm tm = dt.Partitioned();

						haveHour = haveMin = haveSec = true;

						hour = tm.Hour;
						min = tm.Minute;
						sec = tm.Second;
						break;
					}
					case 'y': {		// Year without century (00-99)
						if (!GetNumericToken(input, inputLength, j, width, &num) || num > 99) {
							return false;
						}

						haveYear = true;
						year = (num > 30 ? 1900 : 2000) + num;
						break;
					}
					case 'Y': {		// Year with century
						if (!GetNumericToken(input, inputLength, j, width, &num)) {
							return false;
						}

						haveYear = true;
						year = num;
						break;
					}
					case 'z': {
						if (j >= inputLength) {
							return false;
						}

						if (input[j] == 'Z') {
							// Time is in UTC
							j++;
							haveTimeZone = true;
							break;
						}

						bool minusFound;
						switch (input[j]) {
							case '+': minusFound = false; break;
							case '-': minusFound = true; break;
							default: return false;
						}

						j++;

						// Here should follow exactly 2 digits for hours (HH)
						std::size_t numScannedDigits;

						std::int32_t hours;
						if (!GetNumericToken(input, inputLength, j, 2, &hours, &numScannedDigits) || numScannedDigits != 2) {
							return false;
						}

						// Optionally followed by a colon separator
						bool mustHaveMinutes = false;
						if (j < inputLength && input[j] == ':') {
							mustHaveMinutes = true;
							j++;
						}

						// Optionally followed by exactly 2 digits for minutes (MM)
						std::int32_t minutes = 0;
						if (!GetNumericToken(input, inputLength, j, 2, &minutes, &numScannedDigits) || numScannedDigits != 2) {
							if (mustHaveMinutes || numScannedDigits) {
								return false;
							}
						}

						if (hours > 15 || minutes > 59) {
							return false;
						}

						timeZone = 3600 * hours + 60 * minutes;
						if (minusFound) {
							timeZone = -timeZone;
						}

						haveTimeZone = true;
						break;
					}
					case 'Z': {		// Timezone name
						// TODO: Named timezones are not supported, skip it for now
						while (j < inputLength && ((input[j] >= 'a' && input[j] <= 'z') || (input[j] >= 'A' && input[j] <= 'Z'))) {
							j++;
						}
						break;
					}
					case '%': {
						if (j >= inputLength || input[j++] != '%') {
							return false;
						}
						break;
					}
					case '\0': {
						LOGD("Unexpected format end in DateTime::TryParse()");
						return false;
					}
					default: {
						return false;
					}
				}
			}

			DateTime::Tm tm;
			if (result.IsValid()) {
				tm = result.Partitioned();
			} else {
				tm = DateTime::Now().ResetTime().Partitioned();
			}

			if (haveMon) {
				tm.Month = mon;
			}
			if (haveYear) {
				tm.Year = year;
			}

			if (haveDay) {
				if (day > GetNumberOfDaysInMonth(tm.Month, tm.Year)) {
					return false;
				}

				tm.Day = day;
			} else if (haveYDay) {
				bool isLeap = IsLeapYear(tm.Year);
				if (yday > (isLeap ? 366 : 365)) {
					return false;
				}

				std::int32_t month = 0;
				while (yday - DaysInMonth[isLeap][month] > 0) {
					yday -= DaysInMonth[isLeap][month];
					month++;
				}

				DateTime::Tm tm2 = DateTime(tm.Year, month, yday).Partitioned();
				tm.Month = tm2.Month;
				tm.Day = tm2.Day;
			}

			if (haveHour && hourIsIn12hFormat && isPM) {
				// Translate to 24-hour format
				hour += 12;
			}

			if (haveHour) {
				tm.Hour = hour;
			}
			if (haveMin) {
				tm.Minute = min;
			}
			if (haveSec) {
				tm.Second = sec;
			}
			if (haveMsec) {
				tm.Millisecond = msec;
			}

			result.Set(tm);

			if (haveTimeZone) {
				result.AdjustFromTimezone(timeZone);
			}

			if (haveWDay && result.GetWeekDay() != wday) {
				return false;
			}

			*endIndex = j;
			return true;
		}
	}

	DateTime::TimeZone::TimeZone(DateTime::Tz tz)
	{
		switch (tz) {
			case DateTime::Local:
				// Use a special value for local timezone
				_offset = -1;
				break;

			case DateTime::GMT_12:
			case DateTime::GMT_11:
			case DateTime::GMT_10:
			case DateTime::GMT_9:
			case DateTime::GMT_8:
			case DateTime::GMT_7:
			case DateTime::GMT_6:
			case DateTime::GMT_5:
			case DateTime::GMT_4:
			case DateTime::GMT_3:
			case DateTime::GMT_2:
			case DateTime::GMT_1:
				_offset = -3600 * (DateTime::GMT0 - tz);
				break;

			case DateTime::GMT0:
			case DateTime::GMT1:
			case DateTime::GMT2:
			case DateTime::GMT3:
			case DateTime::GMT4:
			case DateTime::GMT5:
			case DateTime::GMT6:
			case DateTime::GMT7:
			case DateTime::GMT8:
			case DateTime::GMT9:
			case DateTime::GMT10:
			case DateTime::GMT11:
			case DateTime::GMT12:
			case DateTime::GMT13:
				_offset = 3600 * (tz - DateTime::GMT0);
				break;

			case DateTime::A_CST:
				// Central Standard Time in use in Australia (UTC + 9.5)
				_offset = 60l * (9 * MIN_PER_HOUR + MIN_PER_HOUR / 2);
				break;

			default:
				DEATH_ASSERT_UNREACHABLE();
				break;
		}
	}

	std::int32_t DateTime::TimeZone::GetOffset() const
	{
		// Get the offset using standard library
		// It returns the difference GMT-local while we want to have the offset _from_ GMT, hence the '-'
		return (_offset == -1 ? -Implementation::GetTimeZone() : _offset);
	}

	DateTime::Tm::Tm()
		: Millisecond(0), Second(0), Minute(0), Hour(0), Day(0), DayOfYear(0), Month(0), Year(-1), _dayOfWeek(-1)
	{
	}

	DateTime::Tm::Tm(const struct tm& tm, const TimeZone tz)
		: _tz(tz), Millisecond(0), Second((std::int32_t)tm.tm_sec), Minute((std::int32_t)tm.tm_min), Hour((std::int32_t)tm.tm_hour),
			Day((std::int32_t)tm.tm_mday), DayOfYear((std::int32_t)tm.tm_yday), Month((std::int32_t)tm.tm_mon),
			Year(1900 + (std::int32_t)tm.tm_year), _dayOfWeek((std::int32_t)tm.tm_wday)
	{
	}

	bool DateTime::Tm::IsValid() const
	{
		return (Year != -1 && Month >= 0 && Month < 12 && (Day > 0 && Day <= Implementation::GetNumberOfDaysInMonth(Month, Year)) &&
			(Hour >= 0 && Hour < 24) && (Minute >= 0 && Minute < 60) && (Second >= 0 && Second < 62) && (Millisecond >= 0 && Millisecond < 1000));
	}

	void DateTime::Tm::AddMonths(std::int32_t monDiff)
	{
		while (monDiff < -Month) {
			Year--;
			monDiff += MONTHS_IN_YEAR;
		}

		while (monDiff + Month >= MONTHS_IN_YEAR) {
			Year++;
			monDiff -= MONTHS_IN_YEAR;
		}

		Month += monDiff;
	}

	void DateTime::Tm::AddDays(std::int32_t dayDiff)
	{
		while (dayDiff + Day < 1) {
			AddMonths(-1);
			dayDiff += Implementation::GetNumberOfDaysInMonth(Month, Year);
		}

		Day += dayDiff;

		while (Day > Implementation::GetNumberOfDaysInMonth(Month, Year)) {
			Day -= Implementation::GetNumberOfDaysInMonth(Month, Year);
			AddMonths(1);
		}
	}

	void DateTime::Tm::ComputeWeekDay()
	{
		_dayOfWeek = (Implementation::GetTruncatedJDN(Day, Month, Year) + 2) % 7;
	}

	DateTime DateTime::Now()
	{
#if defined(DEATH_TARGET_WINDOWS)
		SYSTEMTIME st;
		::GetLocalTime(&st);
		return DateTime(st);
#else
		time_t t = time(nullptr);
		return DateTime(t);
#endif
	}

	DateTime DateTime::UtcNow()
	{
#if defined(DEATH_TARGET_WINDOWS)
		SYSTEMTIME st;
		::GetSystemTime(&st);
		return DateTime(st);
#else
		time_t t = time(nullptr);
		DateTime dt = DateTime(t);
		dt.AdjustToTimezone(UTC);
		return dt;
#endif
	}

#if defined(DEATH_TARGET_WINDOWS)
	DateTime::DateTime(const struct _SYSTEMTIME& st)
	{
		Set(st.wYear, st.wMonth - 1, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}

	DateTime::DateTime(const struct _FILETIME& ft)
	{
		ULARGE_INTEGER value;
		value.LowPart = ft.dwLowDateTime;
		value.HighPart = ft.dwHighDateTime;
		_time = ((value.QuadPart - 116444736000000000LL) / 10000LL);
	}
#endif

	DateTime& DateTime::Set(const struct tm& tm)
	{
		struct tm tm2(tm);
		time_t timet = mktime(&tm2);

		if (timet == (time_t)-1) {
			// mktime() fails for Jan 1, 1970 if the hour is less than timezone - try to make it work for this case
			if (tm2.tm_year == 70 && tm2.tm_mon == 0 && tm2.tm_mday == 1) {
				return Set((time_t)(
					Implementation::GetTimeZone() +
					tm2.tm_hour * MIN_PER_HOUR * SEC_PER_MIN +
					tm2.tm_min * SEC_PER_MIN +
					tm2.tm_sec));
			}

			LOGE("mktime() failed");

			_time = INT64_MIN;
			return *this;
		}

		// Standardize on moving the time forwards to have consistent behaviour under all platforms and to avoid problems
		if (tm2.tm_hour != tm.tm_hour) {
			tm2 = tm;
			tm2.tm_hour++;
			if (tm2.tm_hour == 24) {
				// This shouldn't normally happen as the DST never starts at 23:00 but if it does, we have a problem as we need to adjust the day
				// as well. However we stop here, i.e. we don't adjust the month (or the year) because mktime() is supposed to take care of this for us.
				tm2.tm_hour = 0;
				tm2.tm_mday++;
			}

			timet = mktime(&tm2);
		}

		return Set(timet);
	}

	DateTime& DateTime::Set(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute, std::int32_t second, std::int32_t millisec)
	{
		DEATH_ASSERT(month >= 0 && month < 12 && day > 0 && day <= Implementation::GetNumberOfDaysInMonth(month, year) &&
			hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 61 /* with leap second */ &&
			millisec >= 0 && millisec < 1000, *this, "Invalid date in DateTime::Set()");

		// The range of time_t type (inclusive)
		constexpr std::int32_t YearMinInRange = 1970;
		constexpr std::int32_t YearMaxInRange = 2037;

		if (year >= YearMinInRange && year <= YearMaxInRange) {
			// Use the standard library version if the date is in range
			struct tm tm;
			tm.tm_year = year - 1900;
			tm.tm_mon = month;
			tm.tm_mday = day;
			tm.tm_hour = hour;
			tm.tm_min = minute;
			tm.tm_sec = second;
			tm.tm_isdst = -1;		// mktime() will guess it
			Set(tm);

			if (IsValid()) {
				SetMillisecond(millisec);
			}
		} else {
			// Get the JDN for the midnight of this day
			_time = Implementation::GetTruncatedJDN(day, month, year);
			_time -= EPOCH_JDN;
			_time *= SECONDS_PER_DAY * 1000;

			// JDN corresponds to GMT, we take localtime
			_time += TimeSpan(hour, minute, second + Implementation::GetTimeZone(), millisec).GetValue();
		}

		return *this;
	}

	DateTime::Tm DateTime::Partitioned(const TimeZone tz) const
	{
		if (!IsValid()) {
			return { };
		}

		time_t time = GetTicks();
		if (time != (time_t)-1) {
			struct tm temp;
			if (const tm* tm = Implementation::TryGetTm(time, tz, &temp)) {
				Tm tm2(*tm, tz);
				std::int32_t timeOnly = (std::int32_t)(_time % MILLISECONDS_PER_DAY);
				tm2.Millisecond = (std::int32_t)(timeOnly % 1000);
				return tm2;
			}
		}

		// If standard library functions cannot be used, try to guess it
		std::int32_t secDiff = tz.GetOffset();
		std::int64_t timeMidnight = _time + (secDiff * 1000);
		std::int32_t timeOnly = (std::int32_t)(timeMidnight % MILLISECONDS_PER_DAY);

		if (timeOnly < 0) {
			timeOnly = MILLISECONDS_PER_DAY + timeOnly;
		}

		timeMidnight -= timeOnly;

		std::int32_t jdn = (std::int32_t)(timeMidnight / MILLISECONDS_PER_DAY) + EPOCH_JDN;

		// Calculate the century
		std::int32_t temp = (jdn + JDN_OFFSET) * 4 - 1;
		std::int32_t century = temp / DAYS_PER_400_YEARS;

		// Then the year and day of year (1 <= dayOfYear <= 366)
		temp = ((temp % DAYS_PER_400_YEARS) / 4) * 4 + 3;
		std::int32_t year = (century * 100) + (temp / DAYS_PER_4_YEARS);
		std::int32_t dayOfYear = (temp % DAYS_PER_4_YEARS) / 4 + 1;

		// And finally the month and day of the month
		temp = dayOfYear * 5 - 3;
		std::int32_t month = temp / DAYS_PER_5_MONTHS;
		std::int32_t day = (temp % DAYS_PER_5_MONTHS) / 5 + 1;

		// Month is counted from March, so shift it back
		if (month < 10) {
			month += 3;
		} else {
			year += 1;
			month -= 9;
		}

		// Year is offset by 4800
		year -= 4800;

		// Construct Tm from these values
		Tm tm;
		tm.Year = year;
		tm.DayOfYear = dayOfYear - 1;
		tm.Month = month - 1;
		tm.Day = day;
		tm.Millisecond = timeOnly % 1000;
		timeOnly -= tm.Millisecond;
		timeOnly /= 1000;

		tm.Second = timeOnly % SEC_PER_MIN;
		timeOnly -= tm.Second;
		timeOnly /= SEC_PER_MIN;

		tm.Minute = timeOnly % MIN_PER_HOUR;
		timeOnly -= tm.Minute;

		tm.Hour = timeOnly / MIN_PER_HOUR;

		return tm;
	}

	DateTime& DateTime::SetYear(std::int32_t year)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Year = year;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetMonth(std::int32_t month)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Month = month;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetDay(std::int32_t mday)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Day = mday;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetHour(std::int32_t hour)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Hour = hour;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetMinute(std::int32_t min)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Minute = min;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetSecond(std::int32_t sec)
	{
		if (IsValid()) {
			Tm tm(Partitioned());
			tm.Second = sec;
			Set(tm);
		}
		return *this;
	}

	DateTime& DateTime::SetMillisecond(std::int32_t millisecond)
	{
		if (IsValid()) {
			_time -= _time % 1000;
			_time += millisecond;
		}
		return *this;
	}

	DateTime& DateTime::ResetTime()
	{
		Tm tm(Partitioned());
		if (tm.Hour != 0 || tm.Minute != 0 || tm.Second != 0 || tm.Millisecond != 0) {
			tm.Millisecond = tm.Second = tm.Minute = tm.Hour = 0;
			Set(tm);
		}
		return *this;
	}

	void DateTime::AdjustToTimezone(const TimeZone tz, bool noDST)
	{
		std::int32_t secDiff = Implementation::GetTimeZone() + tz.GetOffset();

		// We are converting from the local time to some other time zone, but local time zone does not include the DST offset
		// (as it varies depending on the date), so we have to handle DST manually, unless a special flag inhibiting this was specified.
		if (!noDST && Implementation::IsDST(*this) && !tz.IsLocal()) {
			secDiff -= DST_OFFSET;
		}

		_time += secDiff * 1000;
	}

	void DateTime::AdjustFromTimezone(const TimeZone tz, bool noDST)
	{
		std::int32_t secDiff = Implementation::GetTimeZone() + tz.GetOffset();

		if (!noDST && Implementation::IsDST(*this) && !tz.IsLocal()) {
			secDiff -= DST_OFFSET;
		}

		_time -= secDiff * 1000;
	}

#if defined(DEATH_TARGET_WINDOWS)
	struct _SYSTEMTIME DateTime::ToWin32() const
	{
		Tm tm(Partitioned());

		struct _SYSTEMTIME st;
		st.wYear = (WORD)tm.Year;
		st.wMonth = (WORD)(tm.Month + 1);
		st.wDay = (WORD)tm.Day;

		st.wDayOfWeek = 0;
		st.wHour = (WORD)tm.Hour;
		st.wMinute = (WORD)tm.Minute;
		st.wSecond = (WORD)tm.Second;
		st.wMilliseconds = (WORD)tm.Millisecond;
		return st;
	}
#endif

	bool DateTime::TryParse(const StringView input, const StringView format, StringView* endParse)
	{
		std::size_t endIndex;
		if (!Implementation::TryParseFormat(*this, input.data(), input.size(), format.data(), format.size(), &endIndex)) {
			return false;
		}
		if (endParse != nullptr) {
			*endParse = input.exceptPrefix(endIndex);
		}
		return true;
	}

#if defined(DEATH_USE_WCHAR)
	bool DateTime::TryParse(const std::wstring_view input, const std::wstring_view format, std::wstring_view::const_iterator* endParse)
	{
		std::size_t endIndex;
		if (!Implementation::TryParseFormat(*this, input.data(), input.size(), format.data(), format.size(), &endIndex)) {
			return false;
		}
		if (endParse != nullptr) {
			*endParse = input.begin() + endIndex;
		}
		return true;
	}
#endif

}}