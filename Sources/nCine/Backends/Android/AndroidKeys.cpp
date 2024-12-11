#include "AndroidInputManager.h"
#include <android/input.h>

namespace nCine
{
	Keys AndroidKeys::keySymValueToEnum(int keysym)
	{
		// clang-format off
		switch (keysym) {
			case AKEYCODE_UNKNOWN:				return Keys::Unknown;
			case AKEYCODE_SOFT_LEFT:			return Keys::SoftLeft;
			case AKEYCODE_SOFT_RIGHT:			return Keys::SoftRight;
			case AKEYCODE_HOME:					return Keys::Home;
			case AKEYCODE_BACK:					return Keys::Back;
			case AKEYCODE_CALL:					return Keys::Call;
			case AKEYCODE_ENDCALL:				return Keys::EndCall;
			case AKEYCODE_0:					return Keys::D0;
			case AKEYCODE_1:					return Keys::D1;
			case AKEYCODE_2:					return Keys::D2;
			case AKEYCODE_3:					return Keys::D3;
			case AKEYCODE_4:					return Keys::D4;
			case AKEYCODE_5:					return Keys::D5;
			case AKEYCODE_6:					return Keys::D6;
			case AKEYCODE_7:					return Keys::D7;
			case AKEYCODE_8:					return Keys::D8;
			case AKEYCODE_9:					return Keys::D9;
			case AKEYCODE_STAR:					return Keys::Star;
			case AKEYCODE_POUND:				return Keys::Pound;
			case AKEYCODE_DPAD_UP:				return Keys::Up;
			case AKEYCODE_DPAD_DOWN:			return Keys::Down;
			case AKEYCODE_DPAD_LEFT:			return Keys::Left;
			case AKEYCODE_DPAD_RIGHT:			return Keys::Right;
			case AKEYCODE_DPAD_CENTER:			return Keys::DpadCenter;
			case AKEYCODE_VOLUME_UP:			return Keys::VolumeUp;
			case AKEYCODE_VOLUME_DOWN:			return Keys::VolumeDown;
			case AKEYCODE_POWER:				return Keys::Power;
			case AKEYCODE_CAMERA:				return Keys::Camera;
			case AKEYCODE_CLEAR:				return Keys::Clear;
			case AKEYCODE_A:					return Keys::A;
			case AKEYCODE_B:					return Keys::B;
			case AKEYCODE_C:					return Keys::C;
			case AKEYCODE_D:					return Keys::D;
			case AKEYCODE_E:					return Keys::E;
			case AKEYCODE_F:					return Keys::F;
			case AKEYCODE_G:					return Keys::G;
			case AKEYCODE_H:					return Keys::H;
			case AKEYCODE_I:					return Keys::I;
			case AKEYCODE_J:					return Keys::J;
			case AKEYCODE_K:					return Keys::K;
			case AKEYCODE_L:					return Keys::L;
			case AKEYCODE_M:					return Keys::M;
			case AKEYCODE_N:					return Keys::N;
			case AKEYCODE_O:					return Keys::O;
			case AKEYCODE_P:					return Keys::P;
			case AKEYCODE_Q:					return Keys::Q;
			case AKEYCODE_R:					return Keys::R;
			case AKEYCODE_S:					return Keys::S;
			case AKEYCODE_T:					return Keys::T;
			case AKEYCODE_U:					return Keys::U;
			case AKEYCODE_V:					return Keys::V;
			case AKEYCODE_W:					return Keys::W;
			case AKEYCODE_X:					return Keys::X;
			case AKEYCODE_Y:					return Keys::Y;
			case AKEYCODE_Z:					return Keys::Z;
			case AKEYCODE_COMMA:				return Keys::Comma;
			case AKEYCODE_PERIOD:				return Keys::Period;
			case AKEYCODE_ALT_LEFT:				return Keys::LAlt;
			case AKEYCODE_ALT_RIGHT:			return Keys::RAlt;
			case AKEYCODE_SHIFT_LEFT:			return Keys::LShift;
			case AKEYCODE_SHIFT_RIGHT:			return Keys::RShift;
			case AKEYCODE_TAB:					return Keys::Tab;
			case AKEYCODE_SPACE:				return Keys::Space;
			case AKEYCODE_SYM:					return Keys::Sym;
			case AKEYCODE_EXPLORER:				return Keys::Explorer;
			case AKEYCODE_ENVELOPE:				return Keys::Envelope;
			case AKEYCODE_ENTER:				return Keys::Return;
			case AKEYCODE_DEL:					return Keys::Backspace;
			case AKEYCODE_GRAVE:				return Keys::Backquote;
			case AKEYCODE_MINUS:				return Keys::Minus;
			case AKEYCODE_EQUALS:				return Keys::Equals;
			case AKEYCODE_LEFT_BRACKET:			return Keys::LeftBracket;
			case AKEYCODE_RIGHT_BRACKET:		return Keys::RightBracket;
			case AKEYCODE_BACKSLASH:			return Keys::Backslash;
			case AKEYCODE_SEMICOLON:			return Keys::Semicolon;
			case AKEYCODE_APOSTROPHE:			return Keys::Quote;
			case AKEYCODE_SLASH:				return Keys::Slash;
			case AKEYCODE_AT:					return Keys::At;
			case AKEYCODE_NUM:					return Keys::Num;
			case AKEYCODE_HEADSETHOOK:			return Keys::HeadsetHook;
			case AKEYCODE_FOCUS:				return Keys::Focus; // *camera* focus
			case AKEYCODE_PLUS:					return Keys::Plus;
			case AKEYCODE_MENU:					return Keys::Menu;
			case AKEYCODE_NOTIFICATION:			return Keys::Notification;
			case AKEYCODE_SEARCH:				return Keys::Search;
			case AKEYCODE_MEDIA_PLAY_PAUSE:		return Keys::MediaPlayPause;
			case AKEYCODE_MEDIA_STOP:			return Keys::MediaStop;
			case AKEYCODE_MEDIA_NEXT:			return Keys::MediaNext;
			case AKEYCODE_MEDIA_PREVIOUS:		return Keys::MediaPrevious;
			case AKEYCODE_MEDIA_REWIND:			return Keys::MediaRewind;
			case AKEYCODE_MEDIA_FAST_FORWARD:	return Keys::MediaFastForward;
			case AKEYCODE_MUTE:					return Keys::Mute;
			case AKEYCODE_PAGE_UP:				return Keys::PageUp;
			case AKEYCODE_PAGE_DOWN:			return Keys::PageDown;
			case AKEYCODE_PICTSYMBOLS:			return Keys::PictSymbols;
			case AKEYCODE_SWITCH_CHARSET:		return Keys::SwitchCharset;
			case AKEYCODE_BUTTON_A:				return Keys::ButtonA;
			case AKEYCODE_BUTTON_B:				return Keys::ButtonB;
			case AKEYCODE_BUTTON_C:				return Keys::ButtonC;
			case AKEYCODE_BUTTON_X:				return Keys::ButtonX;
			case AKEYCODE_BUTTON_Y:				return Keys::ButtonY;
			case AKEYCODE_BUTTON_Z:				return Keys::ButtonZ;
			case AKEYCODE_BUTTON_L1:			return Keys::ButtonL1;
			case AKEYCODE_BUTTON_R1:			return Keys::ButtonR1;
			case AKEYCODE_BUTTON_L2:			return Keys::ButtonL2;
			case AKEYCODE_BUTTON_R2:			return Keys::ButtonR2;
			case AKEYCODE_BUTTON_THUMBL:		return Keys::ButtonThumbLeft;
			case AKEYCODE_BUTTON_THUMBR:		return Keys::ButtonThumbRight;
			case AKEYCODE_BUTTON_START:			return Keys::ButtonStart;
			case AKEYCODE_BUTTON_SELECT:		return Keys::ButtonSelect;
			case AKEYCODE_BUTTON_MODE:			return Keys::ButtonMode;
#if __ANDROID_API__ >= 13
			case AKEYCODE_ESCAPE:				return Keys::Escape;
			case AKEYCODE_FORWARD_DEL:			return Keys::Delete;
			case AKEYCODE_CTRL_LEFT:			return Keys::LCtrl;
			case AKEYCODE_CTRL_RIGHT:			return Keys::RCtrl;
			case AKEYCODE_CAPS_LOCK:			return Keys::CapsLock;
			case AKEYCODE_SCROLL_LOCK:			return Keys::ScrollLock;
			case AKEYCODE_META_LEFT:			return Keys::LSuper;
			case AKEYCODE_META_RIGHT:			return Keys::RSuper;
			case AKEYCODE_FUNCTION:				return Keys::FunctionKey;
			case AKEYCODE_SYSRQ:				return Keys::SysReq;
			case AKEYCODE_BREAK:				return Keys::Pause;
			case AKEYCODE_MOVE_HOME:			return Keys::MoveHome;
			case AKEYCODE_MOVE_END:				return Keys::MoveEnd;
			case AKEYCODE_INSERT:				return Keys::Insert;
			case AKEYCODE_FORWARD:				return Keys::Forward;
			case AKEYCODE_MEDIA_PLAY:			return Keys::MediaPlay;
			case AKEYCODE_MEDIA_PAUSE:			return Keys::MediaPause;
			case AKEYCODE_MEDIA_CLOSE:			return Keys::MediaClose;
			case AKEYCODE_MEDIA_EJECT:			return Keys::MediaEject;
			case AKEYCODE_MEDIA_RECORD:			return Keys::MediaRecord;
			case AKEYCODE_F1:					return Keys::F1;
			case AKEYCODE_F2:					return Keys::F2;
			case AKEYCODE_F3:					return Keys::F3;
			case AKEYCODE_F4:					return Keys::F4;
			case AKEYCODE_F5:					return Keys::F5;
			case AKEYCODE_F6:					return Keys::F6;
			case AKEYCODE_F7:					return Keys::F7;
			case AKEYCODE_F8:					return Keys::F8;
			case AKEYCODE_F9:					return Keys::F9;
			case AKEYCODE_F10:					return Keys::F10;
			case AKEYCODE_F11:					return Keys::F11;
			case AKEYCODE_F12:					return Keys::F12;
			case AKEYCODE_NUM_LOCK:				return Keys::NumLock;
			case AKEYCODE_NUMPAD_0:				return Keys::NumPad0;
			case AKEYCODE_NUMPAD_1:				return Keys::NumPad1;
			case AKEYCODE_NUMPAD_2:				return Keys::NumPad2;
			case AKEYCODE_NUMPAD_3:				return Keys::NumPad3;
			case AKEYCODE_NUMPAD_4:				return Keys::NumPad4;
			case AKEYCODE_NUMPAD_5:				return Keys::NumPad5;
			case AKEYCODE_NUMPAD_6:				return Keys::NumPad6;
			case AKEYCODE_NUMPAD_7:				return Keys::NumPad7;
			case AKEYCODE_NUMPAD_8:				return Keys::NumPad8;
			case AKEYCODE_NUMPAD_9:				return Keys::NumPad9;
			case AKEYCODE_NUMPAD_DIVIDE:		return Keys::NumPadDivide;
			case AKEYCODE_NUMPAD_MULTIPLY:		return Keys::NumPadMultiply;
			case AKEYCODE_NUMPAD_SUBTRACT:		return Keys::NumPadMinus;
			case AKEYCODE_NUMPAD_ADD:			return Keys::NumPadPlus;
			case AKEYCODE_NUMPAD_DOT:			return Keys::NumPadPeriod;
			case AKEYCODE_NUMPAD_COMMA:			return Keys::NumPadComma;
			case AKEYCODE_NUMPAD_ENTER:			return Keys::NumPadEnter;
			case AKEYCODE_NUMPAD_EQUALS:		return Keys::NumPadEquals;
			case AKEYCODE_NUMPAD_LEFT_PAREN:	return Keys::NumPadLeftParen;
			case AKEYCODE_NUMPAD_RIGHT_PAREN:	return Keys::NumPadRightParen;
			case AKEYCODE_VOLUME_MUTE:			return Keys::VolumeMute;
			case AKEYCODE_INFO:					return Keys::Info;
			case AKEYCODE_CHANNEL_UP:			return Keys::ChannelUp;
			case AKEYCODE_CHANNEL_DOWN:			return Keys::ChannelDown;
			case AKEYCODE_ZOOM_IN:				return Keys::ZoomIn;
			case AKEYCODE_ZOOM_OUT:				return Keys::ZoomOut;
			case AKEYCODE_TV:					return Keys::TV;
			case AKEYCODE_WINDOW:				return Keys::Window;
			case AKEYCODE_GUIDE:				return Keys::Guide;
			case AKEYCODE_DVR:					return Keys::DVR;
			case AKEYCODE_BOOKMARK:				return Keys::Bookmark;
			case AKEYCODE_CAPTIONS:				return Keys::Captions;
			case AKEYCODE_SETTINGS:				return Keys::Settings;
			case AKEYCODE_TV_POWER:				return Keys::TVPower;
			case AKEYCODE_TV_INPUT:				return Keys::TVInput;
			case AKEYCODE_STB_POWER:			return Keys::STBPower;
			case AKEYCODE_STB_INPUT:			return Keys::STBInput;
			case AKEYCODE_AVR_POWER:			return Keys::AVRPower;
			case AKEYCODE_AVR_INPUT:			return Keys::AVRInput;
			case AKEYCODE_PROG_RED:				return Keys::ProgRed;
			case AKEYCODE_PROG_GREEN:			return Keys::ProgGreen;
			case AKEYCODE_PROG_YELLOW:			return Keys::ProgYellow;
			case AKEYCODE_PROG_BLUE:			return Keys::ProgBlue;
			case AKEYCODE_APP_SWITCH:			return Keys::AppSwitch;
			case AKEYCODE_BUTTON_1:				return Keys::Button1;
			case AKEYCODE_BUTTON_2:				return Keys::Button2;
			case AKEYCODE_BUTTON_3:				return Keys::Button3;
			case AKEYCODE_BUTTON_4:				return Keys::Button4;
			case AKEYCODE_BUTTON_5:				return Keys::Button5;
			case AKEYCODE_BUTTON_6:				return Keys::Button6;
			case AKEYCODE_BUTTON_7:				return Keys::Button7;
			case AKEYCODE_BUTTON_8:				return Keys::Button8;
			case AKEYCODE_BUTTON_9:				return Keys::Button9;
			case AKEYCODE_BUTTON_10:			return Keys::Button10;
			case AKEYCODE_BUTTON_11:			return Keys::Button11;
			case AKEYCODE_BUTTON_12:			return Keys::Button12;
			case AKEYCODE_BUTTON_13:			return Keys::Button13;
			case AKEYCODE_BUTTON_14:			return Keys::Button14;
			case AKEYCODE_BUTTON_15:			return Keys::Button15;
			case AKEYCODE_BUTTON_16:			return Keys::Button16;
#endif
			default:							return Keys::Unknown;
		}
		// clang-format on
	}

