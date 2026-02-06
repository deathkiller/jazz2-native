#include "I18n.h"
#include "../Main.h"
#include "Base/Algorithms.h"

#if defined(DEATH_TARGET_ANDROID)
#	include "Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_APPLE)
#	include <CoreFoundation/CFPreferences.h>
#	include <CoreFoundation/CFPropertyList.h>
#	include <CoreFoundation/CFArray.h>
#	include <CoreFoundation/CFString.h>
#endif

#include <Environment.h>
#include <Utf8.h>
#include <Base/Memory.h>
#include <Containers/GrowableArray.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

using namespace Death;
using namespace Death::Containers::Literals;
using namespace Death::Memory;

namespace nCine
{
	struct LanguageInfo
	{
		const char* Identifier;
		const StringView Name;
	};

	/** @brief List of all supported languages */
	static constexpr LanguageInfo SupportedLanguages[] = {
		{ "af", "Afrikaans"_s },
		{ "ar", "Arabic"_s },
		{ "be", "БЕЛАРУСКАЯ"_s },
		{ "bg", "БЪЛГАРСКИ"_s },
		{ "cs", "Čeština"_s },
		{ "da", "Dansk"_s },
		{ "de", "Deutsch"_s },
		{ "el", "Ελληνική"_s },
		{ "en", "English"_s },
		{ "es", "Español"_s },
		{ "et", "Eesti"_s },
		{ "fi", "Suomi"_s },
		{ "fr", "Français"_s },
		{ "gl", "Galego"_s },
		{ "gr", "Greek"_s },
		{ "he", "Hebrew"_s },	// TODO
		{ "hr", "Hrvatski"_s },
		{ "hu", "Magyar"_s },
		{ "it", "Italiano"_s },
		{ "lt", "Lietuvių"_s },
		{ "lv", "Latviešu"_s },
		{ "mo", "ЛИМБА МОЛДОВЕНЯСКЭ"_s },
		{ "nb", "Norsk (Bokmål)"_s },
		{ "nl", "Nederlands"_s },
		{ "nn", "Norsk (Nynorsk)"_s },
		{ "no", "Norsk"_s },
		{ "pl", "Polski"_s },
		{ "pt", "Português"_s },
		{ "ro", "Română"_s },
		{ "ru", "РУССКИЙ"_s },
		{ "sk", "Slovenčina"_s },
		{ "sl", "Slovenščina"_s },
		{ "sr", "Srpski"_s },
		{ "sv", "Svenska"_s },
		{ "tr", "Türkçe"_s },
		{ "ua", "УКРАЇНСЬКА"_s }
	};

	struct MoFileHeader
	{
		std::uint32_t Signature;
		/** @brief The revision number of the file format */
		std::uint32_t Revision;

		/** @brief The number of strings pairs */
		std::uint32_t StringCount;
		/** @brief Offset of table with start offsets of original strings */
		std::uint32_t OrigTableOffset;
		/** @brief Offset of table with start offsets of translated strings */
		std::uint32_t TransTableOffset;
		/** @brief Size of hash table */
		std::uint32_t HashTableSize;
		/** @brief Offset of first hash table entry */
		std::uint32_t HashTableOffset;
	};

	struct I18n::StringDesc
	{
		/** @brief Length of addressed string, not including the trailing NULL */
		std::uint32_t Length;
		/** @brief Offset of string in file */
		std::uint32_t Offset;
	};

	enum class ExpressionTokenType
	{
		Unknown,
		Value,
		Variable,

		Add,
		Subtract,
		Multiply,
		Divide,
		Remainder,

		And,
		Or,

		Equals,
		NotEquals,
		Less,
		EqualsOrLess,
		EqualsOrGreater,
		Greater,

		Ternary
	};

	struct I18n::ExpressionToken
	{
		ExpressionTokenType Type;
		std::int32_t Value;
		ExpressionToken* Comparison;
		ExpressionToken* Left;
		ExpressionToken* Right;

		ExpressionToken(ExpressionTokenType type)
			: Type(type), Value(0), Comparison(nullptr), Left(nullptr), Right(nullptr)
		{
		}

