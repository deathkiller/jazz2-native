#include "RgbLights.h"

#include <cstring>
#include <string>

#define COLORS_LIMITED_SIZE 4

#define KEYBOARD_WIDTH 22
#define KEYBOARD_HEIGHT 6

#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
// Remapping from Razer to Aura™ indices
static const std::uint8_t KeyLayout[] = {
	0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
	44, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 81, 59, 60, 61, 62, 63, 64, 65,
	66, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 83, 84, 85, 89,
	90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 104, 106, 107, 108, 109, 111,
	112, 113, 117, 119, 121, 122, 123, 125, 126, 127, 128, 130
};
#elif defined(DEATH_TARGET_EMSCRIPTEN)
// Remapping from Razer to Aura™ indices
static const std::uint8_t KeyLayout[] = {
	1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
	45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 80, 59, 60, 61, 62, 63, 64, 65,
	67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 84, 85, 86, 89,
	90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 102, 104, 106, 107, 108, 109, 111,
	112, 113, 117, 119, 121, 122, 123, 125, 126, 127, 129, 130
};
#endif

namespace Jazz2::UI
{
	RgbLights& RgbLights::Get()
	{
		static RgbLights current;
		return current;
	}

	RgbLights::RgbLights()
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
#	if defined(DEATH_TARGET_32BIT)
		_hLib = ::LoadLibrary(L"RzChromaSDK.dll");
#	else
		_hLib = ::LoadLibrary(L"RzChromaSDK64.dll");
#	endif
		if (_hLib != NULL) {
			RzInit init = (RzInit)::GetProcAddress(_hLib, "Init");
			_UnInit = (RzUnInit)::GetProcAddress(_hLib, "UnInit");
			_CreateKeyboardEffect = (RzCreateKeyboardEffect)::GetProcAddress(_hLib, "CreateKeyboardEffect");

			std::memset(_lastColors, 0, sizeof(_lastColors));

			init();
		} else {
			_UnInit = nullptr;
			_CreateKeyboardEffect = nullptr;
		}
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		_updateCount = 0;
		_ws = NULL;
		_isConnected = false;
		std::memset(_lastColors, 0, sizeof(_lastColors));

		if (emscripten_websocket_is_supported()) {
			EmscriptenWebSocketCreateAttributes ws_attrs = { "wss://chromasdk.io:13339/razer/chromasdk", NULL, EM_FALSE };

			_ws = emscripten_websocket_new(&ws_attrs);
			emscripten_websocket_set_onopen_callback(_ws, this, emscriptenOnOpen);
			emscripten_websocket_set_onerror_callback(_ws, this, emscriptenOnError);
			emscripten_websocket_set_onclose_callback(_ws, this, emscriptenOnClose);
			emscripten_websocket_set_onmessage_callback(_ws, this, emscriptenOnMessage);
		}
#endif
	}

	RgbLights::~RgbLights()
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (_UnInit != nullptr) {
			_UnInit();
		}
		if (_hLib != NULL) {
			::FreeLibrary(_hLib);
			_hLib = NULL;
		}
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		_isConnected = false;
		if (_ws != NULL) {
			emscripten_websocket_delete(_ws);
			_ws = NULL;
		}
#endif
	}

	bool RgbLights::IsSupported() const
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		return (_CreateKeyboardEffect != nullptr);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		return _isConnected;
#else
		return false;
#endif
	}

	void RgbLights::Update(Color colors[ColorsSize])
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (_CreateKeyboardEffect == nullptr) {
			return;
		}

		if (std::memcmp(_lastColors, colors, sizeof(_lastColors)) == 0) {
			return;
		}

		std::memcpy(_lastColors, colors, sizeof(_lastColors));

		ChromaSDK::Keyboard::CUSTOM_EFFECT_TYPE param = { };
		for (std::int32_t i = COLORS_LIMITED_SIZE; i < ColorsSize; i++) {
			std::int32_t idx = KeyLayout[i - COLORS_LIMITED_SIZE];
			param.Color[idx / KEYBOARD_WIDTH][idx % KEYBOARD_WIDTH] = colors[i].Abgr();
		}

		_CreateKeyboardEffect(ChromaSDK::Keyboard::CHROMA_CUSTOM, &param, nullptr);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		if (!_isConnected) {
			return;
		}

		if (std::memcmp(_lastColors, colors, sizeof(_lastColors)) == 0) {
			return;
		}

		std::memcpy(_lastColors, colors, sizeof(_lastColors));

		std::uint32_t buffer[KEYBOARD_WIDTH * KEYBOARD_HEIGHT] { };
		for (std::int32_t i = COLORS_LIMITED_SIZE; i < ColorsSize; i++) {
			buffer[KeyLayout[i - COLORS_LIMITED_SIZE]] = colors[i].Abgr();
		}

		std::string request;
		request.reserve(2048);

		request.append(R"({"endpoint":"keyboard","effect":"CHROMA_CUSTOM","token":)");

		_updateCount++;
		request.append(std::to_string(_updateCount));

		request.append(R"(,"param":[)");

		for (std::int32_t i = 0; i < KEYBOARD_HEIGHT; i++) {
			if (i > 0) {
				request.append(",");
			}
			request.append("[");
			for (std::int32_t j = 0; j < KEYBOARD_WIDTH; j++) {
				if (j > 0) {
					request.append(",");
				}
				request.append(std::to_string(buffer[i * KEYBOARD_WIDTH + j]));
			}
			request.append("]");
		}

		request.append("]}");

		EMSCRIPTEN_RESULT result = emscripten_websocket_send_utf8_text(_ws, request.c_str());
		if (result) {
			LOGE("Request failed: %i", result);
		}
