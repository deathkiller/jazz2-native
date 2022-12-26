#include "I18n.h"

#include "IO/FileSystem.h"

#include <stdarg.h>

namespace nCine
{
	static constexpr I18n::LanguageInfo SupportedLanguages[] = {
		{ "af", "Afrikaans" },
		{ "bg", "Bulgarian" },
		{ "hr", "Croatian" },
		{ "cs", "Czech" },
		{ "da", "Danish" },
		{ "nl", "Dutch" },
		{ "en", "English" },
		{ "et", "Estonian" },
		{ "fi", "Finnish" },
		{ "fr", "French" },
		{ "de", "German" },
		{ "hu", "Hungarian" },
		{ "it", "Italian" },
		{ "lv", "Latvian" },
		{ "mo", "Moldavian" },
		{ "no", "Norwegian" },
		{ "pl", "Polish" },
		{ "pt", "Portuguese" },
		{ "ro", "Romanian" },
		{ "sr", "Serbian" },
		{ "sk", "Slovak" },
		{ "sl", "Slovenian" },
		{ "es", "Spanish" },
		{ "sv", "Swedish" },
		{ "tr", "Turkish" },
	};

	struct ValueToken : public I18n::ExpressionToken
	{
		ValueToken(const int value)
			: _value(value)
		{
		}

		int operator()(int n) const override
		{
			return _value;
		}

	protected:
		int _value;
	};

	struct VariableToken : public I18n::ExpressionToken
	{
		VariableToken()
		{
		}

		int operator()(int n) const override
		{
			return n;
		}
	};

	struct BinaryExpressionToken : public I18n::ExpressionToken
	{
		BinaryExpressionToken(ExpressionToken* left, ExpressionToken* right)
			:
			_left(left),
			_right(right)
		{
		}

		~BinaryExpressionToken()
		{
			delete _left;
			delete _right;
		}

	protected:
		I18n::ExpressionToken* _left;
		I18n::ExpressionToken* _right;
	};

