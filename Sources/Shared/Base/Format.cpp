#include "Format.h"
#include "../Asserts.h"
#include "../Containers/ArrayView.h"
#include "../Containers/StringView.h"

#include <cstring>
#include <intrin.h>
#include <limits>
#include <type_traits>

namespace Death { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	enum class FormatType : unsigned char {
		Unspecified,
		Character,
		Octal,
		Decimal,
		Hexadecimal,
		HexadecimalUppercase,
		Float,
		FloatUppercase,
		FloatExponent,
		FloatExponentUppercase,
		FloatFixed,
		FloatFixedUppercase
	};

	struct FormatContext {
		std::int32_t Precision;
		FormatType Type;
	};

	template<typename T>
	using is_signed = std::integral_constant<bool, std::numeric_limits<T>::is_signed>;

	template<typename T>
	using uint32_or_64_t = std::conditional_t<std::numeric_limits<T>::digits <= 32, std::uint32_t, std::uint64_t>;

	template<typename T, std::enable_if_t<is_signed<T>::value, int> = 0>
	constexpr bool isNegative(T value) {
		return (value < 0);
	}
	template<typename T, std::enable_if_t<!is_signed<T>::value, int> = 0>
	constexpr bool isNegative(T) {
		return false;
	}

#if !defined(DEATH_TARGET_MSVC)
#	if defined(__has_builtin)
#		if __has_builtin(__builtin_clz)
#			define __DEATH_HAS_BUILTIN_CLZ(n) __builtin_clz(n)
#		endif
#		if __has_builtin(__builtin_clzll)
#			define __DEATH_HAS_BUILTIN_CLZLL(n) __builtin_clzll(n)
#		endif
#	elif defined(DEATH_TARGET_GCC)
#		define __DEATH_HAS_BUILTIN_CLZ(n) __builtin_clz(n)
#		define __DEATH_HAS_BUILTIN_CLZLL(n) __builtin_clzll(n)
#	endif
#endif

#if defined(DEATH_TARGET_MSVC) && !defined(__DEATH_HAS_BUILTIN_CLZLL)
	inline std::int32_t clz(std::uint32_t x) {
		DEATH_DEBUG_ASSERT(x != 0);
		unsigned long r = 0;
		_BitScanReverse(&r, x);
		return 31 ^ static_cast<std::int32_t>(r);
	}
#	define __DEATH_HAS_BUILTIN_CLZ(n) clz(n)

	inline std::int32_t clzll(std::uint64_t x) {
		DEATH_DEBUG_ASSERT(x != 0);
		unsigned long r = 0;
#	if defined(DEATH_TARGET_32BIT)
		if (_BitScanReverse(&r, static_cast<uint32_t>(x >> 32))) {
			return 63 ^ static_cast<std::int32_t>(r + 32);
		}
		_BitScanReverse(&r, static_cast<uint32_t>(x));
#	else
		_BitScanReverse64(&r, x);
#	endif
		return 63 ^ static_cast<std::int32_t>(r);
	}
#	define __DEATH_HAS_BUILTIN_CLZLL(n) clzll(n)
#endif

#if defined(__DEATH_HAS_BUILTIN_CLZLL)
#	define POWERS_OF_10(factor)											\
		factor * 10, (factor) * 100, (factor) * 1000, (factor) * 10000,	\
			(factor) * 100000, (factor) * 1000000, (factor) * 10000000,	\
			(factor) * 100000000, (factor) * 1000000000

	inline std::int32_t countDigitsWithClzll(std::uint64_t n) {
		static constexpr std::uint8_t Bsr2log10[] = {
			1,  1,  1,  2,  2,  2,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,
			6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9,  10, 10, 10,
			10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15, 15,
			15, 16, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 19, 19, 20
		};

		static constexpr std::uint64_t ZeroOrPowersOf10[] = {
			0, 0, POWERS_OF_10(1U), POWERS_OF_10(1000000000ULL),
			10000000000000000000ULL
		};

		std::uint8_t t = Bsr2log10[__DEATH_HAS_BUILTIN_CLZLL(n | 1) ^ 63];
		return t - (n < ZeroOrPowersOf10[t]);
	}
#endif

