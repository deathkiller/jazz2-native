#if defined(WITH_SDL)

#include "SdlInputManager.h"

namespace nCine::Backends
{
	Keys SdlKeys::keySymValueToEnum(int keysym)
	{
		// clang-format off
		switch (keysym) {
			case SDLK_UNKNOWN:			return Keys::Unknown;
			case SDLK_BACKSPACE:		return Keys::Backspace;
			case SDLK_TAB:				return Keys::Tab;
			case SDLK_CLEAR:			return Keys::Clear;
			case SDLK_RETURN:			return Keys::Return;
			case SDLK_PAUSE:			return Keys::Pause;
			case SDLK_ESCAPE:			return Keys::Escape;
			case SDLK_SPACE:			return Keys::Space;
			case SDLK_EXCLAIM:			return Keys::Exclaim;
			case SDLK_QUOTEDBL:			return Keys::QuoteDbl;
			case SDLK_HASH:				return Keys::Hash;
			case SDLK_DOLLAR:			return Keys::Dollar;
			case SDLK_AMPERSAND:		return Keys::Ampersand;
			case SDLK_QUOTE:			return Keys::Quote;
			case SDLK_LEFTPAREN:		return Keys::LeftParen;
			case SDLK_RIGHTPAREN:		return Keys::RightParen;
			case SDLK_ASTERISK:			return Keys::Asterisk;
			case SDLK_PLUS:				return Keys::Plus;
			case SDLK_COMMA:			return Keys::Comma;
			case SDLK_MINUS:			return Keys::Minus;
			case SDLK_PERIOD:			return Keys::Period;
			case SDLK_SLASH:			return Keys::Slash;
			case SDLK_0:				return Keys::D0;
			case SDLK_1:				return Keys::D1;
			case SDLK_2:				return Keys::D2;
			case SDLK_3:				return Keys::D3;
			case SDLK_4:				return Keys::D4;
			case SDLK_5:				return Keys::D5;
			case SDLK_6:				return Keys::D6;
			case SDLK_7:				return Keys::D7;
			case SDLK_8:				return Keys::D8;
			case SDLK_9:				return Keys::D9;
			case SDLK_COLON:			return Keys::Colon;
			case SDLK_SEMICOLON:		return Keys::Semicolon;
			case SDLK_LESS:				return Keys::Less;
			case SDLK_EQUALS:			return Keys::Equals;
			case SDLK_GREATER:			return Keys::Greater;
			case SDLK_QUESTION:			return Keys::Question;
			case SDLK_AT:				return Keys::At;

			case SDLK_LEFTBRACKET:		return Keys::LeftBracket;
			case SDLK_BACKSLASH:		return Keys::Backslash;
			case SDLK_RIGHTBRACKET:		return Keys::RightBracket;
			case SDLK_CARET:			return Keys::Caret;
			case SDLK_UNDERSCORE:		return Keys::Underscore;
			case SDLK_BACKQUOTE:		return Keys::Backquote;
			case SDLK_a:				return Keys::A;
			case SDLK_b:				return Keys::B;
			case SDLK_c:				return Keys::C;
			case SDLK_d:				return Keys::D;
			case SDLK_e:				return Keys::E;
			case SDLK_f:				return Keys::F;
			case SDLK_g:				return Keys::G;
			case SDLK_h:				return Keys::H;
			case SDLK_i:				return Keys::I;
			case SDLK_j:				return Keys::J;
			case SDLK_k:				return Keys::K;
			case SDLK_l:				return Keys::L;
			case SDLK_m:				return Keys::M;
			case SDLK_n:				return Keys::N;
			case SDLK_o:				return Keys::O;
			case SDLK_p:				return Keys::P;
			case SDLK_q:				return Keys::Q;
			case SDLK_r:				return Keys::R;
			case SDLK_s:				return Keys::S;
			case SDLK_t:				return Keys::T;
			case SDLK_u:				return Keys::U;
			case SDLK_v:				return Keys::V;
			case SDLK_w:				return Keys::W;
			case SDLK_x:				return Keys::X;
			case SDLK_y:				return Keys::Y;
			case SDLK_z:				return Keys::Z;
			case SDLK_DELETE:			return Keys::Delete;

			case SDLK_KP_0:				return Keys::NumPad0;
			case SDLK_KP_1:				return Keys::NumPad1;
			case SDLK_KP_2:				return Keys::NumPad2;
			case SDLK_KP_3:				return Keys::NumPad3;
			case SDLK_KP_4:				return Keys::NumPad4;
			case SDLK_KP_5:				return Keys::NumPad5;
			case SDLK_KP_6:				return Keys::NumPad6;
			case SDLK_KP_7:				return Keys::NumPad7;
			case SDLK_KP_8:				return Keys::NumPad8;
			case SDLK_KP_9:				return Keys::NumPad9;
			case SDLK_KP_PERIOD:		return Keys::NumPadPeriod;
			case SDLK_KP_DIVIDE:		return Keys::NumPadDivide;
			case SDLK_KP_MULTIPLY:		return Keys::NumPadMultiply;
			case SDLK_KP_MINUS:			return Keys::NumPadMinus;
			case SDLK_KP_PLUS:			return Keys::NumPadPlus;
			case SDLK_KP_ENTER:			return Keys::NumPadEnter;
			case SDLK_KP_EQUALS:		return Keys::NumPadEquals;

			case SDLK_UP:				return Keys::Up;
			case SDLK_DOWN:				return Keys::Down;
			case SDLK_RIGHT:			return Keys::Right;
			case SDLK_LEFT:				return Keys::Left;
			case SDLK_INSERT:			return Keys::Insert;
			case SDLK_HOME:				return Keys::Home;
			case SDLK_END:				return Keys::End;
			case SDLK_PAGEUP:			return Keys::PageUp;
			case SDLK_PAGEDOWN:			return Keys::PageDown;

			case SDLK_F1:				return Keys::F1;
			case SDLK_F2:				return Keys::F2;
			case SDLK_F3:				return Keys::F3;
			case SDLK_F4:				return Keys::F4;
			case SDLK_F5:				return Keys::F5;
			case SDLK_F6:				return Keys::F6;
			case SDLK_F7:				return Keys::F7;
			case SDLK_F8:				return Keys::F8;
			case SDLK_F9:				return Keys::F9;
			case SDLK_F10:				return Keys::F10;
			case SDLK_F11:				return Keys::F11;
			case SDLK_F12:				return Keys::F12;
			case SDLK_F13:				return Keys::F13;
			case SDLK_F14:				return Keys::F14;
			case SDLK_F15:				return Keys::F15;

			case SDLK_NUMLOCKCLEAR:		return Keys::NumLock;
			case SDLK_CAPSLOCK:			return Keys::CapsLock;
			case SDLK_SCROLLLOCK:		return Keys::ScrollLock;
			case SDLK_RSHIFT:			return Keys::RShift;
			case SDLK_LSHIFT:			return Keys::LShift;
			case SDLK_RCTRL:			return Keys::RCtrl;
			case SDLK_LCTRL:			return Keys::LCtrl;
			case SDLK_RALT:				return Keys::RAlt;
			case SDLK_LALT:				return Keys::LAlt;
			case SDLK_RGUI:				return Keys::RSuper;
			case SDLK_LGUI:				return Keys::LSuper;
			case SDLK_MODE:				return Keys::Mode;
			case SDLK_APPLICATION:		return Keys::Application;

			case SDLK_HELP:				return Keys::Help;
			case SDLK_PRINTSCREEN:		return Keys::PrintScreen;
			case SDLK_SYSREQ:			return Keys::SysReq;
			case SDLK_MENU:				return Keys::Menu;
			case SDLK_POWER:			return Keys::Power;
			case SDLK_UNDO:				return Keys::Undo;

			default:					return Keys::Unknown;
		}
		// clang-format on
	}

