#include "LevelTextLanguages.h"

#if defined(WITH_EDITOR)

#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/Commands.h"
#include "../Files/AtomicFile.h"
#include "../Files/PoFile.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../Formats/LevelTextsChunk.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <cstdio>
#include <cstdlib>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// Formatting of a text is written out while it's edited, as a backslash and "f", so does a backslash itself,
		// the same as the texts themselves are edited
		String EscapeForEditing(StringView text)
		{
			String result;
			std::size_t start = 0;
			for (std::size_t i = 0; i < text.size(); i++) {
				if (text[i] == '\\' || text[i] == '\f') {
					result += text.slice(start, i);
					result += (text[i] == '\\' ? "\\\\"_s : "\\f"_s);
					start = i + 1;
				}
			}
			result += text.exceptPrefix(start);
			return result;
		}

		String UnescapeFromEditing(StringView text)
		{
			String result;
			std::size_t start = 0;
			for (std::size_t i = 0; i + 1 < text.size(); i++) {
				if (text[i] != '\\') {
					continue;
				}
				char next = text[i + 1];
				StringView replacement = (next == '\\' ? "\\"_s : (next == 'f' ? "\f"_s : (next == 'n' ? "\n"_s : StringView())));
				if (!replacement.empty()) {
					result += text.slice(start, i);
					result += replacement;
					start = i + 2;
					i++;
				}
			}
			result += text.exceptPrefix(start);
			return result;
		}

		// As the game names the languages of its translations, "cs" or "pt-br"
		bool IsValidLanguage(StringView language)
		{
			if (language.size() < 2 || language.size() > 15) {
				return false;
			}
			for (char c : language) {
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
					return false;
				}
			}
			return true;
		}

		String FormatLanguage(StringView language)
		{
			StringView name = I18n::GetLanguageName(language);
			return (name.empty() || name == language ? String(language) : String(language + " ("_s + name + ")"_s));
		}

		LocalizedLevelTexts* FindExact(SmallVectorImpl<LocalizedLevelTexts>& languages, StringView language)
		{
			for (LocalizedLevelTexts& entry : languages) {
				if (StringUtils::equalsIgnoreCase(entry.Language, language)) {
					return &entry;
				}
			}
			return nullptr;
		}

		void ReadLanguages(const LevelDocument& document, SmallVectorImpl<LocalizedLevelTexts>& languages)
		{
			languages.clear();
			if (const Formats::ChunkRecord* chunk = document.FindChunk(Formats::LevelTextsChunk::Tag)) {
				Formats::LevelTextsChunk::Read(chunk->Data, languages);
			}
		}

		void WriteLanguages(IEditorContext& session, const SmallVectorImpl<LocalizedLevelTexts>& languages, StringView name)
		{
			SmallVector<std::uint8_t, 0> data;
			Formats::LevelTextsChunk::Write(ArrayView<const LocalizedLevelTexts>(languages.data(), languages.size()), data);
			const Formats::ChunkRecord* chunk = session.GetDocument().FindChunk(Formats::LevelTextsChunk::Tag);
			bool same = (chunk != nullptr ? (chunk->Data.size() == data.size() && std::memcmp(chunk->Data.data(), data.data(), data.size()) == 0) : data.empty());
			if (!same) {
				session.Execute(std::make_unique<SetChunkCommand>(session.GetDocument(), Formats::LevelTextsChunk::Tag, data, String(name)));
			}
		}

		// The texts of the level for translators, the number of a text is its context, so the same text twice is two entries
		void MakeEntries(const LevelDocument& document, const LocalizedLevelTexts* translations, SmallVectorImpl<PoEntry>& entries)
		{
			for (std::size_t i = 0; i < document.Texts.size(); i++) {
				if (document.Texts[i].empty()) {
					continue;
				}
				char number[16];
				std::snprintf(number, sizeof(number), "%u", std::uint32_t(i));
				PoEntry& entry = entries.emplace_back();
				entry.Context = number;
				entry.Id = document.Texts[i];
				entry.Comment = "Level text "_s + number + ", \"|\" splits it into parts, \"\\f\" is formatting"_s;
				if (translations != nullptr && i < translations->Texts.size()) {
					entry.Translation = translations->Texts[i];
				}
			}
		}

		String GetLevelFileStem(IEditorContext& session)
		{
			StringView filePath = session.GetFilePath();
			return (filePath.empty() ? String("level"_s) : String(fs::GetFileNameWithoutExtension(filePath)));
		}

		void ExportCatalog(IEditorContext& session, StringView language)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetImageRoots(true, roots);
			if (roots.empty()) {
				session.ShowMessage(_x("editor", "There is no place to save files to"));
				return;
			}
			String languageCopy = language;
			String name = (language.empty() ? String(GetLevelFileStem(session) + ".pot"_s) : String(GetLevelFileStem(session) + "."_s + language + ".po"_s));
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the file dialog saving the texts of the level as a gettext template, or with their translation to the chosen language
			UiFileDialog::Show(*surface, language.empty() ? _x("editor", "Export Texts for Translators") : _x("editor", "Export Translation"), UiFileDialogMode::Save,
				Death::move(roots), language.empty() ? ".pot"_s : ".po"_s, {}, name, [s, languageCopy](StringView path) {
				SmallVector<LocalizedLevelTexts, 0> languages;
				ReadLanguages(s->GetDocument(), languages);
				SmallVector<PoEntry, 0> entries;
				MakeEntries(s->GetDocument(), languageCopy.empty() ? nullptr : FindExact(languages, languageCopy), entries);
				String projectName = (s->GetDocument().Properties.DisplayName.empty() ? GetLevelFileStem(*s) : s->GetDocument().Properties.DisplayName);
				String text = PoFile::Write(entries, languageCopy, projectName);
				if (AtomicFile::Write(path, text.data(), std::int64_t(text.size()))) {
					s->ShowMessage(_fnx("editor", "{} text was exported", "{} texts were exported", std::int32_t(entries.size()), std::int32_t(entries.size())));
				} else {
					s->ShowMessage(_x("editor", "The file couldn't be written"));
				}
			});
		}

		void ImportCatalog(IEditorContext& session, const std::shared_ptr<LevelTextLanguageState>& state)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetImageRoots(false, roots);
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the file dialog reading the translations of the texts of the level from a gettext catalog
			UiFileDialog::Show(*surface, _x("editor", "Import Translation"), UiFileDialogMode::Open, Death::move(roots), ".po"_s, {}, {}, [s, state](StringView path) {
				auto file = fs::Open(path, FileAccess::Read);
				std::int64_t size = (file->IsValid() ? file->GetSize() : -1);
				if (size <= 0 || size > 16 * 1024 * 1024) {
					s->ShowMessage(_x("editor", "The file can't be read"));
					return;
				}
				String text{NoInit, std::size_t(size)};
				if (file->Read(text.data(), size) != size) {
					s->ShowMessage(_x("editor", "The file can't be read"));
					return;
				}
				SmallVector<PoEntry, 0> entries;
				String language;
				if (!PoFile::Read(text, entries, language)) {
					s->ShowMessage(_x("editor", "The file isn't a translation catalog"));
					return;
				}
				// The language of the catalog, or the one chosen, or as the file is named, "level.cs.po"
				if (language.empty()) {
					language = state->Language;
				}
				if (language.empty()) {
					StringView stem = fs::GetFileNameWithoutExtension(path);
					std::size_t dot = stem.size();
					while (dot > 0 && stem[dot - 1] != '.') {
						dot--;
					}
					language = stem.exceptPrefix(dot);
				}
				language = language.trimmed();
				StringUtils::lowercaseInPlace(language);
				if (!IsValidLanguage(language)) {
					s->ShowMessage(_x("editor", "The catalog doesn't say its language, choose the language first"));
					return;
				}

				const LevelDocument& document = s->GetDocument();
				SmallVector<LocalizedLevelTexts, 0> languages;
				ReadLanguages(document, languages);
				LocalizedLevelTexts* target = FindExact(languages, language);
				if (target == nullptr) {
					target = &languages.emplace_back();
					target->Language = language;
				}
				std::int32_t imported = 0;
				for (const PoEntry& entry : entries) {
					if (entry.Translation.empty()) {
						continue;
					}
					// By the number in the context, or by the text if the catalog was made another way
					std::int32_t index = -1;
					if (!entry.Context.empty()) {
						char* end;
						String context = entry.Context;
						long value = std::strtol(context.data(), &end, 10);
						if (*end == '\0' && value >= 0 && value < std::int32_t(document.Texts.size())) {
							index = std::int32_t(value);
						}
					}
					if (index < 0) {
						for (std::size_t i = 0; i < document.Texts.size(); i++) {
							if (document.Texts[i] == entry.Id) {
								index = std::int32_t(i);
								break;
							}
						}
					}
					if (index < 0) {
						continue;
					}
					if (std::int32_t(target->Texts.size()) <= index) {
						target->Texts.resize(std::size_t(index) + 1);
					}
					target->Texts[index] = entry.Translation;
					imported++;
				}
				// TRANSLATORS: Name of an undo step in the history, translations of the texts of the level read from a file
				WriteLanguages(*s, languages, _x("editor", "Import translation"));
				state->Language = language;
				s->ShowMessage(_fnx("editor", "{} translated text was imported", "{} translated texts were imported", imported, imported));
			});
		}
	}

	ArrayView<const LocalizedLevelTexts> LevelTextLanguageState::GetLanguages(IEditorContext& session)
	{
		const LevelDocument& document = session.GetDocument();
		const Formats::ChunkRecord* chunk = document.FindChunk(Formats::LevelTextsChunk::Tag);
		std::size_t size = (chunk != nullptr ? chunk->Data.size() : 0);
		std::uint32_t revision = session.GetHistory().GetRevision();
		if (revision != _revision || size != _size) {
			_revision = revision;
			_size = size;
			ReadLanguages(document, _languages);
		}
		return _languages;
	}

	void SetLevelTextTranslation(IEditorContext& session, StringView language, std::int32_t index, StringView text)
	{
		if (language.empty() || index < 0 || index >= std::int32_t(Formats::LevelTextsChunk::MaxTexts)) {
			return;
		}
		SmallVector<LocalizedLevelTexts, 0> languages;
		ReadLanguages(session.GetDocument(), languages);
		LocalizedLevelTexts* target = FindExact(languages, language);
		if (target == nullptr) {
			if (text.empty()) {
				return;
			}
			target = &languages.emplace_back();
			target->Language = language;
		}
		if (std::int32_t(target->Texts.size()) <= index) {
			target->Texts.resize(std::size_t(index) + 1);
		}
		target->Texts[index] = text;
		// TRANSLATORS: Name of an undo step in the history, the translation of a text of the level changed
		WriteLanguages(session, languages, _x("editor", "Level text translation"));
	}

	void AddLevelTextLanguageBar(UiStack* content, IEditorContext& session, const std::shared_ptr<LevelTextLanguageState>& state)
	{
		IEditorContext* s = &session;
		auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label of the choice of the language the texts are translated to in the Level Texts dialog
		row->Add<UiLabel>(_x("editor", "Translation"), UiTheme::TextDim);
		auto* dropdown = row->Add<UiDropdown>([state]() {
			// TRANSLATORS: In the choice of the language of translations in the Level Texts dialog, no language, only the texts are edited
			return (state->Language.empty() ? String(_x("editor", "None")) : FormatLanguage(state->Language));
		}, [s, state](SmallVectorImpl<UiMenuEntry>& entries) {
			UiMenuEntry none;
			none.Label = _x("editor", "None");
			none.IsChecked = [state]() { return state->Language.empty(); };
			none.Execute = [state]() { state->Language = {}; };
			entries.push_back(Death::move(none));
			for (const LocalizedLevelTexts& language : state->GetLanguages(*s)) {
				String code = language.Language;
				UiMenuEntry entry;
				entry.Label = FormatLanguage(code);
				entry.IsChecked = [state, code]() { return StringUtils::equalsIgnoreCase(state->Language, code); };
				entry.Execute = [state, code]() { state->Language = code; };
				entries.push_back(Death::move(entry));
			}
			UiMenuEntry add;
			// TRANSLATORS: Last item of the choice of the language of translations in the Level Texts dialog
			add.Label = _x("editor", "Add Language");
			add.Icon = EditorIcon::Plus;
			add.Execute = [s, state]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				// TRANSLATORS: Title and description of the prompt for the code of a new language, keep the codes "cs" and "pt-br" as they are
				ShowTextPrompt(*surface, _x("editor", "Add Language"), _x("editor", "Code of the language, as the game names its translations, e.g. \"cs\" or \"pt-br\""),
					{}, 15, [s, state](StringView text) {
					String language = text;
					StringUtils::lowercaseInPlace(language);
					if (!IsValidLanguage(language)) {
						s->ShowMessage(_x("editor", "That isn't a code of a language"));
						return;
					}
					// The language is saved with its first translated text
					state->Language = language;
				});
			};
			entries.push_back(Death::move(add));
		});
		dropdown->Width = 110.0f;
		dropdown->Tooltip = _x("editor", "Language the texts are translated to, the game shows the translation in that language");

		// TRANSLATORS: Button in the Level Texts dialog, .pot is the file extension of a gettext template, keep it
		auto* exportTemplate = row->Add<UiButton>(_x("editor", "Export .pot"), EditorIcon::Export, [s]() { ExportCatalog(*s, {}); });
		exportTemplate->Tooltip = _x("editor", "Writes the texts for translators, as a gettext template");
		// TRANSLATORS: Button in the Level Texts dialog, .po is the file extension of a gettext catalog, keep it
		auto* exportTranslation = row->Add<UiButton>(_x("editor", "Export .po"), EditorIcon::Export, [s, state]() { ExportCatalog(*s, state->Language); });
		exportTranslation->Tooltip = _x("editor", "Writes the texts with their translation to the chosen language");
		exportTranslation->IsEnabledFunc = [state]() { return !state->Language.empty(); };
		// TRANSLATORS: Button in the Level Texts dialog, .po is the file extension of a gettext catalog, keep it
		auto* importTranslation = row->Add<UiButton>(_x("editor", "Import .po"), EditorIcon::Import, [s, state]() { ImportCatalog(*s, state); });
		importTranslation->Tooltip = _x("editor", "Reads the translations of a gettext catalog into the level");
	}

	void AddLevelTextTranslationBox(UiStack* row, IEditorContext& session, const std::shared_ptr<LevelTextLanguageState>& state, std::int32_t index)
	{
		IEditorContext* s = &session;
		auto* box = row->Add<UiTextBox>();
		box->Flex = 1.0f;
		box->MaxLength = 4096;
		box->Multiline = true;
		box->IsVisibleFunc = [state]() { return !state->Language.empty(); };
		// TRANSLATORS: Placeholder of the empty box of the translation of a text in the Level Texts dialog
		box->PlaceholderFunc = []() { return String(_x("editor", "Not translated")); };
		box->TextFunc = [s, state, index]() {
			const LocalizedLevelTexts* language = Formats::LevelTextsChunk::Find(state->GetLanguages(*s), state->Language);
			// Only its own translation, not the one of the language without its region that the game would fall back to
			if (language == nullptr || !StringUtils::equalsIgnoreCase(language->Language, state->Language) || index >= std::int32_t(language->Texts.size())) {
				return String();
			}
			return EscapeForEditing(language->Texts[index]);
		};
		box->OnCommit = [s, state, index](StringView text) {
			if (state->Language.empty()) {
				return;
			}
			SetLevelTextTranslation(*s, state->Language, index, UnescapeFromEditing(text));
		};
		box->Tooltip = _x("editor", "Translation of the text to the chosen language, the text itself is shown where it's empty");
	}
}

#endif
