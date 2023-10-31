#include "LanguageSelectSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/I18n.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	LanguageSelectSection::LanguageSelectSection()
	{
		auto& resolver = ContentResolver::Get();

		auto& defaultLanguage = _items.emplace_back();
		defaultLanguage.Item.DisplayName = "English"_s;

		// Search both "Content/Translations/" and "Cache/Translations/"
		fs::Directory dir(fs::CombinePath(resolver.GetContentPath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dir.GetNext();
			if (item == nullptr) {
				break;
			}

			AddLanguage(item);
		}

		fs::Directory dirCache(fs::CombinePath(resolver.GetCachePath(), "Translations"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dirCache.GetNext();
			if (item == nullptr) {
				break;
			}

			AddLanguage(item);
		}
	}

	void LanguageSelectSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement(MenuDim, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Language"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void LanguageSelectSection::OnDrawItem(Canvas* canvas, ListViewItem& item, int32_t& charOffset, bool isSelected)
	{
		float centerX = canvas->ViewSize.X * 0.5f;

		if (isSelected) {
			float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

			size_t realNameLength;
			StringView realNameEnd = item.Item.DisplayName.find('\f');
			if (realNameEnd != nullptr) {
				realNameLength = realNameEnd.data() - item.Item.DisplayName.data() + 4;
			} else {
				realNameLength = item.Item.DisplayName.size();
			}

			_root->DrawElement(MenuGlow, 0, centerX, item.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (realNameLength + 3) * 0.5f * size, 4.0f * size, true);

			_root->DrawStringShadow(item.Item.DisplayName, charOffset, centerX, item.Y, IMenuContainer::FontLayer + 10,
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

	void LanguageSelectSection::AddLanguage(const StringView& languageFile)
	{
		if (fs::GetExtension(languageFile) != "mo"_s) {
			return;
		}

		auto language = fs::GetFileNameWithoutExtension(languageFile);
		if (language.empty() || language.size() >= sizeof(PreferencesCache::Language)) {
			return;
		}

		if (language == StringView(PreferencesCache::Language)) {
			_selectedIndex = _items.size();
		}

		auto& episode = _items.emplace_back();
		episode.Item.FileName = languageFile;
		episode.Item.DisplayName = I18n::GetLanguageName(language) + " \f[c:0x707070]· " + language;
	}
}