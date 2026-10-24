#include "CustomLevelSelectSection.h"
#include "StartGameOptionsSection.h"
#include "MenuResources.h"
#include "../../Formats/LevelFile.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/Base/FrameTimer.h"
#include "../../../nCine/I18n.h"

#if defined(WITH_MULTIPLAYER)
#	include "CreateServerOptionsSection.h"
#	include "CreateLocalGameOptionsSection.h"
#endif

#include <Containers/StringConcatenable.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

using namespace Death::IO;
using namespace Death::IO::Compression;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	CustomLevelSelectSection::CustomLevelSelectSection(bool multiplayer, bool privateServer, bool localGame)
		: _multiplayer(multiplayer), _privateServer(privateServer), _localGame(localGame), _selectedIndex(0), _animation(0.0f), _y(0.0f),
			_height(0.0f), _availableHeight(0.0f), _touchTime(0.0f), _touchSpeed(0.0f), _pressedCount(0),
			_noiseCooldown(0.0f), _touchDirection(0), _indexingFinished(false), _indexingCancelled(false), _itemsSorted(false), _selectionMoved(false),
			_previewAnimation(0.0f), _previewWait(0.0f), _previewWaitIndex(-1)
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
			, _previewRequestDone(false), _previewRequestActive(false)
#endif
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		_indexingThread = Thread([](void* arg) {
			auto* _this = static_cast<CustomLevelSelectSection*>(arg);
			_this->AddCustomLevels();
		}, this);
#else
		AddCustomLevels();
		TakeIndexedItems();
#endif
	}

	CustomLevelSelectSection::~CustomLevelSelectSection()
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Indicate that indexing thread should stop
		_indexingCancelled.store(true, std::memory_order_relaxed);
		_indexingThread.Join();
		if (_previewRequestActive) {
			_previewThread.Join();
		}