#endif
	}

	void RgbLights::Clear()
	{
#if defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)
		if (_CreateKeyboardEffect == nullptr) {
			return;
		}

		bool isEmpty = true;
		for (std::int32_t i = 0; i < ColorsSize; i++) {
			if (_lastColors[i] != Color(0, 0, 0, 0)) {
				isEmpty = false;
				break;
			}
		}

		if (isEmpty) {
			return;
		}

		std::memset(_lastColors, 0, sizeof(_lastColors));

		_CreateKeyboardEffect(ChromaSDK::Keyboard::CHROMA_NONE, nullptr, nullptr);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		if (!_isConnected) {
			return;
		}

		bool isEmpty = true;
		for (std::int32_t i = 0; i < ColorsSize; i++) {
			if (_lastColors[i] != Color(0, 0, 0, 0)) {
				isEmpty = false;
				break;
			}
		}

		if (isEmpty) {
			return;
		}

		std::memset(_lastColors, 0, sizeof(_lastColors));

		std::string request;
		request.reserve(80);

		request.append(R"({"endpoint":"keyboard","effect":"CHROMA_NONE","token":)");

		_updateCount++;
		request.append(std::to_string(_updateCount));

		request.append("}");

		EMSCRIPTEN_RESULT result = emscripten_websocket_send_utf8_text(_ws, request.c_str());
		if (result) {
			LOGE("Request failed: %i", result);
		}
#endif
	}

#if defined(DEATH_TARGET_EMSCRIPTEN)
	EM_BOOL RgbLights::emscriptenOnOpen(int32_t eventType, const EmscriptenWebSocketOpenEvent* websocketEvent, void* userData)
	{
		static const char Response[] = R"({"title":"Jazz² Resurrection","description":"Jazz² Resurrection","author":{"name":"Dan R.","contact":"http://deat.tk/jazz2/"},"device_supported":["keyboard","mouse"],"category":"game","ext":"Aura™ Service"})";

		EMSCRIPTEN_RESULT result = emscripten_websocket_send_utf8_text(websocketEvent->socket, Response);
		if (result) {
			LOGE("Response failed: %i", result);
		} else {
			RgbLights* _this = static_cast<RgbLights*>(userData);
			_this->_isConnected = true;
		}
		return EM_TRUE;
	}

	EM_BOOL RgbLights::emscriptenOnError(std::int32_t eventType, const EmscriptenWebSocketErrorEvent* websocketEvent, void* userData)
	{
		RgbLights* _this = static_cast<RgbLights*>(userData);

		_this->_isConnected = false;
		if (_this->_ws != NULL) {
			emscripten_websocket_delete(_this->_ws);
			_this->_ws = NULL;
		}

		return EM_TRUE;
	}

	EM_BOOL RgbLights::emscriptenOnClose(std::int32_t eventType, const EmscriptenWebSocketCloseEvent* websocketEvent, void* userData)
	{
		RgbLights* _this = static_cast<RgbLights*>(userData);

		_this->_isConnected = false;
		if (_this->_ws != NULL) {
			emscripten_websocket_delete(_this->_ws);
			_this->_ws = NULL;
		}

		return EM_TRUE;
	}

	EM_BOOL RgbLights::emscriptenOnMessage(std::int32_t eventType, const EmscriptenWebSocketMessageEvent* websocketEvent, void* userData)
	{
		// Server usually doesn't send anything back
		return EM_TRUE;
	}
#endif
}