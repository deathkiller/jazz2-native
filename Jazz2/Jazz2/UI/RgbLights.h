#pragma once

#include "../../Common.h"
#include "../../nCine/Primitives/Color.h"
#include "../../nCine/Base/FrameTimer.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#	include <emscripten/websocket.h>
#endif

using namespace nCine;

namespace Jazz2::UI
{
	enum struct AuraLight
	{
		// Primary / Logo / Center
		Primary,
		// Secondary / Scroll / Left
		Secondary,
		// Tertiary / Underglow / Right
		Tertiary,
		// Keyboard Logo
		KeyboardLogo,

		Esc, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, PrintScreen, ScrollLock,
		PauseBreak, Tilde, One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Zero,
		Minus, Equals, Backspace, Insert, Home, PageUp, NumLock, NumSlash, NumAsterisk, NumMinus,
		Tab, Q, W, E, R, T, Y, U, I, O, P, OpenBracket, CloseBracket, Enter, Delete, End,
		PageDown, NumSeven, NumEight, NumNine, NumPlus, CapsLock, A, S, D, F, G, H, J, K, L,
		Semicolon, Apostrophe, Backslash, NumFour, NumFive, NumSix, LeftShift, NonUsBackslash,
		Z, X, C, V, B, N, M, Comma, Period, Slash, RightShift, ArrowUp, NumOne, NumTwo, NumThree, NumEnter,
		LeftCtrl, LeftWindows, LeftAlt, Space, RightAlt, Fn, Menu, RightCtrl,
		ArrowLeft, ArrowDown, ArrowRight, NumZero, NumPeriod,

		Unknown = -1
	};

	class RgbLights
	{
		friend class Font;

	public:
		static constexpr int ColorsSize = (4 + 105);
		static constexpr int RefreshRate = FrameTimer::FramesPerSecond / (1000 / 40);

		RgbLights();
		~RgbLights();

		bool IsSupported() const;

		void Update(Color colors[ColorsSize]);
		void Clear();

		static RgbLights& Current();

	private:
		/// Deleted copy constructor
		RgbLights(const RgbLights&) = delete;
		/// Deleted assignment operator
		RgbLights& operator=(const RgbLights&) = delete;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		uint32_t _updateCount;
		EMSCRIPTEN_WEBSOCKET_T _ws;
		bool _isConnected;
		Color _lastColors[ColorsSize];

		static EM_BOOL emscriptenOnOpen(int eventType, const EmscriptenWebSocketOpenEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnError(int eventType, const EmscriptenWebSocketErrorEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnClose(int eventType, const EmscriptenWebSocketCloseEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnMessage(int eventType, const EmscriptenWebSocketMessageEvent* websocketEvent, void* userData);
#endif
	};
}