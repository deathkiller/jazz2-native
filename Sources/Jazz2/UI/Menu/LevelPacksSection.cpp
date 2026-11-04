#include "LevelPacksSection.h"

#if defined(WITH_LEVEL_PACKS)

#include "LevelPackDetailSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../ContentResolver.h"
#include "../../Formats/LevelPreview.h"
#include "../../LevelPacks/LevelPackLibrary.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/Graphics/PngCodec.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

using namespace Death::IO;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	LevelPacksSection::LevelPacksSection(EpisodeSelectMode mode, bool privateServer)
		: _mode(mode), _privateServer(privateServer), _selectedIndex(0), _columns(1), _animation(0.0f), _y(0.0f), _height(0.0f),
			_availableHeight(0.0f), _noiseCooldown(0.0f), _pressedCount(0), _touchTime(0.0f)
	{
	}

	Recti LevelPacksSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void LevelPacksSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
		// A pack may have been removed in its own section
		RefreshItems();
	}

	void LevelPacksSection::RefreshItems()
	{
		String selectedId;
		if (_selectedIndex >= 0 && _selectedIndex < std::int32_t(_items.size())) {
			selectedId = _items[_selectedIndex].Pack->Id;
		}

		auto packs = LevelPacks::LevelPackLibrary::Get().GetPacks();
		SmallVector<ItemData, 0> items;
		for (auto& pack : packs) {
			ItemData& item = items.emplace_back();
			// Previews of packs that were here already are kept
			for (ItemData& existing : _items) {
				if (existing.Pack->Id == pack->Id) {
					item.Preview = Death::move(existing.Preview);
					item.PreviewSize = existing.PreviewSize;
					item.PreviewLoaded = existing.PreviewLoaded;
					break;
				}
			}
			item.Pack = Death::move(pack);
		}
		_items = Death::move(items);

		_selectedIndex = 0;
		for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
			if (_items[i].Pack->Id == selectedId) {
				_selectedIndex = i;
				break;
			}
		}
	}

	void LevelPacksSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}
		_touchTime += timeMult;

		// One preview each frame, the ones that are seen first
		for (ItemData& item : _items) {
			if (!item.PreviewLoaded && item.Bounds.H > 0.0f) {
				item.Preview = LoadPreview(*item.Pack, item.PreviewSize);
				item.PreviewLoaded = true;
				break;
			}
		}

		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		}
		if (_items.empty()) {
			return;
		}
		if (_root->ActionHit(PlayerAction::Fire)) {
			ExecuteSelected();
			return;
		}

		std::int32_t offset = 0;
		PlayerAction action = PlayerAction::None;
		if (_root->ActionPressed(PlayerAction::Left)) {
			offset = -1;
			action = PlayerAction::Left;
		} else if (_root->ActionPressed(PlayerAction::Right)) {
			offset = 1;
			action = PlayerAction::Right;
		} else if (_root->ActionPressed(PlayerAction::Up)) {
			offset = -_columns;
			action = PlayerAction::Up;
		} else if (_root->ActionPressed(PlayerAction::Down)) {
			offset = _columns;
			action = PlayerAction::Down;
		}

		if (offset != 0) {
			if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(action)) {
				MoveSelection(offset);
				_pressedCount = std::min<std::int32_t>(_pressedCount + 6, 10);
			}
		} else {
			_pressedCount = 0;
		}
	}

	void LevelPacksSection::MoveSelection(std::int32_t offset)
	{
		std::int32_t count = std::int32_t(_items.size());
		if (count <= 1) {
			return;
		}

		std::int32_t index = _selectedIndex + offset;
		if (offset == 1 || offset == -1) {
			index = (index + count) % count;
		} else if (index < 0) {
			// Up from the first row goes to the last row, in the same column if it has one
			std::int32_t column = _selectedIndex % _columns;
			std::int32_t lastRowStart = ((count - 1) / _columns) * _columns;
			index = std::min(lastRowStart + column, count - 1);
		} else if (index >= count) {
			// Down from the last row goes to the first row, a shorter row before it goes to its last tile
			std::int32_t row = _selectedIndex / _columns;
			std::int32_t lastRow = (count - 1) / _columns;
			index = (row < lastRow ? count - 1 : _selectedIndex % _columns);
		}
		if (index == _selectedIndex) {
			return;
		}

		if (_noiseCooldown <= 0.0f) {
			_noiseCooldown = 10.0f;
			_root->PlaySfx("MenuSelect"_s, 0.5f);
		}
		_animation = 0.0f;
		_selectedIndex = index;
		EnsureVisibleSelected();
	}

	void LevelPacksSection::EnsureVisibleSelected()
	{
		if (_selectedIndex < 0 || _selectedIndex >= std::int32_t(_items.size())) {
			return;
		}
		Recti contentBounds = _root->GetContentBounds();
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		const Rectf& bounds = _items[_selectedIndex].Bounds;
		if (bounds.H <= 0.0f) {
			return;
		}
		if (bounds.Y < topLine + TileGap) {
			_y += (topLine + TileGap - bounds.Y);
		} else if (bounds.Y + bounds.H > bottomLine - TileGap) {
			_y -= (bounds.Y + bounds.H - (bottomLine - TileGap));
		}
	}

	void LevelPacksSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		// TRANSLATORS: Header of the section with the list of level packs
		_root->DrawMenuTitle(charOffset, _("Level Packs"), centerX, topLine);
	}

	void LevelPacksSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		if (_items.empty()) {
			// TRANSLATORS: Shown in the Level Packs section when no level pack is installed
			_root->DrawStringShadow(_("No level packs are installed yet"), charOffset, centerX, contentBounds.Y + contentBounds.H * 0.4f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		// As many tiles in a row as fit, so a small screen shows a single column
		float availableWidth = contentBounds.W - 24.0f;
		_columns = std::max(1, std::int32_t((availableWidth + TileGap) / (TileWidth + TileGap)));
		std::int32_t rows = (std::int32_t(_items.size()) + _columns - 1) / _columns;
		float gridWidth = _columns * TileWidth + (_columns - 1) * TileGap;
		float left = std::round(centerX - gridWidth * 0.5f);

		_availableHeight = (bottomLine - topLine);
		_height = rows * TileHeight + (rows + 1) * TileGap;
		_y = (_availableHeight < _height ? std::clamp(_y, _availableHeight - _height, 0.0f) : 0.0f);

		for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
			ItemData& item = _items[i];
			std::int32_t column = i % _columns;
			std::int32_t row = i / _columns;
			float x = left + column * (TileWidth + TileGap);
			float y = std::round(topLine + TileGap + _y + row * (TileHeight + TileGap));
			item.Bounds = Rectf(x, y, TileWidth, TileHeight);

			if (y + TileHeight < topLine || y > bottomLine) {
				continue;
			}

			bool isSelected = (i == _selectedIndex);
			float tileCenterX = x + TileWidth * 0.5f;

			if (isSelected) {
				_root->DrawElement(MenuGlow, 0, tileCenterX, y + TileHeight * 0.5f, IMenuContainer::MainLayer - 200, Alignment::Center,
					Colorf(1.0f, 1.0f, 1.0f, 0.2f + 0.1f * Easing::OutElastic(_animation)), 12.0f, 9.0f, true, true);
			}
			_root->DrawSolid(x, y, IMenuContainer::MainLayer - 150, Alignment::TopLeft, Vector2f(TileWidth, TileHeight),
				Colorf(0.0f, 0.0f, 0.0f, isSelected ? 0.3f : 0.18f));

			// The preview fitted into its box, keeping its shape
			float boxX = x + (TileWidth - PreviewWidth) * 0.5f;
			float boxY = y + 4.0f;
			Vector2f previewSize(PreviewWidth, PreviewHeight);
			if (item.Preview != nullptr && item.PreviewSize.X > 0 && item.PreviewSize.Y > 0) {
				float scale = std::min(PreviewWidth / item.PreviewSize.X, PreviewHeight / item.PreviewSize.Y);
				previewSize = Vector2f(std::round(item.PreviewSize.X * scale), std::round(item.PreviewSize.Y * scale));
			}
			float previewX = std::round(boxX + (PreviewWidth - previewSize.X) * 0.5f);
			float previewY = std::round(boxY + (PreviewHeight - previewSize.Y) * 0.5f);
			Colorf borderColor(0.0f, 0.0f, 0.0f, 1.0f);
			_root->DrawSolid(previewX, previewY - 1.0f, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(previewSize.X, 1.0f), borderColor);
			_root->DrawSolid(previewX, previewY + previewSize.Y, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(previewSize.X, 1.0f), borderColor);
			_root->DrawSolid(previewX - 1.0f, previewY, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(1.0f, previewSize.Y), borderColor);
			_root->DrawSolid(previewX + previewSize.X, previewY, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(1.0f, previewSize.Y), borderColor);
			if (item.Preview != nullptr) {
				float alpha = (isSelected ? 1.0f : 0.8f);
				_root->DrawTexture(*item.Preview, previewX, previewY, IMenuContainer::MainLayer - 100, Alignment::TopLeft, previewSize, Colorf(alpha, alpha, alpha, 1.0f));
			} else {
				_root->DrawSolid(previewX, previewY, IMenuContainer::MainLayer - 100, Alignment::TopLeft, previewSize, Colorf(0.1f, 0.1f, 0.12f, 0.6f));
			}

			const auto& pack = *item.Pack;
			String title = FitText(_root, pack.Title, 0.7f, TileWidth - 6.0f);
			float titleY = boxY + PreviewHeight + 11.0f;
			if (isSelected) {
				_root->DrawStringShadow(title, charOffset, tileCenterX, titleY, IMenuContainer::FontLayer + 10,
					Alignment::Center, Font::RandomColor, 0.7f + 0.05f * Easing::OutElastic(_animation), 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
			} else {
				_root->DrawStringShadow(title, charOffset, tileCenterX, titleY, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, 0.7f);
			}

			String details;
			if (!pack.Episodes.empty()) {
				// TRANSLATORS: Details of a level pack in the Level Packs section
				details = _fn("{} episode", "{} episodes", std::int32_t(pack.Episodes.size()), std::int32_t(pack.Episodes.size()));
			} else {
				// TRANSLATORS: Details of a level pack in the Level Packs section
				details = _fn("{} level", "{} levels", std::int32_t(pack.Levels.size()), std::int32_t(pack.Levels.size()));
			}
			if (!pack.Authors.empty()) {
				details = details + " - "_s + pack.Authors;
			}
			_root->DrawStringShadow(FitText(_root, details, 0.6f, TileWidth - 6.0f), charOffset, tileCenterX, titleY + 12.0f, IMenuContainer::FontLayer - 2,
				Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.6f);

			if (!pack.MissingAssets.empty()) {
				// Something the levels need isn't there, they may look or sound wrong
				_root->DrawStringShadow("!"_s, charOffset, previewX + previewSize.X - 4.0f, previewY + 8.0f, IMenuContainer::FontLayer + 12,
					Alignment::Right, Colorf(0.9f, 0.5f, 0.2f, 0.6f), 0.9f);
			}
		}

		if (_y < 0.0f) {
			_root->DrawMenuEdgeGlow(centerX, topLine);
		}
		if (_availableHeight - _y < _height) {
			_root->DrawMenuEdgeGlow(centerX, bottomLine);
		}
	}

	void LevelPacksSection::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}
					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
						if (_availableHeight < _height) {
							_y += touchMove.Y - _touchLast.Y;
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).Length() > 10.0f || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}
				for (std::int32_t i = 0; i < std::int32_t(_items.size()); i++) {
					if (_items[i].Bounds.Contains(_touchLast)) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
			}
			default:
				break;
		}
	}

	void LevelPacksSection::ExecuteSelected()
	{
		if (_selectedIndex < 0 || _selectedIndex >= std::int32_t(_items.size())) {
			return;
		}
		_root->PlaySfx("MenuSelect"_s, 0.6f);
		_root->SwitchToSection<LevelPackDetailSection>(_items[_selectedIndex].Pack, _mode, _privateServer);
	}

	std::unique_ptr<Texture> LevelPacksSection::LoadPreview(const LevelPacks::LevelPackManifest& pack, Vector2i& size)
	{
		auto& resolver = ContentResolver::Get();
		size = Vector2i();

		SmallVector<std::uint8_t, 0> texels;
		String thumbnailPath = fs::CombinePath({ resolver.GetLevelPacksCachePath(), "Thumbnails"_s, String(pack.Id + ".png"_s) });
		if (fs::IsReadableFile(thumbnailPath)) {
			auto s = fs::Open(thumbnailPath, FileAccess::Read);
			PngRgbaImage image;
			PngDecodeOptions options;
			options.VerifyCrc = true;
			options.MaxPixels = 1024 * 1024;
			if (s->IsValid() && PngCodec::DecodeRgba(*s, image, options) == PngError::None) {
				size = Vector2i(image.Width, image.Height);
				texels.resize_for_overwrite(std::size_t(image.Width) * image.Height * 4);
				std::memcpy(texels.data(), image.Pixels.get(), texels.size());
			}
		}

		if (texels.empty()) {
			// The first level of the first episode, or the first level of all
			String levelName;
			if (!pack.Episodes.empty()) {
				levelName = pack.Episodes[0].Name + "/"_s + pack.Episodes[0].FirstLevel;
			} else if (!pack.Levels.empty()) {
				levelName = pack.Levels[0].Name;
			}
			if (!levelName.empty()) {
				String path = resolver.FindContentFile(fs::CombinePath("Episodes"_s, String(levelName + ".j2l"_s)));
				if (path.empty() || !Formats::LevelPreview::Read(path, texels, size)) {
					texels.clear();
					size = Vector2i();
				}
			}
		}

		if (texels.empty() || size.X <= 0 || size.Y <= 0) {
			return nullptr;
		}
		auto texture = std::make_unique<Texture>("LevelPackPreview", Texture::Format::RGBA8, size.X, size.Y);
		texture->LoadFromTexels(texels.data(), 0, 0, size.X, size.Y);
		texture->SetMinFiltering(SamplerFilter::Linear);
		texture->SetMagFiltering(SamplerFilter::Nearest);
		return texture;
	}

	String LevelPacksSection::FitText(IMenuContainer* root, StringView text, float scale, float maxWidth)
	{
		if (root->MeasureString(text, scale).X <= maxWidth) {
			return String(text);
		}
		// Cut on a whole character, a byte that continues one is never the last
		std::size_t length = text.size();
		while (length > 0) {
			length--;
			while (length > 0 && (std::uint8_t(text[length]) & 0xC0) == 0x80) {
				length--;
			}
			String shortened = text.prefix(length) + "..."_s;
			if (root->MeasureString(shortened, scale).X <= maxWidth) {
				return shortened;
			}
		}
		return "..."_s;
	}
}

#endif
