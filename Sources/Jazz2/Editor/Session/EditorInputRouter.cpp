#include "EditorInputRouter.h"

#if defined(WITH_EDITOR)

#include "../Common/LayerView.h"
#include "../Document/LevelDocument.h"
#include "../Input/ActionIds.h"
#include "../Input/ActionRegistry.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../Runtime/PlayTestController.h"
#include "../Shell/ShellCommon.h"
#include "../Tools/BlockGrab.h"
#include "../Tools/EditorTool.h"
#include "../UI/UiRadialMenu.h"
#include "../UI/UiSurface.h"
#include "../UI/UiWidgets.h"
#include "../../LevelHandler.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/IGfxDevice.h"
#include "../../../nCine/Input/IInputManager.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// Part of the view the arrow keys move it by
		constexpr float KeyboardPanFraction = 0.25f;
		// Part of the view a step of the wheel moves it by
		constexpr float WheelPanFraction = 0.12f;
		// Units a frame the cursor of a gamepad moves by with the stick pushed all the way, and the view, then faster
		// the longer the stick is held, up to the boost
		constexpr float GamepadCursorSpeed = 8.0f;
		constexpr float GamepadPanSpeed = 12.0f;
		constexpr float GamepadMaxBoost = 2.0f;
		// Steps of the wheel a frame the right stick scrolls a list by, pushed all the way
		constexpr float GamepadScrollSpeed = 0.15f;
		constexpr float GamepadBoostFrames = 60.0f;
		// How much the zoom changes a frame with a trigger pulled all the way, as a logarithm, about 3× a second
		constexpr float GamepadZoomRate = 0.02f;
	}

	EditorInputRouter::EditorInputRouter(IEditorContext& context, IInputRouterHost& host, PlayTestController& playTest)
		: _context(context), _host(host), _playTest(playTest)
	{
		SetupTouchGestures();
	}

	void EditorInputRouter::Update(float timeMult)
	{
		// A key released while the window didn't have the focus isn't told, it's held no more once it's up
		if (!_heldKeys.empty()) {
			const KeyboardState& keyboard = theApplication().GetInputManager().keyboardState();
			for (std::size_t i = _heldKeys.size(); i > 0; i--) {
				Keys key = _heldKeys[i - 1];
				if (!keyboard.isKeyDown(key)) {
					_heldKeys.erase(_heldKeys.begin() + (i - 1));
					ReleaseHeldActions(key);
				}
			}
		}
		UpdateGamepad(timeMult);
		if (_playTest.IsEditing()) {
			_touch.Update(timeMult);
		}

		// The keyboard of the system covers a part of the screen, it goes away once nothing is typed into, so it's asked
		// for again by the next box tapped
		if (_screenKeyboardShown) {
			UiSurface* surface = _context.GetSurface();
			UiElement* focus = (surface != nullptr ? surface->GetFocus() : nullptr);
			if (focus == nullptr || runtime_cast<UiTextBox>(focus) == nullptr) {
				_screenKeyboardShown = false;
				theApplication().HideScreenKeyboard();
			}
		}
	}

	bool EditorInputRouter::ShowScreenKeyboard(UiTextBox& field)
	{
		if (!field.ShowScreenKeyboard()) {
			return false;
		}
		// The keyboard of the editor closes by itself, only one of the system is hidden again, see Update()
		if (theApplication().CanShowScreenKeyboard()) {
			_screenKeyboardShown = true;
		}
		return true;
	}

	void EditorInputRouter::OnKeyPressed(const KeyboardEvent& event)
	{
		EditorLevelHandler* editHandler = _context.GetEditHandler();
		if (editHandler == nullptr) {
			return;
		}

		ActionRegistry& actions = _context.GetActions();
		_modifiers = event.mod;
		SetInputModality(InputModality::KeyboardMouse);
		// A key held is pressed again and again as it repeats, the side buttons of a mouse aren't, nor are they released
		bool repeat = false;
		if (event.sym < Keys::Count) {
			repeat = (std::find(_heldKeys.begin(), _heldKeys.end(), event.sym) != _heldKeys.end());
			if (!repeat) {
				_heldKeys.push_back(event.sym);
			}
		}
		if (_playTest.IsInGame()) {
			// The actions of playing have keys the game doesn't use, going back and the debug toggles, everything else
			// is the game's. No action has F11 by default, the application switches fullscreen with it before the editor
			// gets the key.
			_handlingShortcut = true;
			bool handled = actions.HandleKey(event, ActionScope::Play);
			_handlingShortcut = false;
			// The keys of the side buttons of a mouse are only the editor's, the game knows no key past the last one
			if (!handled && event.sym < Keys::Count && _playTest.GetPlayTest() != nullptr) {
				_playTest.GetPlayTest()->GetLevelHandler().OnKeyPressed(event);
			}
			return;
		}
		if (_playTest.IsEntering()) {
			// The key that goes back from the game cancels going into it
			EditorAction* stop = actions.Find(ActionIds::PlayStop);
			if (event.sym == Keys::Escape || (stop != nullptr && (stop->Key.Matches(event) || stop->AlternateKey.Matches(event)))) {
				_host.StopPlayTest(false);
			}
			return;
		}
		if (_context.GetSurface()->OnKeyPressed(event)) {
			return;
		}

		ToolManager& tools = _context.GetTools();
		bool noModifiers = (event.mod & (KeyMod::Ctrl | KeyMod::Alt | KeyMod::Shift)) == 0;
		switch (event.sym) {
			case Keys::Space:
				tools.BeginTemporary(EditorToolType::Hand);
				return;
			case Keys::LAlt:
				// The event tool picks events with Alt itself
				if (tools.GetChosenType() != EditorToolType::Event) {
					tools.BeginTemporary(EditorToolType::Eyedropper);
				}
				return;
			case Keys::Escape:
				if (_context.GetBlockGrab().IsActive()) {
					// The corner of a block marked is forgotten first, nothing else is
					_context.GetBlockGrab().Cancel();
				} else if (_context.IsPickingEvent()) {
					// The event list is back to choosing the event to place
					_context.CancelEventPick();
				} else if (tools.GetActive()->IsBusy()) {
					CancelToolStroke();
				} else {
					_host.ClearSelection();
				}
				return;
			default:
				break;
		}
		// A key the tool uses while it's dragged, as X and Y snapping an event off the grid, runs no action
		_toolCallbackDepth++;
		bool takenByTool = tools.GetActive()->OnKeyPressed(event);
		_toolCallbackDepth--;
		if (takenByTool) {
			return;
		}

		if (noModifiers) {
			EditorCamera& camera = editHandler->GetCamera();
			Rectf view = _host.GetViewRect();
			switch (event.sym) {
				case Keys::Left: camera.PanBy(Vector2f(-view.W * KeyboardPanFraction, 0.0f)); return;
				case Keys::Right: camera.PanBy(Vector2f(view.W * KeyboardPanFraction, 0.0f)); return;
				case Keys::Up: camera.PanBy(Vector2f(0.0f, -view.H * KeyboardPanFraction)); return;
				case Keys::Down: camera.PanBy(Vector2f(0.0f, view.H * KeyboardPanFraction)); return;
				default: break;
			}
		}

		_handlingShortcut = true;
		EditorAction* ran = nullptr;
		bool handled = actions.HandleKey(event, ActionScope::Edit, repeat, &ran);
		_handlingShortcut = false;
		if (ran != nullptr && ran->Release && event.sym < Keys::Count) {
			_heldActions.push_back(HeldAction{ event.sym, ran });
		}

		// The number keys choose a layer by their actions, with Shift they hide it, unless an action was given the key
		if (!handled && (event.mod & KeyMod::Shift) != 0 && (event.mod & (KeyMod::Ctrl | KeyMod::Alt)) == 0 &&
			event.sym >= Keys::D1 && event.sym <= Keys::D9) {
			_context.ToggleLayerFlag(std::int32_t(event.sym) - std::int32_t(Keys::D1), DocumentLayer::EditorHidden);
		}
	}

	void EditorInputRouter::OnKeyReleased(const KeyboardEvent& event)
	{
		_modifiers = event.mod;
		_heldKeys.erase(std::remove(_heldKeys.begin(), _heldKeys.end(), event.sym), _heldKeys.end());
		ReleaseHeldActions(event.sym);
		if (_playTest.IsInGame()) {
			// The game knows no key past the last one, as those of the side buttons of a mouse
			if (event.sym < Keys::Count && _playTest.GetPlayTest() != nullptr) {
				_playTest.GetPlayTest()->GetLevelHandler().OnKeyReleased(event);
			}
			return;
		}
		ToolManager& tools = _context.GetTools();
		_toolCallbackDepth++;
		tools.GetActive()->OnKeyReleased(event);
		_toolCallbackDepth--;
		if (event.sym == Keys::Space) {
			tools.EndTemporary(EditorToolType::Hand);
		} else if (event.sym == Keys::LAlt) {
			tools.EndTemporary(EditorToolType::Eyedropper);
		}
	}

	void EditorInputRouter::ReleaseHeldActions(Keys key)
	{
		// Taken out first, an action ended may run another one
		SmallVector<EditorAction*, 2> ended;
		for (std::size_t i = _heldActions.size(); i > 0; i--) {
			if (_heldActions[i - 1].Key == key) {
				ended.push_back(_heldActions[i - 1].Action);
				_heldActions.erase(_heldActions.begin() + (i - 1));
			}
		}
		for (EditorAction* action : ended) {
			action->Release();
		}
	}

	void EditorInputRouter::OnTextInput(const TextInputEvent& event)
	{
		if (_playTest.IsInGame()) {
			if (_playTest.GetPlayTest() != nullptr) {
				_playTest.GetPlayTest()->GetLevelHandler().OnTextInput(event);
			}
			return;
		}
		if (UiSurface* surface = _context.GetSurface()) {
			surface->OnTextInput(event);
		}
	}

	void EditorInputRouter::OnTouchEvent(const TouchEvent& event)
	{
		if (_playTest.IsInGame() && _playTest.GetPlayTest() != nullptr) {
			OnPlayTouchEvent(event);
			return;
		}
		UiSurface* surface = _context.GetSurface();
		if (_barTouchId >= 0) {
			// The level isn't played anymore while a finger still holds the bar, as when a key went back to the editor
			_barTouchId = -1;
			if (surface != nullptr) {
				surface->CancelPointerCapture();
			}
		}
		if (_context.GetEditHandler() == nullptr || surface == nullptr || !_playTest.IsEditing()) {
			return;
		}
		SetInputModality(InputModality::Touch);
		_touch.OnTouchEvent(event, surface->GetScreenSize(), surface->GetScale());
	}

	void EditorInputRouter::OnPlayTouchEvent(const TouchEvent& event)
	{
		LevelHandler& game = _playTest.GetPlayTest()->GetLevelHandler();
		UiSurface* surface = _context.GetSurface();
		if (surface == nullptr) {
			game.OnTouchEvent(event);
			return;
		}

		Vector2f screen = surface->GetScreenSize();
		auto positionOf = [screen](const TouchEvent::Pointer& pointer) {
			return Vector2f(pointer.x * screen.X, pointer.y * screen.Y);
		};
		const TouchEvent::Pointer* action = (event.actionIndex >= 0 && event.actionIndex < std::int32_t(event.count) ? &event.pointers[event.actionIndex] : nullptr);
		bool pressed = (event.type == TouchEventType::Down || event.type == TouchEventType::PointerDown);
		bool released = (event.type == TouchEventType::Up || event.type == TouchEventType::PointerUp);

		// A finger put on the bar over the level, or on a menu opened from it, is the pointer of the editor, the game
		// doesn't see it at all
		if (pressed && action != nullptr && _barTouchId < 0 && surface->IsOverInterface(positionOf(*action))) {
			_barTouchId = action->id;
			SetInputModality(InputModality::Touch);
			Vector2f pos = positionOf(*action);
			PointerMove(pos);
			PointerDown(pos, MouseButton::Left);
			return;
		}
		if (_barTouchId >= 0) {
			if (released && action != nullptr && action->id == _barTouchId) {
				// What it pressed is clicked now, which may end the play-test
				_barTouchId = -1;
				PointerUp(positionOf(*action), MouseButton::Left);
				return;
			}
			std::int32_t index = event.findPointerIndex(_barTouchId);
			if (index < 0) {
				// Gone without being lifted, what it held isn't clicked
				_barTouchId = -1;
				surface->CancelPointerCapture();
			} else if (event.type == TouchEventType::Move) {
				PointerMove(positionOf(event.pointers[index]));
			}
		}

		if (_barTouchId < 0) {
			if (pressed) {
				// A finger in the game closes a menu opened from the bar, as a click elsewhere does
				surface->ClosePopups();
			}
			game.OnTouchEvent(event);
			return;
		}

		// The other fingers go to the game as if the one on the bar wasn't there, the first of them comes and the last of
		// them goes as the only one
		TouchEvent gameEvent = event;
		gameEvent.count = 0;
		gameEvent.actionIndex = -1;
		for (std::uint32_t i = 0; i < event.count && i < TouchEvent::MaxPointers; i++) {
			if (event.pointers[i].id == _barTouchId) {
				continue;
			}
			if (std::int32_t(i) == event.actionIndex) {
				gameEvent.actionIndex = std::int32_t(gameEvent.count);
			}
			gameEvent.pointers[gameEvent.count++] = event.pointers[i];
		}
		if (gameEvent.count == 0) {
			return;
		}
		if (gameEvent.type == TouchEventType::PointerDown && gameEvent.count == 1) {
			gameEvent.type = TouchEventType::Down;
		} else if (gameEvent.type == TouchEventType::PointerUp && gameEvent.count == 1) {
			gameEvent.type = TouchEventType::Up;
		}
		game.OnTouchEvent(gameEvent);
	}

	void EditorInputRouter::SetupTouchGestures()
	{
		_touch.IsOverInterface = [this](Vector2f pos) {
			return _context.GetSurface()->TakesTouchAt(pos);
		};
		_touch.PointerDown = [this](Vector2f pos) {
			PointerMove(pos);
			PointerDown(pos, MouseButton::Left);
		};
		_touch.PointerMove = [this](Vector2f pos) {
			PointerMove(pos);
		};
		_touch.PointerUp = [this](Vector2f pos) {
			PointerUp(pos, MouseButton::Left);
			// A box tapped to type into needs a keyboard, of the system or of the editor, a number too once it's typed
			// into (it takes the focus only by a tap without dragging), a tap into a box being typed into keeps its caret
			UiSurface* surface = _context.GetSurface();
			if (UiTextBox* field = (surface->GetFocus() != nullptr ? runtime_cast<UiTextBox>(surface->GetFocus()) : nullptr)) {
				ShowScreenKeyboard(*field);
			}
		};
		_touch.PointerCancel = [this]() {
			// Two fingers move the view, what one of them started isn't done, a layer handle dragged included
			CancelInteractions(true);
		};
		_touch.PanZoom = [this](Vector2f center, Vector2f pan, float zoom) {
			EditorCamera& camera = _context.GetEditHandler()->GetCamera();
			// The level follows the fingers
			camera.PanBy(Vector2f(-pan.X, -pan.Y));
			if (zoom != 1.0f) {
				camera.ZoomBy(center, zoom);
			}
		};
		_touch.Tap = [this](std::int32_t fingers) {
			if (fingers == 2) {
				_context.Undo();
			} else if (fingers >= 3) {
				_context.Redo();
			}
		};
		_touch.LongPress = [this](Vector2f pos) {
			ShowToolMenu(pos);
		};
	}

	void EditorInputRouter::ShowToolMenu(Vector2f pos)
	{
		UiSurface* surface = _context.GetSurface();
		if (surface == nullptr || _radialMenu != nullptr) {
			return;
		}
		ActionRegistry& actions = _context.GetActions();
		SmallVector<UiMenuEntry, 0> entries;
		for (const char* id : { ActionIds::ToolBrush, ActionIds::ToolEraser, ActionIds::ToolRectangle, ActionIds::ToolFill, ActionIds::ToolSmart, ActionIds::ToolEyedropper,
			ActionIds::ToolSelect, ActionIds::ToolEvent, ActionIds::ToolHand, ActionIds::EditUndo, ActionIds::EditRedo }) {
			if (EditorAction* action = actions.Find(id)) {
				entries.push_back(MakeMenuEntry(*action));
			}
		}
		_radialMenu = UiRadialMenu::Show(*surface, pos, Death::move(entries), [this]() { _radialMenu = nullptr; });
	}

	void EditorInputRouter::OnMouseDown(const MouseEvent& event)
	{
		if (_context.GetEditHandler() == nullptr) {
			return;
		}
		// The side buttons of a mouse have no use of their own, they're pressed as the keys that stand for them, so
		// actions can be given them
		Keys key = Shortcut::GetMouseButtonKey(event.button);
		if (key != Keys::Unknown) {
			KeyboardEvent keyEvent;
			keyEvent.sym = key;
			keyEvent.mod = _modifiers;
			OnKeyPressed(keyEvent);
			return;
		}
		SetInputModality(InputModality::KeyboardMouse);
		PointerDown(ToDrawable(event.x, event.y), event.button);
	}

	void EditorInputRouter::SetInputModality(InputModality modality)
	{
		if (_inputModality == modality) {
			return;
		}
		_inputModality = modality;
		if (UiSurface* surface = _context.GetSurface()) {
			// Tooltips by holding a finger still and lists scrolled by dragging them
			surface->TouchInput = (modality == InputModality::Touch);
		}
		if (modality == InputModality::Gamepad) {
			// The cursor starts where the mouse was, or in the middle of the view
			Rectf view = _host.GetViewRect();
			_gamepadCursor = (_pointerKnown && view.Contains(_pointerPos) ? _pointerPos : Vector2f(view.X + view.W * 0.5f, view.Y + view.H * 0.5f));
			PointerMove(_gamepadCursor);
		}
	}

	void EditorInputRouter::UpdateGamepad(float timeMult)
	{
		IInputManager& input = theApplication().GetInputManager();
		const JoyMappedState* state = nullptr;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks; i++) {
			if (input.isJoyMapped(i)) {
				state = &input.joyMappedState(i);
				break;
			}
		}
		if (state == nullptr) {
			_gamepadButtons = 0;
			return;
		}

		std::uint32_t buttons = 0;
		for (std::int32_t i = 0; i < std::int32_t(ButtonName::Count); i++) {
			if (state->isButtonPressed(ButtonName(i))) {
				buttons |= (1u << i);
			}
		}
		std::uint32_t pressed = (buttons & ~_gamepadButtons);
		std::uint32_t released = (_gamepadButtons & ~buttons);
		_gamepadButtons = buttons;
		auto wasPressed = [pressed](ButtonName name) { return (pressed & (1u << std::int32_t(name))) != 0; };
		auto wasReleased = [released](ButtonName name) { return (released & (1u << std::int32_t(name))) != 0; };
		auto isHeld = [buttons](ButtonName name) { return (buttons & (1u << std::int32_t(name))) != 0; };

		Vector2f leftStick(state->axisValue(AxisName::LeftX), state->axisValue(AxisName::LeftY));
		Vector2f rightStick(state->axisValue(AxisName::RightX), state->axisValue(AxisName::RightY));
		input.deadZoneNormalize(leftStick, IInputManager::LeftStickDeadZone);
		input.deadZoneNormalize(rightStick, IInputManager::RightStickDeadZone);
		float zoom = state->axisValue(AxisName::RightTrigger) - state->axisValue(AxisName::LeftTrigger);
		if (std::abs(zoom) < 0.1f) {
			zoom = 0.0f;
		}

		if (pressed != 0 || leftStick.Length() > 0.0f || rightStick.Length() > 0.0f || zoom != 0.0f) {
			SetInputModality(InputModality::Gamepad);
		}
		if (_inputModality != InputModality::Gamepad || !_playTest.IsEditing()) {
			return;
		}

		UiSurface& surface = *_context.GetSurface();
		ToolManager& tools = _context.GetTools();
		float scale = surface.GetScale();
		EditorCamera& camera = _context.GetEditHandler()->GetCamera();
		Vector2f cursor = _gamepadCursor;

		// The left stick moves the cursor, faster the farther it's pushed, and faster the longer it's held, so it's
		// precise for a short push and quick across the screen
		// While the ring of tools is shown, the stick picks from it
		float push = (_radialMenu == nullptr ? leftStick.Length() : 0.0f);
		if (push > 0.0f) {
			_gamepadCursorHold = std::min(_gamepadCursorHold + timeMult, GamepadBoostFrames);
			float boost = 1.0f + (GamepadMaxBoost - 1.0f) * (_gamepadCursorHold / GamepadBoostFrames);
			cursor += leftStick * (GamepadCursorSpeed * scale * push * boost * timeMult);
		} else {
			_gamepadCursorHold = 0.0f;
		}

		// A box for a value focused or under the cursor takes the pad and Y: the pad changes a number, Y opens the
		// screen keyboard of the system to type into it
		UiElement* fieldElement = (surface.GetFocus() != nullptr ? surface.GetFocus() : surface.GetHover());
		UiTextBox* field = (fieldElement != nullptr ? runtime_cast<UiTextBox>(fieldElement) : nullptr);
		UiNumberBox* numberField = (field != nullptr ? runtime_cast<UiNumberBox>(field) : nullptr);
		bool fieldTakesY = false;
		if (field != nullptr && field->IsUsable() && wasPressed(ButtonName::Y)) {
			fieldTakesY = ShowScreenKeyboard(*field);
		}
		bool fieldTakesPad = (numberField != nullptr && numberField->IsUsable());
		if (fieldTakesPad) {
			std::int32_t change = (std::int32_t(isHeld(ButtonName::Up)) + std::int32_t(isHeld(ButtonName::Right))) -
				(std::int32_t(isHeld(ButtonName::Down)) + std::int32_t(isHeld(ButtonName::Left)));
			if (change != 0) {
				bool apply = false;
				if (wasPressed(ButtonName::Up) || wasPressed(ButtonName::Down) || wasPressed(ButtonName::Left) || wasPressed(ButtonName::Right)) {
					_gamepadRepeat = 18.0f;
					apply = true;
				} else {
					_gamepadRepeat -= timeMult;
					if (_gamepadRepeat <= 0.0f) {
						_gamepadRepeat = 3.0f;
						apply = true;
					}
				}
				if (apply) {
					numberField->StepBy(float(change));
				}
			}
		}

		// The pad moves it by a tile of the layer being edited, repeated while held
		Vector2i step(std::int32_t(isHeld(ButtonName::Right)) - std::int32_t(isHeld(ButtonName::Left)),
			std::int32_t(isHeld(ButtonName::Down)) - std::int32_t(isHeld(ButtonName::Up)));
		if (!fieldTakesPad && (step.X != 0 || step.Y != 0)) {
			bool move = false;
			if (wasPressed(ButtonName::Up) || wasPressed(ButtonName::Down) || wasPressed(ButtonName::Left) || wasPressed(ButtonName::Right)) {
				_gamepadRepeat = 18.0f;
				move = true;
			} else {
				_gamepadRepeat -= timeMult;
				if (_gamepadRepeat <= 0.0f) {
					_gamepadRepeat = 5.0f;
					move = true;
				}
			}
			if (move) {
				LayerView view = _context.GetLayerView(_context.GetCurrentLayer());
				Rectf cell = view.CellToScreen(view.WorldToCell(view.ScreenToWorld(cursor)) + step);
				cursor = Vector2f(cell.X + cell.W * 0.5f, cell.Y + cell.H * 0.5f);
				// The view follows a cursor that left it
				Rectf viewRect = _host.GetViewRect();
				Vector2f follow;
				if (cursor.X < viewRect.X) {
					follow.X = cursor.X - viewRect.X - cell.W;
				} else if (cursor.X > viewRect.X + viewRect.W) {
					follow.X = cursor.X - (viewRect.X + viewRect.W) + cell.W;
				}
				if (cursor.Y < viewRect.Y) {
					follow.Y = cursor.Y - viewRect.Y - cell.H;
				} else if (cursor.Y > viewRect.Y + viewRect.H) {
					follow.Y = cursor.Y - (viewRect.Y + viewRect.H) + cell.H;
				}
				if (follow.X != 0.0f || follow.Y != 0.0f) {
					camera.PanBy(follow);
					cursor -= follow;
				}
			}
		}

		Vector2f screen = surface.GetScreenSize();
		cursor.X = std::clamp(cursor.X, 0.0f, screen.X - 1.0f);
		cursor.Y = std::clamp(cursor.Y, 0.0f, screen.Y - 1.0f);
		bool cursorMoved = (cursor.X != _gamepadCursor.X || cursor.Y != _gamepadCursor.Y);
		_gamepadCursor = cursor;

		// The right stick moves the view, the triggers zoom at the cursor
		// Over a list that scrolls, the right stick scrolls it, it moves the view otherwise
		bool scrolled = false;
		if (std::abs(rightStick.Y) > 0.0f) {
			UiScrollView* scrollView = nullptr;
			for (UiElement* element = surface.GetHover(); element != nullptr && scrollView == nullptr; element = element->GetParent()) {
				scrollView = runtime_cast<UiScrollView>(element);
			}
			if (scrollView != nullptr) {
				UiPointerEvent wheel;
				wheel.Pos = _gamepadCursor;
				wheel.Wheel = Vector2f(0.0f, -rightStick.Y * std::abs(rightStick.Y) * GamepadScrollSpeed * timeMult);
				scrolled = scrollView->OnWheel(wheel);
			}
		}
		if (rightStick.Length() > 0.0f && !scrolled) {
			_gamepadPanHold = std::min(_gamepadPanHold + timeMult, GamepadBoostFrames);
			float boost = 1.0f + (GamepadMaxBoost - 1.0f) * (_gamepadPanHold / GamepadBoostFrames);
			camera.PanBy(rightStick * (GamepadPanSpeed * scale * rightStick.Length() * boost * timeMult));
			cursorMoved = true;
		} else {
			_gamepadPanHold = 0.0f;
		}
		if (zoom != 0.0f) {
			// Smoothly, by a little every frame, the steps of the wheel would make it jump from one to another
			camera.ZoomBy(_gamepadCursor, std::exp(zoom * std::abs(zoom) * GamepadZoomRate * timeMult));
		}
		if (cursorMoved) {
			PointerMove(_gamepadCursor);
		}

		// A and B are the buttons of a mouse, B closes what is shown over the interface first
		if (wasPressed(ButtonName::A)) {
			PointerDown(_gamepadCursor, MouseButton::Left);
		}
		if (wasReleased(ButtonName::A)) {
			PointerUp(_gamepadCursor, MouseButton::Left);
		}
		if (wasPressed(ButtonName::B)) {
			if (surface.HasOverlays()) {
				KeyboardEvent escape;
				escape.sym = Keys::Escape;
				escape.mod = 0;
				surface.OnKeyPressed(escape);
			} else {
				PointerDown(_gamepadCursor, MouseButton::Right);
			}
		}
		if (wasReleased(ButtonName::B)) {
			PointerUp(_gamepadCursor, MouseButton::Right);
		}

		// A dialog waiting for an answer gets only the cursor and A and B, as the keyboard goes only to it, the level
		// isn't changed, played or picked from under it
		if (surface.HasModal()) {
			if (_gamepadPicking && wasReleased(ButtonName::X)) {
				_gamepadPicking = false;
				PointerUp(_gamepadCursor, MouseButton::Left);
			}
			_gamepadYTime = -1.0f;
			return;
		}

		// X picks what is under the cursor, an event with the event tool and a tile otherwise
		if (wasPressed(ButtonName::X)) {
			if (tools.GetChosenType() == EditorToolType::Event) {
				std::int32_t modifiers = _modifiers;
				_modifiers |= KeyMod::Alt;
				PointerDown(_gamepadCursor, MouseButton::Left);
				PointerUp(_gamepadCursor, MouseButton::Left);
				_modifiers = modifiers;
			} else {
				tools.BeginTemporary(EditorToolType::Eyedropper);
				PointerDown(_gamepadCursor, MouseButton::Left);
				_gamepadPicking = true;
			}
		}
		if (wasReleased(ButtonName::X) && _gamepadPicking) {
			_gamepadPicking = false;
			PointerUp(_gamepadCursor, MouseButton::Left);
		}

		// Y goes through the tools when tapped, held it shows them in a ring around the cursor, the left stick picks one
		constexpr float RadialHoldFrames = 14.0f;
		if (wasPressed(ButtonName::Y) && !fieldTakesY) {
			_gamepadYTime = 0.0f;
		} else if (isHeld(ButtonName::Y) && _gamepadYTime >= 0.0f) {
			_gamepadYTime += timeMult;
			if (_gamepadYTime >= RadialHoldFrames && _radialMenu == nullptr) {
				ShowToolMenu(_gamepadCursor);
			}
		}
		if (_radialMenu != nullptr && isHeld(ButtonName::Y)) {
			_radialMenu->PointTo(leftStick);
		}
		if (wasReleased(ButtonName::Y) && _gamepadYTime >= 0.0f) {
			if (_radialMenu != nullptr) {
				_radialMenu->Confirm();
			} else if (_gamepadYTime < RadialHoldFrames) {
				static const char* const ToolCycle[] = { ActionIds::ToolBrush, ActionIds::ToolEraser, ActionIds::ToolRectangle, ActionIds::ToolFill,
					ActionIds::ToolSelect, ActionIds::ToolEvent, ActionIds::ToolHand };
				static const EditorToolType ToolCycleTypes[] = { EditorToolType::Brush, EditorToolType::Eraser, EditorToolType::Rectangle,
					EditorToolType::Fill, EditorToolType::Select, EditorToolType::Event, EditorToolType::Hand };
				std::int32_t current = -1;
				for (std::int32_t i = 0; i < std::int32_t(arraySize(ToolCycleTypes)); i++) {
					if (ToolCycleTypes[i] == tools.GetChosenType()) {
						current = i;
					}
				}
				_context.GetActions().Execute(ToolCycle[(current + 1) % std::int32_t(arraySize(ToolCycle))]);
			}
			_gamepadYTime = -1.0f;
		}

		if (wasPressed(ButtonName::LeftBumper)) {
			_context.SetCurrentLayer(_context.GetCurrentLayer() - 1);
		}
		if (wasPressed(ButtonName::RightBumper)) {
			_context.SetCurrentLayer(_context.GetCurrentLayer() + 1);
		}
		if (wasPressed(ButtonName::Back)) {
			_context.Undo();
		}
		if (wasPressed(ButtonName::LeftStick)) {
			_context.Redo();
		}
		if (wasPressed(ButtonName::Start)) {
			_host.StartPlayTest(PlayStartMode::Resume);
		}
	}

	void EditorInputRouter::PointerDown(Vector2f pos, MouseButton button)
	{
		_pointerPos = pos;
		_pointerKnown = true;

		if (_toolPointerDown || _panning) {
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Button = button;
		uiEvent.Modifiers = _modifiers;
		// The panels are only seen while a ring grows over them, they don't take what is meant for the game. While the
		// view flies into the level they don't take anything either, the level is played as it is then.
		if (_playTest.IsEntering() || (!_playTest.IsEditing() && _playTest.GetTransition().IsActive())) {
			return;
		}
		if (_context.GetSurface()->OnPointerDown(uiEvent) || !_playTest.IsEditing()) {
			return;
		}

		if (button == MouseButton::Middle) {
			_panning = true;
			_context.GetEditHandler()->GetCamera().BeginDrag(_pointerPos);
			return;
		}
		if (button == MouseButton::Left || button == MouseButton::Right) {
			_toolPointerDown = true;
			_toolButton = button;
			_toolCallbackDepth++;
			_context.GetTools().GetActive()->OnPointerDown(MakeToolPointer(_pointerPos, button));
			_toolCallbackDepth--;
		}
	}

	void EditorInputRouter::CancelToolStroke()
	{
		ToolManager& tools = _context.GetTools();
		// Whatever the tool runs while it's cancelled is its own
		_toolCallbackDepth++;
		tools.GetActive()->Cancel();
		_toolCallbackDepth--;
		_toolPointerDown = false;
		// A tool used for a while waits for the release to end, which doesn't come to it anymore
		if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::Space)) {
			tools.EndTemporary(EditorToolType::Hand);
		}
		if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::LAlt)) {
			tools.EndTemporary(EditorToolType::Eyedropper);
		}
	}

	void EditorInputRouter::CancelInteractions(bool releasePointer)
	{
		// Only what follows the pointer, a path clicked point by point waits for its next point, see EditorTool::HasPointerCapture()
		if (_toolPointerDown || _context.GetTools().GetActive()->HasPointerCapture()) {
			CancelToolStroke();
		}
		UiSurface* surface = _context.GetSurface();
		if (releasePointer && surface != nullptr) {
			// A handle of the layer dragged, or anything else of the interface held by the pointer, is let go without
			// doing what the release would do
			surface->CancelPointerCapture();
		}
	}

	void EditorInputRouter::ReleasePointer(bool panning)
	{
		_toolPointerDown = false;
		if (panning) {
			_panning = false;
		}
	}

	void EditorInputRouter::OnMouseUp(const MouseEvent& event)
	{
		if (_context.GetEditHandler() == nullptr || Shortcut::GetMouseButtonKey(event.button) != Keys::Unknown) {
			return;
		}
		PointerUp(ToDrawable(event.x, event.y), event.button);
	}

	void EditorInputRouter::PointerUp(Vector2f pos, MouseButton button)
	{
		_pointerPos = pos;

		if (_toolPointerDown && button == _toolButton) {
			ToolManager& tools = _context.GetTools();
			_toolPointerDown = false;
			_toolCallbackDepth++;
			tools.GetActive()->OnPointerUp(MakeToolPointer(_pointerPos, button));
			_toolCallbackDepth--;
			// A tool used for a while that waited for the release is done now
			if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::Space)) {
				tools.EndTemporary(EditorToolType::Hand);
			}
			if (!theApplication().GetInputManager().keyboardState().isKeyDown(Keys::LAlt)) {
				tools.EndTemporary(EditorToolType::Eyedropper);
			}
			return;
		}
		if (_panning && button == MouseButton::Middle) {
			_panning = false;
			_context.GetEditHandler()->GetCamera().EndDrag();
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Button = button;
		uiEvent.Modifiers = _modifiers;
		_context.GetSurface()->OnPointerUp(uiEvent);
	}

	void EditorInputRouter::OnMouseMove(const MouseState& state)
	{
		if (_context.GetEditHandler() == nullptr) {
			return;
		}
		Vector2f pos = ToDrawable(state.x, state.y);
		bool moved = ((pos - _mousePos).Length() >= 4.0f);
		_mousePos = pos;
		if (_inputModality == InputModality::Gamepad && !moved) {
			// The mouse barely moved, the gamepad is still used
			return;
		}
		SetInputModality(InputModality::KeyboardMouse);
		PointerMove(pos);
	}

	void EditorInputRouter::PointerMove(Vector2f pos)
	{
		_pointerPos = pos;
		_pointerKnown = true;
		// Also while a tool or the view holds the pointer, what is shown by it stays with it
		UiSurface* surface = _context.GetSurface();
		if (surface != nullptr) {
			surface->TrackPointer(pos);
		}

		if (_toolPointerDown) {
			_toolCallbackDepth++;
			_context.GetTools().GetActive()->OnPointerMove(MakeToolPointer(_pointerPos, _toolButton));
			_toolCallbackDepth--;
			return;
		}
		if (_panning) {
			_context.GetEditHandler()->GetCamera().DragTo(_pointerPos);
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Modifiers = _modifiers;
		if (!surface->OnPointerMove(uiEvent) && _playTest.IsEditing()) {
			_toolCallbackDepth++;
			_context.GetTools().GetActive()->OnPointerMove(MakeToolPointer(_pointerPos, _toolButton));
			_toolCallbackDepth--;
		}
	}

	void EditorInputRouter::OnMouseWheel(const ScrollEvent& event)
	{
		EditorLevelHandler* editHandler = _context.GetEditHandler();
		if (editHandler == nullptr) {
			return;
		}

		UiPointerEvent uiEvent;
		uiEvent.Pos = _pointerPos;
		uiEvent.Wheel = Vector2f(event.x, event.y);
		uiEvent.Modifiers = _modifiers;
		if (_context.GetSurface()->OnWheel(uiEvent) || !_playTest.IsEditing()) {
			return;
		}

		// The wheel zooms, with Shift or Ctrl it moves the view, with Ctrl+Shift it sizes the interface and with
		// Alt it changes the layer; a horizontal wheel always moves the view
		bool ctrl = (_modifiers & KeyMod::Ctrl) != 0;
		bool shift = (_modifiers & KeyMod::Shift) != 0;
		bool alt = (_modifiers & KeyMod::Alt) != 0;
		if (event.y != 0.0f && ctrl && shift) {
			_host.ChangeUiScale(event.y > 0.0f ? 1 : -1);
			return;
		}
		if (event.y != 0.0f && alt) {
			_context.SetCurrentLayer(_context.GetCurrentLayer() + (event.y > 0.0f ? -1 : 1));
			return;
		}

		EditorCamera& camera = editHandler->GetCamera();
		Rectf view = _host.GetViewRect();
		float step = std::round(std::min(view.W, view.H) * WheelPanFraction);
		Vector2f pan(event.x, 0.0f);
		if (shift) {
			pan.X -= event.y;
		} else if (ctrl) {
			pan.Y -= event.y;
		} else if (event.y != 0.0f) {
			camera.ZoomSteps(_pointerPos, event.y);
		}
		if (pan.X != 0.0f || pan.Y != 0.0f) {
			camera.PanBy(pan * step);
		}
	}

	bool EditorInputRouter::GetHoverPointer(ToolPointer& pointer)
	{
		if (!_pointerKnown || _context.GetEditHandler() == nullptr || (!_toolPointerDown && _context.GetSurface()->IsOverInterface(_pointerPos))) {
			return false;
		}
		pointer = MakeToolPointer(_pointerPos, _toolButton);
		return true;
	}

	ToolPointer EditorInputRouter::MakeToolPointer(Vector2f pos, MouseButton button)
	{
		ToolPointer pointer;
		pointer.Screen = pos;
		pointer.Button = button;
		pointer.Modifiers = _modifiers;
		LayerView view = _context.GetLayerView(_context.GetCurrentLayer());
		pointer.World = view.ScreenToWorld(pos);
		pointer.Cell = view.WorldToCell(pointer.World);
		return pointer;
	}

	Vector2f EditorInputRouter::ToDrawable(std::int32_t x, std::int32_t y)
	{
		IGfxDevice& gfxDevice = theApplication().GetGfxDevice();
		float scaleX = (gfxDevice.width() > 0 ? float(gfxDevice.drawableWidth()) / float(gfxDevice.width()) : 1.0f);
		float scaleY = (gfxDevice.height() > 0 ? float(gfxDevice.drawableHeight()) / float(gfxDevice.height()) : 1.0f);
		return Vector2f(x * scaleX, y * scaleY);
	}
}

#endif