#endif
	}

	Recti CustomLevelSelectSection::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
	}

	void CustomLevelSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void CustomLevelSelectSection::OnUpdate(float timeMult)
	{
		TakeIndexedItems();

		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		// A quarter of a second
		if (_previewAnimation < 1.0f) {
			_previewAnimation = std::min(_previewAnimation + timeMult * 0.067f, 1.0f);
		}

		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero && _availableHeight < _height) {
				float y = _y + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (y < (_availableHeight - _height) && _touchDirection < 0) {
					y = (_availableHeight - _height);
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (y > 0.0f && _touchDirection > 0) {
					y = 0.0f;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_y = y;
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
		}

		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}

		UpdatePreview(timeMult);

		if (_root->ActionHit(PlayerAction::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (!_items.empty()) {
			if (_root->ActionHit(PlayerAction::Fire)) {
				ExecuteSelected();
			} else if (_items.size() > 1) {
				if (_root->ActionPressed(PlayerAction::Up)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerAction::Up)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						std::int32_t offset;
						if (_selectedIndex > 0) {
							_selectedIndex--;
							offset = -ItemHeight / 2;
						} else {
							_selectedIndex = (std::int32_t)(_items.size() - 1);
							offset = 0;
						}
						_selectionMoved = true;
						EnsureVisibleSelected(offset);
						_pressedCount = std::min<std::int32_t>(_pressedCount + 6, 10);
					}
				} else if (_root->ActionPressed(PlayerAction::Down)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerAction::Down)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						std::int32_t offset;
						if (_selectedIndex < _items.size() - 1) {
							_selectedIndex++;
							offset = ItemHeight / 2;
						} else {
							_selectedIndex = 0;
							offset = 0;
						}
						_selectionMoved = true;
						EnsureVisibleSelected(offset);
						_pressedCount = std::min<std::int32_t>(_pressedCount + 6, 10);
					}
				} else {
					_pressedCount = 0;
				}
			}

			_touchTime += timeMult;
		}
	}

	void CustomLevelSelectSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		std::int32_t charOffset = 0;
		// TRANSLATORS: Header of the section with the list of custom levels
		_root->DrawMenuTitle(charOffset, _("Play Custom Levels"), centerX, topLine);
	}

	void CustomLevelSelectSection::DrawPreview(float right, float centerY, float maxWidth)
	{
		// Where the level starts, small on the left of its name, in a thin black border with its corners rounded off by
		// a pixel, over a soft shadow dropped below it
		if (_preview == nullptr || _selectedIndex < 0 || _selectedIndex >= (std::int32_t)_items.size() ||
			_items[_selectedIndex].Path != _previewPath || _previewSize.X <= 0 || _previewSize.Y <= 0) {
			return;
		}
		float width = std::min(std::round(maxWidth), 48.0f);
		if (width < 24.0f) {
			return;
		}
		Vector2f size(width, std::max(std::round(width * _previewSize.Y / _previewSize.X), 1.0f));
		// A preview much taller than wide (it may be 1 by 256) is fitted into twice its width in height instead, the shadow
		// texture is made for the drawn size, so it would get far too large otherwise
		float maxHeight = width * 2.0f;
		if (size.Y > maxHeight) {
			size.Y = maxHeight;
			size.X = std::max(std::round(maxHeight * _previewSize.X / _previewSize.Y), 1.0f);
		}
		// Fades in while it slides a few pixels to its place from the left, on whole pixels
		constexpr float SlideDistance = 8.0f;
		float eased = Easing::OutCubic(_previewAnimation);
		float x = std::round(right - size.X - (1.0f - eased) * SlideDistance);
		float y = std::round(centerY - size.Y * 0.5f);
		float alpha = eased;

		// Dropped a little below and to the right of it
		UpdatePreviewShadow(Vector2i(std::int32_t(size.X), std::int32_t(size.Y)));
		float spread = float(PreviewShadowSpread);
		_root->DrawTexture(*_previewShadow, x - spread + 1.0f, y - spread + 2.0f, IMenuContainer::MainLayer - 110, Alignment::TopLeft,
			Vector2f(size.X + spread * 2.0f, size.Y + spread * 2.0f), Colorf(1.0f, 1.0f, 1.0f, alpha));

		// The border leaves out its corner pixels, which is what rounds the corners
		Colorf borderColor(0.0f, 0.0f, 0.0f, alpha);
		_root->DrawSolid(x, y - 1.0f, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(size.X, 1.0f), borderColor);
		_root->DrawSolid(x, y + size.Y, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(size.X, 1.0f), borderColor);
		_root->DrawSolid(x - 1.0f, y, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(1.0f, size.Y), borderColor);
		_root->DrawSolid(x + size.X, y, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(1.0f, size.Y), borderColor);

		_root->DrawTexture(*_preview, x, y, IMenuContainer::MainLayer - 100, Alignment::TopLeft, size, Colorf(1.0f, 1.0f, 1.0f, alpha));
	}

	void CustomLevelSelectSection::UpdatePreview(float timeMult)
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		if (_previewRequestActive && _previewRequestDone.load(std::memory_order_acquire)) {
			// The thread has nothing left to do but to end
			_previewThread.Join();
			_previewRequestActive = false;
			_previewRequestDone.store(false, std::memory_order_relaxed);
			// Kept even when it's empty, so a level whose preview can't be read isn't read again and again
			CachePreview(Death::move(_previewRequest));
		}
#endif

		if (_selectedIndex < 0 || _selectedIndex >= (std::int32_t)_items.size()) {
			return;
		}
		const ItemData& item = _items[_selectedIndex];
		if (item.Path == _previewPath) {
			// Shown already, or known to have none
			return;
		}

		if (!item.HasPreview) {
			// Most levels have none (none converted from the original game has), they're known by the header
			// already, so nothing is read for them at all
			_preview = nullptr;
			_previewSize = Vector2i();
			_previewPath = item.Path;
			return;
		}
		for (std::int32_t i = std::int32_t(_previewCache.size()) - 1; i >= 0; i--) {
			if (_previewCache[i].Path == item.Path) {
				ShowPreview(_previewCache[i]);
				return;
			}
		}

		// Not while the selection is moving quickly, reading a level takes a while
		if (_previewWaitIndex != _selectedIndex) {
			_previewWaitIndex = _selectedIndex;
			_previewWait = 0.0f;
		}
		_previewWait += timeMult;
		if (_previewWait <= 15.0f) {
			return;
		}

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		if (_previewRequestActive) {
			// One at a time, the next one starts once it's done
			return;
		}
		// The thread is started after the request is filled in, so it sees all of it
		_previewRequest = PreviewData();
		_previewRequest.Path = item.Path;
		_previewRequestActive = true;
		_previewThread = Thread([](void* arg) {
			auto* _this = static_cast<CustomLevelSelectSection*>(arg);
			ReadPreview(_this->_previewRequest);
			_this->_previewRequestDone.store(true, std::memory_order_release);
		}, this);
