#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../LevelDescriptor.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>

#include <memory>

namespace Jazz2::Editor
{
	class IEditorContext;
	class UiStack;

	/**
		@brief Language of the translations shown in the dialog of the level texts

		The translations are kept in the chunk of the level @ref Formats::LevelTextsChunk, read again only when the level
		changed, as the boxes of all texts ask for them every frame.
	*/
	struct LevelTextLanguageState
	{
		/** @brief Language whose translations are edited, empty for none */
		String Language;

		/** @brief Returns the translations of the level, read again if it changed */
		ArrayView<const LocalizedLevelTexts> GetLanguages(IEditorContext& session);

	private:
		std::uint32_t _revision = UINT32_MAX;
		std::size_t _size = 0;
		SmallVector<LocalizedLevelTexts, 0> _languages;
	};

	/** @brief Adds the choice of the language and the buttons to export and import the texts for translators */
	void AddLevelTextLanguageBar(UiStack* content, IEditorContext& session, const std::shared_ptr<LevelTextLanguageState>& state);
	/** @brief Adds the box of the translation of a text, shown only while a language is chosen */
	void AddLevelTextTranslationBox(UiStack* row, IEditorContext& session, const std::shared_ptr<LevelTextLanguageState>& state, std::int32_t index);
	/** @brief Changes the translation of a text as an undoable step, an empty one removes it */
	void SetLevelTextTranslation(IEditorContext& session, StringView language, std::int32_t index, StringView text);
}

#endif