	int AndroidKeys::keyModMaskToEnumMask(int keymod)
	{
		int result = 0;

		if (keymod != 0) {
			result |= (keymod & AMETA_SHIFT_LEFT_ON) ? KeyMod::LShift : 0;
			result |= (keymod & AMETA_SHIFT_RIGHT_ON) ? KeyMod::RShift : 0;
			result |= (keymod & AMETA_CTRL_LEFT_ON) ? KeyMod::LCtrl : 0;
			result |= (keymod & AMETA_CTRL_RIGHT_ON) ? KeyMod::RCtrl : 0;
			result |= (keymod & AMETA_ALT_LEFT_ON) ? KeyMod::LAlt : 0;
			result |= (keymod & AMETA_ALT_RIGHT_ON) ? KeyMod::RAlt : 0;
			result |= (keymod & AMETA_META_LEFT_ON) ? KeyMod::LSuper : 0;
			result |= (keymod & AMETA_META_RIGHT_ON) ? KeyMod::RSuper : 0;
			result |= (keymod & AMETA_NUM_LOCK_ON) ? KeyMod::NumLock : 0;
			result |= (keymod & AMETA_CAPS_LOCK_ON) ? KeyMod::CapsLock : 0;
			result |= (keymod & AMETA_SYM_ON) ? KeyMod::Sym : 0;
		}

		return result;
	}
}