	int SdlKeys::keyModMaskToEnumMask(int keymod)
	{
		int result = 0;

		if (keymod != 0) {
			result |= (keymod & KMOD_LSHIFT) ? KeyMod::LShift : 0;
			result |= (keymod & KMOD_RSHIFT) ? KeyMod::RShift : 0;
			result |= (keymod & KMOD_LCTRL) ? KeyMod::LCtrl : 0;
			result |= (keymod & KMOD_RCTRL) ? KeyMod::RCtrl : 0;
			result |= (keymod & KMOD_LALT) ? KeyMod::LAlt : 0;
			result |= (keymod & KMOD_RALT) ? KeyMod::RAlt : 0;
			result |= (keymod & KMOD_LGUI) ? KeyMod::LSuper : 0;
			result |= (keymod & KMOD_RGUI) ? KeyMod::RSuper : 0;
			result |= (keymod & KMOD_NUM) ? KeyMod::NumLock : 0;
			result |= (keymod & KMOD_CAPS) ? KeyMod::CapsLock : 0;
			result |= (keymod & KMOD_MODE) ? KeyMod::Mode : 0;
		}

		return result;
	}

	int SdlKeys::enumToKeysValue(Keys keysym)
	{
		// clang-format off
		switch (keysym) {
			case Keys::Unknown:				return SDLK_UNKNOWN;
			case Keys::Backspace:			return SDLK_BACKSPACE;
			case Keys::Tab:					return SDLK_TAB;
			case Keys::Clear:				return SDLK_CLEAR;
			case Keys::Return:				return SDLK_RETURN;
			case Keys::Pause:				return SDLK_PAUSE;
			case Keys::Escape:				return SDLK_ESCAPE;
			case Keys::Space:				return SDLK_SPACE;
			case Keys::Exclaim:				return SDLK_EXCLAIM;
			case Keys::QuoteDbl:			return SDLK_QUOTEDBL;
			case Keys::Hash:				return SDLK_HASH;
			case Keys::Dollar:				return SDLK_DOLLAR;
			case Keys::Ampersand:			return SDLK_AMPERSAND;
			case Keys::Quote:				return SDLK_QUOTE;
			case Keys::LeftParen:			return SDLK_LEFTPAREN;
			case Keys::RightParen:			return SDLK_RIGHTPAREN;
			case Keys::Asterisk:			return SDLK_ASTERISK;
			case Keys::Plus:				return SDLK_PLUS;
			case Keys::Comma:				return SDLK_COMMA;
			case Keys::Minus:				return SDLK_MINUS;
			case Keys::Period:				return SDLK_PERIOD;
			case Keys::Slash:				return SDLK_SLASH;
			case Keys::D0:					return SDLK_0;
			case Keys::D1:					return SDLK_1;
			case Keys::D2:					return SDLK_2;
			case Keys::D3:					return SDLK_3;
			case Keys::D4:					return SDLK_4;
			case Keys::D5:					return SDLK_5;
			case Keys::D6:					return SDLK_6;
			case Keys::D7:					return SDLK_7;
			case Keys::D8:					return SDLK_8;
			case Keys::D9:					return SDLK_9;
			case Keys::Colon:				return SDLK_COLON;
			case Keys::Semicolon:			return SDLK_SEMICOLON;
			case Keys::Less:				return SDLK_LESS;
			case Keys::Equals:				return SDLK_EQUALS;
			case Keys::Greater:				return SDLK_GREATER;
			case Keys::Question:			return SDLK_QUESTION;
			case Keys::At:					return SDLK_AT;

			case Keys::LeftBracket:			return SDLK_LEFTBRACKET;
			case Keys::Backslash:			return SDLK_BACKSLASH;
			case Keys::RightBracket:		return SDLK_RIGHTBRACKET;
			case Keys::Caret:				return SDLK_CARET;
			case Keys::Underscore:			return SDLK_UNDERSCORE;
			case Keys::Backquote:			return SDLK_BACKQUOTE;
			case Keys::A:					return SDLK_a;
			case Keys::B:					return SDLK_b;
			case Keys::C:					return SDLK_c;
			case Keys::D:					return SDLK_d;
			case Keys::E:					return SDLK_e;
			case Keys::F:					return SDLK_f;
			case Keys::G:					return SDLK_g;
			case Keys::H:					return SDLK_h;
			case Keys::I:					return SDLK_i;
			case Keys::J:					return SDLK_j;
			case Keys::K:					return SDLK_k;
			case Keys::L:					return SDLK_l;
			case Keys::M:					return SDLK_m;
			case Keys::N:					return SDLK_n;
			case Keys::O:					return SDLK_o;
			case Keys::P:					return SDLK_p;
			case Keys::Q:					return SDLK_q;
			case Keys::R:					return SDLK_r;
			case Keys::S:					return SDLK_s;
			case Keys::T:					return SDLK_t;
			case Keys::U:					return SDLK_u;
			case Keys::V:					return SDLK_v;
			case Keys::W:					return SDLK_w;
			case Keys::X:					return SDLK_x;
			case Keys::Y:					return SDLK_y;
			case Keys::Z:					return SDLK_z;
			case Keys::Delete:				return SDLK_DELETE;

			case Keys::NumPad0:				return SDLK_KP_0;
			case Keys::NumPad1:				return SDLK_KP_1;
			case Keys::NumPad2:				return SDLK_KP_2;
			case Keys::NumPad3:				return SDLK_KP_3;
			case Keys::NumPad4:				return SDLK_KP_4;
			case Keys::NumPad5:				return SDLK_KP_5;
			case Keys::NumPad6:				return SDLK_KP_6;
			case Keys::NumPad7:				return SDLK_KP_7;
			case Keys::NumPad8:				return SDLK_KP_8;
			case Keys::NumPad9:				return SDLK_KP_9;
			case Keys::NumPadPeriod:		return SDLK_KP_PERIOD;
			case Keys::NumPadDivide:		return SDLK_KP_DIVIDE;
			case Keys::NumPadMultiply:		return SDLK_KP_MULTIPLY;
			case Keys::NumPadMinus:			return SDLK_KP_MINUS;
			case Keys::NumPadPlus:			return SDLK_KP_PLUS;
			case Keys::NumPadEnter:			return SDLK_KP_ENTER;
			case Keys::NumPadEquals:		return SDLK_KP_EQUALS;

			case Keys::Up:					return SDLK_UP;
			case Keys::Down:				return SDLK_DOWN;
			case Keys::Right:				return SDLK_RIGHT;
			case Keys::Left:				return SDLK_LEFT;
			case Keys::Insert:				return SDLK_INSERT;
			case Keys::Home:				return SDLK_HOME;
			case Keys::End:					return SDLK_END;
			case Keys::PageUp:				return SDLK_PAGEUP;
			case Keys::PageDown:			return SDLK_PAGEDOWN;

			case Keys::F1:					return SDLK_F1;
			case Keys::F2:					return SDLK_F2;
			case Keys::F3:					return SDLK_F3;
			case Keys::F4:					return SDLK_F4;
			case Keys::F5:					return SDLK_F5;
			case Keys::F6:					return SDLK_F6;
			case Keys::F7:					return SDLK_F7;
			case Keys::F8:					return SDLK_F8;
			case Keys::F9:					return SDLK_F9;
			case Keys::F10:					return SDLK_F10;
			case Keys::F11:					return SDLK_F11;
			case Keys::F12:					return SDLK_F12;
			case Keys::F13:					return SDLK_F13;
			case Keys::F14:					return SDLK_F14;
			case Keys::F15:					return SDLK_F15;

			case Keys::NumLock:				return SDLK_NUMLOCKCLEAR;
			case Keys::CapsLock:			return SDLK_CAPSLOCK;
			case Keys::ScrollLock:			return SDLK_SCROLLLOCK;
			case Keys::RShift:				return SDLK_RSHIFT;
			case Keys::LShift:				return SDLK_LSHIFT;
			case Keys::RCtrl:				return SDLK_RCTRL;
			case Keys::LCtrl:				return SDLK_LCTRL;
			case Keys::RAlt:				return SDLK_RALT;
			case Keys::LAlt:				return SDLK_LALT;
			case Keys::RSuper:				return SDLK_RGUI;
			case Keys::LSuper:				return SDLK_LGUI;
			case Keys::Mode:				return SDLK_MODE;
			case Keys::Application:			return SDLK_APPLICATION;

			case Keys::Help:				return SDLK_HELP;
			case Keys::PrintScreen:			return SDLK_PRINTSCREEN;
			case Keys::SysReq:				return SDLK_SYSREQ;
			case Keys::Menu:				return SDLK_MENU;
			case Keys::Power:				return SDLK_POWER;
			case Keys::Undo:				return SDLK_UNDO;

			default:						return SDLK_UNKNOWN;
		}
		// clang-format on
	}

