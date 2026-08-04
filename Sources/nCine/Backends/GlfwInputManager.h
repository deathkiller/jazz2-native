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
		static Keys keySymValueToEnum(int keysym);
		static int keyModMaskToEnumMask(int keymod);
		static int enumToKeysValue(Keys keysym);
	};

	/**
		@brief Information about GLFW mouse state
	*/
	class GlfwMouseState : public MouseState
	{
	public:
		GlfwMouseState();

		bool isButtonDown(MouseButton button) const override;
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
		inline bool isKeyDown(Keys key) const override
		{
			const int glfwKey = GlfwKeys::enumToKeysValue(key);
			if (glfwKey == GLFW_KEY_UNKNOWN)
				return false;
			else
				return glfwGetKey(GlfwGfxDevice::windowHandle(), glfwKey) == GLFW_PRESS;
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

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

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
		static bool hasFocus();
		/** @brief Updates joystick state structures and simulates events */
		static void updateJoystickStates();

		const MouseState& mouseState() const override;
		inline const KeyboardState& keyboardState() const override {
			return _keyboardState;
		}

		String getClipboardText() const override;
		bool setClipboardText(StringView text) override;

		bool isJoyPresent(int joyId) const override;
		const char* joyName(int joyId) const override;
		const JoystickGuid joyGuid(int joyId) const override;
		int joyNumButtons(int joyId) const override;
		int joyNumHats(int joyId) const override;
		int joyNumAxes(int joyId) const override;
		const JoystickState& joystickState(int joyId) const override;
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override;
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override;

		void setCursor(Cursor cursor) override;

	private:
		static const int MaxNumJoysticks = GLFW_JOYSTICK_LAST - GLFW_JOYSTICK_1 + 1;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class JoystickEventsSimulator
		{
		public:
			JoystickEventsSimulator();
			void resetJoystickState(int joyId);
			void simulateButtonsEvents(int joyId, int numButtons, const unsigned char* buttons);
			void simulateHatsEvents(int joyId, int numHats, const unsigned char* hats);
			void simulateAxesEvents(int joyId, int numAxes, const float* axesValues);

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

		static void monitorCallback(GLFWmonitor* monitor, int event);
		static void windowCloseCallback(GLFWwindow* window);
		static void windowContentScaleCallback(GLFWwindow* window, float xscale, float yscale);
		static void windowSizeCallback(GLFWwindow* window, int width, int height);
		static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
		static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void charCallback(GLFWwindow* window, unsigned int c);
		static void cursorPosCallback(GLFWwindow* window, double x, double y);
		static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void joystickCallback(int joy, int event);
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static EM_BOOL emscriptenHandleTouch(int eventType, const EmscriptenTouchEvent* event, void* userData);
#endif

		static Keys keySymValueToEnum(int keysym);
		static KeyMod keyModValueToEnum(int keymod);
		static int enumToKeysValue(Keys keysym);

		friend class GlfwGfxDevice; // for setWindowPosition()
	};

	inline const MouseState& GlfwInputManager::mouseState() const
	{
		double xCursor, yCursor;

		glfwGetCursorPos(GlfwGfxDevice::windowHandle(), &xCursor, &yCursor);
		_mouseState.x = int(xCursor);
		_mouseState.y = int(yCursor);

		return _mouseState;
	}
}

#endif