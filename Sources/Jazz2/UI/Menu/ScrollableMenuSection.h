#pragma once

#include "MenuSection.h"
#include "MenuResources.h"

#include "../../../nCine/Base/FrameTimer.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	template<class TItem>
	class ScrollableMenuSection : public MenuSection
	{
	public:
		ScrollableMenuSection();

		Recti GetClipRectangle(const Vector2i& viewSize) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize) override;

	protected:
		struct ListViewItem {
			TItem Item;
			int Y;
			int Height;

			ListViewItem() { }
			ListViewItem(TItem item) : Item(item) { }
		};

		static constexpr int ItemHeight = 40;
		static constexpr int TopLine = 131;
		static constexpr int BottomLine = 42;

		SmallVector<ListViewItem> _items;
		int _selectedIndex;
		float _animation;
		float _transitionTime;
		int _y;
		int _height;
		Vector2i _touchStart;
		Vector2i _touchLast;
		float _touchTime;
		bool _scrollable;

		void EnsureVisibleSelected();
		virtual void OnExecuteSelected() = 0;
		virtual void OnLayoutItem(Canvas* canvas, ListViewItem& item);
		virtual void OnDrawEmptyText(Canvas* canvas, int& charOffset) { }
		virtual void OnDrawItem(Canvas* canvas, ListViewItem& item, int& charOffset, bool isSelected) = 0;
		virtual void OnHandleInput();
		virtual void OnTouchUp(int newIndex, const Vector2i& viewSize, const Vector2i& touchPos);
	};

	template<class TItem>
	ScrollableMenuSection<TItem>::ScrollableMenuSection()
		:
		_selectedIndex(0),
		_animation(0.0f),
		_transitionTime(0.0f),
		_y(0),
		_height(0),
		_scrollable(false)
	{
	}

	template<class TItem>
	Recti ScrollableMenuSection<TItem>::GetClipRectangle(const Vector2i& viewSize)
	{
		return Recti(0, TopLine - 1, viewSize.X, viewSize.Y - TopLine - BottomLine + 2);
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		_animation = 0.0f;
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnUpdate(float timeMult)
	{
		if (_animation < 1.0f) {
			_animation = std::min(_animation + timeMult * 0.016f, 1.0f);
		}

		OnHandleInput();

		_touchTime += timeMult;
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnLayoutItem(Canvas* canvas, ListViewItem& item)
	{
		item.Height = ItemHeight * 4 / 5;
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnHandleInput()
	{
		if (_root->ActionHit(PlayerActions::Menu)) {
			_root->PlaySfx("MenuSelect"_s, 0.5f);
			_root->LeaveSection();
			return;
		} else if (!_items.empty()) {
			if (_root->ActionHit(PlayerActions::Fire)) {
				OnExecuteSelected();
			} else if (_items.size() > 1) {
				if (_root->ActionHit(PlayerActions::Up)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					if (_selectedIndex > 0) {
						_selectedIndex--;
					} else {
						_selectedIndex = (int)(_items.size() - 1);
					}
					EnsureVisibleSelected();
				} else if (_root->ActionHit(PlayerActions::Down)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					if (_selectedIndex < _items.size() - 1) {
						_selectedIndex++;
					} else {
						_selectedIndex = 0;
					}
					EnsureVisibleSelected();
				}
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnDrawClipped(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		int charOffset = 0;

		if (_items.empty()) {
			_scrollable = false;
			OnDrawEmptyText(canvas, charOffset);
			return;
		}

		int bottomLine = viewSize.Y - BottomLine;
		int availableHeight = (bottomLine - TopLine);

		if (_height == 0) {
			_height = ItemHeight * 2 / 3;

			for (int i = 0; i < _items.size(); i++) {
				auto& item = _items[i];
				item.Y = _height + TopLine + _y;
				OnLayoutItem(canvas, item);
				_height += item.Height;
			}
			_height -= ItemHeight / 2;

			if (availableHeight - _height < 0) {
				_scrollable = true;
			}

			EnsureVisibleSelected();
		}

		if (availableHeight - _height < 0) {
			_y = std::clamp(_y, availableHeight - _height, 0);
			_scrollable = true;
		} else {
			_y = (availableHeight - _height) / 2;
			_scrollable = false;
		}

		Vector2i center = Vector2i(viewSize.X / 2, TopLine + ItemHeight / 2 + _y);

		for (int i = 0; i < _items.size(); i++) {
			auto& item = _items[i];
			item.Y = center.Y;

			if (center.Y > TopLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				OnDrawItem(canvas, item, charOffset, _selectedIndex == i);
			}

			center.Y += item.Height;
		}

		if (_items[0].Y < TopLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, TopLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		int itemHeight = _items[_items.size() - 1].Height - ItemHeight * 4 / 5 + ItemHeight / 2;
		if (_items[_items.size() - 1].Y > bottomLine - itemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, center.X, bottomLine, 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnTouchEvent(const TouchEvent& event, const Vector2i& viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				int pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					int y = (int)(event.pointers[pointerIndex].y * viewSize.Y);
					if (y < 80) {
						_root->PlaySfx("MenuSelect"_s, 0.5f);
						_root->LeaveSection();
						return;
					}

					_touchStart = Vector2i((int)(event.pointers[pointerIndex].x * viewSize.X), y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				break;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2i::Zero) {
					int pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2i touchMove = Vector2i((int)(event.pointers[pointerIndex].x * viewSize.X), (int)(event.pointers[pointerIndex].y * viewSize.Y));
						if (_scrollable) {
							_y += touchMove.Y - _touchLast.Y;
						}
						_touchLast = touchMove;
					}
				}
				break;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2i::Zero || (_touchStart - _touchLast).SqrLength() > 100 || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2i::Zero;
				if (alreadyMoved) {
					return;
				}

				for (int i = 0; i < _items.size(); i++) {
					if (std::abs(_touchLast.Y - _items[i].Y) < 22) {
						OnTouchUp(i, viewSize, _touchLast);
						break;
					}
				}
				break;
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnTouchUp(int newIndex, const Vector2i& viewSize, const Vector2i& touchPos)
	{
		int halfW = viewSize.X / 2;
		if (std::abs(touchPos.X - halfW) < 150) {
			if (_selectedIndex == newIndex) {
				OnExecuteSelected();
			} else {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				_selectedIndex = newIndex;
				EnsureVisibleSelected();
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::EnsureVisibleSelected()
	{
		if (!_scrollable) {
			return;
		}

		auto& item = _items[_selectedIndex];
		int bottomLine = _root->GetViewSize().Y - BottomLine;
		if (item.Y < TopLine + ItemHeight / 2) {
			// Scroll up
			_y += (TopLine + ItemHeight / 2 - item.Y);
			return;
		}
		
		int itemHeight = item.Height - ItemHeight * 4 / 5 + ItemHeight / 2;
		if (item.Y > bottomLine - itemHeight) {
			// Scroll down
			_y += (bottomLine - itemHeight - item.Y);
			return;
		}
	}
}