	int SdlKeys::enumToScancode(Keys keysym)
	{
		// clang-format off
		switch (keysym) {
			case Keys::Unknown:				return SDL_SCANCODE_UNKNOWN;
			case Keys::Backspace:			return SDL_SCANCODE_BACKSPACE;
			case Keys::Tab:					return SDL_SCANCODE_TAB;
			case Keys::Clear:				return SDL_SCANCODE_CLEAR;
			case Keys::Return:				return SDL_SCANCODE_RETURN;
			case Keys::Pause:				return SDL_SCANCODE_PAUSE;
			case Keys::Escape:				return SDL_SCANCODE_ESCAPE;
			case Keys::Space:				return SDL_SCANCODE_SPACE;
			case Keys::Exclaim:				return SDL_SCANCODE_1; // not a scancode
			case Keys::QuoteDbl:			return SDL_SCANCODE_APOSTROPHE; // not a scancode
			case Keys::Hash:				return SDL_SCANCODE_3; // not a scancode
			case Keys::Dollar:				return SDL_SCANCODE_4; // not a scancode
			case Keys::Ampersand:			return SDL_SCANCODE_7; // not a scancode
			case Keys::Quote:				return SDL_SCANCODE_APOSTROPHE; // not a scancode
			case Keys::LeftParen:			return SDL_SCANCODE_9; // not a scancode
			case Keys::RightParen:			return SDL_SCANCODE_0; // not a scancode
			case Keys::Asterisk:			return SDL_SCANCODE_8; // not a scancode
			case Keys::Plus:				return SDL_SCANCODE_EQUALS; // not a scancode
			case Keys::Comma:				return SDL_SCANCODE_COMMA;
			case Keys::Minus:				return SDL_SCANCODE_MINUS;
			case Keys::Period:				return SDL_SCANCODE_PERIOD;
			case Keys::Slash:				return SDL_SCANCODE_SLASH;
			case Keys::D0:					return SDL_SCANCODE_0;
			case Keys::D1:					return SDL_SCANCODE_1;
			case Keys::D2:					return SDL_SCANCODE_2;
			case Keys::D3:					return SDL_SCANCODE_3;
			case Keys::D4:					return SDL_SCANCODE_4;
			case Keys::D5:					return SDL_SCANCODE_5;
			case Keys::D6:					return SDL_SCANCODE_6;
			case Keys::D7:					return SDL_SCANCODE_7;
			case Keys::D8:					return SDL_SCANCODE_8;
			case Keys::D9:					return SDL_SCANCODE_9;
			case Keys::Colon:				return SDL_SCANCODE_SEMICOLON; // not a scancode
			case Keys::Semicolon:			return SDL_SCANCODE_SEMICOLON;
			case Keys::Less:				return SDL_SCANCODE_COMMA; // not a scancode
			case Keys::Equals:				return SDL_SCANCODE_EQUALS;
			case Keys::Greater:				return SDL_SCANCODE_PERIOD; // not a scancode
			case Keys::Question:			return SDL_SCANCODE_SLASH; // not a scancode
			case Keys::At:					return SDL_SCANCODE_2; // not a scancode

			case Keys::LeftBracket:			return SDL_SCANCODE_LEFTBRACKET;
			case Keys::Backslash:			return SDL_SCANCODE_BACKSLASH;
			case Keys::RightBracket:		return SDL_SCANCODE_RIGHTBRACKET;
			case Keys::Caret:				return SDL_SCANCODE_6; // not a scancode
			case Keys::Underscore:			return SDL_SCANCODE_MINUS; // not a scancode
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
			case Keys::Delete:				return SDL_SCANCODE_DELETE;

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
			case Keys::NumPadPeriod:		return SDL_SCANCODE_KP_PERIOD;
			case Keys::NumPadDivide:		return SDL_SCANCODE_KP_DIVIDE;
			case Keys::NumPadMultiply:		return SDL_SCANCODE_KP_MULTIPLY;
			case Keys::NumPadMinus:			return SDL_SCANCODE_KP_MINUS;
			case Keys::NumPadPlus:			return SDL_SCANCODE_KP_PLUS;
			case Keys::NumPadEnter:			return SDL_SCANCODE_KP_ENTER;
			case Keys::NumPadEquals:		return SDL_SCANCODE_KP_EQUALS;

			case Keys::Up:					return SDL_SCANCODE_UP;
			case Keys::Down:				return SDL_SCANCODE_DOWN;
			case Keys::Right:				return SDL_SCANCODE_RIGHT;
			case Keys::Left:				return SDL_SCANCODE_LEFT;
			case Keys::Insert:				return SDL_SCANCODE_INSERT;
			case Keys::Home:				return SDL_SCANCODE_HOME;
			case Keys::End:					return SDL_SCANCODE_END;
			case Keys::PageUp:				return SDL_SCANCODE_PAGEUP;
			case Keys::PageDown:			return SDL_SCANCODE_PAGEDOWN;

			case Keys::F1:					return SDL_SCANCODE_F1;
			case Keys::F2:					return SDL_SCANCODE_F2;
			case Keys::F3:					return SDL_SCANCODE_F3;
			case Keys::F4:					return SDL_SCANCODE_F4;
			case Keys::F5:					return SDL_SCANCODE_F5;
			case Keys::F6:					return SDL_SCANCODE_F6;
			case Keys::F7:					return SDL_SCANCODE_F7;
			case Keys::F8:					return SDL_SCANCODE_F8;
			case Keys::F9:					return SDL_SCANCODE_F9;
			case Keys::F10:					return SDL_SCANCODE_F10;
			case Keys::F11:					return SDL_SCANCODE_F11;
			case Keys::F12:					return SDL_SCANCODE_F12;
			case Keys::F13:					return SDL_SCANCODE_F13;
			case Keys::F14:					return SDL_SCANCODE_F14;
			case Keys::F15:					return SDL_SCANCODE_F15;

			case Keys::NumLock:				return SDL_SCANCODE_NUMLOCKCLEAR;
			case Keys::CapsLock:			return SDL_SCANCODE_CAPSLOCK;
			case Keys::ScrollLock:			return SDL_SCANCODE_SCROLLLOCK;
			case Keys::RShift:				return SDL_SCANCODE_RSHIFT;
			case Keys::LShift:				return SDL_SCANCODE_LSHIFT;
			case Keys::RCtrl:				return SDL_SCANCODE_RCTRL;
			case Keys::LCtrl:				return SDL_SCANCODE_LCTRL;
			case Keys::RAlt:				return SDL_SCANCODE_RALT;
			case Keys::LAlt:				return SDL_SCANCODE_LALT;
			case Keys::RSuper:				return SDL_SCANCODE_RGUI;
			case Keys::LSuper:				return SDL_SCANCODE_LGUI;
			case Keys::Mode:				return SDL_SCANCODE_MODE;
			case Keys::Application:			return SDL_SCANCODE_APPLICATION;

			case Keys::Help:				return SDL_SCANCODE_HELP;
			case Keys::PrintScreen:			return SDL_SCANCODE_PRINTSCREEN;
			case Keys::SysReq:				return SDL_SCANCODE_SYSREQ;
			case Keys::Menu:				return SDL_SCANCODE_MENU;
			case Keys::Power:				return SDL_SCANCODE_POWER;
			case Keys::Undo:				return SDL_SCANCODE_UNDO;

			default:						return SDL_SCANCODE_UNKNOWN;
		}
		// clang-format on
	}
}

#endif