	inline std::int32_t countDigits(std::uint64_t n) {
#if defined(__DEATH_HAS_BUILTIN_CLZLL)
		return countDigitsWithClzll(n);
#else
		std::int32_t digitCount = 1;
		while (true) {
			if (n < 10) return digitCount;
			if (n < 100) return digitCount + 1;
			if (n < 1000) return digitCount + 2;
			if (n < 10000) return digitCount + 3;
			n /= 10000u;
			digitCount += 4;
		}
		return digitCount;
#endif
	}

#if defined(__DEATH_HAS_BUILTIN_CLZ)
#	define K(T) (((sizeof(#T) - 1ull) << 32) - T)

	inline std::int32_t countDigitsWithClz(std::uint32_t n) {
		// An optimization by Kendall Willets from https://bit.ly/3uOIQrB.
		// This increments the upper 32 bits (log10(T) - 1) when >= T is added.
		static constexpr std::uint64_t Table[] = {
			K(0),			K(0),			K(0),			// 8
			K(10),			K(10),			K(10),			// 64
			K(100),			K(100),			K(100),			// 512
			K(1000),		K(1000),		K(1000),		// 4096
			K(10000),		K(10000),		K(10000),		// 32k
			K(100000),		K(100000),		K(100000),		// 256k
			K(1000000),		K(1000000),		K(1000000),		// 2048k
			K(10000000),	K(10000000),	K(10000000),	// 16M
			K(100000000),	K(100000000),	K(100000000),	// 128M
			K(1000000000),	K(1000000000),	K(1000000000),	// 1024M
			K(1000000000),	K(1000000000)					// 4B
		};
		std::uint64_t inc = Table[__DEATH_HAS_BUILTIN_CLZ(n | 1) ^ 31];
		return static_cast<std::int32_t>((n + inc) >> 32);
	}
#endif

	inline std::int32_t countDigits(std::uint32_t n) {
#if defined(__DEATH_HAS_BUILTIN_CLZ)
		return countDigitsWithClz(n);
#else
		std::int32_t digitCount = 1;
		while (true) {
			if (n < 10) return digitCount;
			if (n < 100) return digitCount + 1;
			if (n < 1000) return digitCount + 2;
			if (n < 10000) return digitCount + 3;
			n /= 10000u;
			digitCount += 4;
		}
		return digitCount;
#endif
	}

	template<int Bits, typename TUint>
	std::int32_t countDigits(TUint n) {
#if defined(__DEATH_HAS_BUILTIN_CLZLL)
		if (std::numeric_limits<TUint>::digits == 32) {
			return (__DEATH_HAS_BUILTIN_CLZ(static_cast<std::uint32_t>(n) | 1) ^ 31) / Bits + 1;
		}
#endif
		std::int32_t digitCount = 0;
		do {
			++digitCount;
		} while ((n >>= Bits) != 0);
		return digitCount;
	}

	inline const char* getTwoDigits(std::size_t value) {
		alignas(2) static const char Digits[] =
			"0001020304050607080910111213141516171819"
			"2021222324252627282930313233343536373839"
			"4041424344454647484950515253545556575859"
			"6061626364656667686970717273747576777879"
			"8081828384858687888990919293949596979899";
		return &Digits[value * 2];
	}

	template<typename TUint>
	char* formatDecimal(char* out, TUint value, std::int32_t digitCount) {
		unsigned n = static_cast<std::make_unsigned<std::int32_t>::type>(digitCount);
		while (value >= 100) {
			n -= 2;
			std::memcpy(out + n, getTwoDigits(static_cast<unsigned>(value % 100)), 2);
			value /= 100;
		}
		if (value >= 10) {
			n -= 2;
			std::memcpy(out + n, getTwoDigits(static_cast<unsigned>(value)), 2);
		} else {
			out[--n] = static_cast<char>('0' + value);
		}
		return out + n;
	}

	template<int Bits, typename TUint>
	constexpr char* formatBase2e(char* out, TUint value, std::int32_t digitCount, bool upperCase = false) {
		const char* digits = (upperCase ? "0123456789ABCDEF" : "0123456789abcdef");
		out += digitCount;
		do {
			unsigned digit = static_cast<unsigned>(value & ((1u << Bits) - 1));
			*--out = (Bits < 4 ? static_cast<char>('0' + digit) : digits[digit]);
		} while ((value >>= Bits) != 0);
		return out;
	}

