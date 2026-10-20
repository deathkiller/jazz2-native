#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/** @brief Entry of a gettext catalog */
	struct PoEntry
	{
		/** @brief Context of the text, may be empty */
		String Context;
		/** @brief Text to translate */
		String Id;
		/** @brief Translation, empty if it isn't translated */
		String Translation;
		/** @brief Comment for the translators, may be empty */
		String Comment;
	};

	/**
		@brief Reads and writes gettext catalogs, `.po` and `.pot` files

		Only what a catalog of single texts needs --- contexts, texts and their translations, comments for the
		translators, and the header with the language. Plural forms are read as their first form, a fuzzy translation
		as none, as gettext itself takes it.
	*/
	class PoFile
	{
	public:
		PoFile() = delete;

		/**
		 * @brief Writes a catalog
		 *
		 * Without a @p language it's a template (`.pot`), all translations are left empty then.
		 */
		static String Write(ArrayView<const PoEntry> entries, StringView language, StringView projectName);
		/** @brief Reads a catalog, @p language gets the language of its header, returns `false` if it's damaged */
		static bool Read(StringView text, SmallVectorImpl<PoEntry>& entries, String& language);
	};
}

#endif
