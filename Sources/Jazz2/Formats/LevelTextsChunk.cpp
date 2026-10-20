#include "LevelTextsChunk.h"
#include "VarUint.h"

#include <Containers/StringUtils.h>
#include <Utf8.h>

#include <algorithm>

using namespace Death::Containers::Literals;

namespace Jazz2::Formats
{
	namespace
	{
		// Fields of the record of a language, returns false if they don't fit
		bool ReadLanguage(const std::uint8_t*& p, const std::uint8_t* end, LocalizedLevelTexts& language)
		{
			if (p >= end) {
				return false;
			}
			std::uint32_t nameLength = *p++;
			if (std::uint32_t(end - p) < nameLength) {
				return false;
			}
			language.Language = String(reinterpret_cast<const char*>(p), nameLength);
			p += nameLength;

			std::uint32_t count;
			if (!ReadVarUint(p, end, count) || count > LevelTextsChunk::MaxTexts) {
				return false;
			}
			for (std::uint32_t i = 0; i < count; i++) {
				std::uint32_t textId, length;
				if (!ReadVarUint(p, end, textId) || textId >= LevelTextsChunk::MaxTexts || !ReadVarUint(p, end, length) ||
					length > LevelTextsChunk::MaxTextLength || std::uint32_t(end - p) < length) {
					return false;
				}
				if (language.Texts.size() <= textId) {
					language.Texts.resize(textId + 1);
				}
				language.Texts[textId] = String(reinterpret_cast<const char*>(p), length);
				p += length;
			}
			return true;
		}
	}

	bool LevelTextsChunk::Read(ArrayView<const std::uint8_t> data, SmallVectorImpl<LocalizedLevelTexts>& languages)
	{
		languages.clear();
		const std::uint8_t* p = data.data();
		const std::uint8_t* end = p + data.size();
		std::uint32_t count;
		// A newer version only adds fields to the records, so any version but 0 is read
		if (p >= end || *p++ == 0 || !ReadVarUint(p, end, count) || count > MaxLanguages) {
			return false;
		}
		for (std::uint32_t i = 0; i < count; i++) {
			std::uint32_t size;
			if (!ReadVarUint(p, end, size) || std::uint32_t(end - p) < size) {
				return false;
			}
			const std::uint8_t* recordEnd = p + size;
			LocalizedLevelTexts language;
			if (!ReadLanguage(p, recordEnd, language)) {
				return false;
			}
			if (!language.Language.empty()) {
				languages.push_back(Death::move(language));
			}
			// What a newer version added is skipped
			p = recordEnd;
		}
		return true;
	}

	void LevelTextsChunk::Write(ArrayView<const LocalizedLevelTexts> languages, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		std::uint32_t count = 0;
		// Only the texts that are written count, a language with none of them would be an empty record
		auto hasAnyText = [](const LocalizedLevelTexts& language) {
			for (std::size_t i = 0; i < language.Texts.size() && i < MaxTexts; i++) {
				if (!language.Texts[i].empty()) {
					return true;
				}
			}
			return false;
		};
		for (const LocalizedLevelTexts& language : languages) {
			if (!language.Language.empty() && hasAnyText(language) && count < MaxLanguages) {
				count++;
			}
		}
		if (count == 0) {
			// No translations, no chunk
			return;
		}

		data.push_back(Version);
		WriteVarUint(data, count);
		SmallVector<std::uint8_t, 0> record;
		std::uint32_t written = 0;
		for (const LocalizedLevelTexts& language : languages) {
			if (language.Language.empty() || !hasAnyText(language) || written >= MaxLanguages) {
				continue;
			}
			written++;
			record.clear();
			// Not in the middle of a character either
			std::size_t nameLength = Death::Utf8::FitPrefix(language.Language, UINT8_MAX);
			record.push_back(std::uint8_t(nameLength));
			record.append(reinterpret_cast<const std::uint8_t*>(language.Language.data()), reinterpret_cast<const std::uint8_t*>(language.Language.data()) + nameLength);

			std::uint32_t textCount = 0;
			for (std::size_t i = 0; i < language.Texts.size() && i < MaxTexts; i++) {
				textCount += (language.Texts[i].empty() ? 0 : 1);
			}
			WriteVarUint(record, textCount);
			for (std::size_t i = 0; i < language.Texts.size() && i < MaxTexts; i++) {
				const String& text = language.Texts[i];
				if (text.empty()) {
					continue;
				}
				// Not in the middle of a character
				std::size_t length = Death::Utf8::FitPrefix(text, MaxTextLength);
				WriteVarUint(record, std::uint32_t(i));
				WriteVarUint(record, std::uint32_t(length));
				record.append(reinterpret_cast<const std::uint8_t*>(text.data()), reinterpret_cast<const std::uint8_t*>(text.data()) + length);
			}

			WriteVarUint(data, std::uint32_t(record.size()));
			data.append(record.begin(), record.end());
		}
	}

	const LocalizedLevelTexts* LevelTextsChunk::Find(ArrayView<const LocalizedLevelTexts> languages, StringView language)
	{
		if (language.empty() || languages.empty()) {
			return nullptr;
		}
		for (const LocalizedLevelTexts& entry : languages) {
			if (StringUtils::equalsIgnoreCase(entry.Language, language)) {
				return &entry;
			}
		}
		// The language without its region, "pt" for "pt-br", as the translations of the game are looked up
		std::size_t baseLength = 0;
		while (baseLength < language.size() && language[baseLength] != '-' && language[baseLength] != '_') {
			baseLength++;
		}
		if (baseLength < language.size()) {
			StringView baseLanguage = language.prefix(baseLength);
			for (const LocalizedLevelTexts& entry : languages) {
				if (StringUtils::equalsIgnoreCase(entry.Language, baseLanguage)) {
					return &entry;
				}
			}
		}
		return nullptr;
	}
}