	template<typename T>
	constexpr std::int32_t formatNumber(const Containers::MutableStringView& buffer, T value, FormatContext& context) {
		std::int32_t precision = context.Precision;
		if (precision == -1) {
			precision = 1;
		}

		switch (context.Type) {
			case FormatType::Character: {
				if (1 <= buffer.size()) {
					char* begin = buffer.data();
					*begin = static_cast<char>(value);
				}
				return 1;
			}
			case FormatType::Unspecified:
			case FormatType::Decimal: {
				auto absValue = static_cast<uint32_or_64_t<T>>(value);
				bool negative = isNegative(value);
				if (negative) absValue = ~absValue + 1;

				std::int32_t digitCount = countDigits(absValue);
				std::int32_t size = (negative ? 1 : 0) + (digitCount < precision ? precision : digitCount);

				if (size <= buffer.size()) {
					char* begin = buffer.data();
					if (negative) {
						*begin++ = '-';
					}
					if (digitCount < precision) {
						for (std::int32_t i = 0; i < precision - digitCount; i++) {
							*begin++ = '0';
						}
					}
					formatDecimal(begin, absValue, digitCount);
				}

				return size;
			}
			case FormatType::Octal: {
				auto absValue = static_cast<uint32_or_64_t<T>>(value);

				std::int32_t digitCount = countDigits<3>(absValue);
				std::int32_t size = (digitCount < precision ? precision : digitCount + 1);

				if (buffer.data() && size <= buffer.size()) {
					char* begin = buffer.data();
					if (digitCount < precision) {
						for (std::int32_t i = 0; i < precision - digitCount; i++) {
							*begin++ = '0';
						}
					} else {
						*begin++ = '0'; // '0' prefix for octal numbers
					}
					formatBase2e<3>(begin, absValue, digitCount, false);
				}

				return size;
			}
			case FormatType::Hexadecimal:
			case FormatType::HexadecimalUppercase: {
				auto absValue = static_cast<uint32_or_64_t<T>>(value);

				std::int32_t digitCount = countDigits<4>(absValue);
				std::int32_t size = (digitCount < precision ? precision : digitCount);

				if (buffer.data() && size <= buffer.size()) {
					char* begin = buffer.data();
					if (digitCount < precision) {
						for (std::int32_t i = 0; i < precision - digitCount; i++) {
							*begin++ = '0';
						}
					}
					formatBase2e<4>(begin, absValue, digitCount, context.Type == FormatType::HexadecimalUppercase);
				}

				return size;
			}

			default:
			case FormatType::Float:
			case FormatType::FloatUppercase:
			case FormatType::FloatExponent:
			case FormatType::FloatExponentUppercase:
			case FormatType::FloatFixed:
			case FormatType::FloatFixedUppercase:
				DEATH_ASSERT_UNREACHABLE("Floating-point type used for an integral value", 0);
				return 0;
		}
	}

	template<class> char formatTypeChar(FormatType type);

	template<> char formatTypeChar<float>(FormatType type) {
		switch (type) {
			case FormatType::Unspecified:
			case FormatType::Float: return 'g';
			case FormatType::FloatUppercase: return 'G';
			case FormatType::FloatExponent: return 'e';
			case FormatType::FloatExponentUppercase: return 'E';
			case FormatType::FloatFixed: return 'f';
			case FormatType::FloatFixedUppercase: return 'F';

			case FormatType::Character:
				DEATH_ASSERT_UNREACHABLE("Character type used for a floating-point value", 'g');
				return 'g';

			case FormatType::Decimal:
			case FormatType::Octal:
			case FormatType::Hexadecimal:
			case FormatType::HexadecimalUppercase:
				DEATH_ASSERT_UNREACHABLE("Integral type used for a floating-point value", 'g');
				return 'g';
		}

		DEATH_ASSERT_UNREACHABLE();
		return 'g';
	}

	std::size_t Formatter<int>::format(const Containers::MutableStringView& buffer, int value, FormatContext& context) {
		return formatNumber(buffer, value, context);
	}

	std::size_t Formatter<unsigned int>::format(const Containers::MutableStringView& buffer, unsigned int value, FormatContext& context) {
		return formatNumber(buffer, value, context);
	}

