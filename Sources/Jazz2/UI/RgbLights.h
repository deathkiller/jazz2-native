#pragma once

#include "../../Common.h"
#include "../../nCine/Primitives/Color.h"
#include "../../nCine/Base/FrameTimer.h"

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <emscripten/emscripten.h>
#	include <emscripten/websocket.h>
#endif

#include <CommonWindows.h>

using namespace nCine;

#define AURA_REFRESH_INTERVAL 40		// 40 ms
#define AURA_COLORS_SIZE (4 + 105)		// 4 Main colors + Keyboard mapping = 327 bytes, see AuraLight struct
#define AURA_COLORS_LIMITED_SIZE 4		// Only first 4 main colors
#define AURA_KEYBOARD_WIDTH 22
#define AURA_KEYBOARD_HEIGHT 6

#if defined(DEATH_TARGET_WINDOWS)
namespace ChromaSDK::Keyboard
{
	//! Chroma keyboard effect types
	typedef enum EFFECT_TYPE {
		CHROMA_NONE = 0,			//!< No effect.
		CHROMA_BREATHING,			//!< Breathing effect (This effect has deprecated and should not be used).
		CHROMA_CUSTOM,				//!< Custom effect.
		CHROMA_REACTIVE,			//!< Reactive effect (This effect has deprecated and should not be used).
		CHROMA_STATIC,				//!< Static effect.
		CHROMA_SPECTRUMCYCLING,		//!< Spectrum cycling effect (This effect has deprecated and should not be used).
		CHROMA_WAVE,				//!< Wave effect (This effect has deprecated and should not be used).
		CHROMA_RESERVED,			//!< Reserved.
		CHROMA_CUSTOM_KEY,			//!< Custom effects with keys.
		CHROMA_CUSTOM2,
		CHROMA_INVALID				//!< Invalid effect.
	} EFFECT_TYPE;

	//! Maximum number of rows in a keyboard.
	const std::size_t MAX_ROW = 6;

	//! Maximum number of columns in a keyboard.
	const std::size_t MAX_COLUMN = 22;

	//! Custom effect (This effect type has deprecated and should not be used).
	typedef struct CUSTOM_EFFECT_TYPE {
		COLORREF Color[MAX_ROW][MAX_COLUMN];	//!< Grid layout. 6 rows by 22 columns.
	} CUSTOM_EFFECT_TYPE;
}
#endif

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
	public:
		static constexpr std::int32_t ColorsSize = AURA_COLORS_SIZE;
		static constexpr std::int32_t RefreshRate = (std::int32_t)(FrameTimer::FramesPerSecond / (1000 / AURA_REFRESH_INTERVAL));

		RgbLights();
		~RgbLights();

		bool IsSupported() const;

		void Update(Color colors[ColorsSize]);
		void Clear();

		static RgbLights& Get();

	private:
		RgbLights(const RgbLights&) = delete;
		RgbLights& operator=(const RgbLights&) = delete;

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		using RzInit = int (*)();
		using RzUnInit = int (*)();
		using RzCreateKeyboardEffect = int (*)(ChromaSDK::Keyboard::EFFECT_TYPE Effect, void* pParam, void* pEffectId);

		HMODULE _hLib;
		RzUnInit _UnInit;
		RzCreateKeyboardEffect _CreateKeyboardEffect;
		Color _lastColors[ColorsSize];
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		std::uint32_t _updateCount;
		EMSCRIPTEN_WEBSOCKET_T _ws;
		bool _isConnected;
		Color _lastColors[ColorsSize];

		static EM_BOOL emscriptenOnOpen(std::int32_t eventType, const EmscriptenWebSocketOpenEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnError(std::int32_t eventType, const EmscriptenWebSocketErrorEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnClose(std::int32_t eventType, const EmscriptenWebSocketCloseEvent* websocketEvent, void* userData);
		static EM_BOOL emscriptenOnMessage(std::int32_t eventType, const EmscriptenWebSocketMessageEvent* websocketEvent, void* userData);
#endif
	};
}