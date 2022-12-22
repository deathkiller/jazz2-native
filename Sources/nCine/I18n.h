#pragma once

#include "Base/HashMap.h"
#include "IO/IFileStream.h"

#include <optional>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine
{
	class I18n
	{
	public:
		static constexpr char ContextSeparator = 0x04;

		struct ExpressionToken
		{
			virtual ~ExpressionToken() { };
			virtual int operator()(int n) const = 0;
		};

		class Evaluator
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
		};

		struct LanguageInfo
		{
			const char* Identifier;
			const char* Name;
		};

		I18n();
		~I18n();

		void Unload();
		bool LoadMoFile(const StringView& path);
		bool LoadMoFile(const std::unique_ptr<IFileStream>& fileHandle);

		const char* LookupTranslation(const char* msgid, uint32_t* resultLength);
		const char* LookupPlural(int n, const char* translation, uint32_t translationLength);

		static I18n& Current();
		static StringView GetLanguageName(const StringView& langId);

	private:
		/// Deleted copy constructor
		I18n(const I18n&) = delete;
		/// Deleted assignment operator
		I18n& operator=(const I18n&) = delete;

		struct MoFileHeader
		{
			uint32_t Signature;
			// The revision number of the file format
			uint32_t Revision;

			// The number of strings pairs
			uint32_t StringCount;
			// Offset of table with start offsets of original strings
			uint32_t OrigTableOffset;
			// Offset of table with start offsets of translated strings
			uint32_t TransTableOffset;
			// Size of hash table
			uint32_t HashTableSize;
			// Offset of first hash table entry
			uint32_t HashTableOffset;
		};

		struct StringDesc
		{
			// Length of addressed string, not including the trailing NULL
			uint32_t Length;
			// Offset of string in file
			uint32_t Offset;
		};

		std::unique_ptr<char[]> _file;
		uint32_t _stringCount;
		const StringDesc* _origTable;
		const StringDesc* _transTable;
		uint32_t _hashSize;
		const uint32_t* _hashTable;
		const ExpressionToken* _pluralExp;
		uint32_t _pluralCount;

		static void ExtractPluralExpression(const char* nullEntry, const ExpressionToken** pluralExp, uint32_t* pluralCount);
	};

	inline StringView _(const char* text)
	{
		uint32_t resultLength;
		const char* result = I18n::Current().LookupTranslation(text, &resultLength);
		return StringView(result != nullptr ? result : text);
	}
	
	inline StringView _x(const StringView& context, const char* text)
	{
		uint32_t resultLength;
		const char* result = I18n::Current().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(text) }).data(), &resultLength);
		return (result != nullptr ? result : text);
	}

	inline StringView _n(const char* singular, const char* plural, int n)
	{
		uint32_t resultLength;
		const char* result = I18n::Current().LookupTranslation(singular, &resultLength);
		if (result != nullptr) {
			return I18n::Current().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	inline StringView _nx(const StringView& context, const char* singular, const char* plural, int n)
	{
		uint32_t resultLength;
		const char* result = I18n::Current().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(singular) }).data(), &resultLength);
		if (result != nullptr) {
			return I18n::Current().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	String _f(const char* text, ...);
	String _fn(const char* singular, const char* plural, int n, ...);
}