		ExpressionToken(ExpressionTokenType type, std::int32_t value)
			: Type(type), Value(value), Comparison(nullptr), Left(nullptr), Right(nullptr)
		{
		}

		ExpressionToken(ExpressionTokenType type, ExpressionToken* left, ExpressionToken* right)
			: Type(type), Value(0), Comparison(nullptr), Left(left), Right(right)
		{
		}

		ExpressionToken(ExpressionTokenType type, ExpressionToken* comparison, ExpressionToken* left, ExpressionToken* right)
			: Type(type), Value(0), Comparison(comparison), Left(left), Right(right)
		{
		}

		~ExpressionToken()
		{
			if (Comparison != nullptr) {
				delete Comparison;
			}
			if (Left != nullptr) {
				delete Left;
			}
			if (Right != nullptr) {
				delete Right;
			}
		}

		std::int32_t operator()(std::int32_t n) const noexcept
		{
			switch (Type) {
				default:
				case ExpressionTokenType::Unknown: return INT32_MIN;

				case ExpressionTokenType::Value: return Value;
				case ExpressionTokenType::Variable: return n;

				case ExpressionTokenType::Add: return (*Left)(n) + (*Right)(n);
				case ExpressionTokenType::Subtract: return (*Left)(n) - (*Right)(n);
				case ExpressionTokenType::Multiply: return (*Left)(n) * (*Right)(n);
				case ExpressionTokenType::Divide: return (*Left)(n) / (*Right)(n);
				case ExpressionTokenType::Remainder: return (*Left)(n) % (*Right)(n);

				case ExpressionTokenType::And: return (*Left)(n) != 0 && (*Right)(n) != 0;
				case ExpressionTokenType::Or: return (*Left)(n) != 0 || (*Right)(n) != 0;

				case ExpressionTokenType::Equals: return (*Left)(n) == (*Right)(n);
				case ExpressionTokenType::NotEquals: return (*Left)(n) != (*Right)(n);
				case ExpressionTokenType::Less: return (*Left)(n) < (*Right)(n);
				case ExpressionTokenType::EqualsOrLess: return (*Left)(n) <= (*Right)(n);
				case ExpressionTokenType::EqualsOrGreater: return (*Left)(n) >= (*Right)(n);
				case ExpressionTokenType::Greater: return (*Left)(n) > (*Right)(n);

				case ExpressionTokenType::Ternary: return (*Comparison)(n) ? (*Left)(n) : (*Right)(n);
			}
		}
	};

	class I18n::Evaluator
	{
	public:
		static const ExpressionToken* Parse(const char* s);

	private:
		static ExpressionToken* ParseNumber(const char*& s, bool negative);
		static ExpressionToken* ParseAtom(const char*& s);
		static ExpressionToken* ParseFactors(const char*& s);
		static ExpressionToken* ParseSummands(const char*& s);
		static ExpressionToken* ParseComparison(const char*& s);
		static ExpressionToken* ParseCondition(const char*& s);
		static ExpressionToken* ParseTernary(const char*& s);

		static bool ContainsUnknownToken(ExpressionToken* token);
	};

