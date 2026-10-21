#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Input/TouchGestures.h"
#include "../Runtime/PlayTestTypes.h"
#include "../Tools/EditorContext.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"

using namespace nCine;

namespace Jazz2::Editor
{
	class PlayTestController;
	class UiRadialMenu;
	class UiTextBox;

	/**
		@brief What @ref EditorInputRouter needs of the level editor beyond @ref IEditorContext
	*/
	class IInputRouterHost
	{
	public:
		virtual ~IInputRouterHost() = default;

		/** @brief Returns the view of the level on the screen, what the panels don't cover */
		virtual Rectf GetViewRect() const = 0;
		/** @brief Clears the selection of what the editing actions work on, the events or the tiles */
		virtual void ClearSelection() = 0;
		/** @brief Makes the interface larger or smaller by a step, or chooses its size by the window with 0 */
		virtual void ChangeUiScale(std::int32_t direction) = 0;
		/** @brief Starts playing the level */
		virtual void StartPlayTest(PlayStartMode mode) = 0;
		/** @brief Stops playing the level and returns to editing, @p keep keeps the play-test to resume later */
		virtual void StopPlayTest(bool keep) = 0;
	};

	/**
		@brief Takes the keyboard, the mouse, the touch screen and the gamepad in the level editor

		Gives the input to the interface, to the active tool and to the view of the level, and while the level is
		played to the game, except the keys of the actions of playing. The side buttons of a mouse are pressed as keys
		of their own, past the last key the game knows, so they never reach it. Knows what the editor was used with last
		(see @ref InputModality), moves the cursor of a gamepad and knows whether the pointer holds a stroke of a tool,
		so the changes made meanwhile from elsewhere end it first, see @ref CancelInteractions().
	*/
	class EditorInputRouter
	{
	public:
		/** @brief Creates the input of an editor, @p playTest tells whether the level is played */
		EditorInputRouter(IEditorContext& context, IInputRouterHost& host, PlayTestController& playTest);

		EditorInputRouter(const EditorInputRouter&) = delete;
		EditorInputRouter& operator=(const EditorInputRouter&) = delete;

		/** @brief Reads the gamepad and uses the editor by it, and moves on the gestures of the touch screen, every frame */
		void Update(float timeMult);

		/** @brief Called when a key is pressed */
		void OnKeyPressed(const KeyboardEvent& event);
		/** @brief Called when a key is released */
		void OnKeyReleased(const KeyboardEvent& event);
		/** @brief Called when text is typed */
		void OnTextInput(const TextInputEvent& event);
		/** @brief Called for a touch event */
		void OnTouchEvent(const TouchEvent& event);
		/** @brief Called when a button of the mouse is pressed */
		void OnMouseDown(const MouseEvent& event);
		/** @brief Called when a button of the mouse is released */
		void OnMouseUp(const MouseEvent& event);
		/** @brief Called when the mouse is moved */
		void OnMouseMove(const MouseState& state);
		/** @brief Called when the wheel of the mouse is turned */
		void OnMouseWheel(const ScrollEvent& event);

		/** @brief Moves the pointer to a position on the screen */
		void PointerMove(Vector2f pos);
		/** @brief Switches what the editor is used with, the hints follow */
		void SetInputModality(InputModality modality);
		/** @brief Returns what the editor was used with last */
		InputModality GetInputModality() const {
			return _inputModality;
		}
		/** @brief Returns the cursor moved by a gamepad, in pixels of the screen */
		Vector2f GetGamepadCursor() const {
			return _gamepadCursor;
		}
		/** @brief Places the cursor of a gamepad, the pointer isn't moved with it */
		void SetGamepadCursor(Vector2f pos) {
			_gamepadCursor = pos;
		}
		/** @brief Shows the menu of tools in a ring around a position on the screen */
		void ShowToolMenu(Vector2f pos);
		/** @brief Returns the menu of tools in a ring shown, or `nullptr` */
		UiRadialMenu* GetRadialMenu() const {
			return _radialMenu;
		}
		/** @brief Returns the modifier keys held, as the last key event had them */
		std::int32_t GetModifiers() const {
			return _modifiers;
		}
		/** @brief Returns the last position of the pointer, `false` if it's not known yet */
		bool GetPointerPos(Vector2f& pos) const {
			pos = _pointerPos;
			return _pointerKnown;
		}
		/** @brief Returns the pointer over the view, `false` if it's not over the view */
		bool GetHoverPointer(ToolPointer& pointer);