	struct MultiplyToken : public BinaryExpressionToken
	{
		MultiplyToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) * (*this->_right)(n);
		}
	};

	struct DivideToken : public BinaryExpressionToken
	{
		DivideToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) / (*this->_right)(n);
		}
	};

	struct RemainderToken : public BinaryExpressionToken
	{
		RemainderToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) % (*this->_right)(n);
		}
	};

	struct AddToken : public BinaryExpressionToken
	{
		AddToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) + (*this->_right)(n);
		}
	};

	struct SubtractToken : public BinaryExpressionToken
	{
		SubtractToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) - (*this->_right)(n);
		}
	};

	struct LogicalAndToken : public BinaryExpressionToken
	{
		LogicalAndToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) != 0 && (*this->_right)(n) != 0;
		}
	};

	struct LogicalOrToken : public BinaryExpressionToken
	{
		LogicalOrToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) != 0 || (*this->_right)(n) != 0;
		}
	};

	struct CompareEqualsToken : public BinaryExpressionToken
	{
		CompareEqualsToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) == (*this->_right)(n);
		}
	};

	struct CompareNotEqualsToken : public BinaryExpressionToken
	{
		CompareNotEqualsToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) != (*this->_right)(n);
		}
	};

	struct CompareLessToken : public BinaryExpressionToken
	{
		CompareLessToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) < (*this->_right)(n);
		}
	};

	struct CompareEqualsOrLessToken : public BinaryExpressionToken
	{
		CompareEqualsOrLessToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) <= (*this->_right)(n);
		}
	};

	struct CompareEqualsOrGreaterToken : public BinaryExpressionToken
	{
		CompareEqualsOrGreaterToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) >= (*this->_right)(n);
		}
	};

	struct CompareGreaterToken : public BinaryExpressionToken
	{
		CompareGreaterToken(ExpressionToken* left, ExpressionToken* right)
			: BinaryExpressionToken(left, right)
		{
		}

		int operator()(int n) const override
		{
			return (*this->_left)(n) > (*this->_right)(n);
		}
	};

	struct TernaryExpressionToken : public I18n::ExpressionToken
	{
		TernaryExpressionToken(ExpressionToken* comparison, ExpressionToken* left, ExpressionToken* right)
			:
			_comparison(comparison),
			_left(left),
			_right(right)
		{
		}

		~TernaryExpressionToken()
		{
			delete _comparison;
			delete _left;
			delete _right;
		}

		int operator()(int n) const override
		{
			return (*this->_comparison)(n) ? (*this->_left)(n) : (*this->_right)(n);
		}

	protected:
		I18n::ExpressionToken* _comparison;
		I18n::ExpressionToken* _left;
		I18n::ExpressionToken* _right;
	};

	I18n::ExpressionToken* I18n::Evaluator::ParseNumber(const char*& s, bool negative)
	{
		char* endPtr;
		int value = strtol(s, &endPtr, 10);
		s = endPtr;

		if (negative) {
			value = -value;
		}

		return new ValueToken(value);
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
			return new ValueToken(0);
		} else if (*s == 'n') {
			s++;
			return new VariableToken();
		} else if (std::isdigit(*s)) {
			return ParseNumber(s, false);
		} else if (*s == '-' && std::isdigit(*(s + 1))) {
			s++;
			return ParseNumber(s, true);
		}

		// Unexpected character
		return new ValueToken(0);
	}

	I18n::ExpressionToken* I18n::Evaluator::ParseFactors(const char*& s)
	{
		I18n::ExpressionToken* left = ParseAtom(s);
		while (*s != '\0') {
			if (*s == '*') {
				s++;
				I18n::ExpressionToken* right = ParseAtom(s);
				left = new MultiplyToken(left, right);
				continue;
			} else if (*s == '/') {
				s++;
				I18n::ExpressionToken* right = ParseAtom(s);
				left = new DivideToken(left, right);
				continue;
			} else if (*s == '%') {
				s++;
				I18n::ExpressionToken* right = ParseAtom(s);
				left = new RemainderToken(left, right);
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
				I18n::ExpressionToken* right = ParseFactors(s);
				left = new AddToken(left, right);
				continue;
			} else if (*s == '-') {
				s++;
				I18n::ExpressionToken* right = ParseFactors(s);
				left = new SubtractToken(left, right);
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
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareEqualsToken(left, right);
				continue;
			} else if (*s == '!' && *(s + 1) == '=') {
				s += 2;
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareNotEqualsToken(left, right);
				continue;
			} else if (*s == '<' && *(s + 1) == '=') {
				s += 2;
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareEqualsOrLessToken(left, right);
				continue;
			} else if (*s == '>' && *(s + 1) == '=') {
				s += 2;
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareEqualsOrGreaterToken(left, right);
				continue;
			} else if (*s == '<') {
				s++;
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareLessToken(left, right);
				continue;
			} else if (*s == '>') {
				s++;
				I18n::ExpressionToken* right = ParseSummands(s);
				left = new CompareGreaterToken(left, right);
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
				I18n::ExpressionToken* right = ParseComparison(s);
				left = new LogicalAndToken(left, right);
				continue;
			} else if (*s == '|' && *(s + 1) == '|') {
				s += 2;
				I18n::ExpressionToken* right = ParseComparison(s);
				left = new LogicalOrToken(left, right);
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
			return comparison;
		}
		s++;
		I18n::ExpressionToken* right = ParseTernary(s);
		return new TernaryExpressionToken(comparison, left, right);
	}

	const I18n::ExpressionToken* I18n::Evaluator::Parse(const char* s)
	{
		I18n::ExpressionToken* result = ParseTernary(s);
		while (*s == ' ') {
			s++;
		}
		if (*s != '\0' && *s != ';' && *s != '\r' && *s != '\n') {
			delete result;
			return nullptr;
		}
		return result;
	}

	I18n& I18n::Current()
	{
		static I18n current;
		return current;
	}

	I18n::I18n()
		: _fileSize(0), _stringCount(0), _origTable(nullptr), _transTable(nullptr), _hashSize(0), _hashTable(nullptr), _pluralExp(nullptr), _pluralCount(0)
	{
	}

	I18n::~I18n()
	{
		if (_pluralExp != nullptr) {
			delete _pluralExp;
			_pluralExp = nullptr;
		}
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

		if (_pluralExp != nullptr) {
			delete _pluralExp;
			_pluralExp = nullptr;
		}
	}

	bool I18n::LoadFromFile(const StringView& path)
	{
		return LoadFromFile(fs::Open(path, FileAccessMode::Read));
	}

	bool I18n::LoadFromFile(const std::unique_ptr<IFileStream>& fileHandle)
	{
		uint32_t fileSize = fileHandle->GetSize();
		RETURNF_ASSERT_MSG(fileSize > 32 && fileSize < 16 * 1024 * 1024, "Cannot open specified file");

		_file = std::make_unique<char[]>(fileSize + 1);
		fileHandle->Read(_file.get(), fileSize);
		_file[fileSize] = '\0';
		_fileSize = fileSize;

		constexpr uint32_t SignatureLE = 0x950412de;
		constexpr uint32_t SignatureBE = 0xde120495;
		MoFileHeader* data = (MoFileHeader*)_file.get();
		if (!(data->Signature == SignatureLE || data->Signature == SignatureBE) || data->StringCount <= 0 ||
			data->OrigTableOffset + data->StringCount > fileSize || data->TransTableOffset + data->StringCount > fileSize ||
			data->HashTableOffset + data->HashTableSize > fileSize) {
			LOGE("Invalid \".mo\" file");
			Unload();
			return false;
		}

		_stringCount = data->StringCount;
		_origTable = (const StringDesc*)((char*)data + data->OrigTableOffset);
		_transTable = (const StringDesc*)((char*)data + data->TransTableOffset);
		_hashSize = data->HashTableSize;
		_hashTable = (_hashSize > 2 ? (const uint32_t*)((char*)data + data->HashTableOffset) : nullptr);

		uint32_t entryLength;
		const char* nullEntry = LookupTranslation("", &entryLength);
		ExtractPluralExpression(nullEntry, &_pluralExp, &_pluralCount);
		return true;
	}

	const char* I18n::LookupTranslation(const char* msgid, uint32_t* resultLength)
	{
		if (_hashTable != nullptr) {
			// Use the hashing table
			constexpr uint32_t HashWordBits = 32;

			const char* str = msgid;
			uint32_t hashValue = 0;
			uint32_t len = 0;
			while (*str != '\0') {
				hashValue <<= 4;
				hashValue += (uint8_t)*str++;
				uint32_t g = hashValue & ((uint32_t) 0xf << (HashWordBits - 4));
				if (g != 0) {
					hashValue ^= g >> (HashWordBits - 8);
					hashValue ^= g;
				}
				len++;
			}

			uint32_t idx = hashValue % _hashSize;
			uint32_t incr = 1 + (hashValue % (_hashSize - 2));

			while (true) {
				uint32_t nstr = _hashTable[idx];
				if (nstr == 0) {
					// Hash table entry is empty
					return nullptr;
				}
				nstr--;

				// Compare `msgid` with the original string at index `nstr`. We compare the lengths with `>=`, not `==`,
				// because plural entries are represented by strings with an embedded NULL.
				if (nstr < _stringCount && _origTable[nstr].Length >= len && (std::strcmp(msgid, _file.get() + _origTable[nstr].Offset) == 0)) {
					if (_transTable[nstr].Offset >= _fileSize) {
						return nullptr;
					}
					*resultLength = _transTable[nstr].Length;
					return (char*)(_file.get() + _transTable[nstr].Offset);
				}

				if (idx >= _hashSize - incr) {
					idx -= _hashSize - incr;
				} else {
					idx += incr;
				}
			}
		} else {
			// Binary search in the sorted array of messages
			size_t bottom = 0;
			size_t top = _stringCount;
			while (bottom < top) {
				size_t idx = (bottom + top) / 2;
				if (_origTable[idx].Offset >= _fileSize) {
					return nullptr;
				}
				int cmpVal = std::strcmp(msgid, (_file.get() + _origTable[idx].Offset));
				if (cmpVal < 0) {
					top = idx;
				} else if (cmpVal > 0) {
					bottom = idx + 1;
				} else {
					if (_transTable[idx].Offset >= _fileSize) {
						return nullptr;
					}
					*resultLength = _transTable[idx].Length;
					return (char*)(_file.get() + _transTable[idx].Offset);
				}
			}
		}

		return nullptr;
	}

	const char* I18n::LookupPlural(int n, const char* translation, uint32_t translationLength)
	{
		int index = (*_pluralExp)(n);

		const char* p = translation;
		while (index-- > 0) {
			p = strchr(p, '\0');
			p++;
			if (p >= translation + translationLength) {
				return translation;
			}
		}
		return p;
	}

	StringView I18n::GetLanguageName(const StringView& langId)
	{
		auto suffix = langId.findAny("-_"_s);
		size_t langLength = (suffix != nullptr ? suffix.data() - langId.data() : langId.size());

		size_t bottom = 0;
		size_t top = _countof(SupportedLanguages);
		while (bottom < top) {
			size_t index = (bottom + top) / 2;
			int cmpVal = std::strncmp(langId.data(), SupportedLanguages[index].Identifier, langLength);
			if (cmpVal < 0) {
				top = index;
			} else if (cmpVal > 0) {
				bottom = index + 1;
			} else {
				return SupportedLanguages[index].Name;
			}
		}

		return { };
	}

	void I18n::ExtractPluralExpression(const char* nullEntry, const ExpressionToken** pluralExp, uint32_t* pluralCount)
	{
		// Destroy previous evaluator
		if (*pluralExp != nullptr) {
			delete *pluralExp;
		}

		if (nullEntry != nullptr) {
			const char* plural = strstr(nullEntry, "plural=");
			const char* nplurals = strstr(nullEntry, "nplurals=");
			if (plural != nullptr && nplurals != nullptr) {
				nplurals += 9;
				while (*nplurals != '\0' && std::isspace(*nplurals)) {
					nplurals++;
				}
				char* endp;
				*pluralCount = strtoul(nplurals, &endp, 10);
				if (nplurals != endp) {
					plural += 7;
					*pluralExp = Evaluator::Parse(plural);
					if (*pluralExp != nullptr) {
						return;
					}
				}
			}
		}

		*pluralExp = new CompareNotEqualsToken(new VariableToken(), new ValueToken(1));
		*pluralCount = 2;
	}

	String _f(const char* text, ...)
	{
		uint32_t resultLength;
		const char* translated = I18n::Current().LookupTranslation(text, &resultLength);
		const char* format = (translated != nullptr ? translated : text);

		va_list args;
		va_start(args, text);
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const int totalChars = _vscprintf(format, args);
		String result(NoInit, totalChars);
		vsnprintf_s(result.data(), totalChars + 1, totalChars, format, args);
#else
		const int totalChars = ::vsnprintf(nullptr, 0, format, args);
		String result(NoInit, totalChars);
		::vsnprintf(result.data(), totalChars, format, args);
#endif
		va_end(args);
		return result;
	}

	String _fn(const char* singular, const char* plural, int n, ...)
	{
		uint32_t resultLength;
		const char* translated = I18n::Current().LookupTranslation(singular, &resultLength);
		const char* format;
		if (translated != nullptr) {
			format = I18n::Current().LookupPlural(n, translated, resultLength);
		} else {
			format = (n == 1 ? singular : plural);
		}

		va_list args;
		va_start(args, n);
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_MINGW)
		const int totalChars = _vscprintf(format, args);
		String result(NoInit, totalChars);
		vsnprintf_s(result.data(), totalChars + 1, totalChars, format, args);
#else
		const int totalChars = ::vsnprintf(nullptr, 0, format, args);
		String result(NoInit, totalChars);
		::vsnprintf(result.data(), totalChars, format, args);
#endif
		va_end(args);
		return result;
	}
}
