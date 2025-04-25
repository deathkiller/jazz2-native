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

	/// Utility functions to convert between engine key enumerations and GLFW ones
	class GlfwKeys
	{
	public:
		static Keys keySymValueToEnum(int keysym);
		static int keyModMaskToEnumMask(int keymod);
		static int enumToKeysValue(Keys keysym);
	};

	/// Information about GLFW mouse state
	class GlfwMouseState : public MouseState
	{
	public:
		GlfwMouseState();

		bool isButtonDown(MouseButton button) const override;
	};

	/// Information about a GLFW scroll event
	class GlfwScrollEvent : public ScrollEvent
	{
	public:
		GlfwScrollEvent() {}

		friend class GlfwInputManager;
	};

	/// Information about GLFW keyboard state
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

	/// Information about GLFW joystick state
	class GlfwJoystickState : public JoystickState
	{
	public:
		GlfwJoystickState()
			: numButtons_(0), numHats_(0), numAxes_(0), buttons_(nullptr), hats_(nullptr), axesValues_(nullptr) {}

		bool isButtonPressed(int buttonId) const override;
		unsigned char hatState(int hatId) const override;
		float axisValue(int axisId) const override;

	private:
		int numButtons_;
		int numHats_;
		int numAxes_;

		const unsigned char* buttons_;
		const unsigned char* hats_;
		const float* axesValues_;

		friend class GlfwInputManager;
	};

	/// Slass for parsing and dispatching GLFW input events
	class GlfwInputManager : public IInputManager
	{
	public:
		GlfwInputManager();
		~GlfwInputManager() override;

		/// Detects window focus gain/loss events
		static bool hasFocus();
		/// Updates joystick state structures and simulates events
		static void updateJoystickStates();

		const MouseState& mouseState() const override;
		inline const KeyboardState& keyboardState() const override {
			return keyboardState_;
		}

		String getClipboardText() const override;

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
			/// Minimum difference between two axis readings in order to trigger an event
			static const float AxisEventTolerance;

			/// Old state used to simulate joystick buttons events
			unsigned char buttonsState_[MaxNumJoysticks][MaxNumButtons];
			/// Old state used to simulate joystick hats events
			unsigned char hatsState_[MaxNumJoysticks][MaxNumHats];
			/// Old state used to simulate joystick axes events
			float axesValuesState_[MaxNumJoysticks][MaxNumAxes];
		};
#endif

		static bool windowHasFocus_;
		static GlfwMouseState mouseState_;
		static MouseEvent mouseEvent_;
		static GlfwScrollEvent scrollEvent_;
		static GlfwKeyboardState keyboardState_;
		static KeyboardEvent keyboardEvent_;
		static TextInputEvent textInputEvent_;
		static GlfwJoystickState nullJoystickState_;
		static SmallVector<GlfwJoystickState, MaxNumJoysticks> joystickStates_;
		static JoyButtonEvent joyButtonEvent_;
		static JoyHatEvent joyHatEvent_;
		static JoyAxisEvent joyAxisEvent_;
		static JoyConnectionEvent joyConnectionEvent_;
		static JoystickEventsSimulator joyEventsSimulator_;

		/// The window width before a window content scale event
		static int preScalingWidth_;
		/// The window height before a window content scale event
		static int preScalingHeight_;
		/// The last frame a window size callback was called
		static unsigned long int lastFrameWindowSizeChanged_;

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
		mouseState_.x = int(xCursor);
		mouseState_.y = int(yCursor);

		return mouseState_;
	}
}

#endif