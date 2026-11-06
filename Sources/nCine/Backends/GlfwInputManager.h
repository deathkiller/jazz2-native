#pragma once

#if defined(WITH_GLFW) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Input/IInputManager.h"
#include "GlfwGfxDevice.h" // for WindowHandle()
#include "../../Main.h"

#include <cstdio>

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/html5.h>
#endif

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine::Backends
{
	class GlfwInputManager;

	/**
		@brief Utility functions to convert between engine key enumerations and GLFW ones
	*/
	class GlfwKeys
	{
	public:
		static Keys KeySymValueToEnum(int keysym);
		static int KeyModMaskToEnumMask(int keymod);
		static int EnumToKeysValue(Keys keysym);
	};

	/**
		@brief Information about GLFW mouse state
	*/
	class GlfwMouseState : public MouseState
	{
	public:
		GlfwMouseState();

		bool IsButtonDown(MouseButton button) const override;
	};

	/**
		@brief Information about a GLFW scroll event
	*/
	class GlfwScrollEvent : public ScrollEvent
	{
	public:
		GlfwScrollEvent() {}

		friend class GlfwInputManager;
	};

	/**
		@brief Information about GLFW keyboard state
	*/
	class GlfwKeyboardState : public KeyboardState
	{
	public:
		inline bool IsKeyDown(Keys key) const override
		{
			const int glfwKey = GlfwKeys::EnumToKeysValue(key);
			if (glfwKey == GLFW_KEY_UNKNOWN)
				return false;
			else
				return glfwGetKey(GlfwGfxDevice::GetWindowHandle(), glfwKey) == GLFW_PRESS;
		}

		friend class GlfwInputManager;
	};

	/**
		@brief Information about GLFW joystick state
	*/
	class GlfwJoystickState : public JoystickState
	{
	public:
		GlfwJoystickState()
			: _numButtons(0), _numHats(0), _numAxes(0), _buttons(nullptr), _hats(nullptr), _axesValues(nullptr) {}

		bool IsButtonPressed(int buttonId) const override;
		unsigned char GetHatState(int hatId) const override;
		float GetAxisValue(int axisId) const override;

	private:
		int _numButtons;
		int _numHats;
		int _numAxes;

		const unsigned char* _buttons;
		const unsigned char* _hats;
		const float* _axesValues;

		friend class GlfwInputManager;
	};

	/**
		@brief GLFW-based input manager
		
		Parses GLFW window, keyboard, mouse and joystick events and dispatches them to the
		registered input event handler, and exposes the current input device states.
	*/
	class GlfwInputManager : public IInputManager
	{
	public:
		GlfwInputManager();
		~GlfwInputManager() override;

		/** @brief Returns `true` if the window currently has input focus */
		static bool HasFocus();
		/** @brief Lets input over a window besides the main one go where input over the main window goes, see @ref AuxiliaryWindow */
#if defined(NCINE_HAS_AUXILIARY_WINDOWS) || defined(DOXYGEN_GENERATING_OUTPUT)
		static void AttachAuxiliaryWindow(GLFWwindow* window);
#endif
		/** @brief Updates joystick state structures and simulates events */
		static void UpdateJoystickStates();

		const MouseState& GetMouseState() const override;
		inline const KeyboardState& GetKeyboardState() const override {
			return _keyboardState;
		}

		String GetClipboardText() const override;
		bool SetClipboardText(StringView text) override;

		bool IsJoyPresent(int joyId) const override;
		const char* GetJoyName(int joyId) const override;
		const JoystickGuid GetJoyGuid(int joyId) const override;
		int GetJoyButtonCount(int joyId) const override;
		int GetJoyHatCount(int joyId) const override;
		int GetJoyAxisCount(int joyId) const override;
		const JoystickState& GetJoystickState(int joyId) const override;
		bool JoystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override;
		bool JoystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void SetCursor(Cursor cursor) override;

	private:
		static const int MaxNumJoysticks = GLFW_JOYSTICK_LAST - GLFW_JOYSTICK_1 + 1;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class JoystickEventsSimulator
		{
		public:
			JoystickEventsSimulator();
			void ResetJoystickState(int joyId);
			void SimulateButtonsEvents(int joyId, int numButtons, const unsigned char* buttons);
			void SimulateHatsEvents(int joyId, int numHats, const unsigned char* hats);
			void SimulateAxesEvents(int joyId, int numAxes, const float* axesValues);

		private:
			static const unsigned int MaxNumButtons = 16;
			static const unsigned int MaxNumHats = 4;
			static const unsigned int MaxNumAxes = 10;
			/** @brief Minimum difference between two axis readings in order to trigger an event */
			static const float AxisEventTolerance;

			/** @brief Old state used to simulate joystick buttons events */
			unsigned char _buttonsState[MaxNumJoysticks][MaxNumButtons];
			/** @brief Old state used to simulate joystick hats events */
			unsigned char _hatsState[MaxNumJoysticks][MaxNumHats];
			/** @brief Old state used to simulate joystick axes events */
			float _axesValuesState[MaxNumJoysticks][MaxNumAxes];
		};
#endif

		static bool _windowHasFocus;
		static GlfwMouseState _mouseState;
		static MouseEvent _mouseEvent;
		static GlfwScrollEvent _scrollEvent;
		static GlfwKeyboardState _keyboardState;
		static KeyboardEvent _keyboardEvent;
		static TextInputEvent _textInputEvent;
		static GlfwJoystickState _nullJoystickState;
		static SmallVector<GlfwJoystickState, MaxNumJoysticks> _joystickStates;
		static JoyButtonEvent _joyButtonEvent;
		static JoyHatEvent _joyHatEvent;
		static JoyAxisEvent _joyAxisEvent;
		static JoyConnectionEvent _joyConnectionEvent;
		static JoystickEventsSimulator _joyEventsSimulator;

		/** @brief The window width before a window content scale event */
		static int _preScalingWidth;
		/** @brief The window height before a window content scale event */
		static int _preScalingHeight;
		/** @brief The last frame a window size callback was called */
		static unsigned long int _lastFrameWindowSizeChanged;

		static void MonitorCallback(GLFWmonitor* monitor, int event);
		static void WindowCloseCallback(GLFWwindow* window);
		static void WindowContentScaleCallback(GLFWwindow* window, float xscale, float yscale);
		static void WindowSizeCallback(GLFWwindow* window, int width, int height);
		static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void CharCallback(GLFWwindow* window, unsigned int c);
		static void CursorPosCallback(GLFWwindow* window, double x, double y);
		static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void JoystickCallback(int joy, int event);
		// Position over a window of the application relative to the main window, and the identifier of the window, 0 for the main one
		static Vector2i ToMainWindowPosition(GLFWwindow* window, double x, double y, std::uint32_t& windowId);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static EM_BOOL EmscriptenHandleTouch(int eventType, const EmscriptenTouchEvent* event, void* userData);
#endif

		static Keys KeySymValueToEnum(int keysym);
		static KeyMod KeyModValueToEnum(int keymod);
		static int EnumToKeysValue(Keys keysym);

		friend class GlfwGfxDevice; // for setWindowPosition()
	};

	inline const MouseState& GlfwInputManager::GetMouseState() const
	{
		double xCursor, yCursor;

		glfwGetCursorPos(GlfwGfxDevice::GetWindowHandle(), &xCursor, &yCursor);
		_mouseState.X = int(xCursor);
		_mouseState.Y = int(yCursor);

		return _mouseState;
	}
}

#endif