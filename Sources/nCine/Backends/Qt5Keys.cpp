#if defined(WITH_QT5)

#include "Qt5InputManager.h"

namespace nCine::Backends
{
	Keys Qt5Keys::keySymValueToEnum(int keysym)
	{
		// clang-format off
		switch (keysym) {
			case Qt::Key_unknown:			return Keys::Unknown;
			case Qt::Key_Backspace:		return Keys::Backspace;
			case Qt::Key_Tab:				return Keys::Tab;
			case Qt::Key_Clear:				return Keys::CLEAR;
			case Qt::Key_Return:			return Keys::Return;
			case Qt::Key_Pause:				return Keys::Pause;
			case Qt::Key_Escape:			return Keys::Escape;
			case Qt::Key_Space:				return Keys::Space;
			case Qt::Key_Exclam:			return Keys::EXCLAIM;
			case Qt::Key_QuoteDbl:			return Keys::QuoteDBL;
			case Qt::Key_NumberSign:		return Keys::HASH;
			case Qt::Key_Dollar:			return Keys::DOLLAR;
			case Qt::Key_Ampersand:			return Keys::AMPERSAND;
			case Qt::Key_Apostrophe:		return Keys::Quote; // TEST
			case Qt::Key_ParenLeft:			return Keys::LeftPAREN;
			case Qt::Key_ParenRight:		return Keys::RightPAREN;
			case Qt::Key_Asterisk:			return Keys::ASTERISK;
			case Qt::Key_Plus:				return Keys::Plus;
			case Qt::Key_Comma:				return Keys::Comma;
			case Qt::Key_Minus:				return Keys::Minus;
			case Qt::Key_Period:			return Keys::Period;
			case Qt::Key_Slash:				return Keys::Slash;
			case Qt::Key_0:					return Keys::D0;
			case Qt::Key_1:					return Keys::D1;
			case Qt::Key_2:					return Keys::D2;
			case Qt::Key_3:					return Keys::D3;
			case Qt::Key_4:					return Keys::D4;
			case Qt::Key_5:					return Keys::D5;
			case Qt::Key_6:					return Keys::D6;
			case Qt::Key_7:					return Keys::D7;
			case Qt::Key_8:					return Keys::D8;
			case Qt::Key_9:					return Keys::D9;
			case Qt::Key_Colon:				return Keys::COLON;
			case Qt::Key_Semicolon:			return Keys::Semicolon;
			case Qt::Key_Less:				return Keys::LESS;
			case Qt::Key_Equal:				return Keys::Equals;
			case Qt::Key_Greater:			return Keys::GREATER;
			case Qt::Key_Question:			return Keys::QUESTION;
			case Qt::Key_At:				return Keys::AT;

			case Qt::Key_BracketLeft:		return Keys::LeftBracket;
			case Qt::Key_Backslash:			return Keys::Backslash;
			case Qt::Key_BracketRight:		return Keys::RightBracket;
			case Qt::Key_AsciiCircum:		return Keys::CARET; // TEST
			case Qt::Key_Underscore:		return Keys::UNDERSCORE;
			case Qt::Key_QuoteLeft:			return Keys::Backquote;
			case Qt::Key_A:					return Keys::A;
			case Qt::Key_B:					return Keys::B;
			case Qt::Key_C:					return Keys::C;
			case Qt::Key_D:					return Keys::D;
			case Qt::Key_E:					return Keys::E;
			case Qt::Key_F:					return Keys::F;
			case Qt::Key_G:					return Keys::G;
			case Qt::Key_H:					return Keys::H;
			case Qt::Key_I:					return Keys::I;
			case Qt::Key_J:					return Keys::J;
			case Qt::Key_K:					return Keys::K;
			case Qt::Key_L:					return Keys::L;
			case Qt::Key_M:					return Keys::M;
			case Qt::Key_N:					return Keys::N;
			case Qt::Key_O:					return Keys::O;
			case Qt::Key_P:					return Keys::P;
			case Qt::Key_Q:					return Keys::Q;
			case Qt::Key_R:					return Keys::R;
			case Qt::Key_S:					return Keys::S;
			case Qt::Key_T:					return Keys::T;
			case Qt::Key_U:					return Keys::U;
			case Qt::Key_V:					return Keys::V;
			case Qt::Key_W:					return Keys::W;
			case Qt::Key_X:					return Keys::X;
			case Qt::Key_Y:					return Keys::Y;
			case Qt::Key_Z:					return Keys::Z;
			case Qt::Key_Delete:			return Keys::Delete;

			case Qt::Key_Enter:				return Keys::NumPadEnter;

			case Qt::Key_Up:				return Keys::Up;
			case Qt::Key_Down:				return Keys::Down;
			case Qt::Key_Right:				return Keys::Right;
			case Qt::Key_Left:				return Keys::Left;
			case Qt::Key_Insert:			return Keys::Insert;
			case Qt::Key_Home:				return Keys::Home;
			case Qt::Key_End:				return Keys::End;
			case Qt::Key_PageUp:			return Keys::PageUp;
			case Qt::Key_PageDown:			return Keys::PageDown;

			case Qt::Key_F1:				return Keys::F1;
			case Qt::Key_F2:				return Keys::F2;
			case Qt::Key_F3:				return Keys::F3;
			case Qt::Key_F4:				return Keys::F4;
			case Qt::Key_F5:				return Keys::F5;
			case Qt::Key_F6:				return Keys::F6;
			case Qt::Key_F7:				return Keys::F7;
			case Qt::Key_F8:				return Keys::F8;
			case Qt::Key_F9:				return Keys::F9;
			case Qt::Key_F10:				return Keys::F10;
			case Qt::Key_F11:				return Keys::F11;
			case Qt::Key_F12:				return Keys::F12;
			case Qt::Key_F13:				return Keys::F13;
			case Qt::Key_F14:				return Keys::F14;
			case Qt::Key_F15:				return Keys::F15;

			case Qt::Key_NumLock:			return Keys::NumLock;
			case Qt::Key_CapsLock:			return Keys::CapsLock;
			case Qt::Key_ScrollLock:		return Keys::ScrollLock;
				//case Qt::Key_Shift:				return Keys::RShift;
			case Qt::Key_Shift:				return Keys::LShift;
				//case Qt::Key_Control:			return Keys::RCtrl;
			case Qt::Key_Control:			return Keys::LCtrl;
			case Qt::Key_AltGr:				return Keys::RAlt;
			case Qt::Key_Alt:				return Keys::LAlt;
			case Qt::Key_Super_R:			return Keys::RSuper;
			case Qt::Key_Super_L:			return Keys::LSuper;
			case Qt::Key_Mode_switch:		return Keys::MODE;
			case Qt::Key_ApplicationLeft:	return Keys::APPLICATION;

			case Qt::Key_Help:				return Keys::HELP;
			case Qt::Key_Print:				return Keys::PrintScreen;
			case Qt::Key_SysReq:			return Keys::SYSREQ;
			case Qt::Key_Menu:				return Keys::Menu;
			case Qt::Key_PowerOff:			return Keys::POWER;
			case Qt::Key_Undo:				return Keys::UNDO;

			default:					return Keys::Unknown;
		}
		// clang-format on
	}