	const I18n::ExpressionToken* I18n::Evaluator::Parse(const char* s)
	{
		I18n::ExpressionToken* result = ParseTernary(s);
		while (*s == ' ') {
			s++;
		}
		if ((*s != '\0' && *s != ';' && *s != '\r' && *s != '\n') || ContainsUnknownToken(result)) {
			delete result;
			return nullptr;
		}
		return result;
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseNumber(const char*& s, bool negative)
	{
		char* endPtr;
		std::int32_t value = strtol(s, &endPtr, 10);
		s = endPtr;

		if (negative) {
			value = -value;
		}

		return new ExpressionToken(ExpressionTokenType::Value, value);
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseAtom(const char*& s)
	{
		while (*s == ' ') {
			s++;
		}

		if (*s == '(') {
			s++;
			ExpressionToken* atom = ParseTernary(s);
			while (*s == ' ') {
				s++;
			}
			if (*s == ')') {
				s++;
				return atom;
			}
			// Unbalanced brackets
			return new ExpressionToken(ExpressionTokenType::Unknown);
		} else if (*s == 'n') {
			s++;
			// Variable
			return new ExpressionToken(ExpressionTokenType::Variable);
		} else if (std::isdigit(*s)) {
			return ParseNumber(s, false);
		} else if (*s == '-' && std::isdigit(*(s + 1))) {
			s++;
			return ParseNumber(s, true);
		}

		// Unexpected character
		return new ExpressionToken(ExpressionTokenType::Unknown);
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseFactors(const char*& s)
	{
		I18n::ExpressionToken* left = ParseAtom(s);
		while (*s != '\0') {
			if (*s == '*') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Multiply, left, ParseAtom(s));
				continue;
			} else if (*s == '/') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Divide, left, ParseAtom(s));
				continue;
			} else if (*s == '%') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Remainder, left, ParseAtom(s));
				continue;
			} else if (*s == ' ') {
				s++;
				continue;
			}
			break;
		}
		return left;
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseSummands(const char*& s)
	{
		I18n::ExpressionToken* left = ParseFactors(s);
		while (*s != '\0') {
			if (*s == '+') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Add, left, ParseFactors(s));
				continue;
			} else if (*s == '-') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Subtract, left, ParseFactors(s));
				continue;
			} else if (*s == ' ') {
				s++;
				continue;
			}
			break;
		}
		return left;
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseComparison(const char*& s)
	{
		I18n::ExpressionToken* left = ParseSummands(s);
		while (*s != '\0') {
			if (*s == '=' && *(s + 1) == '=') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::Equals, left, ParseSummands(s));
				continue;
			} else if (*s == '!' && *(s + 1) == '=') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::NotEquals, left, ParseSummands(s));
				continue;
			} else if (*s == '<' && *(s + 1) == '=') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::EqualsOrLess, left, ParseSummands(s));
				continue;
			} else if (*s == '>' && *(s + 1) == '=') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::EqualsOrGreater, left, ParseSummands(s));
				continue;
			} else if (*s == '<') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Less, left, ParseSummands(s));
				continue;
			} else if (*s == '>') {
				s++;
				left = new ExpressionToken(ExpressionTokenType::Greater, left, ParseSummands(s));
				continue;
			} else if (*s == ' ') {
				s++;
				continue;
			}
			break;
		}
		return left;
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseCondition(const char*& s)
	{
		I18n::ExpressionToken* left = ParseComparison(s);
		while (*s != '\0') {
			if (*s == '&' && *(s + 1) == '&') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::And, left, ParseComparison(s));
				continue;
			} else if (*s == '|' && *(s + 1) == '|') {
				s += 2;
				left = new ExpressionToken(ExpressionTokenType::Or, left, ParseComparison(s));
				continue;
			} else if (*s == ' ') {
				s++;
				continue;
			}
			break;
		}
		return left;
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseTernary(const char*& s)
	{
		I18n::ExpressionToken* comparison = ParseCondition(s);
		while (*s == ' ') {
			s++;
		}
		if (*s != '?') {
			return comparison;
		}
		s++;
		I18n::ExpressionToken* left = ParseTernary(s);
		while (*s == ' ') {
			s++;
		}
		if (*s != ':') {
			// Syntax error, ternary is not complete
			delete left;
			comparison->Type = ExpressionTokenType::Unknown;
			return comparison;
		}
		s++;
		return new ExpressionToken(ExpressionTokenType::Ternary, comparison, left, ParseTernary(s));
	}

	bool I18n::Evaluator::ContainsUnknownToken(ExpressionToken* token)
	{
		Array<ExpressionToken*> queue;
		arrayReserve(queue, 32);
		arrayAppend(queue, token);

		std::size_t i = 0;
		while (i < arraySize(queue)) {
			auto* token = queue[i++];
			if (token->Type == ExpressionTokenType::Unknown) {
				return true;
			}
			if (token->Comparison != nullptr) {
				arrayAppend(queue, token->Comparison);
			}
			if (token->Left != nullptr) {
				arrayAppend(queue, token->Left);
			}
			if (token->Right != nullptr) {
				arrayAppend(queue, token->Right);
			}
		}
		return false;
	}

	I18n& I18n::Get()
	{
		static I18n current;
		return current;
	}

	I18n::I18n()
		: _fileSize(0), _stringCount(0), _origTable(nullptr), _transTable(nullptr), _hashSize(0), _hashTable(nullptr), _pluralExpression(nullptr)
	{
	}

	I18n::~I18n()
	{
		if (_pluralExpression != nullptr) {
			delete _pluralExpression;
			_pluralExpression = nullptr;
		}
	}

	bool I18n::LoadFromFile(StringView path)
	{
		return LoadFromFile(fs::Open(path, FileAccess::Read), path);
	}

	bool I18n::LoadFromFile(const std::unique_ptr<Stream>& stream, StringView displayPath)
	{
		std::int64_t fileSize = stream->GetSize();
		if DEATH_UNLIKELY(fileSize < 32 || fileSize > 16 * 1024 * 1024) {
			if (fileSize > 0) {
				LOGE("Translation \"{}\" is corrupted", displayPath);
			}
			return false;
		}

		_file = std::make_unique<char[]>(fileSize + 1);
		stream->Read(_file.get(), fileSize);
		_file[fileSize] = '\0';
		_fileSize = std::uint32_t(fileSize);

		constexpr std::uint32_t SignatureLE = 0x950412de;
		constexpr std::uint32_t SignatureBE = 0xde120495;
		MoFileHeader* data = reinterpret_cast<MoFileHeader*>(_file.get());

		bool shouldSwapBytes = false;
		if DEATH_UNLIKELY(data->Signature == SignatureBE) {
			// Different endianness - swap bytes in all fields
			data->Signature = SignatureLE;
			data->Revision = SwapBytes(data->Revision);
			data->StringCount = SwapBytes(data->StringCount);
			data->OrigTableOffset = SwapBytes(data->OrigTableOffset);
			data->TransTableOffset = SwapBytes(data->TransTableOffset);
			data->HashTableSize = SwapBytes(data->HashTableSize);
			data->HashTableOffset = SwapBytes(data->HashTableOffset);
			shouldSwapBytes = true;
		}

		if DEATH_UNLIKELY(data->Signature != SignatureLE || data->StringCount <= 0 ||
			data->OrigTableOffset + data->StringCount > fileSize || data->TransTableOffset + data->StringCount > fileSize ||
			data->HashTableOffset + data->HashTableSize > fileSize) {
			LOGE("Translation \"{}\" is corrupted", displayPath);
			Unload();
			return false;
		}

		_stringCount = data->StringCount;
		_origTable = (StringDesc*)((char*)data + data->OrigTableOffset);
		_transTable = (StringDesc*)((char*)data + data->TransTableOffset);
		_hashSize = data->HashTableSize;
		_hashTable = (_hashSize > 2 ? (std::uint32_t*)((char*)data + data->HashTableOffset) : nullptr);

		if DEATH_UNLIKELY(shouldSwapBytes) {
			for (std::uint32_t i = 0; i < _stringCount; i++) {
				_origTable[i].Length = SwapBytes(_origTable[i].Length);
				_origTable[i].Offset = SwapBytes(_origTable[i].Offset);
				_transTable[i].Length = SwapBytes(_transTable[i].Length);
				_transTable[i].Offset = SwapBytes(_transTable[i].Offset);
			}
			if (_hashTable != nullptr) {
				for (std::uint32_t i = 0; i < _hashSize; i++) {
					_hashTable[i] = SwapBytes(_hashTable[i]);
				}
			}
		}

		if (_pluralExpression != nullptr) {
			delete _pluralExpression;
		}

		StringView nullEntry = LookupTranslation("");
		_pluralExpression = ExtractPluralExpression(nullEntry);

		LOGI("Translation \"{}\" loaded", displayPath);

		return true;
	}

	void I18n::Unload()
	{
		_file = nullptr;
		_fileSize = 0;
		_stringCount = 0;
		_origTable = nullptr;
		_transTable = nullptr;
		_hashSize = 0;
		_hashTable = nullptr;

		if (_pluralExpression != nullptr) {
			delete _pluralExpression;
			_pluralExpression = nullptr;
		}
	}

	StringView I18n::LookupTranslation(const char* msgid)
	{
		if DEATH_LIKELY(_hashTable != nullptr) {
			// Use the hash table for faster lookups
			constexpr std::uint32_t HashWordBits = 32;

			const char* str = msgid;
			std::uint32_t hashValue = 0;
			std::uint32_t len = 0;
			while (*str != '\0') {
				hashValue <<= 4;
				hashValue += (std::uint8_t)*str++;
				std::uint32_t g = hashValue & (0x0Fu << (HashWordBits - 4));
				if (g != 0) {
					hashValue ^= g >> (HashWordBits - 8);
					hashValue ^= g;
				}
				len++;
			}

			std::uint32_t idx = hashValue % _hashSize;
			std::uint32_t incr = 1 + (hashValue % (_hashSize - 2));

			while (true) {
				std::uint32_t nstr = _hashTable[idx];
				if DEATH_UNLIKELY(nstr == 0) {
					// Hash table entry is empty
					return {};
				}
				nstr--;

				// Compare `msgid` with the original string at index `nstr`.
				// We compare the lengths with `>=`, not `==`, because plural entries are represented by strings with an embedded NULL.
				if (nstr < _stringCount && _origTable[nstr].Length >= len && (std::strcmp(msgid, _file.get() + _origTable[nstr].Offset) == 0)) {
					if DEATH_UNLIKELY(_transTable[nstr].Offset >= _fileSize) {
						return {};
					}
					return StringView(&_file[_transTable[nstr].Offset], _transTable[nstr].Length);
				}

				if (idx >= _hashSize - incr) {
					idx -= _hashSize - incr;
				} else {
					idx += incr;
				}
			}
		} else {
			// Binary search in the sorted array of messages if hashing table is not available
			std::size_t bottom = 0;
			std::size_t top = _stringCount;
			while (bottom < top) {
				std::size_t idx = (bottom + top) / 2;
				if DEATH_UNLIKELY(_origTable[idx].Offset >= _fileSize) {
					return {};
				}
				std::int32_t cmpVal = std::strcmp(msgid, (_file.get() + _origTable[idx].Offset));
				if (cmpVal < 0) {
					top = idx;
				} else if (cmpVal > 0) {
					bottom = idx + 1;
				} else {
					if DEATH_UNLIKELY(_transTable[idx].Offset >= _fileSize) {
						return {};
					}
					return StringView(&_file[_transTable[idx].Offset], _transTable[idx].Length);
				}
			}
		}

		return {};
	}

	StringView I18n::LookupPlural(std::int32_t n, StringView translation)
	{
		std::int32_t index = (*_pluralExpression)(n);

		while (true) {
			StringView sep = translation.findOr('\0', translation.end());
			if (index == 0) {
				return translation.prefix(sep.begin());
			}
			if DEATH_UNLIKELY(sep.begin() == translation.end()) {
				return {};
			}
			translation = translation.suffix(sep.end());
			index--;
		}
	}

	StringView I18n::GetTranslationDescription()
	{
		StringView nullEntry = LookupTranslation("");
		if (nullEntry != nullptr) {
			if (StringView languageTeamBegin = nullEntry.find("Language-Team:"_s)) {
				languageTeamBegin = nullEntry.suffix(languageTeamBegin.end());
				StringView languageTeamEnd = languageTeamBegin.findOr('\n', nullEntry.end());
				return nullEntry.slice(languageTeamBegin.begin(), languageTeamEnd.begin()).trimmed();
			}
		}

		return {};
	}

	Array<String> I18n::GetPreferredLanguages()
	{
		Array<String> preferred;

#if defined(DEATH_TARGET_ANDROID)
		String langId = Backends::AndroidJniWrap_Activity::getPreferredLanguage();
		if (!langId.empty()) {
			StringUtils::lowercaseInPlace(langId);
			arrayAppend(preferred, std::move(langId));
		}
#elif defined(DEATH_TARGET_APPLE)
		CFTypeRef preferences = CFPreferencesCopyAppValue(CFSTR("AppleLanguages"), kCFPreferencesCurrentApplication);
		if (preferences != nullptr && CFGetTypeID(preferences) == CFArrayGetTypeID()) {
			CFArrayRef prefArray = (CFArrayRef)preferences;
			std::int32_t n = CFArrayGetCount(prefArray);
			char buffer[256];

			for (std::int32_t i = 0; i < n; i++) {
				CFTypeRef element = CFArrayGetValueAtIndex(prefArray, i);
				if (element == nullptr || CFGetTypeID(element) != CFStringGetTypeID() || !CFStringGetCString((CFStringRef)element, buffer, sizeof(buffer), kCFStringEncodingASCII)) {
					break;
				}
				String langId = String(buffer);
				StringUtils::lowercaseInPlace(langId);
				arrayAppend(preferred, std::move(langId));
			}
		}
#elif defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_UNIX)
		char* langRaw = ::getenv("LANG");
		if (langRaw == nullptr || langRaw[0] == '\0' || ::strcasecmp(langRaw, "C") == 0) {
			langRaw = ::getenv("LC_ALL");
			if (langRaw == nullptr || langRaw[0] == '\0' || ::strcasecmp(langRaw, "C") == 0) {
				langRaw = ::getenv("LC_MESSAGES");
				if (langRaw == nullptr || langRaw[0] == '\0' || ::strcasecmp(langRaw, "C") == 0) {
					// No suitable environment variable is defined
					return preferred;
				}
			}
		}

		String langId = langRaw;
		StringView suffix = langId.findAny(".@"_s);
		if (suffix != nullptr) {
			langId = langId.prefix(suffix.begin());
		}
		if (::strcasecmp(langId.data(), "C") != 0) {
			StringUtils::replaceAllInPlace(langId, '_', '-');
			StringUtils::lowercaseInPlace(langId);
			arrayAppend(preferred, std::move(langId));
		}
#elif defined(DEATH_TARGET_WINDOWS)
		// Get list of all preferred UI languages
		ULONG numberOfLanguages = 0;
		ULONG bufferSize = 0;
		if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numberOfLanguages, nullptr, &bufferSize)) {
			Array<wchar_t> languages(NoInit, bufferSize);
			if (::GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numberOfLanguages, languages.data(), &bufferSize)) {
				wchar_t* buffer = languages.data();
				for (ULONG i = 0; i < numberOfLanguages; i++) {
					String langId = Utf8::FromUtf16(buffer);
					StringUtils::lowercaseInPlace(langId);
					arrayAppend(preferred, std::move(langId));

					while (*buffer != L'\0') {
						buffer++;
					}
					buffer++;
				}
			}
		}
