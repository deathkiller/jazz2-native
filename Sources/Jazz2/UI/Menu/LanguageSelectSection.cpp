#include "LanguageSelectSection.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	LanguageSelectSection::LanguageSelectSection()
	{
		auto& resolver = ContentResolver::Get();

		auto& defaultLanguage = _items.emplace_back();
		defaultLanguage.Item.DisplayName = "English \f[c:#707070]· en"_s;

		// Search first "Cache/Translations/" and then "Content/Translations/"
		HashMap<String, bool> foundLanguages;
		for (auto item : fs::Directory(fs::CombinePath(resolver.GetCachePath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddLanguage(item, foundLanguages, true);
		}

		for (auto item : fs::Directory(fs::CombinePath(resolver.GetContentPath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories)) {
			AddLanguage(item, foundLanguages, false);
		}
	}

	void LanguageSelectSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawElement(MenuDim, centerX, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - topLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		std::int32_t charOffset = 0;
		_root->DrawStringShadow(_("Language"), charOffset, centerX, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void LanguageSelectSection::OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			_root->DrawStringGlow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
				Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
		} else {
			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer,
				Alignment::Center, Font::DefaultColor, 0.9f);
		}
	}

	void LanguageSelectSection::OnExecuteSelected()
	{
		bool success = false;
		auto& selectedItem = _items[_selectedIndex];
		if (!selectedItem.Item.FileName.empty()) {
			if (I18n::Get().LoadFromFile(selectedItem.Item.FileName)) {
				auto language = fs::GetFileNameWithoutExtension(selectedItem.Item.FileName);
				std::memcpy(PreferencesCache::Language, language.data(), language.size());
				std::memset(PreferencesCache::Language + language.size(), 0, sizeof(PreferencesCache::Language) - language.size());
				success = true;
			}
		} else {
			I18n::Get().Unload();
			std::memset(PreferencesCache::Language, 0, sizeof(PreferencesCache::Language));
			success = true;
		}

		_root->PlaySfx("MenuSelect"_s, 0.6f);

		if (success) {
			PreferencesCache::Save();
			// It will automatically recreate the menu
			_root->ApplyPreferencesChanges(ChangedPreferencesType::Language);
		} else {
			_root->LeaveSection();
		}
	}

	void LanguageSelectSection::AddLanguage(const StringView languageFile, HashMap<String, bool>& foundLanguages, bool fromCache)
	{
		if (fs::GetExtension(languageFile) != "mo"_s) {
			return;
		}

		auto language = fs::GetFileNameWithoutExtension(languageFile);
		if (language.empty() || language.size() >= sizeof(PreferencesCache::Language)) {
			return;
		}

		// Add each language only once
		if (!foundLanguages.try_emplace(language, true).second) {
			return;
		}

		if (language == StringView(PreferencesCache::Language)) {
			_selectedIndex = _items.size();
		}

		auto& episode = _items.emplace_back();
		episode.Item.FileName = languageFile;
		episode.Item.DisplayName = fromCache
			? String{I18n::GetLanguageName(language) + " \f[c:#707070]· "_s + language + "⁺"_s}
			: String{I18n::GetLanguageName(language) + " \f[c:#707070]· "_s + language};
	}
}