#include "LanguageSelectSection.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

namespace Jazz2::UI::Menu
{
	// Applies the chosen language and recreates the menu (or leaves the section on failure)
	static void ApplyLanguage(IMenuContainer* root, const String& fileName)
	{
		bool success = false;
		if (!fileName.empty()) {
			if (I18n::Get().LoadFromFile(fileName)) {
				auto language = fs::GetFileNameWithoutExtension(fileName);
				std::memcpy(PreferencesCache::Language, language.data(), language.size());
				std::memset(PreferencesCache::Language + language.size(), 0, sizeof(PreferencesCache::Language) - language.size());
				success = true;
			}
		} else {
			I18n::Get().Unload();
			std::memset(PreferencesCache::Language, 0, sizeof(PreferencesCache::Language));
			success = true;
		}

		if (success) {
			PreferencesCache::Save();
			// This recreates the whole menu (destroying this section), so it must be the last thing done
			root->ApplyPreferencesChanges(ChangedPreferencesType::Language);
		} else {
			root->LeaveSection();
		}
	}

	void LanguageSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		SetTitle(_("Language"));

		auto& resolver = ContentResolver::Get();
		auto list = std::make_unique<ScrollView>();

		// Default (built-in English)
		list->Add<ListItem>("English \f[c:#707070]· en"_s, [root]() { ApplyLanguage(root, {}); });

		std::int32_t selectedIndex = 0;
		std::int32_t nextIndex = 1;
		HashMap<String, bool> foundLanguages;

		auto addLanguage = [&](StringView languageFile, bool fromCache) {
			if (fs::GetExtension(languageFile) != "mo"_s) {
				return;
			}
			auto language = fs::GetFileNameWithoutExtension(languageFile);
			if (language.empty() || language.size() >= sizeof(PreferencesCache::Language)) {
				return;
			}
			// Add each language only once (cache directory takes precedence over content)
			if (!foundLanguages.try_emplace(language, true).second) {
				return;
			}

			String displayName = fromCache
				? String{I18n::GetLanguageName(language) + " \f[c:#707070]· "_s + language + "⁺"_s}
				: String{I18n::GetLanguageName(language) + " \f[c:#707070]· "_s + language};
			if (language == StringView(PreferencesCache::Language)) {
				selectedIndex = nextIndex;
			}

			String fileName = languageFile;
			list->Add<ListItem>(displayName, [root, fileName]() { ApplyLanguage(root, fileName); });
			nextIndex++;
		};

		// Search first "Cache/Translations/" and then "Content/Translations/"
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetCachePath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			addLanguage(item, true);
		}
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetContentPath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			addLanguage(item, false);
		}

		list->SetSelectedIndex(selectedIndex);
		SetContent(std::move(list));
	}
}
