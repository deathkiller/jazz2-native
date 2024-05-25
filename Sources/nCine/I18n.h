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
	/**
		@brief Provides support for internationalization and translations
	*/
	class I18n
	{
	public:
		struct ExpressionToken;

		static constexpr char ContextSeparator = 0x04;

		I18n();
		~I18n();

		I18n(const I18n&) = delete;
		I18n& operator=(const I18n&) = delete;

		/** @brief Load a catalog from file in gettext MO format, previously loaded catalog will be unloaded */
		bool LoadFromFile(const StringView path);
		/** @brief Load a catalog from stream in gettext MO format, previously loaded catalog will be unloaded */
		bool LoadFromFile(const std::unique_ptr<Stream>& fileHandle);
		/** @brief Unload all loaded catalogs */
		void Unload();

		/** @brief Looks up raw translation by @p msgid */
		const char* LookupTranslation(const char* msgid, std::uint32_t* resultLength);
		/** @brief Looks up plural variant of translation returned by @ref LookupTranslation()  */
		const char* LookupPlural(std::int32_t n, const char* translation, std::uint32_t translationLength);

		/** @brief Returns description of currently loaded catalog */
		StringView GetTranslationDescription();

		/** @brief Returns primary translation catalog */
		static I18n& Get();
		/** @brief Returns list of user's preferred languages */
		static Array<String> GetPreferredLanguages();
		/** @brief Returns localized name of language by ID */
		static StringView GetLanguageName(const StringView langId);
		/** @brief Returns language ID without any regional/territorial specifiers */
		static StringView TryRemoveLanguageSpecifiers(const StringView langId);

	private:
		struct StringDesc;
		class Evaluator;

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

	/** @brief Translates text in singular form using primary translation catalog */
	inline StringView _(const char* text)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(text, &resultLength);
		return StringView(result != nullptr ? result : text);
	}
	
	/** @brief Translates text in singular form using primary translation catalog and specified @p context */
	inline StringView _x(StringView context, const char* text)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(text) }).data(), &resultLength);
		return (result != nullptr ? result : text);
	}

	/** @brief Translates text in singular or plural form using primary translation catalog */
	inline StringView _n(const char* singular, const char* plural, std::int32_t n)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(singular, &resultLength);
		if (result != nullptr) {
			return I18n::Get().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	/** @brief Translates text in singular or plural form using primary translation catalog and specified @p context */
	inline StringView _nx(StringView context, const char* singular, const char* plural, std::int32_t n)
	{
		std::uint32_t resultLength;
		const char* result = I18n::Get().LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, StringView(singular) }).data(), &resultLength);
		if (result != nullptr) {
			return I18n::Get().LookupPlural(n, result, resultLength);
		}
		return (n == 1 ? singular : plural);
	}

	/** @brief Translates formatted text in singular form using primary translation catalog */
	String _f(const char* text, ...);
	/** @brief Translates formatted text in singular or plural form using primary translation catalog */
	String _fn(const char* singular, const char* plural, std::int32_t n, ...);
}
