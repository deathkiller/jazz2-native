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
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr char ContextSeparator = 0x04;
#endif

		I18n();
		~I18n();

		I18n(const I18n&) = delete;
		I18n& operator=(const I18n&) = delete;

		/** @brief Load a catalog from file in gettext MO format, previously loaded catalog will be unloaded */
		bool LoadFromFile(StringView path);
		/** @brief Load a catalog from stream in gettext MO format, previously loaded catalog will be unloaded */
		bool LoadFromFile(const std::unique_ptr<Stream>& fileHandle);
		/** @brief Unload all loaded catalogs */
		void Unload();

		/** @brief Looks up raw translation by @p msgid */
		StringView LookupTranslation(const char* msgid);
		/** @brief Looks up plural variant of translation returned by @ref LookupTranslation()  */
		StringView LookupPlural(std::int32_t n, StringView translation);

		/** @brief Returns description of currently loaded catalog */
		StringView GetTranslationDescription();

		/** @brief Returns primary translation catalog */
		static I18n& Get();
		/** @brief Returns list of user's preferred languages */
		static Array<String> GetPreferredLanguages();
		/** @brief Returns localized name of language by ID (ISO 639-1) */
		static StringView GetLanguageName(StringView langId);
		/** @brief Returns language ID (ISO 639-1) without any regional/territorial specifiers */
		static StringView TryRemoveLanguageSpecifiers(StringView langId);

	private:
		struct StringDesc;
		struct ExpressionToken;
		class Evaluator;

		std::unique_ptr<char[]> _file;
		std::uint32_t _fileSize;
		std::uint32_t _stringCount;
		StringDesc* _origTable;
		StringDesc* _transTable;
		std::uint32_t _hashSize;
		std::uint32_t* _hashTable;
		const ExpressionToken* _pluralExpression;

		static const ExpressionToken* ExtractPluralExpression(StringView nullEntry);
	};

	/** @relates I18n
		@brief Translates text in singular form using primary translation catalog
	*/
	inline StringView _(const char* text)
	{
		I18n& i18n = I18n::Get();
		StringView result = i18n.LookupTranslation(text);
		return (result ? result : StringView(text));
	}
	
	/** @relates I18n
		@brief Translates text in singular form using primary translation catalog and specified @p context
	*/
	inline StringView _x(StringView context, const char* text)
	{
		I18n& i18n = I18n::Get();
		StringView textView = text;
		StringView result = i18n.LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, textView }).data());
		return (result ? result : textView);
	}

	/** @relates I18n
		@brief Translates text in singular or plural form using primary translation catalog
	*/
	inline StringView _n(const char* singular, const char* plural, std::int32_t n)
	{
		I18n& i18n = I18n::Get();
		if (StringView result = i18n.LookupTranslation(singular)) {
			return i18n.LookupPlural(n, result);
		}
		return (n == 1 ? singular : plural);
	}

	/** @relates I18n
		@brief Translates text in singular or plural form using primary translation catalog and specified @p context
	*/
	inline StringView _nx(StringView context, const char* singular, const char* plural, std::int32_t n)
	{
		I18n& i18n = I18n::Get();
		StringView singularView = singular;
		if (StringView result = i18n.LookupTranslation(String(&I18n::ContextSeparator, 1).join({ context, singularView }).data())) {
			return i18n.LookupPlural(n, result);
		}
		return (n == 1 ? singular : plural);
	}

	/** @relates I18n
		@brief Translates formatted text in singular form using primary translation catalog
	*/
	template<class ...Args>
	String _f(const char* text, const Args&... args)
	{
		I18n& i18n = I18n::Get();
		StringView translated = i18n.LookupTranslation(text);
		return format(translated ? translated.data() : text, args...);
	}

	/** @relates I18n
		@brief Translates formatted text in singular or plural form using primary translation catalog
	*/
	template<class ...Args>
	String _fn(const char* textSingular, const char* textPlural, std::int32_t n, const Args&... args)
	{
		I18n& i18n = I18n::Get();
		StringView translated = i18n.LookupTranslation(textSingular);
		const char* fmt;
		if (translated != nullptr) {
			fmt = i18n.LookupPlural(n, translated).data();
		} else {
			fmt = (n == 1 ? textSingular : textPlural);
		}
		return format(fmt, args...);
	}
}