#else
		PreviewData preview;
		preview.Path = item.Path;
		ReadPreview(preview);
		ShowPreview(preview);
		CachePreview(Death::move(preview));
#endif
	}

	void CustomLevelSelectSection::ShowPreview(const PreviewData& preview)
	{
		_preview = nullptr;
		_previewSize = Vector2i();
		_previewPath = preview.Path;
		if (preview.Size.X <= 0 || preview.Size.Y <= 0) {
			return;
		}

		_preview = std::make_unique<Texture>("LevelPreview", Texture::Format::RGBA8, preview.Size.X, preview.Size.Y);
		_preview->LoadFromTexels(preview.Texels.data(), 0, 0, preview.Size.X, preview.Size.Y);
		_preview->SetMinFiltering(SamplerFilter::Linear);
		_preview->SetMagFiltering(SamplerFilter::Nearest);
		_previewSize = preview.Size;
		_previewAnimation = 0.0f;
	}

	void CustomLevelSelectSection::UpdatePreviewShadow(Vector2i size)
	{
		if (_previewShadow != nullptr && _previewShadowSize == size) {
			return;
		}

		// Black, faded out with the square of the distance from the edge of the preview, as the shadows of the editor's
		// menus are (see Editor::UiSkin), around the same corners rounded off by a pixel
		constexpr float MaxOpacity = 0.45f;
		constexpr float Radius = 1.0f;
		const std::int32_t spread = PreviewShadowSpread;
		const std::int32_t width = size.X + spread * 2;
		const std::int32_t height = size.Y + spread * 2;
		const float innerHalfX = size.X * 0.5f - Radius;
		const float innerHalfY = size.Y * 0.5f - Radius;

		SmallVector<std::uint8_t, 0> texels;
		texels.resize_for_overwrite(std::size_t(width) * std::size_t(height) * 4);
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				float qx = std::abs(x + 0.5f - width * 0.5f) - innerHalfX;
				float qy = std::abs(y + 0.5f - height * 0.5f) - innerHalfY;
				float outsideX = std::max(qx, 0.0f), outsideY = std::max(qy, 0.0f);
				float distance = std::sqrt(outsideX * outsideX + outsideY * outsideY) + std::min(std::max(qx, qy), 0.0f) - Radius;
				float fade = 1.0f - std::clamp(distance / float(spread), 0.0f, 1.0f);

				std::uint8_t* texel = &texels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
				texel[0] = 0;
				texel[1] = 0;
				texel[2] = 0;
				texel[3] = std::uint8_t(fade * fade * MaxOpacity * 255.0f + 0.5f);
			}
		}

		_previewShadow = std::make_unique<Texture>("LevelPreviewShadow", Texture::Format::RGBA8, width, height);
		_previewShadow->LoadFromTexels(texels.data(), 0, 0, width, height);
		_previewShadow->SetMinFiltering(SamplerFilter::Linear);
		_previewShadow->SetMagFiltering(SamplerFilter::Linear);
		_previewShadowSize = size;
	}

	void CustomLevelSelectSection::CachePreview(PreviewData&& preview)
	{
		for (std::size_t i = 0; i < _previewCache.size(); i++) {
			if (_previewCache[i].Path == preview.Path) {
				_previewCache.erase(_previewCache.begin() + i);
				break;
			}
		}
		if (std::int32_t(_previewCache.size()) >= MaxCachedPreviews) {
			_previewCache.erase(_previewCache.begin());
		}
		_previewCache.push_back(Death::move(preview));
	}

	void CustomLevelSelectSection::ReadPreview(PreviewData& preview)
	{
		// The editor and the converter store previews of at most 128x72 pixels, anything much larger isn't one of them
		constexpr std::int32_t MaxPreviewSize = 256;

		preview.Texels.clear();
		preview.Size = Vector2i();

		auto s = fs::Open(preview.Path, FileAccess::Read);
		Formats::LevelHeader header;
		if (!s->IsValid() || !Formats::LevelFile::ReadHeader(*s, header)) {
			return;
		}
		Formats::LevelReader reader(*s, header);
		Formats::LevelProperties properties;
		reader.ReadProperties(properties);
		if (!reader.SkipToChunks()) {
			return;
		}
		SmallVector<Formats::ChunkRecord, 0> chunks;
		reader.ReadSelectedChunks(chunks, [](std::uint32_t tag, std::uint32_t) {
			return (tag == Formats::LevelFile::PreviewChunk);
		});
		for (const Formats::ChunkRecord& chunk : chunks) {
			if (chunk.Tag != Formats::LevelFile::PreviewChunk || chunk.Data.size() < 4) {
				continue;
			}
			std::int32_t width = chunk.Data[0] | (chunk.Data[1] << 8);
			std::int32_t height = chunk.Data[2] | (chunk.Data[3] << 8);
			if (width <= 0 || height <= 0 || width > MaxPreviewSize || height > MaxPreviewSize ||
				chunk.Data.size() < 4 + std::size_t(width) * std::size_t(height) * 3) {
				return;
			}
			const std::size_t pixelCount = std::size_t(width) * std::size_t(height);
			preview.Texels.resize_for_overwrite(pixelCount * 4);
			for (std::size_t i = 0; i < pixelCount; i++) {
				preview.Texels[i * 4] = chunk.Data[4 + i * 3];
				preview.Texels[i * 4 + 1] = chunk.Data[4 + i * 3 + 1];
				preview.Texels[i * 4 + 2] = chunk.Data[4 + i * 3 + 2];
				preview.Texels[i * 4 + 3] = 255;
			}
			preview.Size = Vector2i(width, height);
			return;
		}
	}

	void CustomLevelSelectSection::OnDrawClipped(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;
		std::int32_t charOffset = 0;

		if (_items.empty()) {
			_root->DrawStringShadow(_("No custom level found!"), charOffset, centerX, contentBounds.Y + contentBounds.H * 0.33f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		_availableHeight = (bottomLine - topLine);
		constexpr float spacing = 20.0f;

		_y = (_availableHeight < _height ? std::clamp(_y, _availableHeight - _height, 0.0f) : 0.0f);

		Vector2f center = Vector2f(centerX, topLine + 12.0f + _y);
		float column1 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.25f) : 20.0f);
		float column2 = contentBounds.X + (contentBounds.W >= 460 ? (contentBounds.W * 0.52f) : (contentBounds.W * 0.44f));

		std::size_t itemsCount = _items.size();
		for (std::int32_t i = 0; i < itemsCount; i++) {
			_items[i].Y = center.Y;

			if (center.Y > topLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				StringView levelName, displayName;
#if defined(WITH_MULTIPLAYER)
				if (_items[i].LevelName == CreateServerOptionsSection::FromPlaylist) {
					// TRANSLATORS: Item in the list of levels when creating a multiplayer server, starts the server with the configured playlist instead of a single level
					levelName = _("Create server from playlist");
				} else
#endif
				{
					levelName = _items[i].LevelName;
					displayName = _items[i].DisplayName;
				}

				if (_selectedIndex == i) {
					float easing = Easing::OutElastic(_animation);
					float size = 0.7f + easing * 0.12f;
					float xMultiplier = _items[i].DisplayName.size() * 0.25f;
					float x = column1 + xMultiplier - easing * xMultiplier;

					_root->DrawElement(MenuGlow, 0, centerX, center.Y, IMenuContainer::MainLayer - 200, Alignment::Center,
						Colorf(1.0f, 1.0f, 1.0f, 0.2f), 26.0f, 3.0f, true, true);

					_root->DrawStringShadow(levelName, charOffset, x, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Left, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

					DrawPreview(column1 - 16.0f, center.Y, column1 - 16.0f - (contentBounds.X + 6.0f));
				} else {
					_root->DrawStringShadow(levelName, charOffset, column1, center.Y, IMenuContainer::FontLayer,
						Alignment::Left, Font::DefaultColor, 0.7f);
				}

				_root->DrawStringShadow(displayName, charOffset, column2, center.Y, IMenuContainer::FontLayer + 10 - 2,
					Alignment::Left, Font::DefaultColor, 0.7f);
			}

			center.Y += spacing;
		}

		_height = center.Y - (topLine + _y);

		if (_items[0].Y < TopLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, topLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_items[itemsCount - 1].Y > bottomLine - ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void CustomLevelSelectSection::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
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
							float delta = touchMove.Y - _touchLast.Y;
							if (delta != 0.0f) {
								_y += delta;
								if (delta < -0.1f && _touchDirection >= 0) {
									_touchDirection = -1;
									_touchSpeed = 0.0f;
								} else if (delta > 0.1f && _touchDirection <= 0) {
									_touchDirection = 1;
									_touchSpeed = 0.0f;
								}
								_touchSpeed = (0.8f * _touchSpeed) + (0.2f * std::abs(delta) / TouchKineticDivider);
							}
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

				float halfW = viewSize.X * 0.5f;
				std::size_t itemsCount = _items.size();
				for (std::int32_t i = 0; i < itemsCount; i++) {
					if (std::abs(_touchLast.X - halfW) < 150.0f && std::abs(_touchLast.Y - _items[i].Y) < 22.0f) {
						if (_selectedIndex == i) {
							ExecuteSelected();
						} else {
							_root->PlaySfx("MenuSelect"_s, 0.5f);
							_animation = 0.0f;
							_selectedIndex = i;
							_selectionMoved = true;
							EnsureVisibleSelected();
						}
						break;
					}
				}
				break;
			}
		}
	}

	void CustomLevelSelectSection::ExecuteSelected()
	{
		if (_items.empty()) {
			return;
		}

		auto& selectedItem = _items[_selectedIndex];
		_root->PlaySfx("MenuSelect"_s, 0.6f);

		String levelName;
#if defined(WITH_MULTIPLAYER)
		if (selectedItem.LevelName == CreateServerOptionsSection::FromPlaylist) {
			levelName = selectedItem.LevelName;
		} else
#endif
		{
			levelName = "unknown/"_s + selectedItem.LevelName;
		}

#if defined(WITH_MULTIPLAYER)
		if (_localGame) {
			_root->SwitchToSection<CreateLocalGameOptionsSection>(levelName, nullptr);
			return;
		}
		if (_multiplayer) {
			_root->SwitchToSection<CreateServerOptionsSection>(levelName, nullptr, _privateServer);
			return;
		}
#endif
		_root->SwitchToSection<StartGameOptionsSection>(levelName, nullptr);
	}

	void CustomLevelSelectSection::EnsureVisibleSelected(std::int32_t offset)
	{
		Recti contentBounds = _root->GetContentBounds();
		float topLine = contentBounds.Y + TopLine;
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		std::int32_t y = _items[_selectedIndex].Y + offset;
		if (y < topLine + ItemHeight * 0.5f) {
			_y += (topLine + ItemHeight * 0.5f - y);
		} else if (y > bottomLine - ItemHeight * 0.5f) {
			_y += (bottomLine - ItemHeight * 0.5f - y);
		}
	}

	void CustomLevelSelectSection::TakeIndexedItems()
	{
		if (_itemsSorted) {
			return;
		}

		bool finished;
		{
			std::unique_lock<Death::Threading::Spinlock> lock(_indexedItemsLock);
			finished = _indexingFinished.load(std::memory_order_relaxed);
			for (ItemData& item : _indexedItems) {
				_items.push_back(Death::move(item));
			}
			_indexedItems.clear();
		}

		if (finished) {
			// Sorted once all levels are found. The selection stays on the level it's on if the user moved it, otherwise
			// it's on the first one again - the levels came in the order they were found, the one first then may be anywhere.
			_itemsSorted = true;
			String selectedName;
			if (_selectionMoved && _selectedIndex >= 0 && _selectedIndex < (std::int32_t)_items.size()) {
				selectedName = _items[_selectedIndex].LevelName;
			}
			nCine::sort(_items.begin(), _items.end(), [](const ItemData& a, const ItemData& b) -> bool {
				return (a.LevelName < b.LevelName);
			});
			if (!_selectionMoved) {
				_selectedIndex = 0;
			} else if (!selectedName.empty()) {
				for (std::int32_t i = 0; i < (std::int32_t)_items.size(); i++) {
					if (_items[i].LevelName == selectedName) {
						_selectedIndex = i;
						break;
					}
				}
			}
			_previewWaitIndex = -1;
		}
	}

	void CustomLevelSelectSection::PublishItem(ItemData&& item)
	{
		std::unique_lock<Death::Threading::Spinlock> lock(_indexedItemsLock);
		_indexedItems.push_back(Death::move(item));
	}

	void CustomLevelSelectSection::AddCustomLevels()
	{
		auto& resolver = ContentResolver::Get();

#if defined(WITH_MULTIPLAYER)
		if (_multiplayer && !_localGame) {
			ItemData level;
			level.LevelName = CreateServerOptionsSection::FromPlaylist;
			PublishItem(Death::move(level));
		}
#endif

		// Search user "Content/Episodes/", "Content/Episodes/" and "Cache/Episodes/", the first file with a given name
		// wins, the same way as ContentResolver::TryLoadLevel() resolves it
		HashMap<String, bool> foundLevels;
		auto addLevelsIn = [&](StringView path) {
			for (auto item : fs::Directory(fs::CombinePath({ path, "Episodes"_s, "unknown"_s }), fs::EnumerationOptions::SkipDirectories)) {
				if (_indexingCancelled.load(std::memory_order_relaxed)) {
					return false;
				}
				if (foundLevels.try_emplace(String(fs::GetFileName(item)), true).second) {
					AddLevel(item);
				}
			}
			return true;
		};

		StringView userContentPath = resolver.GetUserContentPath();
		if (!userContentPath.empty() && !addLevelsIn(userContentPath)) {
			return;
		}
		if (!addLevelsIn(resolver.GetContentPath()) || !addLevelsIn(resolver.GetCachePath())) {
			return;
		}

		// The main thread sorts the list then, it's the only one that touches it (see TakeIndexedItems())
		std::unique_lock<Death::Threading::Spinlock> lock(_indexedItemsLock);
		_indexingFinished.store(true, std::memory_order_relaxed);
	}

	void CustomLevelSelectSection::AddLevel(StringView levelFile)
	{
		if (fs::GetExtension(levelFile) != "j2l"_s) {
			return;
		}

		auto s = fs::Open(levelFile, FileAccess::Read);
		if (!s->IsValid()) {
			LOGW("Cannot open level \"{}\" for reading", levelFile);
			return;
		}

		Formats::LevelHeader header;
		if (!Formats::LevelFile::ReadHeader(*s, header)) {
			LOGW("Level \"{}\" has invalid signature or unsupported version", levelFile);
			return;
		}
		LevelFlags flags = header.Flags;

#if !defined(DEATH_DEBUG)
		// Don't show hidden levels in Release build if unlock cheat is not active, but show all levels in Debug build
		if ((flags & LevelFlags::IsHidden) == LevelFlags::IsHidden && !PreferencesCache::AllowCheatsUnlock) {
			return;
		}
		if ((flags & LevelFlags::IsMultiplayerLevel) == LevelFlags::IsMultiplayerLevel && !_multiplayer) {
			return;
		}
#endif

		// The display name comes first, so nothing else has to be read
		Formats::LevelReader reader(*s, header);
		Formats::LevelProperties properties;
		reader.ReadProperties(properties);

		ItemData level;
		level.LevelName = fs::GetFileNameWithoutExtension(levelFile);
		level.Path = levelFile;
		level.HasPreview = ((flags & LevelFlags::HasPreview) == LevelFlags::HasPreview);
		if (!properties.Author.empty()) {
			// TRANSLATORS: Level in the list of custom levels, with its name and the name of who made it
			level.DisplayName = _f("{} by {}", properties.DisplayName, properties.Author);
		} else {
			level.DisplayName = Death::move(properties.DisplayName);
		}

#if defined(DEATH_DEBUG)
		if ((flags & LevelFlags::IsHidden) == LevelFlags::IsHidden) {
			level.DisplayName += " [Hidden]"_s;
		}
		if ((flags & LevelFlags::IsMultiplayerLevel) == LevelFlags::IsMultiplayerLevel) {
			level.DisplayName += " [Multiplayer]"_s;
		} else if ((flags & LevelFlags::HasMultiplayerSpawnPoints) == LevelFlags::HasMultiplayerSpawnPoints) {
			level.DisplayName += " [MultiSpawnPoints]"_s;
		}
		// The script of a level is the file of the same name next to it, see LevelHandler
		if (fs::FileExists(String(levelFile.exceptSuffix(".j2l"_s) + ".j2as"_s))) {
			level.DisplayName += " [Script]"_s;
		}
#endif

		PublishItem(Death::move(level));
	}
}