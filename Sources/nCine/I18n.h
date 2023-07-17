#pragma once

#include "Base/HashMap.h"

#include <optional>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace Death::IO;

namespace nCine
{
	class I18n
	{
	public:
		static constexpr char ContextSeparator = 0x04;

		struct ExpressionToken
		{
			virtual ~ExpressionToken() { };
			virtual std::int32_t operator()(std::int32_t n) const = 0;
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
			const StringView Name;
		};

		I18n();
		~I18n();

		void Unload();
		bool LoadFromFile(const StringView& path);
		bool LoadFromFile(const std::unique_ptr<Stream>& fileHandle);

		const char* LookupTranslation(const char* msgid, std::uint32_t* resultLength);
		const char* LookupPlural(std::int32_t n, const char* translation, std::uint32_t translationLength);

		StringView GetTranslationDescription();

		static I18n& Get();
		static Array<String> GetPreferredLanguages();
		static StringView GetLanguageName(const StringView& langId);
		static StringView TryRemoveLanguageSpecifiers(const StringView& langId);

	private:
		/// Deleted copy constructor
		I18n(const I18n&) = delete;
		/// Deleted assignment operator
		I18n& operator=(const I18n&) = delete;

		struct MoFileHeader
		{
			std::uint32_t Signature;
			// The revision number of the file format
			std::uint32_t Revision;

			// The number of strings pairs
			std::uint32_t StringCount;
			// Offset of table with start offsets of original strings
			std::uint32_t OrigTableOffset;
			// Offset of table with start offsets of translated strings
			std::uint32_t TransTableOffset;
			// Size of hash table
			std::uint32_t HashTableSize;
			// Offset of first hash table entry
			std::uint32_t HashTableOffset;
		};

		struct StringDesc
		{
			// Length of addressed string, not including the trailing NULL
			std::uint32_t Length;
			// Offset of string in file
			std::uint32_t Offset;
		};

		std::unique_ptr<char[]> _file;
		std::uint32_t _fileSize;
		std::uint32_t _stringCount;
		const StringDesc* _origTable;
		const StringDesc* _transTable;
		std::uint32_t _hashSize;
		const std::uint32_t* _hashTable;
		const ExpressionToken* _pluralExpression;

		static const ExpressionToken* ExtractPluralExpression(const char* nullEntry);
	};

	inline StringView _(const char* text)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(text, &resultLength);
		return StringView(result != nullptr ? result : text);
	}
	
	inline StringView _x(const StringView& context, const char* text)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(text) }).data(), &resultLength);
		return (result != nullptr ? result : text);
	}

	inline StringView _n(const char* singular, const char* plural, int n)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(singular, &resultLength);
		if (result != nullptr) {
			return I18n::Get().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	inline StringView _nx(const StringView& context, const char* singular, const char* plural, int n)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(singular) }).data(), &resultLength);
		if (result != nullptr) {
			return I18n::Get().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	String _f(const char* text, ...);
	String _fn(const char* singular, const char* plural, std::int32_t n, ...);
}
