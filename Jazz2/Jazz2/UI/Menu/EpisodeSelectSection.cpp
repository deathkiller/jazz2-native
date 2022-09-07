#include "EpisodeSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "../../../nCine/Base/Algorithms.h"

namespace Jazz2::UI::Menu
{
	EpisodeSelectSection::EpisodeSelectSection()
		:
		_selectedIndex(0),
		_animation(0.0f)
	{
		// Search both "Content/Episodes/" and "Cache/Episodes/"
		fs::Directory dir(fs::JoinPath("Content"_s, "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dir.GetNext();
			if (item == nullptr) {
				break;
			}

			AddEpisode(item);
		}

		fs::Directory dirCache(fs::JoinPath("Cache"_s, "Episodes"_s), fs::EnumerationOptions::SkipDirectories);
		while (true) {
			StringView item = dirCache.GetNext();
			if (item == nullptr) {
				break;
			}

			AddEpisode(item);
		}

		quicksort(_items.begin(), _items.end(), [](const EpisodeSelectSection::ItemData& a, const EpisodeSelectSection::ItemData& b) -> bool {
			return (a.Position < b.Position);
		});
	}

	void EpisodeSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		if (_root->ActionHit(PlayerActions::Fire)) {
			_root->PlaySfx("MenuSelect"_s, 0.6f);
			ExecuteSelected();
		} else if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (_root->ActionHit(PlayerActions::Up)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex > 0) {
				_selectedIndex--;
			} else {
				_selectedIndex = _items.size() - 1;
			}
		} else if (_root->ActionHit(PlayerActions::Down)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_animation = 0.0f;
			if (_selectedIndex < _items.size() - 1) {
				_selectedIndex++;
			} else {
				_selectedIndex = 0;
			}
		}
	}

	void EpisodeSelectSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Vector2f center = Vector2f(viewSize.X * 0.5f, viewSize.Y * 0.5f);

		constexpr float topLine = 131.0f;
		float bottomLine = viewSize.Y - 42;
		_root->DrawElement("MenuDim"_s, center.X, (topLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::White, Vector2f(680.0f, bottomLine - topLine + 2), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement("MenuLine"_s, 0, center.X, topLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement("MenuLine"_s, 1, center.X, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		center.Y = topLine + (bottomLine - topLine) * 0.7f / _items.size();
		int charOffset = 0;

		_root->DrawStringShadow("Play Story"_s, charOffset, center.X, topLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		for (int i = 0; i < _items.size(); i++) {
			_items[i].TouchY = center.Y;

			if (_selectedIndex == i) {
				float size = 0.5f + IMenuContainer::EaseOutElastic(_animation) * 0.6f;

				_root->DrawElement("MenuGlow"_s, 0, center.X, center.Y, IMenuContainer::MainLayer, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.4f * size), (_items[i].DisplayName.size() + 3) * 0.5f * size, 4.0f * size, true);

				_root->DrawStringShadow(_items[i].DisplayName, charOffset, center.X, center.Y, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
			} else {
				_root->DrawStringShadow(_items[i].DisplayName, charOffset, center.X, center.Y, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.9f);
			}

			center.Y += (bottomLine - topLine) * 0.94f / _items.size();
		}
	}

	void EpisodeSelectSection::OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize)
	{
		if (event.type == TouchEventType::Down) {
			int pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				float y = event.pointers[pointerIndex].y * (float)viewSize.Y;

				if (y < 80.0f) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_root->LeaveSection();
					return;
				}

				for (int i = 0; i < _items.size(); i++) {
					if (std::abs(x - 0.5f) < 0.22f && std::abs(y - _items[i].TouchY) < 30.0f) {
						if (_selectedIndex == i) {
							_root->PlaySfx("MenuSelect"_s, 0.6f);
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
						}
						break;
					}
				}
			}
		}
	}

	void EpisodeSelectSection::ExecuteSelected()
	{
		auto& selectedItem = _items[_selectedIndex];
		_root->SwitchToSectionPtr(std::make_unique<StartGameOptionsSection>(selectedItem.Name, selectedItem.FirstLevel, selectedItem.PreviousEpisode));
	}

	void EpisodeSelectSection::AddEpisode(const StringView& episodeFile)
	{
		if (!fs::HasExtension(episodeFile, "j2e"_s)) {
			return;
		}

		auto s = fs::Open(episodeFile, FileAccessMode::Read);
		if (s->GetSize() < 16) {
			return;
		}

		uint64_t signature = s->ReadValue<uint64_t>();
		uint8_t version = s->ReadValue<uint8_t>();
		if (signature != 0x2095A59FF0BFBBEF || version != 2) {
			return;
		}

		auto& episode = _items.emplace_back();
		episode.Name = fs::GetFileNameWithoutExtension(episodeFile);

		uint16_t flags = s->ReadValue<uint16_t>();

		uint8_t nameLength = s->ReadValue<uint8_t>();
		episode.DisplayName = String(NoInit, nameLength);
		s->Read(episode.DisplayName.data(), nameLength);

		episode.Position = s->ReadValue<uint16_t>();

		nameLength = s->ReadValue<uint8_t>();
		episode.FirstLevel = String(NoInit, nameLength);
		s->Read(episode.FirstLevel.data(), nameLength);

		nameLength = s->ReadValue<uint8_t>();
		episode.PreviousEpisode = String(NoInit, nameLength);
		s->Read(episode.PreviousEpisode.data(), nameLength);
	}
}