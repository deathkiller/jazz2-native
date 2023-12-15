#include "ServerSelectSection.h"

#if defined(WITH_MULTIPLAYER)

#include "StartGameOptionsSection.h"
#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Base/FrameTimer.h"

using namespace Death::IO;
using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	ServerSelectSection::ServerSelectSection()
		: _selectedIndex(0), _animation(0.0f), _y(0.0f), _height(0.0f), _pressedCount(0), _noiseCooldown(0.0f), _discovery(Multiplayer::ServerDiscovery(this))
	{
	}

	ServerSelectSection::~ServerSelectSection()
	{
	}

	Recti ServerSelectSection::GetClipRectangle(const Vector2i& viewSize)
	{
		return Recti(0, TopLine - 1.0f, viewSize.X, viewSize.Y - TopLine - BottomLine + 2.0f);
	}

	void ServerSelectSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	void ServerSelectSection::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}
		if (_noiseCooldown > 0.0f) {
			_noiseCooldown -= timeMult;
		}

		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (!_items.empty()) {
			if (_root->ActionHit(PlayerActions::Fire)) {
				ExecuteSelected();
			} else if (_items.size() > 1) {
				if (_root->ActionPressed(PlayerActions::Up)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerActions::Up)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						if (_selectedIndex > 0) {
							_selectedIndex--;
						} else {
							_selectedIndex = (int32_t)(_items.size() - 1);
						}
						EnsureVisibleSelected();
						_pressedCount = std::min(_pressedCount + 6, 10);
					}
				} else if (_root->ActionPressed(PlayerActions::Down)) {
					if (_animation >= 1.0f - (_pressedCount * 0.096f) || _root->ActionHit(PlayerActions::Down)) {
						if (_noiseCooldown <= 0.0f) {
							_noiseCooldown = 10.0f;
							_root->PlaySfx("MenuSelect"_s, 0.5f);
						}
						_animation = 0.0f;

						if (_selectedIndex < _items.size() - 1) {
							_selectedIndex++;
						} else {
							_selectedIndex = 0;
						}
						EnsureVisibleSelected();
						_pressedCount = std::min(_pressedCount + 6, 10);
					}
				} else {
					_pressedCount = 0;
				}
			}

			_touchTime += timeMult;
		}
	}

	void ServerSelectSection::OnDraw(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		float centerX = viewSize.X * 0.5f;
		float bottomLine = viewSize.Y - BottomLine;
		_root->DrawElement(MenuDim, centerX, (TopLine + bottomLine) * 0.5f, IMenuContainer::BackgroundLayer,
			Alignment::Center, Colorf::Black, Vector2f(680.0f, bottomLine - TopLine + 2.0f), Vector4f(1.0f, 0.0f, 0.4f, 0.3f));
		_root->DrawElement(MenuLine, 0, centerX, TopLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);
		_root->DrawElement(MenuLine, 1, centerX, bottomLine, IMenuContainer::MainLayer, Alignment::Center, Colorf::White, 1.6f);

		int32_t charOffset = 0;
		_root->DrawStringShadow(_("Connect to Server"), charOffset, centerX, TopLine - 21.0f, IMenuContainer::FontLayer,
			Alignment::Center, Colorf(0.46f, 0.46f, 0.46f, 0.5f), 0.9f, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
	}

	void ServerSelectSection::OnDrawClipped(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		int32_t charOffset = 0;

		if (_items.empty()) {
			_root->DrawStringShadow(_("No servers found, but still searchin'!"), charOffset, viewSize.X * 0.5f, viewSize.Y * 0.55f, IMenuContainer::FontLayer,
				Alignment::Center, Colorf(0.62f, 0.44f, 0.34f, 0.5f), 0.9f, 0.4f, 0.6f, 0.6f, 0.8f, 0.88f);
			return;
		}

		float bottomLine = viewSize.Y - BottomLine;
		float availableHeight = (bottomLine - TopLine);
		constexpr float spacing = 20.0f;

		_y = (availableHeight - _height < 0.0f ? std::clamp(_y, availableHeight - _height, 0.0f) : 0.0f);

		Vector2f center = Vector2f(viewSize.X * 0.5f, TopLine + 12.0f + _y);
		float column1 = viewSize.X * 0.25f;
		float column2 = viewSize.X * 0.52f;

		std::size_t itemsCount = _items.size();
		for (int32_t i = 0; i < itemsCount; i++) {
			_items[i].Y = center.Y;

			if (center.Y > TopLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				if (_selectedIndex == i) {
					float xMultiplier = _items[i].Desc.EndpointString.size() * 0.5f;
					float easing = IMenuContainer::EaseOutElastic(_animation);
					float x = column1 + xMultiplier - easing * xMultiplier;
					float size = 0.7f + easing * 0.12f;

					_root->DrawStringShadow(_items[i].Desc.Name, charOffset, x, center.Y, IMenuContainer::FontLayer + 10,
						Alignment::Left, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					_root->DrawStringShadow(_items[i].Desc.Name, charOffset, column1, center.Y, IMenuContainer::FontLayer,
						Alignment::Left, Font::DefaultColor, 0.7f);
				}

				_root->DrawStringShadow(_items[i].Desc.EndpointString, charOffset, column2, center.Y, IMenuContainer::FontLayer + 10 - 2,
					Alignment::Left, Font::DefaultColor, 0.7f);
			}

			center.Y += spacing;
		}

		_height = center.Y - (TopLine + _y);

		if (_items[0].Y < TopLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, TopLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		if (_items[itemsCount - 1].Y > bottomLine - ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	void ServerSelectSection::OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					float y = event.pointers[pointerIndex].y * (float)viewSize.Y;
					if (y < 80.0f) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * (float)viewSize.X, event.pointers[pointerIndex].y * (float)viewSize.Y);
						_y += touchMove.Y - _touchLast.Y;
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
				for (int32_t i = 0; i < itemsCount; i++) {
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

	void ServerSelectSection::OnServerFound(Multiplayer::ServerDesc&& desc)
	{
		for (auto& item : _items) {
			if (item.Desc.EndpointString == desc.EndpointString) {
				item.Desc = std::move(desc);
				return;
			}
		}

		_items.emplace_back(std::move(desc));
	}

	void ServerSelectSection::ExecuteSelected()
	{
		if (_items.empty()) {
			return;
		}

		_root->PlaySfx("MenuSelect"_s, 0.6f);

		auto& selectedItem = _items[_selectedIndex];
		_root->ConnectToServer(selectedItem.Desc.EndpointString, selectedItem.Desc.Endpoint.port);
	}

	void ServerSelectSection::EnsureVisibleSelected()
	{
		float bottomLine = _root->GetViewSize().Y - BottomLine;
		if (_items[_selectedIndex].Y < TopLine + ItemHeight * 0.5f) {
			_y += (TopLine + ItemHeight * 0.5f - _items[_selectedIndex].Y);
		} else if (_items[_selectedIndex].Y > bottomLine - ItemHeight * 0.5f) {
			_y += (bottomLine - ItemHeight * 0.5f - _items[_selectedIndex].Y);
		}
	}

	ServerSelectSection::ItemData::ItemData(Multiplayer::ServerDesc&& desc)
		: Desc(std::move(desc))
	{
	}
}

#endif