#pragma once

#include "MenuSection.h"
#include "MenuResources.h"

#include "../../../nCine/Base/FrameTimer.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	/** @brief Simplifies creation of unified scrollable menu sections */
	template<class TItem>
	class ScrollableMenuSection : public MenuSection
	{
	public:
		ScrollableMenuSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

	protected:
		/** @brief Generic item in @ref ScrollableMenuSection */
		struct ListViewItem {
			TItem Item;
			std::int32_t Y;
			std::int32_t Height;

			ListViewItem() { }
			ListViewItem(TItem item) : Item(item) { }
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::int32_t ItemHeight = 40;
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;

		SmallVector<ListViewItem> _items;
		std::int32_t _selectedIndex;
		float _animation;
		float _transitionTime;
		std::int32_t _y;
		std::int32_t _height;
		std::int32_t _availableHeight;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		float _touchSpeed;
		std::int8_t _touchDirection;
		bool _scrollable;
#endif

		/** @brief Makes currently selected item visible in the viewport */
		void EnsureVisibleSelected(std::int32_t offset = 0);
		/** @brief Called when the selected item should be executed */
		virtual void OnExecuteSelected() = 0;
		/** @brief Called when an item layout should be calculated */
		virtual void OnLayoutItem(Canvas* canvas, ListViewItem& item);
		/** @brief Called when an information about empty list should be drawn */
		virtual void OnDrawEmptyText(Canvas* canvas, std::int32_t& charOffset) { }
		/** @brief Called when an item should be drawn */
		virtual void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) = 0;
		/** @brief Called when input should be handled */
		virtual void OnHandleInput();
		/** @brief Called when back button is pressed */
		virtual void OnBackPressed();
		/** @brief Called when selected item is changed */
		virtual void OnSelectionChanged(ListViewItem& item) { }
		/** @brief Called when a touch event is released */
		virtual void OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos);
	};

	template<class TItem>
	ScrollableMenuSection<TItem>::ScrollableMenuSection()
		: _selectedIndex(0), _animation(0.0f), _transitionTime(0.0f), _y(0), _height(0), _availableHeight(0),
			_touchTime(0.0f), _touchSpeed(0.0f), _touchDirection(0), _scrollable(false)
	{
	}

	template<class TItem>
	Recti ScrollableMenuSection<TItem>::GetClipRectangle(const Recti& contentBounds)
	{
		return Recti(contentBounds.X, contentBounds.Y + TopLine - 1, contentBounds.W, contentBounds.H - TopLine - BottomLine + 2);
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

		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero && _scrollable) {
				float y = _y + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (y < (_availableHeight - _height) && _touchDirection < 0) {
					y = float(_availableHeight - _height);
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (y > 0.0f && _touchDirection > 0) {
					y = 0.0f;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_y = std::int32_t(y);
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
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
		if (_root->ActionHit(PlayerAction::Menu)) {
			OnBackPressed();
		} else if (!_items.empty()) {
			if (_root->ActionHit(PlayerAction::Fire)) {
				OnExecuteSelected();
			} else if (_items.size() > 1) {
				if (_root->ActionHit(PlayerAction::Up)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					std::int32_t offset;
					if (_selectedIndex > 0) {
						_selectedIndex--;
						offset = -ItemHeight / 3;
					} else {
						_selectedIndex = (std::int32_t)(_items.size() - 1);
						offset = 0;
					}
					EnsureVisibleSelected(offset);
					OnSelectionChanged(_items[_selectedIndex]);
				} else if (_root->ActionHit(PlayerAction::Down)) {
					_root->PlaySfx("MenuSelect"_s, 0.5f);
					_animation = 0.0f;

					std::int32_t offset;
					if (_selectedIndex < _items.size() - 1) {
						_selectedIndex++;
						offset = ItemHeight / 3;
					} else {
						_selectedIndex = 0;
						offset = 0;
					}
					EnsureVisibleSelected(offset);
					OnSelectionChanged(_items[_selectedIndex]);
				}
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnBackPressed()
	{
		_root->PlaySfx("MenuSelect"_s, 0.5f);
		_root->LeaveSection();
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnDrawClipped(Canvas* canvas)
	{
		Vector2i viewSize = canvas->ViewSize;
		Recti contentBounds = _root->GetContentBounds();
		Recti clipRect = GetClipRectangle(contentBounds);
		std::int32_t charOffset = 0;

		if (_items.empty()) {
			_scrollable = false;
			OnDrawEmptyText(canvas, charOffset);
			return;
		}

		std::int32_t topLine = clipRect.Y + 1;
		std::int32_t bottomLine = clipRect.Y + clipRect.H - 1;
		_availableHeight = (bottomLine - topLine);

		if (_height == 0) {
			_height = ItemHeight * 2 / 3;

			for (std::int32_t i = 0; i < _items.size(); i++) {
				auto& item = _items[i];
				item.Y = _height + topLine + _y;
				OnLayoutItem(canvas, item);
				_height += item.Height;
			}
			_height -= ItemHeight / 2;

			if (_availableHeight - _height < 0) {
				_scrollable = true;
			}

			EnsureVisibleSelected();
		}

		if (_availableHeight - _height < 0) {
			_y = std::clamp(_y, _availableHeight - _height, 0);
			_scrollable = true;
		} else {
			_y = (_availableHeight - _height) / 2;
			_scrollable = false;
		}

		Vector2i center = Vector2i(viewSize.X / 2, topLine + ItemHeight / 2 + _y);

		for (std::int32_t i = 0; i < _items.size(); i++) {
			auto& item = _items[i];
			item.Y = center.Y;

			if (center.Y > topLine - ItemHeight && center.Y < bottomLine + ItemHeight) {
				OnDrawItem(canvas, item, charOffset, _selectedIndex == i);
			}

			center.Y += item.Height;
		}

		if (_items[0].Y < topLine + ItemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, float(center.X), float(topLine), 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
		std::int32_t itemHeight = _items[_items.size() - 1].Height - ItemHeight * 4 / 5 + ItemHeight / 2;
		if (_items[_items.size() - 1].Y > bottomLine - itemHeight / 2) {
			_root->DrawElement(MenuGlow, 0, float(center.X), float(bottomLine), 900, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 30.0f, 5.0f);
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnTouchEvent(const TouchEvent& event, Vector2i viewSize)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					std::int32_t y = std::int32_t(event.pointers[pointerIndex].y * float(viewSize.Y));
					if (y < 80) {
						OnBackPressed();
						return;
					}

					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, float(y));
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
						if (_scrollable) {
							float delta = touchMove.Y - _touchLast.Y;
							if (delta != 0.0f) {
								_y += std::int32_t(delta);
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
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).SqrLength() > 100 || _touchTime > FrameTimer::FramesPerSecond);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return;
				}

				for (std::int32_t i = 0; i < _items.size(); i++) {
					if (std::abs(_touchLast.Y - _items[i].Y) < 22) {
						OnTouchUp(i, viewSize, _touchLast.As<std::int32_t>());
						break;
					}
				}
				break;
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos)
	{
		std::int32_t halfW = viewSize.X / 2;
		if (std::abs(touchPos.X - halfW) < 150) {
			if (_selectedIndex == newIndex) {
				OnExecuteSelected();
			} else {
				_root->PlaySfx("MenuSelect"_s, 0.5f);
				_animation = 0.0f;
				_selectedIndex = newIndex;
				EnsureVisibleSelected();
				OnSelectionChanged(_items[_selectedIndex]);
			}
		}
	}

	template<class TItem>
	void ScrollableMenuSection<TItem>::EnsureVisibleSelected(std::int32_t offset)
	{
		if (!_scrollable) {
			return;
		}

		Recti contentBounds = _root->GetContentBounds();
		Recti clipRect = GetClipRectangle(contentBounds);
		std::int32_t topLine = clipRect.Y + 1;
		std::int32_t bottomLine = clipRect.Y + clipRect.H - 1;
		std::int32_t y = _items[_selectedIndex].Y + offset;

		if (y < topLine + ItemHeight / 2) {
			// Scroll up
			_y += (topLine + ItemHeight / 2 - y);
			return;
		}
		
		std::int32_t itemHeight = _items[_selectedIndex].Height - ItemHeight * 4 / 5 + ItemHeight / 2;
		if (y > bottomLine - itemHeight) {
			// Scroll down
			_y += (bottomLine - itemHeight - y);
			return;
		}
	}
}