#endif

		return preferred;
	}

	StringView I18n::GetLanguageName(StringView langId)
	{
		StringView baseLanguage = TryRemoveLanguageSpecifiers(langId);

		std::size_t bottom = 0;
		std::size_t top = arraySize(SupportedLanguages);
		while (bottom < top) {
			std::size_t index = (bottom + top) / 2;
			std::int32_t cmpVal = strncmp(langId.data(), SupportedLanguages[index].Identifier, baseLanguage.size());
			if (cmpVal < 0) {
				top = index;
			} else if (cmpVal > 0) {
				bottom = index + 1;
			} else {
				return SupportedLanguages[index].Name;
			}
		}

		return {};
	}

	StringView I18n::TryRemoveLanguageSpecifiers(StringView langId)
	{
		StringView suffix = langId.findAny("-_.@"_s);
		return (suffix != nullptr ? langId.prefix(suffix.begin()) : langId);
	}

	const I18n::ExpressionToken* I18n::ExtractPluralExpression(StringView nullEntry)
	{
		if (nullEntry) {
			if (StringView pluralBegin = nullEntry.find("plural="_s)) {
				pluralBegin = nullEntry.suffix(pluralBegin.end());
				if (const ExpressionToken* parsedExpression = Evaluator::Parse(pluralBegin.data())) {
					return parsedExpression;
				}
			}
		}

		// Use default rule: (n != 1)
		return new ExpressionToken(ExpressionTokenType::NotEquals,
			new ExpressionToken(ExpressionTokenType::Variable),
			new ExpressionToken(ExpressionTokenType::Value, 1));
	}
}
