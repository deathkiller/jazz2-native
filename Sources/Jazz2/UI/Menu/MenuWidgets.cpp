#include "MenuWidgets.h"
#include "../Font.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::UI::Menu
{
	void ListItem::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
	}

	void ListItem::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		float centerX = bounds.X + bounds.W * 0.5f;
		float y = bounds.Y + bounds.H * 0.5f;
		root->DrawMenuListItem(charOffset, Text, centerX, y, Selected, Animation.Raw());
	}

	void ListItem::Activate(IMenuContainer* root)
	{
		if (OnActivate) {
			OnActivate();
		}
	}

	void ChoiceItem::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
	}

	void ChoiceItem::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		float centerX = bounds.X + bounds.W * 0.5f;
		float y = bounds.Y + bounds.H * 0.5f;
		root->DrawMenuListItem(charOffset, Label, centerX, y, Selected, Animation.Raw(), ReadOnly);
		if (Value) {
			StringView value = Value();
			// Arrows are shown only for editable rows (selected, not read-only, has an OnChange handler);
			// a value with no handler (e.g., a read-only resolution display) still draws but without arrows
			bool showArrows = (Selected && !ReadOnly && (bool)OnChange);
			root->DrawMenuValue(charOffset, value, centerX, y + 22.0f, Selected, ReadOnly, showArrows, Animation.Raw(), ArrowSpacing);
		}
	}

	bool ChoiceItem::OnNavigate(const WidgetInput& input, IMenuContainer* root)
	{
		if (input.Left || input.Right || input.Fire) {
			if (!ReadOnly && OnChange) {
				root->PlaySfx("MenuSelect"_s, 0.6f);
				OnChange(input.Left ? -1 : 1);
				Animation.Restart(0.0f);
			}
			return true;
		}
		return false;
	}

	bool ChoiceItem::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		// A tap cycles the value (left half decrements, right half increments, mirroring the `< >` arrows), but
		// only once the row is selected, so the first tap just selects it (matching the keyboard flow). Returning
		// false lets the container select an unselected row.
		if (!Selected || ReadOnly || !OnChange || event.type != TouchEventType::Up) {
			return false;
		}
		std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
		if (pointerIndex == -1) {
			return false;
		}
		float x = event.pointers[pointerIndex].x * viewSize.X;
		root->PlaySfx("MenuSelect"_s, 0.6f);
		OnChange(x < Bounds.X + Bounds.W * 0.5f ? -1 : 1);
		Animation.Restart(0.0f);
		return true;
	}

	void CustomValueItem::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
	}

	void CustomValueItem::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		float centerX = bounds.X + bounds.W * 0.5f;
		float y = bounds.Y + bounds.H * 0.5f;
		root->DrawMenuListItem(charOffset, Label, centerX, y, Selected, Animation.Raw(), ReadOnly);
		if (Selected && !ReadOnly && (bool)OnChange) {
			root->DrawMenuArrows(charOffset, centerX, y + 22.0f, Animation.Raw(), ArrowSpacing);
		}
		if (DrawValue) {
			DrawValue(root, centerX, y + 22.0f, charOffset, Selected, ReadOnly);
		}
	}

	bool CustomValueItem::OnNavigate(const WidgetInput& input, IMenuContainer* root)
	{
		if (input.Left || input.Right || input.Fire) {
			if (!ReadOnly) {
				if (OnChange) {
					root->PlaySfx("MenuSelect"_s, 0.6f);
					OnChange(input.Left ? -1 : 1);
					Animation.Restart(0.0f);
					return true;
				}
				if (input.Fire && OnActivate) {
					OnActivate();
					return true;
				}
			}
			return true;
		}
		return false;
	}

	bool CustomValueItem::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		// A tap cycles the value (left half decrements, right half increments) once selected, or activates the row
		// when it has no cycler - the touch equivalent of the keyboard Left/Right/Fire handling above
		if (!Selected || ReadOnly || event.type != TouchEventType::Up) {
			return false;
		}
		std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
		if (pointerIndex == -1) {
			return false;
		}
		if (OnChange) {
			float x = event.pointers[pointerIndex].x * viewSize.X;
			root->PlaySfx("MenuSelect"_s, 0.6f);
			OnChange(x < Bounds.X + Bounds.W * 0.5f ? -1 : 1);
			Animation.Restart(0.0f);
			return true;
		}
		if (OnActivate) {
			OnActivate();
			return true;
		}
		return false;
	}

	void CanvasWidget::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
	}

	void CanvasWidget::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		if (OnDrawContent) {
			OnDrawContent(root, canvas, bounds, charOffset, Selected, Animation.Raw());
		}
	}

	void CanvasWidget::Activate(IMenuContainer* root)
	{
		if (OnActivate) {
			OnActivate();
		}
	}

	bool CanvasWidget::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		if (OnTouch && event.type == TouchEventType::Up) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				OnTouch(Vector2i((std::int32_t)(event.pointers[pointerIndex].x * viewSize.X), (std::int32_t)(event.pointers[pointerIndex].y * viewSize.Y)));
				return true;
			}
		}
		return false;
	}

	void CanvasWidget::OnSelected()
	{
		Animation.Restart(0.0f);
		if (OnSelectedChanged) {
			OnSelectedChanged();
		}
	}

	float ListContainer::GetHeight() const
	{
		float total = 0.0f;
		for (const auto& child : _children) {
			if (child->Visible) {
				total += child->GetHeight();
			}
		}
		return total;
	}

	void ListContainer::OnUpdate(float timeMult)
	{
		for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
			_children[i]->Selected = (i == _selectedIndex);
			_children[i]->OnUpdate(timeMult);
		}
	}

	bool ListContainer::OnNavigate(const WidgetInput& input, IMenuContainer* root)
	{
		if (_children.empty()) {
			return false;
		}
		// While the selected child is capturing text input, route everything to it (no selection changes)
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size() && _children[_selectedIndex]->IsCapturingInput()) {
			return _children[_selectedIndex]->OnNavigate(input, root);
		}
		if (input.Up) {
			MoveSelection(-1, root, input.PlayNavSound);
			return true;
		}
		if (input.Down) {
			MoveSelection(1, root, input.PlayNavSound);
			return true;
		}
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size()) {
			Widget* selected = _children[_selectedIndex].get();
			// Let the selected widget handle Left/Right/Fire first (e.g., a ChoiceItem changing its value)
			if (selected->OnNavigate(input, root)) {
				return true;
			}
			if (input.Fire) {
				root->PlaySfx("MenuSelect"_s, 0.6f);
				selected->Activate(root);
				return true;
			}
		}
		return false;
	}

	void ListContainer::MoveSelection(std::int32_t direction, IMenuContainer* root, bool playSound)
	{
		if (_selectedIndex < 0) {
			return;
		}

		std::int32_t count = (std::int32_t)_children.size();
		std::int32_t idx = _selectedIndex;
		for (std::int32_t step = 0; step < count; step++) {
			idx += direction;
			if (idx < 0) {
				idx = count - 1;
			} else if (idx >= count) {
				idx = 0;
			}
			if (_children[idx]->Focusable && _children[idx]->Visible) {
				break;
			}
		}

		if (idx != _selectedIndex && _children[idx]->Focusable) {
			_selectedIndex = idx;
			_children[idx]->OnSelected();
			if (playSound) {
				root->PlaySfx("MenuSelect"_s, 0.5f);
			}
			OnSelectionMoved();
		}
	}

	void ListContainer::SelectAt(std::int32_t index, IMenuContainer* root)
	{
		if (index < 0 || index >= (std::int32_t)_children.size()) {
			return;
		}
		if (index == _selectedIndex) {
			root->PlaySfx("MenuSelect"_s, 0.6f);
			_children[index]->Activate(root);
		} else {
			_selectedIndex = index;
			_children[index]->OnSelected();
			root->PlaySfx("MenuSelect"_s, 0.5f);
			OnSelectionMoved();
		}
	}

	void ListContainer::SetSelectedIndex(std::int32_t index, bool centerInView)
	{
		if (index >= 0 && index < (std::int32_t)_children.size() && _children[index]->Focusable) {
			_selectedIndex = index;
			if (centerInView) {
				OnSelectionSet();
			} else {
				OnSelectionMoved();
			}
		}
	}

	bool ListContainer::OnKeyPressed(const nCine::KeyboardEvent& event, IMenuContainer* root)
	{
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size()) {
			return _children[_selectedIndex]->OnKeyPressed(event, root);
		}
		return false;
	}

	void ListContainer::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size()) {
			_children[_selectedIndex]->OnTextInput(event);
		}
	}

	bool ListContainer::IsCapturingInput() const
	{
		if (_selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size()) {
			return _children[_selectedIndex]->IsCapturingInput();
		}
		return false;
	}

	void StackLayout::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		if (Spread) {
			std::int32_t count = 0;
			for (const auto& child : _children) {
				if (child->Visible) {
					count++;
				}
			}
			if (count > 0) {
				// Distribute item centers across the band, top-weighted (matching the original sliders layout):
				// first center at band*0.35/N, then spaced band*0.9/N apart
				float initial = bounds.H * 0.35f / count;
				float step = bounds.H * 0.9f / count;
				std::int32_t visibleIndex = 0;
				for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
					auto& child = _children[i];
					if (!child->Visible) {
						continue;
					}
					float h = child->GetHeight();
					child->Selected = (i == _selectedIndex);
					float cy = bounds.Y + initial + visibleIndex * step;
					child->Draw(root, canvas, Rectf(bounds.X, cy - h * 0.5f, bounds.W, h), charOffset);
					visibleIndex++;
				}
			}
			return;
		}

		float totalHeight = GetHeight();
		float y = bounds.Y + (bounds.H - totalHeight) * 0.5f;
		for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
			auto& child = _children[i];
			if (!child->Visible) {
				continue;
			}
			float h = child->GetHeight();
			child->Selected = (i == _selectedIndex);
			child->Draw(root, canvas, Rectf(bounds.X, y, bounds.W, h), charOffset);
			y += h;
		}
	}

	bool StackLayout::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x * viewSize.X;
				float y = event.pointers[pointerIndex].y * viewSize.Y;
				for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
					auto& child = _children[i];
					if (child->Focusable && child->Visible && child->Bounds.Contains(x, y)) {
						// Re-tapping the already-selected child lets it self-handle the touch (e.g., a Slider adjusting
						// its value); if it doesn't consume the event, fall back to the standard select/activate
						if (i == _selectedIndex && child->OnTouchEvent(event, viewSize, root)) {
							return true;
						}
						SelectAt(i, root);
						return true;
					}
				}
			}
		}
		return false;
	}

	ScrollView::ScrollView()
		: _scrollY(0.0f), _contentHeight(0.0f), _availableHeight(0.0f), _bandTop(0.0f), _touchStart(Vector2f::Zero),
			_touchLast(Vector2f::Zero), _touchTime(0.0f), _touchSpeed(0.0f), _touchDirection(0), _scrollable(false),
			_ensureVisibleRequested(false)
	{
	}

	void ScrollView::OnUpdate(float timeMult)
	{
		// Kinetic scrolling momentum (only while no finger is down)
		if (_touchSpeed > 0.0f) {
			if (_touchStart == Vector2f::Zero && _scrollable) {
				float y = _scrollY + (_touchSpeed * (std::int32_t)_touchDirection * TouchKineticDivider * timeMult);
				if (y < (_availableHeight - _contentHeight) && _touchDirection < 0) {
					y = (_availableHeight - _contentHeight);
					_touchDirection = 1;
					_touchSpeed *= TouchKineticDamping;
				} else if (y > 0.0f && _touchDirection > 0) {
					y = 0.0f;
					_touchDirection = -1;
					_touchSpeed *= TouchKineticDamping;
				}
				_scrollY = y;
			}

			_touchSpeed = std::max(_touchSpeed - TouchKineticFriction * TouchKineticDivider * timeMult, 0.0f);
		}

		_touchTime += timeMult;

		ListContainer::OnUpdate(timeMult);
	}

	void ScrollView::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		_bandTop = bounds.Y;
		_availableHeight = bounds.H;

		float content = ItemSpacing * 2.0f / 3.0f;
		for (const auto& child : _children) {
			if (child->Visible) {
				content += child->GetHeight();
			}
		}
		content -= ItemSpacing * 0.5f;
		// Reserve extra space before the first and after the last item (for rows with oversized content)
		content += ContentPadding * 2.0f;
		_contentHeight = content;

		if (_availableHeight - _contentHeight < 0.0f) {
			_scrollable = true;
			_scrollY = std::clamp(_scrollY, _availableHeight - _contentHeight, 0.0f);
		} else {
			_scrollable = false;
			_scrollY = (_availableHeight - _contentHeight) * 0.5f;
		}

		// Scroll a programmatically-selected item into view (centered) on the first layout after it was set
		if (_ensureVisibleRequested) {
			_ensureVisibleRequested = false;
			if (_scrollable && _selectedIndex >= 0 && _selectedIndex < (std::int32_t)_children.size()) {
				float before = 0.0f;
				for (std::int32_t i = 0; i < _selectedIndex; i++) {
					if (_children[i]->Visible) {
						before += _children[i]->GetHeight();
					}
				}
				float desired = (_availableHeight * 0.5f) - (ItemSpacing * 0.5f) - ContentPadding - before;
				_scrollY = std::clamp(desired, _availableHeight - _contentHeight, 0.0f);
			}
		}

		float centerX = bounds.X + bounds.W * 0.5f;
		float topLine = bounds.Y;
		float bottomLine = bounds.Y + bounds.H;
		float cy = topLine + ItemSpacing * 0.5f + ContentPadding + _scrollY;

		for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
			auto& child = _children[i];
			if (!child->Visible) {
				continue;
			}
			float h = child->GetHeight();
			child->Selected = (i == _selectedIndex);
			Rectf childRect(bounds.X, cy - h * 0.5f, bounds.W, h);
			if (cy > topLine - ItemSpacing && cy < bottomLine + ItemSpacing) {
				child->Draw(root, canvas, childRect, charOffset);
			} else {
				// Keep the bounds up to date for touch hit-testing even when culled
				child->Bounds = childRect;
			}
			cy += h;
		}

		if (_scrollable) {
			if (_scrollY < -0.5f) {
				root->DrawMenuEdgeGlow(centerX, topLine);
			}
			if (_scrollY > (_availableHeight - _contentHeight) + 0.5f) {
				root->DrawMenuEdgeGlow(centerX, bottomLine);
			}
		}
	}

	bool ScrollView::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		switch (event.type) {
			case TouchEventType::Down: {
				std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
				if (pointerIndex != -1) {
					_touchStart = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
					_touchLast = _touchStart;
					_touchTime = 0.0f;
				}
				return true;
			}
			case TouchEventType::Move: {
				if (_touchStart != Vector2f::Zero) {
					std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
					if (pointerIndex != -1) {
						Vector2f touchMove = Vector2f(event.pointers[pointerIndex].x * viewSize.X, event.pointers[pointerIndex].y * viewSize.Y);
						if (_scrollable) {
							float delta = touchMove.Y - _touchLast.Y;
							if (delta != 0.0f) {
								_scrollY += delta;
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
				return true;
			}
			case TouchEventType::Up: {
				bool alreadyMoved = (_touchStart == Vector2f::Zero || (_touchStart - _touchLast).SqrLength() > 100.0f || _touchTime > 60.0f);
				_touchStart = Vector2f::Zero;
				if (alreadyMoved) {
					return true;
				}
				for (std::int32_t i = 0; i < (std::int32_t)_children.size(); i++) {
					auto& child = _children[i];
					if (child->Focusable && child->Visible && child->Bounds.Contains(_touchLast.X, _touchLast.Y)) {
						// Let the row self-handle the tap (e.g., a bespoke row picking a column); otherwise select/activate it
						if (!child->OnTouchEvent(event, viewSize, root)) {
							SelectAt(i, root);
						}
						break;
					}
				}
				return true;
			}
		}
		return false;
	}

	void ScrollView::OnSelectionMoved()
	{
		EnsureVisibleSelected();
	}

	void ScrollView::OnSelectionSet()
	{
		_ensureVisibleRequested = true;
	}

	void ScrollView::EnsureVisibleSelected()
	{
		if (!_scrollable || _selectedIndex < 0 || _selectedIndex >= (std::int32_t)_children.size()) {
			return;
		}

		// Scroll by the item's full extent (top/bottom), not just its center, so taller multi-line items
		// (e.g., a ChoiceItem with its value on a second line) are brought entirely into view. A little extra
		// padding past the item is kept so the viewport scrolls slightly more than strictly needed, leaving the
		// selection a bit inside the edge (revealing part of the adjacent item) instead of flush against it. The
		// scroll offset is clamped in Draw, so this never over-scrolls past the first/last item.
		constexpr float EdgePadding = ItemSpacing * 0.5f;
		auto& selected = _children[_selectedIndex];
		float itemTop = selected->Bounds.Y;
		float itemBottom = selected->Bounds.Y + selected->Bounds.H;
		float topLine = _bandTop;
		float bottomLine = _bandTop + _availableHeight;

		if (itemTop - EdgePadding < topLine) {
			_scrollY += (topLine - (itemTop - EdgePadding));
		} else if (itemBottom + EdgePadding > bottomLine) {
			_scrollY += (bottomLine - (itemBottom + EdgePadding));
		}
	}

	void Slider::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
		// The hold-to-accelerate cooldown advances every frame (gated against in OnNavigate)
		if (_pressedCooldown < 1.0f) {
			_pressedCooldown = std::min(_pressedCooldown + timeMult * 0.008f, 1.0f);
		}
	}

	void Slider::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		float centerX = bounds.X + bounds.W * 0.5f;
		float centerY = bounds.Y + bounds.H * 0.5f;

		root->DrawMenuListItem(charOffset, Label, centerX, centerY, Selected, Animation.Raw());

		if (Selected) {
			root->DrawMenuArrows(charOffset, centerX, centerY + 22.0f, Animation.Raw());
		}

		float value = (Value ? Value() : 0.0f);
		std::int32_t currentBlockCount = (std::int32_t)std::round(value * BlockCount);
		currentBlockCount = std::clamp(currentBlockCount, 0, (std::int32_t)BlockCount);

		// Dim background bar (all blocks) then the bright filled portion on top
		char stringBuffer[BlockCount + 1];
		for (std::int32_t j = 0; j < BlockCount; j++) {
			stringBuffer[j] = '|';
		}
		stringBuffer[BlockCount] = '\0';
		root->DrawStringShadow(stringBuffer, charOffset, centerX - 66.0f, centerY + 24.0f, IMenuContainer::FontShadowLayer + 2,
			Alignment::Left, Colorf(0.38f, 0.37f, 0.34f, 0.34f), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

		for (std::int32_t j = 0; j < currentBlockCount; j++) {
			stringBuffer[j] = '|';
		}
		stringBuffer[currentBlockCount] = '\0';
		root->DrawStringShadow(stringBuffer, charOffset, centerX - 66.0f, centerY + 24.0f, IMenuContainer::FontLayer - 10,
			Alignment::Left, (Selected ? Font::RandomColor : Font::DefaultColor), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
	}

	bool Slider::OnNavigate(const WidgetInput& input, IMenuContainer* root)
	{
		if (input.LeftHeld || input.RightHeld) {
			// Adjust immediately on the initial press, then again whenever the (accelerating) cooldown elapses
			if (_pressedCooldown >= 1.0f - (_pressedCount * 0.096f) || input.Left || input.Right) {
				Apply(root, input.LeftHeld ? -StepSize : StepSize);
				_pressedCooldown = 0.0f;
				_pressedCount = std::min(_pressedCount + 6, 10);
			}
			return true;
		}
		_pressedCount = 0;
		return false;
	}

	bool Slider::OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize, IMenuContainer* root)
	{
		if (event.type == TouchEventType::Down) {
			std::int32_t pointerIndex = event.findPointerIndex(event.actionIndex);
			if (pointerIndex != -1) {
				float x = event.pointers[pointerIndex].x;
				// Only the central bar region reacts; tapping its left/right half nudges the value down/up
				if (std::abs(x - 0.5f) < 0.22f) {
					Apply(root, x < 0.5f ? -StepSize : StepSize);
					return true;
				}
			}
		}
		return false;
	}

	void Slider::Apply(IMenuContainer* root, float delta)
	{
		if (OnAdjust) {
			OnAdjust(delta);
		}
		root->PlaySfx("MenuSelect"_s, 0.6f);
	}

	void TextInput::OnUpdate(float timeMult)
	{
		if (Selected) {
			Animation.Update(timeMult);
		}
		if (_active) {
			_buffer.Update(timeMult);
		}
	}

	void TextInput::Draw(IMenuContainer* root, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset)
	{
		Bounds = bounds;
		float centerX = bounds.X + bounds.W * 0.5f;
		float y = bounds.Y + bounds.H * 0.5f;

		root->DrawMenuListItem(charOffset, Label, centerX, y, Selected, Animation.Raw(), ReadOnly);

		String displayText = (_active ? String(_buffer.GetText()) : (Value ? Value() : String{}));
		bool showIcon = (ShowIcon && ShowIcon());

		Vector2f textSize = root->MeasureString(displayText, 0.8f);

		Colorf valueColor = (Selected && _active
			? Colorf(0.62f, 0.44f, 0.34f, 0.5f)
			: (Selected ? Colorf(0.46f, 0.46f, 0.46f, ReadOnly ? 0.36f : 0.5f) : (ReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)));
		root->DrawStringShadow(displayText, charOffset, centerX + (showIcon ? 12.0f : 0.0f), y + 22.0f, IMenuContainer::FontLayer - 10,
			Alignment::Center, valueColor, 0.8f);

		if (showIcon && !Icon.empty()) {
			root->DrawStringShadow(Icon, charOffset, centerX - textSize.X * 0.5f - 5.0f, y + 22.0f, IMenuContainer::FontLayer - 10,
				Alignment::Center, (Selected ? Colorf(0.46f, 0.46f, 0.46f, ReadOnly ? 0.36f : 0.5f) : (ReadOnly ? Font::TransparentDefaultColor : Font::DefaultColor)));
		}

		if (Selected && _active) {
			Vector2f textToCursorSize = root->MeasureString(displayText.prefix(_buffer.GetCursor()), 0.8f);
			root->DrawSolid(centerX - textSize.X * 0.5f + textToCursorSize.X + 1.0f, y + 22.0f - 1.0f, IMenuContainer::FontLayer + 10, Alignment::Center, Vector2f(1.0f, 12.0f),
				Colorf(1.0f, 1.0f, 1.0f, std::clamp(sinf(_buffer.GetCaretAnim() * 0.1f) * 1.4f, 0.0f, 0.8f)), true);
		}
	}

	void TextInput::Activate(IMenuContainer* root)
	{
		// The selection sound is played by the container before Activate is called
		if (_active || ReadOnly) {
			return;
		}
		if (CanEdit && !CanEdit()) {
			return;
		}
		String initial = (Value ? Value() : String{});
		_buffer.Activate(initial);
		_active = true;
		if (OnEditStateChanged) {
			OnEditStateChanged(true);
		}
	}

	bool TextInput::OnNavigate(const WidgetInput& input, IMenuContainer* root)
	{
		if (_active) {
			if (input.Fire) {
				Commit(root);
			}
			return true;
		}
		return false;
	}

	bool TextInput::OnKeyPressed(const nCine::KeyboardEvent& event, IMenuContainer* root)
	{
		if (!_active) {
			return false;
		}
		switch (event.sym) {
			case Keys::Escape:
				Cancel(root);
				return true;
			case Keys::Return:
				Commit(root);
				return true;
			default:
				_buffer.OnKeyPressed(event);
				return true;
		}
	}

	void TextInput::OnTextInput(const nCine::TextInputEvent& event)
	{
		if (_active) {
			_buffer.OnTextInput(event);
		}
	}

	void TextInput::Commit(IMenuContainer* root)
	{
		if (!_active || _buffer.GetText().empty()) {
			return;
		}
		root->PlaySfx("MenuSelect"_s, 0.5f);
		if (OnCommit) {
			OnCommit(_buffer.GetText());
		}
		_active = false;
		_buffer.Deactivate();
		if (OnEditStateChanged) {
			OnEditStateChanged(false);
		}
	}

	void TextInput::Cancel(IMenuContainer* root)
	{
		if (!_active) {
			return;
		}
		root->PlaySfx("MenuSelect"_s, 0.5f);
		_active = false;
		_buffer.Deactivate();
		if (OnEditStateChanged) {
			OnEditStateChanged(false);
		}
	}
}