	std::size_t Formatter<long long>::format(const Containers::MutableStringView& buffer, long long value, FormatContext& context) {
		return formatNumber(buffer, value, context);
	}

	std::size_t Formatter<unsigned long long>::format(const Containers::MutableStringView& buffer, unsigned long long value, FormatContext& context) {
		return formatNumber(buffer, value, context);
	}

	std::size_t Formatter<float>::format(const Containers::MutableStringView& buffer, float value, FormatContext& context) {
		std::int32_t precision = context.Precision;
		if (precision == -1) precision = std::numeric_limits<float>::digits10;
		const char format[] { '%', '.', '*', formatTypeChar<float>(context.Type), '\0' };
		return std::snprintf(buffer.data(), buffer.size(), format, precision, double(value));
	}

	std::size_t Formatter<double>::format(const Containers::MutableStringView& buffer, double value, FormatContext& context) {
		std::int32_t precision = context.Precision;
		if (precision == -1) precision = std::numeric_limits<double>::digits10;
		const char format[] { '%', '.', '*', formatTypeChar<float>(context.Type), '\0' };
		return std::snprintf(buffer.data(), buffer.size(), format, precision, value);
	}

	std::size_t Formatter<long double>::format(const Containers::MutableStringView& buffer, long double value, FormatContext& context) {
		std::int32_t precision = context.Precision;
		if (precision == -1) precision = std::numeric_limits<long double>::digits10;
		const char format[] { '%', '.', '*', 'L', formatTypeChar<float>(context.Type), '\0' };
		return std::snprintf(buffer.data(), buffer.size(), format, precision, value);
	}

	std::size_t Formatter<Containers::StringView>::format(const Containers::MutableStringView& buffer, Containers::StringView value, FormatContext& context) {
		std::size_t size = value.size();
		std::int32_t precision = context.Precision;
		if(std::size_t(precision) < size) size = precision;
		DEATH_ASSERT(context.Type == FormatType::Unspecified, "Type specifier cannot be used for a string value", {});
		if (buffer.data() && size) std::memcpy(buffer.data(), value.data(), size);
		return size;
	}

	std::size_t Formatter<bool>::format(const Containers::MutableStringView& buffer, bool value, FormatContext& context) {
		using namespace Death::Containers::Literals;
		DEATH_ASSERT(context.Type == FormatType::Unspecified, "Type specifier cannot be used for a bool value", {});
		return Formatter<Containers::StringView>::format(buffer, value ? "true"_s : "false"_s, context);
	}

	std::size_t Formatter<const char*>::format(const Containers::MutableStringView& buffer, const char* value, FormatContext& context) {
		return Formatter<Containers::StringView>::format(buffer, value, context);
	}

	namespace
	{
		std::int32_t parseNumber(Containers::StringView format, std::size_t& formatOffset) {
			std::int32_t number = -1;
			while (formatOffset < format.size() && format[formatOffset] >= '0' && format[formatOffset] <= '9') {
				if (number == -1) number = 0;
				else number *= 10;
				number += (format[formatOffset] - '0');
				++formatOffset;
			}
			return number;
		}

