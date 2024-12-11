#if defined(WITH_GLFW)

#include "GlfwInputManager.h"

namespace nCine
{
	Keys GlfwKeys::keySymValueToEnum(int keysym)
	{
		// clang-format off
		switch (keysym) {
			case GLFW_KEY_UNKNOWN:			return Keys::Unknown;
			case GLFW_KEY_BACKSPACE:		return Keys::Backspace;
			case GLFW_KEY_TAB:				return Keys::Tab;
			case GLFW_KEY_ENTER:			return Keys::Return;
			case GLFW_KEY_ESCAPE:			return Keys::Escape;
			case GLFW_KEY_SPACE:			return Keys::Space;
			case GLFW_KEY_APOSTROPHE:		return Keys::Quote;
			case GLFW_KEY_EQUAL:			return Keys::Plus;
			case GLFW_KEY_COMMA:			return Keys::Comma;
			case GLFW_KEY_MINUS:			return Keys::Minus;
			case GLFW_KEY_PERIOD:			return Keys::Period;
			case GLFW_KEY_SLASH:			return Keys::Slash;
			case GLFW_KEY_0:				return Keys::D0;
			case GLFW_KEY_1:				return Keys::D1;
			case GLFW_KEY_2:				return Keys::D2;
			case GLFW_KEY_3:				return Keys::D3;
			case GLFW_KEY_4:				return Keys::D4;
			case GLFW_KEY_5:				return Keys::D5;
			case GLFW_KEY_6:				return Keys::D6;
			case GLFW_KEY_7:				return Keys::D7;
			case GLFW_KEY_8:				return Keys::D8;
			case GLFW_KEY_9:				return Keys::D9;
			case GLFW_KEY_SEMICOLON:		return Keys::Semicolon;

			case GLFW_KEY_LEFT_BRACKET:		return Keys::LeftBracket;
			case GLFW_KEY_BACKSLASH:		return Keys::Backslash;
			case GLFW_KEY_RIGHT_BRACKET:	return Keys::RightBracket;
			case GLFW_KEY_GRAVE_ACCENT:		return Keys::Backquote;
			case GLFW_KEY_WORLD_1:			return Keys::World1;
			case GLFW_KEY_WORLD_2:			return Keys::World2;
			case GLFW_KEY_A:				return Keys::A;
			case GLFW_KEY_B:				return Keys::B;
			case GLFW_KEY_C:				return Keys::C;
			case GLFW_KEY_D:				return Keys::D;
			case GLFW_KEY_E:				return Keys::E;
			case GLFW_KEY_F:				return Keys::F;
			case GLFW_KEY_G:				return Keys::G;
			case GLFW_KEY_H:				return Keys::H;
			case GLFW_KEY_I:				return Keys::I;
			case GLFW_KEY_J:				return Keys::J;
			case GLFW_KEY_K:				return Keys::K;
			case GLFW_KEY_L:				return Keys::L;
			case GLFW_KEY_M:				return Keys::M;
			case GLFW_KEY_N:				return Keys::N;
			case GLFW_KEY_O:				return Keys::O;
			case GLFW_KEY_P:				return Keys::P;
			case GLFW_KEY_Q:				return Keys::Q;
			case GLFW_KEY_R:				return Keys::R;
			case GLFW_KEY_S:				return Keys::S;
			case GLFW_KEY_T:				return Keys::T;
			case GLFW_KEY_U:				return Keys::U;
			case GLFW_KEY_V:				return Keys::V;
			case GLFW_KEY_W:				return Keys::W;
			case GLFW_KEY_X:				return Keys::X;
			case GLFW_KEY_Y:				return Keys::Y;
			case GLFW_KEY_Z:				return Keys::Z;
			case GLFW_KEY_DELETE:			return Keys::Delete;

			case GLFW_KEY_KP_0:				return Keys::NumPad0;
			case GLFW_KEY_KP_1:				return Keys::NumPad1;
			case GLFW_KEY_KP_2:				return Keys::NumPad2;
			case GLFW_KEY_KP_3:				return Keys::NumPad3;
			case GLFW_KEY_KP_4:				return Keys::NumPad4;
			case GLFW_KEY_KP_5:				return Keys::NumPad5;
			case GLFW_KEY_KP_6:				return Keys::NumPad6;
			case GLFW_KEY_KP_7:				return Keys::NumPad7;
			case GLFW_KEY_KP_8:				return Keys::NumPad8;
			case GLFW_KEY_KP_9:				return Keys::NumPad9;
			case GLFW_KEY_KP_DECIMAL:		return Keys::NumPadPeriod;
			case GLFW_KEY_KP_DIVIDE:		return Keys::NumPadDivide;
			case GLFW_KEY_KP_MULTIPLY:		return Keys::NumPadMultiply;
			case GLFW_KEY_KP_SUBTRACT:		return Keys::NumPadMinus;
			case GLFW_KEY_KP_ADD:			return Keys::NumPadPlus;
			case GLFW_KEY_KP_ENTER:			return Keys::NumPadEnter;
			case GLFW_KEY_KP_EQUAL:			return Keys::NumPadEquals;

			case GLFW_KEY_UP:				return Keys::Up;
			case GLFW_KEY_DOWN:				return Keys::Down;
			case GLFW_KEY_RIGHT:			return Keys::Right;
			case GLFW_KEY_LEFT:				return Keys::Left;
			case GLFW_KEY_INSERT:			return Keys::Insert;
			case GLFW_KEY_HOME:				return Keys::Home;
			case GLFW_KEY_END:				return Keys::End;
			case GLFW_KEY_PAGE_UP:			return Keys::PageUp;
			case GLFW_KEY_PAGE_DOWN:		return Keys::PageDown;

			case GLFW_KEY_F1:				return Keys::F1;
			case GLFW_KEY_F2:				return Keys::F2;
			case GLFW_KEY_F3:				return Keys::F3;
			case GLFW_KEY_F4:				return Keys::F4;
			case GLFW_KEY_F5:				return Keys::F5;
			case GLFW_KEY_F6:				return Keys::F6;
			case GLFW_KEY_F7:				return Keys::F7;
			case GLFW_KEY_F8:				return Keys::F8;
			case GLFW_KEY_F9:				return Keys::F9;
			case GLFW_KEY_F10:				return Keys::F10;
			case GLFW_KEY_F11:				return Keys::F11;
			case GLFW_KEY_F12:				return Keys::F12;
			case GLFW_KEY_F13:				return Keys::F13;
			case GLFW_KEY_F14:				return Keys::F14;
			case GLFW_KEY_F15:				return Keys::F15;

			case GLFW_KEY_NUM_LOCK:			return Keys::NumLock;
			case GLFW_KEY_CAPS_LOCK:		return Keys::CapsLock;
			case GLFW_KEY_SCROLL_LOCK:		return Keys::ScrollLock;
			case GLFW_KEY_RIGHT_SHIFT:		return Keys::RShift;
			case GLFW_KEY_LEFT_SHIFT:		return Keys::LShift;
			case GLFW_KEY_RIGHT_CONTROL:	return Keys::RCtrl;
			case GLFW_KEY_LEFT_CONTROL:		return Keys::LCtrl;
			case GLFW_KEY_RIGHT_ALT:		return Keys::RAlt;
			case GLFW_KEY_LEFT_ALT:			return Keys::LAlt;
			case GLFW_KEY_RIGHT_SUPER:		return Keys::RSuper;
			case GLFW_KEY_LEFT_SUPER:		return Keys::LSuper;

			case GLFW_KEY_PRINT_SCREEN:		return Keys::PrintScreen;
			case GLFW_KEY_PAUSE:			return Keys::Pause;
			case GLFW_KEY_MENU:				return Keys::Menu;

			default:						return Keys::Unknown;
		}
		// clang-format on
	}