	int Qt5Keys::keyModMaskToEnumMask(Qt::KeyboardModifiers keymod)
	{
		int result = 0;

		if (keymod != Qt::NoModifier) {
			result |= (keymod & Qt::ShiftModifier) ? KeyMod::LSHIFT : 0;
			result |= (keymod & Qt::ShiftModifier) ? KeyMod::RSHIFT : 0;
			result |= (keymod & Qt::ControlModifier) ? KeyMod::LCTRL : 0;
			result |= (keymod & Qt::ControlModifier) ? KeyMod::RCTRL : 0;
			result |= (keymod & Qt::AltModifier) ? KeyMod::LALT : 0;
			result |= (keymod & Qt::AltModifier) ? KeyMod::RALT : 0;
			result |= (keymod & Qt::MetaModifier) ? KeyMod::LSUPER : 0;
			result |= (keymod & Qt::MetaModifier) ? KeyMod::RSUPER : 0;
			result |= (keymod & Qt::KeypadModifier) ? KeyMod::NUM : 0;
			//result |= (keymod & KMOD_CAPS) ? KeyMod::CAPS : 0;
			result |= (keymod & Qt::GroupSwitchModifier) ? KeyMod::MODE : 0;
		}

		return result;
	}

	/*

	int SdlKeys::enumToKeysValue(Keys keysym)
	{
		// clang-format off
		switch (keysym)
		{
			case Keys::Unknown:			return Qt::Key_UNKNOWN;
			case Keys::Backspace:			return Qt::Key_BACKSPACE;
			case Keys::Tab:				return Qt::Key_TAB;
			case Keys::CLEAR:				return Qt::Key_CLEAR;
			case Keys::Return:			return Qt::Key_RETURN;
			case Keys::Pause:				return Qt::Key_PAUSE;
			case Keys::Escape:			return Qt::Key_ESCAPE;
			case Keys::Space:				return Qt::Key_SPACE;
			case Keys::EXCLAIM:			return Qt::Key_EXCLAIM;
			case Keys::QuoteDBL:			return Qt::Key_QUOTEDBL;
			case Keys::HASH:				return Qt::Key_HASH;
			case Keys::DOLLAR:			return Qt::Key_DOLLAR;
			case Keys::AMPERSAND:			return Qt::Key_AMPERSAND;
			case Keys::Quote:				return Qt::Key_QUOTE;
			case Keys::LeftPAREN:			return Qt::Key_LEFTPAREN;
			case Keys::RightPAREN:		return Qt::Key_RIGHTPAREN;
			case Keys::ASTERISK:			return Qt::Key_ASTERISK;
			case Keys::Plus:				return Qt::Key_PLUS;
			case Keys::Comma:				return Qt::Key_COMMA;
			case Keys::Minus:				return Qt::Key_MINUS;
			case Keys::Period:			return Qt::Key_PERIOD;
			case Keys::Slash:				return Qt::Key_SLASH;
			case Keys::D0:				return Qt::Key_0;
			case Keys::D1:				return Qt::Key_1;
			case Keys::D2:				return Qt::Key_2;
			case Keys::D3:				return Qt::Key_3;
			case Keys::D4:				return Qt::Key_4;
			case Keys::D5:				return Qt::Key_5;
			case Keys::D6:				return Qt::Key_6;
			case Keys::D7:				return Qt::Key_7;
			case Keys::D8:				return Qt::Key_8;
			case Keys::D9:				return Qt::Key_9;
			case Keys::COLON:				return Qt::Key_COLON;
			case Keys::Semicolon:			return Qt::Key_SEMICOLON;
			case Keys::LESS:				return Qt::Key_LESS;
			case Keys::Equals:			return Qt::Key_EQUALS;
			case Keys::GREATER:			return Qt::Key_GREATER;
			case Keys::QUESTION:			return Qt::Key_QUESTION;
			case Keys::AT:				return Qt::Key_AT;

			case Keys::LeftBracket:		return Qt::Key_LEFTBRACKET;
			case Keys::Backslash:			return Qt::Key_BACKSLASH;
			case Keys::RightBracket:		return Qt::Key_RIGHTBRACKET;
			case Keys::CARET:				return Qt::Key_CARET;
			case Keys::UNDERSCORE:		return Qt::Key_UNDERSCORE;
			case Keys::Backquote:			return Qt::Key_BACKQUOTE;
			case Keys::A:					return Qt::Key_a;
			case Keys::B:					return Qt::Key_b;
			case Keys::C:					return Qt::Key_c;
			case Keys::D:					return Qt::Key_d;
			case Keys::E:					return Qt::Key_e;
			case Keys::F:					return Qt::Key_f;
			case Keys::G:					return Qt::Key_g;
			case Keys::H:					return Qt::Key_h;
			case Keys::I:					return Qt::Key_i;
			case Keys::J:					return Qt::Key_j;
			case Keys::K:					return Qt::Key_k;
			case Keys::L:					return Qt::Key_l;
			case Keys::M:					return Qt::Key_m;
			case Keys::N:					return Qt::Key_n;
			case Keys::O:					return Qt::Key_o;
			case Keys::P:					return Qt::Key_p;
			case Keys::Q:					return Qt::Key_q;
			case Keys::R:					return Qt::Key_r;
			case Keys::S:					return Qt::Key_s;
			case Keys::T:					return Qt::Key_t;
			case Keys::U:					return Qt::Key_u;
			case Keys::V:					return Qt::Key_v;
			case Keys::W:					return Qt::Key_w;
			case Keys::X:					return Qt::Key_x;
			case Keys::Y:					return Qt::Key_y;
			case Keys::Z:					return Qt::Key_z;
			case Keys::Delete:			return Qt::Key_DELETE;

			case Keys::NumPad0:				return Qt::Key_KP_0;
			case Keys::NumPad1:				return Qt::Key_KP_1;
			case Keys::NumPad2:				return Qt::Key_KP_2;
			case Keys::NumPad3:				return Qt::Key_KP_3;
			case Keys::NumPad4:				return Qt::Key_KP_4;
			case Keys::NumPad5:				return Qt::Key_KP_5;
			case Keys::NumPad6:				return Qt::Key_KP_6;
			case Keys::NumPad7:				return Qt::Key_KP_7;
			case Keys::NumPad8:				return Qt::Key_KP_8;
			case Keys::NumPad9:				return Qt::Key_KP_9;
			case Keys::NumPadPeriod:			return Qt::Key_KP_PERIOD;
			case Keys::NumPadDivide:			return Qt::Key_KP_DIVIDE;
			case Keys::NumPadMultiply:		return Qt::Key_KP_MULTIPLY;
			case Keys::NumPadMinus:			return Qt::Key_KP_MINUS;
			case Keys::NumPadPlus:			return Qt::Key_KP_PLUS;
			case Keys::NumPadEnter:			return Qt::Key_KP_ENTER;
			case Keys::NumPadEquals:			return Qt::Key_KP_EQUALS;

			case Keys::Up:				return Qt::Key_UP;
			case Keys::Down:				return Qt::Key_DOWN;
			case Keys::Right:				return Qt::Key_RIGHT;
			case Keys::Left:				return Qt::Key_LEFT;
			case Keys::Insert:			return Qt::Key_INSERT;
			case Keys::Home:				return Qt::Key_HOME;
			case Keys::End:				return Qt::Key_END;
			case Keys::PageUp:			return Qt::Key_PAGEUP;
			case Keys::PageDown:			return Qt::Key_PAGEDOWN;

			case Keys::F1:				return Qt::Key_F1;
			case Keys::F2:				return Qt::Key_F2;
			case Keys::F3:				return Qt::Key_F3;
			case Keys::F4:				return Qt::Key_F4;
			case Keys::F5:				return Qt::Key_F5;
			case Keys::F6:				return Qt::Key_F6;
			case Keys::F7:				return Qt::Key_F7;
			case Keys::F8:				return Qt::Key_F8;
			case Keys::F9:				return Qt::Key_F9;
			case Keys::F10:				return Qt::Key_F10;
			case Keys::F11:				return Qt::Key_F11;
			case Keys::F12:				return Qt::Key_F12;
			case Keys::F13:				return Qt::Key_F13;
			case Keys::F14:				return Qt::Key_F14;
			case Keys::F15:				return Qt::Key_F15;

			case Keys::NumLock:			return Qt::Key_NUMLOCKCLEAR;
			case Keys::CapsLock:			return Qt::Key_CAPSLOCK;
			case Keys::ScrollLock:		return Qt::Key_SCROLLLOCK;
			case Keys::RShift:			return Qt::Key_RSHIFT;
			case Keys::LShift:			return Qt::Key_LSHIFT;
			case Keys::RCtrl:				return Qt::Key_RCTRL;
			case Keys::LCtrl:				return Qt::Key_LCTRL;
			case Keys::RAlt:				return Qt::Key_RALT;
			case Keys::LAlt:				return Qt::Key_LALT;
			case Keys::RSuper:			return Qt::Key_RGUI;
			case Keys::LSuper:			return Qt::Key_LGUI;
			case Keys::MODE:				return Qt::Key_MODE;
			case Keys::APPLICATION:		return Qt::Key_APPLICATION;

			case Keys::HELP:				return Qt::Key_HELP;
			case Keys::PrintScreen:		return Qt::Key_PRINTSCREEN;
			case Keys::SYSREQ:			return Qt::Key_SYSREQ;
			case Keys::Menu:				return Qt::Key_MENU;
			case Keys::POWER:				return Qt::Key_POWER;
			case Keys::UNDO:				return Qt::Key_UNDO;

			default:						return Qt::Key_UNKNOWN;
		}
		// clang-format on
	}

	int SdlKeys::enumToScancode(Keys keysym)
	{
		// clang-format off
		switch (keysym)
		{
			case Keys::Unknown:			return SDL_SCANCODE_UNKNOWN;
			case Keys::Backspace:			return SDL_SCANCODE_BACKSPACE;
			case Keys::Tab:				return SDL_SCANCODE_TAB;
			case Keys::CLEAR:				return SDL_SCANCODE_CLEAR;
			case Keys::Return:			return SDL_SCANCODE_RETURN;
			case Keys::Pause:				return SDL_SCANCODE_PAUSE;
			case Keys::Escape:			return SDL_SCANCODE_ESCAPE;
			case Keys::Space:				return SDL_SCANCODE_SPACE;
			case Keys::EXCLAIM:			return SDL_SCANCODE_1; // not a scancode
			case Keys::QuoteDBL:			return SDL_SCANCODE_APOSTROPHE; // not a scancode
			case Keys::HASH:				return SDL_SCANCODE_3; // not a scancode
			case Keys::DOLLAR:			return SDL_SCANCODE_4; // not a scancode
			case Keys::AMPERSAND:			return SDL_SCANCODE_7; // not a scancode
			case Keys::Quote:				return SDL_SCANCODE_APOSTROPHE; // not a scancode
			case Keys::LeftPAREN:			return SDL_SCANCODE_9; // not a scancode
			case Keys::RightPAREN:		return SDL_SCANCODE_0; // not a scancode
			case Keys::ASTERISK:			return SDL_SCANCODE_8; // not a scancode
			case Keys::Plus:				return SDL_SCANCODE_EQUALS; // not a scancode
			case Keys::Comma:				return SDL_SCANCODE_COMMA;
			case Keys::Minus:				return SDL_SCANCODE_MINUS;
			case Keys::Period:			return SDL_SCANCODE_PERIOD;
			case Keys::Slash:				return SDL_SCANCODE_SLASH;
			case Keys::D0:				return SDL_SCANCODE_0;
			case Keys::D1:				return SDL_SCANCODE_1;
			case Keys::D2:				return SDL_SCANCODE_2;
			case Keys::D3:				return SDL_SCANCODE_3;
			case Keys::D4:				return SDL_SCANCODE_4;
			case Keys::D5:				return SDL_SCANCODE_5;
			case Keys::D6:				return SDL_SCANCODE_6;
			case Keys::D7:				return SDL_SCANCODE_7;
			case Keys::D8:				return SDL_SCANCODE_8;
			case Keys::D9:				return SDL_SCANCODE_9;
			case Keys::COLON:				return SDL_SCANCODE_SEMICOLON; // not a scancode
			case Keys::Semicolon:			return SDL_SCANCODE_SEMICOLON;
			case Keys::LESS:				return SDL_SCANCODE_COMMA; // not a scancode
			case Keys::Equals:			return SDL_SCANCODE_EQUALS;
			case Keys::GREATER:			return SDL_SCANCODE_PERIOD; // not a scancode
			case Keys::QUESTION:			return SDL_SCANCODE_SLASH; // not a scancode
			case Keys::AT:				return SDL_SCANCODE_2; // not a scancode

			case Keys::LeftBracket:		return SDL_SCANCODE_LEFTBRACKET;
			case Keys::Backslash:			return SDL_SCANCODE_BACKSLASH;
			case Keys::RightBracket:		return SDL_SCANCODE_RIGHTBRACKET;
			case Keys::CARET:				return SDL_SCANCODE_6; // not a scancode
			case Keys::UNDERSCORE:		return SDL_SCANCODE_MINUS; // not a scancode
			case Keys::Backquote:			return SDL_SCANCODE_GRAVE; // not a scancode
			case Keys::A:					return SDL_SCANCODE_A;
			case Keys::B:					return SDL_SCANCODE_B;
			case Keys::C:					return SDL_SCANCODE_C;
			case Keys::D:					return SDL_SCANCODE_D;
			case Keys::E:					return SDL_SCANCODE_E;
			case Keys::F:					return SDL_SCANCODE_F;
			case Keys::G:					return SDL_SCANCODE_G;
			case Keys::H:					return SDL_SCANCODE_H;
			case Keys::I:					return SDL_SCANCODE_I;
			case Keys::J:					return SDL_SCANCODE_J;
			case Keys::K:					return SDL_SCANCODE_K;
			case Keys::L:					return SDL_SCANCODE_L;
			case Keys::M:					return SDL_SCANCODE_M;
			case Keys::N:					return SDL_SCANCODE_N;
			case Keys::O:					return SDL_SCANCODE_O;
			case Keys::P:					return SDL_SCANCODE_P;
			case Keys::Q:					return SDL_SCANCODE_Q;
			case Keys::R:					return SDL_SCANCODE_R;
			case Keys::S:					return SDL_SCANCODE_S;
			case Keys::T:					return SDL_SCANCODE_T;
			case Keys::U:					return SDL_SCANCODE_U;
			case Keys::V:					return SDL_SCANCODE_V;
			case Keys::W:					return SDL_SCANCODE_W;
			case Keys::X:					return SDL_SCANCODE_X;
			case Keys::Y:					return SDL_SCANCODE_Y;
			case Keys::Z:					return SDL_SCANCODE_Z;
			case Keys::Delete:			return SDL_SCANCODE_DELETE;

			case Keys::NumPad0:				return SDL_SCANCODE_KP_0;
			case Keys::NumPad1:				return SDL_SCANCODE_KP_1;
			case Keys::NumPad2:				return SDL_SCANCODE_KP_2;
			case Keys::NumPad3:				return SDL_SCANCODE_KP_3;
			case Keys::NumPad4:				return SDL_SCANCODE_KP_4;
			case Keys::NumPad5:				return SDL_SCANCODE_KP_5;
			case Keys::NumPad6:				return SDL_SCANCODE_KP_6;
			case Keys::NumPad7:				return SDL_SCANCODE_KP_7;
			case Keys::NumPad8:				return SDL_SCANCODE_KP_8;
			case Keys::NumPad9:				return SDL_SCANCODE_KP_9;
			case Keys::NumPadPeriod:			return SDL_SCANCODE_KP_PERIOD;
			case Keys::NumPadDivide:			return SDL_SCANCODE_KP_DIVIDE;
			case Keys::NumPadMultiply:		return SDL_SCANCODE_KP_MULTIPLY;
			case Keys::NumPadMinus:			return SDL_SCANCODE_KP_MINUS;
			case Keys::NumPadPlus:			return SDL_SCANCODE_KP_PLUS;
			case Keys::NumPadEnter:			return SDL_SCANCODE_KP_ENTER;
			case Keys::NumPadEquals:			return SDL_SCANCODE_KP_EQUALS;

			case Keys::Up:				return SDL_SCANCODE_UP;
			case Keys::Down:				return SDL_SCANCODE_DOWN;
			case Keys::Right:				return SDL_SCANCODE_RIGHT;
			case Keys::Left:				return SDL_SCANCODE_LEFT;
			case Keys::Insert:			return SDL_SCANCODE_INSERT;
			case Keys::Home:				return SDL_SCANCODE_HOME;
			case Keys::End:				return SDL_SCANCODE_END;
			case Keys::PageUp:			return SDL_SCANCODE_PAGEUP;
			case Keys::PageDown:			return SDL_SCANCODE_PAGEDOWN;

			case Keys::F1:				return SDL_SCANCODE_F1;
			case Keys::F2:				return SDL_SCANCODE_F2;
			case Keys::F3:				return SDL_SCANCODE_F3;
			case Keys::F4:				return SDL_SCANCODE_F4;
			case Keys::F5:				return SDL_SCANCODE_F5;
			case Keys::F6:				return SDL_SCANCODE_F6;
			case Keys::F7:				return SDL_SCANCODE_F7;
			case Keys::F8:				return SDL_SCANCODE_F8;
			case Keys::F9:				return SDL_SCANCODE_F9;
			case Keys::F10:				return SDL_SCANCODE_F10;
			case Keys::F11:				return SDL_SCANCODE_F11;
			case Keys::F12:				return SDL_SCANCODE_F12;
			case Keys::F13:				return SDL_SCANCODE_F13;
			case Keys::F14:				return SDL_SCANCODE_F14;
			case Keys::F15:				return SDL_SCANCODE_F15;

			case Keys::NumLock:			return SDL_SCANCODE_NUMLOCKCLEAR;
			case Keys::CapsLock:			return SDL_SCANCODE_CAPSLOCK;
			case Keys::ScrollLock:		return SDL_SCANCODE_SCROLLLOCK;
			case Keys::RShift:			return SDL_SCANCODE_RSHIFT;
			case Keys::LShift:			return SDL_SCANCODE_LSHIFT;
			case Keys::RCtrl:				return SDL_SCANCODE_RCTRL;
			case Keys::LCtrl:				return SDL_SCANCODE_LCTRL;
			case Keys::RAlt:				return SDL_SCANCODE_RALT;
			case Keys::LAlt:				return SDL_SCANCODE_LALT;
			case Keys::RSuper:			return SDL_SCANCODE_RGUI;
			case Keys::LSuper:			return SDL_SCANCODE_LGUI;
			case Keys::MODE:				return SDL_SCANCODE_MODE;
			case Keys::APPLICATION:		return SDL_SCANCODE_APPLICATION;

			case Keys::HELP:				return SDL_SCANCODE_HELP;
			case Keys::PrintScreen:		return SDL_SCANCODE_PRINTSCREEN;
			case Keys::SYSREQ:			return SDL_SCANCODE_SYSREQ;
			case Keys::Menu:				return SDL_SCANCODE_MENU;
			case Keys::POWER:				return SDL_SCANCODE_POWER;
			case Keys::UNDO:				return SDL_SCANCODE_UNDO;

			default:						return SDL_SCANCODE_UNKNOWN;
		}
		// clang-format on
	}
	*/
}

#endif