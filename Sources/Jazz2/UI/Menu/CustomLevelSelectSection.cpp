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
			_noiseCooldown(0.0f), _touchDirection(0), _previewWait(0.0f)
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		_indexingThread = Thread([](void* arg) {
			auto* _this = static_cast<CustomLevelSelectSection*>(arg);
			_this->AddCustomLevels();
		}, this);
#else
		AddCustomLevels();
#endif
	}

	CustomLevelSelectSection::~CustomLevelSelectSection()
	{
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Indicate that indexing thread should stop
		_selectedIndex = -1;
		_indexingThread.Join();
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
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
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

		// Not while the selection is moving quickly, reading a level takes a while
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_items.size() && _items[_selectedIndex].Path != _previewPath) {
			_previewWait += timeMult;
			if (_previewWait > 15.0f) {
				_previewWait = 0.0f;
				LoadPreview(_selectedIndex);
			}
		}

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
		_root->DrawMenuTitle(charOffset, _("Play Custom Levels"), centerX, topLine);
	}

	void CustomLevelSelectSection::DrawPreview(float right, float centerY, float maxWidth)
	{
		// Where the level starts, small on the left of its name, with a soft shadow around
		if (_preview == nullptr || _selectedIndex < 0 || _selectedIndex >= (std::int32_t)_items.size() ||
			_items[_selectedIndex].Path != _previewPath || _previewSize.X <= 0 || _previewSize.Y <= 0) {
			return;
		}
		float width = std::min(std::round(maxWidth), 96.0f);
		if (width < 40.0f) {
			return;
		}
		Vector2f size(width, std::round(width * _previewSize.Y / _previewSize.X));
		float x = std::round(right - size.X);
		float y = std::round(centerY - size.Y * 0.5f);
		for (std::int32_t i = 4; i > 0; i--) {
			float grow = i * 1.5f;
			_root->DrawSolid(x - grow + 1.0f, y - grow + 2.0f, IMenuContainer::MainLayer - 110 + (4 - i), Alignment::TopLeft,
				Vector2f(size.X + grow * 2.0f, size.Y + grow * 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.12f));
		}
		_root->DrawSolid(x - 1.0f, y - 1.0f, IMenuContainer::MainLayer - 105, Alignment::TopLeft, Vector2f(size.X + 2.0f, size.Y + 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.5f));
		_root->DrawTexture(*_preview, x, y, IMenuContainer::MainLayer - 100, Alignment::TopLeft, size, Colorf::White);
	}

	void CustomLevelSelectSection::LoadPreview(std::int32_t index)
	{
		_preview = nullptr;
		_previewSize = Vector2i();
		if (index < 0 || index >= (std::int32_t)_items.size()) {
			return;
		}
		_previewPath = _items[index].Path;
		if (_previewPath.empty()) {
			return;
		}

		auto s = fs::Open(_previewPath, FileAccess::Read);
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
		reader.ReadChunks(chunks, false);
		for (const Formats::ChunkRecord& chunk : chunks) {
			if (chunk.Tag != Formats::LevelFile::PreviewChunk || chunk.Data.size() < 4) {
				continue;
			}
			std::int32_t width = chunk.Data[0] | (chunk.Data[1] << 8);
			std::int32_t height = chunk.Data[2] | (chunk.Data[3] << 8);
			if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || chunk.Data.size() < 4 + std::size_t(width) * std::size_t(height) * 3) {
				return;
			}
			SmallVector<std::uint8_t, 0> texels;
			texels.resize_for_overwrite(std::size_t(width) * std::size_t(height) * 4);
			for (std::size_t i = 0; i < std::size_t(width) * std::size_t(height); i++) {
				texels[i * 4] = chunk.Data[4 + i * 3];
				texels[i * 4 + 1] = chunk.Data[4 + i * 3 + 1];
				texels[i * 4 + 2] = chunk.Data[4 + i * 3 + 2];
				texels[i * 4 + 3] = 255;
			}
			_preview = std::make_unique<Texture>("LevelPreview", Texture::Format::RGBA8, width, height);
			_preview->LoadFromTexels(texels.data(), 0, 0, width, height);
			_preview->SetMinFiltering(SamplerFilter::Linear);
			_preview->SetMagFiltering(SamplerFilter::Nearest);
			_previewSize = Vector2i(width, height);
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

					DrawPreview(column1 - 8.0f, center.Y, column1 - 8.0f - (contentBounds.X + 6.0f));
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

	void CustomLevelSelectSection::AddCustomLevels()
	{
		auto& resolver = ContentResolver::Get();

#if defined(WITH_MULTIPLAYER)
		if (_multiplayer && !_localGame) {
			auto& level = _items.emplace_back();
			level.LevelName = CreateServerOptionsSection::FromPlaylist;
		}
#endif

		// Search user "Content/Episodes/", "Content/Episodes/" and "Cache/Episodes/", the first file with a given name
		// wins, the same way as ContentResolver::TryLoadLevel() resolves it
		HashMap<String, bool> foundLevels;
		auto addLevelsIn = [&](StringView path) {
			for (auto item : fs::Directory(fs::CombinePath({ path, "Episodes"_s, "unknown"_s }), fs::EnumerationOptions::SkipDirectories)) {
				if (_selectedIndex < 0) {
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

		nCine::sort(_items.begin(), _items.end(), [](const ItemData& a, const ItemData& b) -> bool {
			return (a.LevelName < b.LevelName);
		});
	}

	void CustomLevelSelectSection::AddLevel(StringView levelFile)
	{
		if (fs::GetExtension(levelFile) != "j2l"_s) {
			return;
		}

		auto s = fs::Open(levelFile, FileAccess::Read);
		DEATH_ASSERT(s->IsValid(), "Cannot open file for reading", );

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

		auto& level = _items.emplace_back();
		level.LevelName = fs::GetFileNameWithoutExtension(levelFile);
		level.Path = levelFile;
		if (!properties.Author.empty()) {
			// TRANSLATORS: Level in the list of custom levels, with its name and the name of who made it
			level.DisplayName = _f("{} by {}", properties.DisplayName, properties.Author);
		} else {
			level.DisplayName = std::move(properties.DisplayName);
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
#endif
	}
}