	int GlfwKeys::keyModMaskToEnumMask(int keymod)
	{
		int result = 0;

		if (keymod != 0) {
			result |= ((keymod & GLFW_MOD_SHIFT) != 0 ? KeyMod::LShift : 0);
			result |= ((keymod & GLFW_MOD_CONTROL) != 0 ? KeyMod::LCtrl : 0);
			result |= ((keymod & GLFW_MOD_ALT) != 0 ? KeyMod::LAlt : 0);
			result |= ((keymod & GLFW_MOD_SUPER) != 0 ? KeyMod::LSuper : 0);
		}

		return result;
	}

	int GlfwKeys::enumToKeysValue(Keys keysym)
	{
		// clang-format off
		switch (keysym) {
			case Keys::Unknown:				return GLFW_KEY_UNKNOWN;
			case Keys::Backspace:			return GLFW_KEY_BACKSPACE;
			case Keys::Tab:					return GLFW_KEY_TAB;
			case Keys::Return:				return GLFW_KEY_ENTER;
			case Keys::Escape:				return GLFW_KEY_ESCAPE;
			case Keys::Space:				return GLFW_KEY_SPACE;
			case Keys::Quote:				return GLFW_KEY_APOSTROPHE;
			case Keys::Plus:				return GLFW_KEY_EQUAL;
			case Keys::Comma:				return GLFW_KEY_COMMA;
			case Keys::Minus:				return GLFW_KEY_MINUS;
			case Keys::Period:				return GLFW_KEY_PERIOD;
			case Keys::Slash:				return GLFW_KEY_SLASH;
			case Keys::D0:					return GLFW_KEY_0;
			case Keys::D1:					return GLFW_KEY_1;
			case Keys::D2:					return GLFW_KEY_2;
			case Keys::D3:					return GLFW_KEY_3;
			case Keys::D4:					return GLFW_KEY_4;
			case Keys::D5:					return GLFW_KEY_5;
			case Keys::D6:					return GLFW_KEY_6;
			case Keys::D7:					return GLFW_KEY_7;
			case Keys::D8:					return GLFW_KEY_8;
			case Keys::D9:					return GLFW_KEY_9;
			case Keys::Semicolon:			return GLFW_KEY_SEMICOLON;

			case Keys::LeftBracket:			return GLFW_KEY_LEFT_BRACKET;
			case Keys::Backslash:			return GLFW_KEY_BACKSLASH;
			case Keys::RightBracket:		return GLFW_KEY_RIGHT_BRACKET;
			case Keys::Backquote:			return GLFW_KEY_GRAVE_ACCENT;
			case Keys::World1:				return GLFW_KEY_WORLD_1;
			case Keys::World2:				return GLFW_KEY_WORLD_2;
			case Keys::A:					return GLFW_KEY_A;
			case Keys::B:					return GLFW_KEY_B;
			case Keys::C:					return GLFW_KEY_C;
			case Keys::D:					return GLFW_KEY_D;
			case Keys::E:					return GLFW_KEY_E;
			case Keys::F:					return GLFW_KEY_F;
			case Keys::G:					return GLFW_KEY_G;
			case Keys::H:					return GLFW_KEY_H;
			case Keys::I:					return GLFW_KEY_I;
			case Keys::J:					return GLFW_KEY_J;
			case Keys::K:					return GLFW_KEY_K;
			case Keys::L:					return GLFW_KEY_L;
			case Keys::M:					return GLFW_KEY_M;
			case Keys::N:					return GLFW_KEY_N;
			case Keys::O:					return GLFW_KEY_O;
			case Keys::P:					return GLFW_KEY_P;
			case Keys::Q:					return GLFW_KEY_Q;
			case Keys::R:					return GLFW_KEY_R;
			case Keys::S:					return GLFW_KEY_S;
			case Keys::T:					return GLFW_KEY_T;
			case Keys::U:					return GLFW_KEY_U;
			case Keys::V:					return GLFW_KEY_V;
			case Keys::W:					return GLFW_KEY_W;
			case Keys::X:					return GLFW_KEY_X;
			case Keys::Y:					return GLFW_KEY_Y;
			case Keys::Z:					return GLFW_KEY_Z;
			case Keys::Delete:				return GLFW_KEY_DELETE;

			case Keys::NumPad0:				return GLFW_KEY_KP_0;
			case Keys::NumPad1:				return GLFW_KEY_KP_1;
			case Keys::NumPad2:				return GLFW_KEY_KP_2;
			case Keys::NumPad3:				return GLFW_KEY_KP_3;
			case Keys::NumPad4:				return GLFW_KEY_KP_4;
			case Keys::NumPad5:				return GLFW_KEY_KP_5;
			case Keys::NumPad6:				return GLFW_KEY_KP_6;
			case Keys::NumPad7:				return GLFW_KEY_KP_7;
			case Keys::NumPad8:				return GLFW_KEY_KP_8;
			case Keys::NumPad9:				return GLFW_KEY_KP_9;
			case Keys::NumPadPeriod:		return GLFW_KEY_KP_DECIMAL;
			case Keys::NumPadDivide:		return GLFW_KEY_KP_DIVIDE;
			case Keys::NumPadMultiply:		return GLFW_KEY_KP_MULTIPLY;
			case Keys::NumPadMinus:			return GLFW_KEY_KP_SUBTRACT;
			case Keys::NumPadPlus:			return GLFW_KEY_KP_ADD;
			case Keys::NumPadEnter:			return GLFW_KEY_KP_ENTER;
			case Keys::NumPadEquals:		return GLFW_KEY_KP_EQUAL;

			case Keys::Up:					return GLFW_KEY_UP;
			case Keys::Down:				return GLFW_KEY_DOWN;
			case Keys::Right:				return GLFW_KEY_RIGHT;
			case Keys::Left:				return GLFW_KEY_LEFT;
			case Keys::Insert:				return GLFW_KEY_INSERT;
			case Keys::Home:				return GLFW_KEY_HOME;
			case Keys::End:					return GLFW_KEY_END;
			case Keys::PageUp:				return GLFW_KEY_PAGE_UP;
			case Keys::PageDown:			return GLFW_KEY_PAGE_DOWN;

			case Keys::F1:					return GLFW_KEY_F1;
			case Keys::F2:					return GLFW_KEY_F2;
			case Keys::F3:					return GLFW_KEY_F3;
			case Keys::F4:					return GLFW_KEY_F4;
			case Keys::F5:					return GLFW_KEY_F5;
			case Keys::F6:					return GLFW_KEY_F6;
			case Keys::F7:					return GLFW_KEY_F7;
			case Keys::F8:					return GLFW_KEY_F8;
			case Keys::F9:					return GLFW_KEY_F9;
			case Keys::F10:					return GLFW_KEY_F10;
			case Keys::F11:					return GLFW_KEY_F11;
			case Keys::F12:					return GLFW_KEY_F12;
			case Keys::F13:					return GLFW_KEY_F13;
			case Keys::F14:					return GLFW_KEY_F14;
			case Keys::F15:					return GLFW_KEY_F15;

			case Keys::NumLock:				return GLFW_KEY_NUM_LOCK;
			case Keys::CapsLock:			return GLFW_KEY_CAPS_LOCK;
			case Keys::ScrollLock:			return GLFW_KEY_SCROLL_LOCK;
			case Keys::RShift:				return GLFW_KEY_RIGHT_SHIFT;
			case Keys::LShift:				return GLFW_KEY_LEFT_SHIFT;
			case Keys::RCtrl:				return GLFW_KEY_RIGHT_CONTROL;
			case Keys::LCtrl:				return GLFW_KEY_LEFT_CONTROL;
			case Keys::RAlt:				return GLFW_KEY_RIGHT_ALT;
			case Keys::LAlt:				return GLFW_KEY_LEFT_ALT;
			case Keys::RSuper:				return GLFW_KEY_RIGHT_SUPER;
			case Keys::LSuper:				return GLFW_KEY_LEFT_SUPER;

			case Keys::PrintScreen:			return GLFW_KEY_PRINT_SCREEN;
			case Keys::Pause:				return GLFW_KEY_PAUSE;
			case Keys::Menu:				return GLFW_KEY_MENU;

			default:						return GLFW_KEY_UNKNOWN;
		}
		// clang-format on
	}
}

#endif