		template<class Writer, class FormattedWriter, class Formatter>
		void formatWith(const Writer writer, const FormattedWriter formattedWriter, Containers::StringView format, Containers::ArrayView<Formatter> formatters) {
			bool inPlaceholder = false;
			std::size_t placeholderOffset = 0;
			std::size_t formatterToGo = 0;
			int placeholderIndex = -1;
			FormatContext context{-1, FormatType::Unspecified};
			for (std::size_t formatOffset = 0; formatOffset != format.size(); ) {
				// Placeholder begin (or escaped {)
				if (format[formatOffset] == '{') {
					if (formatOffset + 1 < format.size() && format[formatOffset+1] == '{') {
						writer(format.slice(formatOffset, formatOffset + 1));
						formatOffset += 2;
						continue;
					}

					DEATH_DEBUG_ASSERT(!inPlaceholder);
					inPlaceholder = true;
					placeholderOffset = formatOffset;
					placeholderIndex = -1;
					context.Precision = -1;
					context.Type = FormatType::Unspecified;

					formatOffset++;
					continue;
				}

				// Placeholder end (or escaped })
				if (format[formatOffset] == '}') {
					if (!inPlaceholder && formatOffset + 1 < format.size() && format[formatOffset+1] == '}') {
						writer(format.slice(formatOffset, formatOffset + 1));
						formatOffset += 2;
						continue;
					}

					DEATH_ASSERT(inPlaceholder, "Mismatched }", );
					inPlaceholder = false;

					// If the placeholder was numbered, use that number, otherwise just use the formatter that's next
					if (placeholderIndex != -1) formatterToGo = placeholderIndex;

					if (formatterToGo < formatters.size()) {
						// Formatter index is in bounds, write
						formattedWriter(formatters[formatterToGo], context);
					} else {
						// Otherwise just verbatim copy the placeholder (including })
						writer(format.slice(placeholderOffset, formatOffset + 1));
					}

					// Next time we see an unnumbered placeholder, take the next formatter
					formatterToGo++;

					formatOffset++;
					continue;
				}

				// Placeholder contents
				if (inPlaceholder) {
					// Placeholder index
					placeholderIndex = parseNumber(format, formatOffset);

					// Formatting options
					if (formatOffset < format.size() && format[formatOffset] == ':') {
						formatOffset++;

						// Precision
						if (formatOffset + 1 < format.size() && format[formatOffset] == '.') {
							formatOffset++;
							context.Precision = parseNumber(format, formatOffset);
							DEATH_ASSERT(context.Precision != -1, ("Invalid character in precision specifier \"{}\"", format.slice(formatOffset, formatOffset + 1)), );
						}

						// Type
						if (formatOffset < format.size() && format[formatOffset] != '}') {
							switch(format[formatOffset]) {
								case 'c': context.Type = FormatType::Character; break;
								case 'o': context.Type = FormatType::Octal; break;
								case 'd': context.Type = FormatType::Decimal; break;
								case 'x': context.Type = FormatType::Hexadecimal; break;
								case 'X': context.Type = FormatType::HexadecimalUppercase; break;
								case 'g': context.Type = FormatType::Float; break;
								case 'G': context.Type = FormatType::FloatUppercase; break;
								case 'e': context.Type = FormatType::FloatExponent; break;
								case 'E': context.Type = FormatType::FloatExponentUppercase; break;
								case 'f': context.Type = FormatType::FloatFixed; break;
								case 'F': context.Type = FormatType::FloatFixedUppercase; break;
								default:
									DEATH_ASSERT(false, ("Invalid type specifier \"{}\"", format.slice(formatOffset, formatOffset + 1)), );
									break;
							}
							formatOffset++;
						}
					}

					// Unexpected end, break -- the assert at the end of function takes care of this
					if (formatOffset == format.size()) break;

					// Next should be the placeholder end
					DEATH_ASSERT(format[formatOffset] == '}', ("Unknown placeholder content \"{}\"", format.slice(formatOffset, formatOffset + 1)), );
					continue;
				}

				// Other things, just copy. Grab as much as I can to avoid calling functions on single bytes.
				std::size_t next = formatOffset;
				while (next < format.size() && format[next] != '{' && format[next] != '}') {
					next++;
				}

				writer(format.slice(formatOffset, next));
				formatOffset = next;
			}

			DEATH_ASSERT(!inPlaceholder, "Unexpected end of format string", );
		}
	}

	std::size_t formatFormatters(char* buffer, std::size_t bufferSize, const char* const format, BufferFormatter* const formatters, std::size_t formatterCount) {
		std::size_t bufferOffset = 0;
		formatWith([buffer, bufferSize, &bufferOffset](Containers::StringView data) {
			if (buffer != nullptr) {
				std::memcpy(buffer + bufferOffset, data.data(), data.size());
			}
			bufferOffset += data.size();
		}, [buffer, bufferSize, &bufferOffset](BufferFormatter& formatter, FormatContext& context) {
			std::size_t size;
			if (buffer != nullptr) {
				size = formatter(Containers::MutableStringView{buffer + bufferOffset, bufferSize - bufferOffset}, context);
				DEATH_DEBUG_ASSERT(bufferOffset + size <= bufferSize, ("Buffer too small, expected at least {} but got {}", bufferOffset + size, bufferSize), );
			} else {
				size = formatter(nullptr, context);
			}
			bufferOffset += size;
		}, format, Containers::arrayView(formatters, formatterCount));
		return bufferOffset;
	}

}}
