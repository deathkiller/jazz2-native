#pragma once

#include "../Input/IInputManager.h"

namespace nCine
{
	class XinputMouseState : public MouseState
	{
	public:
		inline bool isLeftButtonDown() const override
		{
			return false;
		}
		inline bool isMiddleButtonDown() const override
		{
			return false;
		}
		inline bool isRightButtonDown() const override
		{
			return false;
		}
		inline bool isFourthButtonDown() const override
		{
			return false;
		}
		inline bool isFifthButtonDown() const override
		{
			return false;
		}
	};

	class XinputKeyboardState : public KeyboardState
	{
	public:
		inline bool isKeyDown(KeySym key) const override {
			return false;
		}
	};

	class XinputJoystickState : public JoystickState
	{
	public:
		bool isButtonPressed(int buttonId) const override {
			return false;
		}
		unsigned char hatState(int hatId) const override {
			return 0;
		}
		short int axisValue(int axisId) const override {
			return 0;
		}
		float axisNormValue(int axisId) const override {
			return 0.0f;
		}
	};

	/// The class for parsing and dispatching GLFW input events
	class XinputInputManager : public IInputManager
	{
	public:
		XinputInputManager();
		~XinputInputManager() override;

		const MouseState& mouseState() const override {
			return mouseState_;
		}
		inline const KeyboardState& keyboardState() const override {
			return keyboardState_;
		}

		bool isJoyPresent(int joyId) const override {
			return false;
		}
		const char* joyName(int joyId) const override {
			return "XInput";
		}
		const char* joyGuid(int joyId) const override {
			return "XInput";
		}
		int joyNumButtons(int joyId) const override {
			return 0;
		}
		int joyNumHats(int joyId) const override {
			return 0;
		}
		int joyNumAxes(int joyId) const override {
			return 0;
		}
		const JoystickState& joystickState(int joyId) const override {
			return joystickState_;
		}

		void setCursor(Cursor cursor) override { }

	private:
		static const int MaxNumJoysticks = 4;

		static XinputMouseState mouseState_;
		static XinputKeyboardState keyboardState_;
		static XinputJoystickState joystickState_;
	};
}