		/** @brief Returns `true` while the pointer holds a stroke of the active tool */
		bool IsToolPointerDown() const {
			return _toolPointerDown;
		}
		/** @brief Returns `true` while the tool is being told about the pointer, the commands it runs then are its own */
		bool IsInToolCallback() const {
			return (_toolCallbackDepth > 0);
		}
		/** @brief Returns `true` while an action is run by its key, not chosen from a menu or by a button */
		bool IsHandlingShortcut() const {
			return _handlingShortcut;
		}
		/**
		 * @brief Ends what the pointer is in the middle of, see @ref EditorSession::CancelInteractions()
		 *
		 * The stroke of a tool that follows the pointer is cancelled, as by <kbd>Esc</kbd>, and with @p releasePointer
		 * also what the interface holds by the pointer.
		 */
		void CancelInteractions(bool releasePointer);
		/** @brief Forgets that the pointer holds a stroke of the tool, and with @p panning that it moves the view */
		void ReleasePointer(bool panning);

	private:
		IEditorContext& _context;
		IInputRouterHost& _host;
		PlayTestController& _playTest;

		// Last position of the pointer in pixels of the drawable, which may differ from the window on a high-DPI display
		Vector2f _pointerPos;
		bool _pointerKnown = false;
		bool _panning = false;
		bool _toolPointerDown = false;
		MouseButton _toolButton = MouseButton::Left;
		std::int32_t _modifiers = 0;
		// An action is run by its key, not chosen from a menu or by a button
		bool _handlingShortcut = false;
		// The tool is being told about the pointer, the commands it runs then are its own, see CancelInteractions()
		std::int32_t _toolCallbackDepth = 0;

		// What the editor was used with last, and the state of the gamepad
		InputModality _inputModality = InputModality::KeyboardMouse;
		Vector2f _mousePos;
		Vector2f _gamepadCursor;
		std::uint32_t _gamepadButtons = 0;
		float _gamepadRepeat = 0.0f;
		// How long Y has been held, held long it opens the menu of tools around the cursor
		float _gamepadYTime = -1.0f;
		UiRadialMenu* _radialMenu = nullptr;
		TouchGestures _touch;
		// Frames the sticks have been held for, they move faster then
		float _gamepadCursorHold = 0.0f;
		float _gamepadPanHold = 0.0f;
		bool _gamepadPicking = false;
		// The finger that pressed the bar shown over a level being played, it's the pointer, not a finger of the game, -1 if none
		std::int32_t _barTouchId = -1;
		// The keyboard of the system was shown for a box being typed into, it's hidden once no box is, see Update()
		bool _screenKeyboardShown = false;

		// Presses the pointer at a position on the screen, by the mouse or a gamepad
		void PointerDown(Vector2f pos, MouseButton button);
		// Passes the fingers on the screen to the level being played, except one on the bar shown over it, which is the pointer
		void OnPlayTouchEvent(const TouchEvent& event);
		// Shows the keyboard of the system for a box being typed into, remembering it was asked for, returns `false` if there's none
		bool ShowScreenKeyboard(UiTextBox& field);
		// Releases the pointer at a position on the screen
		void PointerUp(Vector2f pos, MouseButton button);
		// Reads the gamepad and uses the editor by it
		void UpdateGamepad(float timeMult);
		// Sets up what the fingers on a touch screen do
		void SetupTouchGestures();
		// Cancels the stroke of the active tool, a tool used for a while ends unless its key is still held
		void CancelToolStroke();
		// Makes a pointer for a tool from a position on the screen
		ToolPointer MakeToolPointer(Vector2f pos, MouseButton button);
		// Converts a position of the mouse from the window to the drawable
		static Vector2f ToDrawable(std::int32_t x, std::int32_t y);
	};
}

#endif
