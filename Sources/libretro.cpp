#include "Main.h"

#if defined(WITH_LIBRETRO)

#include "libretro/libretro.h"

#include "nCine/Application.h"
#include "nCine/IAppEventHandler.h"
#include "nCine/Graphics/IGfxDevice.h"
#include "nCine/Graphics/RHI/Rhi.h"
#include "nCine/Input/IInputManager.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/Input/JoyMapping.h"

#include "Jazz2/ContentResolver.h"

#if defined(WITH_RHI_GL)
#	include "nCine/Graphics/RHI/GL/GLDevice.h"
#	include "nCine/Graphics/RHI/GL/GLFramebuffer.h"
#endif

#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#include <cstring>
#include <memory>
#include <vector>

using namespace Death;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace nCine;

// Defined in Main.cpp when building with WITH_LIBRETRO
std::unique_ptr<IAppEventHandler> CreateAppEventHandler();
bool Jazz2_SaveStateToStream(Death::IO::Stream& dest);
bool Jazz2_LoadStateFromStream(std::shared_ptr<Death::IO::Stream> src);

static retro_environment_t _environCb;
static retro_video_refresh_t _videoCb;
static retro_input_poll_t _inputPollCb;
static retro_input_state_t _inputStateCb;
static retro_audio_sample_t _audioCb;
static retro_audio_sample_batch_t _audioBatchCb;
// The engine presents once during initialization (inside retro_load_game), but the frontend's
// video driver only becomes callable inside retro_run
static bool _inRetroRun = false;
#if defined(WITH_RHI_GL)
// Hardware rendering: the frontend owns the GL context, which only exists once it calls
// context_reset - engine initialization is deferred until then
static retro_hw_render_callback _hwRender = {};
static bool _pendingInit = false;
#endif

namespace nCine
{
	/** @brief Presents the software renderer's CPU framebuffer through `retro_video_refresh` */
	class LibretroGfxDevice : public IGfxDevice
	{
	public:
		LibretroGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
			: IGfxDevice(windowMode, contextInfo, displayMode), _lastWidth(0), _lastHeight(0)
		{
			drawableWidth_ = width_;
			drawableHeight_ = height_;

			numMonitors_ = 1;
			monitors_[0].name = "libretro";
			monitors_[0].position = Vector2i(0, 0);
			monitors_[0].scale = Vector2f(1.0f, 1.0f);
			monitors_[0].numVideoModes = 1;
			currentVideoMode_.width = width_;
			currentVideoMode_.height = height_;
			currentVideoMode_.refreshRate = 60.0f;
			monitors_[0].videoModes[0] = currentVideoMode_;

#if defined(WITH_RHI_SOFTWARE)
			// Same init as SdlGfxDevice::initSoftwarePresent(): give the render pipeline a valid
			// CPU screen buffer before the first frame
			RHI::Device::ResizeScreenFramebuffer(drawableWidth_, drawableHeight_);
#endif
			initDeviceViewport();
		}

		void setSwapInterval(int interval) override {}
		void setResolution(bool fullscreen, int width = 0, int height = 0) override {}
		void setWindowPosition(int x, int y) override {}
		void setWindowTitle(StringView windowTitle) override {}
		void setWindowIcon(StringView iconFilename) override {}
		void setWindowSize(int width, int height) override {}

		const VideoMode& currentVideoMode(unsigned int monitorIndex) const override {
			return currentVideoMode_;
		}

	protected:
		void setResolutionInternal(int width, int height) override {}

	private:
		std::int32_t _lastWidth;
		std::int32_t _lastHeight;
		std::vector<std::uint32_t> _converted;

		void update() override
		{
#if defined(WITH_RHI_GL)
			// Hardware rendering: the frame was drawn straight into the frontend's FBO
			// (GLFramebuffer::SetDefaultHandle), just tell the frontend it is ready
			if (_inRetroRun) {
				_videoCb(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)drawableWidth_, (unsigned)drawableHeight_, 0);
			}
			return;
#else
			// Same presentation sequence as SdlGfxDevice::presentSoftware()
			RHI::Device::FlushSoftwareRenderer();
			RHI::Device::EndFrame();
			if (!_inRetroRun) {
				return;
			}
			const auto fb = RHI::Device::GetScreenFramebuffer();
			if (fb.pixels == nullptr || fb.width <= 0 || fb.height <= 0) {
				_videoCb(nullptr, _lastWidth, _lastHeight, _lastWidth * 4);
				return;
			}

			if (fb.width != _lastWidth || fb.height != _lastHeight) {
				_lastWidth = fb.width;
				_lastHeight = fb.height;
				retro_game_geometry geometry = {};
				geometry.base_width = (unsigned)fb.width;
				geometry.base_height = (unsigned)fb.height;
				geometry.max_width = 1920;
				geometry.max_height = 1080;
				geometry.aspect_ratio = (float)fb.width / (float)fb.height;
				_environCb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
			}

			// The engine framebuffer is R,G,B,A bytes with the bottom scanline first (OpenGL convention);
			// libretro XRGB8888 wants packed 0x00RRGGBB rows top-down, so swizzle and flip vertically
			_converted.resize((std::size_t)fb.width * fb.height);
			for (std::int32_t y = 0; y < fb.height; y++) {
				const std::uint8_t* src = fb.pixels + (std::size_t)(fb.height - 1 - y) * fb.strideBytes;
				std::uint32_t* dst = _converted.data() + (std::size_t)y * fb.width;
				for (std::int32_t x = 0; x < fb.width; x++) {
					dst[x] = ((std::uint32_t)src[0] << 16) | ((std::uint32_t)src[1] << 8) | src[2];
					src += 4;
				}
			}
			_videoCb(_converted.data(), fb.width, fb.height, fb.width * 4);
#endif
		}
	};

	/** @brief Exposes the frontend's RetroPad as a standard mapped gamepad */
	class LibretroInputManager : public IInputManager
	{
	public:
		static constexpr std::int32_t NumPads = 4;
		static constexpr std::int32_t NumButtons = 16;
		static constexpr std::int32_t NumAxes = 4;

		// Raw button ids are the RETRO_DEVICE_ID_JOYPAD_* values, raw axes are left/right stick X/Y;
		// this mapping translates them to the engine's unified gamepad layout
		static constexpr char RetroPadGuid[] = "030000004c6962726574726f00000000";
		static constexpr char RetroPadMapping[] =
			"030000004c6962726574726f00000000,RetroPad,"
			"a:b0,b:b8,x:b1,y:b9,back:b2,start:b3,"
			"dpup:b4,dpdown:b5,dpleft:b6,dpright:b7,"
			"leftshoulder:b10,rightshoulder:b11,lefttrigger:b12,righttrigger:b13,"
			"leftstick:b14,rightstick:b15,"
			"leftx:a0,lefty:a1,rightx:a2,righty:a3,";

		LibretroInputManager()
		{
			joyMapping_.Init(this);
			joyMapping_.AddMappingsFromString(RetroPadMapping);
		}

		void connectJoystick()
		{
			for (std::int32_t joyId = 0; joyId < NumPads; joyId++) {
				JoyConnectionEvent event;
				event.joyId = joyId;
				joyMapping_.OnJoyConnected(event);
				if (inputEventHandler_ != nullptr) {
					inputEventHandler_->OnJoyConnected(event);
				}
			}
		}

		void processFrame()
		{
			_inputPollCb();

			for (std::int32_t port = 0; port < NumPads; port++) {
				RetroJoystickState& joyState = joyStates_[port];

				for (std::int32_t id = 0; id < NumButtons; id++) {
					bool pressed = _inputStateCb((unsigned)port, RETRO_DEVICE_JOYPAD, 0, (unsigned)id) != 0;
					if (pressed != joyState.buttons[id]) {
						joyState.buttons[id] = pressed;
						JoyButtonEvent event;
						event.joyId = port;
						event.buttonId = id;
						if (pressed) {
							joyMapping_.OnJoyButtonPressed(event);
							if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyButtonPressed(event);
						} else {
							joyMapping_.OnJoyButtonReleased(event);
							if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyButtonReleased(event);
						}
					}
				}

				static const unsigned AnalogIndex[NumAxes] = {
					RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_INDEX_ANALOG_LEFT,
					RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_INDEX_ANALOG_RIGHT
				};
				static const unsigned AnalogId[NumAxes] = {
					RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y,
					RETRO_DEVICE_ID_ANALOG_X, RETRO_DEVICE_ID_ANALOG_Y
				};
				for (std::int32_t id = 0; id < NumAxes; id++) {
					std::int16_t raw = _inputStateCb((unsigned)port, RETRO_DEVICE_ANALOG, AnalogIndex[id], AnalogId[id]);
					float value = (raw < 0 ? raw / 32768.0f : raw / 32767.0f);
					if (value != joyState.axes[id]) {
						joyState.axes[id] = value;
						JoyAxisEvent event;
						event.joyId = port;
						event.axisId = id;
						event.value = value;
						joyMapping_.OnJoyAxisMoved(event);
						if (inputEventHandler_ != nullptr) inputEventHandler_->OnJoyAxisMoved(event);
					}
				}
			}
		}

		const MouseState& mouseState() const override { return mouseState_; }
		const KeyboardState& keyboardState() const override { return keyState_; }

		bool isJoyPresent(int joyId) const override { return (joyId >= 0 && joyId < NumPads); }
		const char* joyName(int joyId) const override { return "RetroPad"; }
		const JoystickGuid joyGuid(int joyId) const override { return JoystickGuid(StringView(RetroPadGuid)); }
		int joyNumButtons(int joyId) const override { return NumButtons; }
		int joyNumHats(int joyId) const override { return 0; }
		int joyNumAxes(int joyId) const override { return NumAxes; }
		const JoystickState& joystickState(int joyId) const override {
			return joyStates_[(joyId >= 0 && joyId < NumPads) ? joyId : 0];
		}
		bool joystickRumble(int joyId, float lowFreqIntensity, float highFreqIntensity, uint32_t durationMs) override { return false; }
		bool joystickRumbleTriggers(int joyId, float left, float right, uint32_t durationMs) override { return false; }
		void setCursor(Cursor cursor) override {}

	private:
		class RetroMouseState : public MouseState
		{
		public:
			bool isButtonDown(MouseButton button) const override { return false; }
		};

		class RetroKeyboardState : public KeyboardState
		{
		public:
			bool isKeyDown(Keys key) const override { return false; }
		};

		class RetroJoystickState : public JoystickState
		{
		public:
			bool buttons[NumButtons] = {};
			float axes[NumAxes] = {};

			bool isButtonPressed(int buttonId) const override {
				return (buttonId >= 0 && buttonId < NumButtons && buttons[buttonId]);
			}
			unsigned char hatState(int hatId) const override { return 0; }
			float axisValue(int axisId) const override {
				return (axisId >= 0 && axisId < NumAxes ? axes[axisId] : 0.0f);
			}
		};

		RetroMouseState mouseState_;
		RetroKeyboardState keyState_;
		RetroJoystickState joyStates_[NumPads];
	};

	/** @brief Drives the engine one frame at a time from `retro_run` */
	class LibretroApplication : public Application
	{
	public:
		bool Init()
		{
			PreInitCommon(CreateAppEventHandler());
			if (shouldQuit_) {
				return false;
			}

			// Pacing is normally the frontend's job (vsync or audio sync), but the engine steps a
			// fixed 1/60 s per retro_run, so an unthrottled frontend would fast-forward the game:
			// keep the engine's own 60 fps limiter as a backstop. It is bypassed automatically
			// while the frontend reports fast-forwarding (see retro_run).
			SetFrameLimiter(true);
			appCfg_.withVSync = false;

			IGfxDevice::ContextInfo contextInfo(appCfg_);
			DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, DisplayMode::VSync::Disabled);
			std::int32_t width = (appCfg_.resolution.X > 0 ? appCfg_.resolution.X : 720);
			std::int32_t height = (appCfg_.resolution.Y > 0 ? appCfg_.resolution.Y : 405);
			const IGfxDevice::WindowMode windowMode(width, height, 0, 0, false, false, false);

			gfxDevice_ = std::make_unique<LibretroGfxDevice>(windowMode, contextInfo, displayMode);
			inputManager_ = std::make_unique<LibretroInputManager>();

			InitCommon();

			SetAutoSuspension(false);
			SetFocus(true);
			static_cast<LibretroInputManager&>(*inputManager_).connectJoystick();
			return !shouldQuit_;
		}

		void RunFrame()
		{
			static_cast<LibretroInputManager&>(*inputManager_).processFrame();
			Step();
		}

		void SetFrameLimiter(bool enabled)
		{
			appCfg_.frameLimit = (enabled ? 60 : 0);
		}

		void Shutdown()
		{
			ShutdownCommon();
		}
	};

	Application& theApplication()
	{
		static LibretroApplication instance;
		return instance;
	}

	static LibretroApplication& theLibretroApplication()
	{
		return static_cast<LibretroApplication&>(theApplication());
	}
}

static bool _gameInitialized = false;

#if defined(WITH_RHI_GL)
static void OnContextReset()
{
#if defined(WITH_GLEW)
	glewExperimental = GL_TRUE;
	const GLenum glewErr = glewInit();
	if (glewErr != GLEW_OK && glewErr != GLEW_ERROR_NO_GLX_DISPLAY) {
		return;
	}
	glGetError();	// glewInit() can leave a stray GL error behind
#endif
	if (_pendingInit) {
		_pendingInit = false;
		_gameInitialized = theLibretroApplication().Init();
	}
	// ponytail: first-time init only - recreating every GPU resource after a real context loss
	// is not supported by the engine, cache_context asks the frontend to avoid that
}

static void OnContextDestroy()
{
	// Called by the frontend with the context still current - the only safe point to free GL resources
	if (_gameInitialized) {
		theLibretroApplication().Shutdown();
		_gameInitialized = false;
	}
}
#endif

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	_environCb = cb;

	// The core can start without content if "<system>/jazz2/Content" exists
	bool supportNoGame = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &supportNoGame);

}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { _videoCb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { _audioCb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { _audioBatchCb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { _inputPollCb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { _inputStateCb = cb; }

RETRO_API void retro_init(void) {}

RETRO_API void retro_deinit(void) {}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	std::memset(info, 0, sizeof(*info));
	info->library_name = NCINE_APP_NAME;
	info->library_version = NCINE_VERSION;
	// Load any file inside the game directory (the one containing Content/ and Source/)
	info->valid_extensions = "pak|j2a|j2l|j2e";
	info->need_fullpath = true;
	info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	std::memset(info, 0, sizeof(*info));
	info->geometry.base_width = 720;
	info->geometry.base_height = 405;
	info->geometry.max_width = 1920;
	info->geometry.max_height = 1080;
	info->geometry.aspect_ratio = 16.0f / 9.0f;
	info->timing.fps = 60.0;
	// ponytail: audio goes straight out through OpenAL, this rate only satisfies the frontend
	info->timing.sample_rate = 48000.0;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {}

RETRO_API void retro_reset(void) {}

RETRO_API void retro_run(void)
{
	if (!_gameInitialized) {
		return;
	}
	// The internal 60 fps limiter is only a backstop for unthrottled frontends; lift it while
	// the frontend fast-forwards so the feature actually speeds the game up
	bool fastForwarding = false;
	_environCb(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fastForwarding);
	theLibretroApplication().SetFrameLimiter(!fastForwarding);
#if defined(WITH_RHI_GL)
	// The frontend renders its own UI with the same GL context between two frames: repoint the
	// "screen" at its FBO (the id can change every frame) and re-sync every cached GL state
	nCine::RHI::GL::GLFramebuffer::SetDefaultHandle((GLuint)_hwRender.get_current_framebuffer());
	nCine::RHI::GL::GLDevice::ResyncExternalStateChanges();
#endif
	_inRetroRun = true;
	theLibretroApplication().RunFrame();
	_inRetroRun = false;
	// ponytail: real audio goes out through OpenAL; this silence only feeds the frontend's
	// audio sync so retro_run is paced at 60 fps even with vsync off (48000 Hz / 60)
	if (_audioBatchCb != nullptr) {
		static const std::int16_t silence[800 * 2] = {};
		_audioBatchCb(silence, 800);
	}
	if (theLibretroApplication().ShouldQuit()) {
		_environCb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
	}
}

// Save states piggyback on the game's own level-resume snapshot (the "Continue" feature):
// level, tilemap/event state and players are restored, but not frame-exact actor state - fine
// for manual save/load, unsuitable for netplay/run-ahead/rewind.
// ponytail: fixed generous cap - the snapshot is deflate-compressed and typically tens of KB
static constexpr std::size_t StateBufferSize = 2 * 1024 * 1024;

RETRO_API size_t retro_serialize_size(void)
{
	return StateBufferSize;
}

RETRO_API bool retro_serialize(void* data, size_t size)
{
	if (!_gameInitialized) {
		return false;
	}
	MemoryStream ms;
	// Fails outside a resumable local game session (menus, cinematics) - the frontend just
	// reports that saving failed
	if (!Jazz2_SaveStateToStream(ms) || (std::size_t)ms.GetSize() > size) {
		return false;
	}
	std::memcpy(data, ms.GetBuffer(), (std::size_t)ms.GetSize());
	std::memset((std::uint8_t*)data + ms.GetSize(), 0, size - (std::size_t)ms.GetSize());
	return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
	if (!_gameInitialized || size < 11) {
		return false;
	}
	// Validate the signature synchronously; the actual level reload happens on the next frame
	std::uint64_t signature;
	std::memcpy(&signature, data, sizeof(signature));
	if (signature != 0x2095A59FF0BFBBEF) {
		return false;
	}
	auto ms = std::make_shared<MemoryStream>(Containers::InPlaceInit,
		Containers::ArrayView<const std::uint8_t>((const std::uint8_t*)data, (std::size_t)size));
	return Jazz2_LoadStateFromStream(std::move(ms));
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code) {}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
	// "<system>/jazz2" can hold Source/ (original Jazz Jackrabbit 2 files), Cache/ and
	// optionally Content/ for content-less launch
	const char* systemDir = nullptr;
	_environCb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemDir);
	String systemBase;
	if (systemDir != nullptr && systemDir[0] != '\0') {
		// The frontend may point the system directory straight at the game root (or at the
		// Source folder itself, see below), otherwise the "<system>/jazz2" convention applies
		systemBase = (fs::DirectoryExists(fs::CombinePath(systemDir, "Content"_s))
			? String(systemDir) : fs::CombinePath(systemDir, "jazz2"_s));
	}

	// The core assets directory can hold Content/ (and the generated Cache/) on frontends that
	// separate engine data from the user's game files (Recalbox: core_assets = bios/jazz2)
	const char* assetsDir = nullptr;
	_environCb(RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY, &assetsDir);

	// Find the game root (the directory containing Content/) starting from the loaded file
	// and make it the working directory, so the engine's relative Content/, Source/ and
	// Cache/ paths (NCINE_PACKAGED_CONTENT_PATH build) resolve there.
	// ponytail: chdir affects the whole frontend process, revisit if it ever conflicts
	String baseDir;
	if (game != nullptr && game->path != nullptr) {
		baseDir = (fs::DirectoryExists(game->path) ? String(game->path) : String(fs::GetDirectoryName(game->path)));
		bool found = false;
		for (std::int32_t i = 0; i < 3 && !baseDir.empty(); i++) {
			if (fs::DirectoryExists(fs::CombinePath(baseDir, "Content"_s))) {
				found = true;
				break;
			}
			String parent = fs::GetDirectoryName(baseDir);
			baseDir = std::move(parent);
		}
		if (!found) {
			baseDir = String();
		}
	}
	if (baseDir.empty() && assetsDir != nullptr && assetsDir[0] != '\0' &&
		fs::DirectoryExists(fs::CombinePath(assetsDir, "Content"_s))) {
		baseDir = assetsDir;
	}
	if (baseDir.empty() && !systemBase.empty() && fs::DirectoryExists(fs::CombinePath(systemBase, "Content"_s))) {
		baseDir = systemBase;
	}
	if (baseDir.empty() || !fs::SetWorkingDirectory(baseDir)) {
		return false;
	}

	// Source/ next to the game content wins (portable layout, same Anims.j2a check as
	// ContentResolver), otherwise fall back to the frontend's system directory:
	// "<system>/jazz2/Source" or ".../Sources" (with Cache/ alongside)
	String localSource = fs::CombinePath(baseDir, "Source"_s);
	bool localSourceValid = !fs::FindPathCaseInsensitive(fs::CombinePath(localSource, "Anims.j2a"_s)).empty() ||
		!fs::FindPathCaseInsensitive(fs::CombinePath(localSource, "AnimsSw.j2a"_s)).empty();
	// The system directory may BE the Source folder (Recalbox: system = the rom's directory,
	// which holds Anims.j2a); the Cache then goes next to Content/ in the working directory
	if (!localSourceValid && systemDir != nullptr && systemDir[0] != '\0' &&
		(!fs::FindPathCaseInsensitive(fs::CombinePath(systemDir, "Anims.j2a"_s)).empty() ||
		 !fs::FindPathCaseInsensitive(fs::CombinePath(systemDir, "AnimsSw.j2a"_s)).empty())) {
		Jazz2::ContentResolver::Get().OverridePaths(systemDir, fs::CombinePath(baseDir, "Cache/"_s));
		localSourceValid = true;
	}
	if (!localSourceValid && !systemBase.empty()) {
		for (StringView sourceName : { "Source/"_s, "Sources/"_s }) {
			String systemSource = fs::CombinePath(systemBase, sourceName);
			if (fs::DirectoryExists(systemSource)) {
				Jazz2::ContentResolver::Get().OverridePaths(systemSource, fs::CombinePath(systemBase, "Cache/"_s));
				break;
			}
		}
	}

	retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!_environCb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixelFormat)) {
		return false;
	}

#if defined(WITH_RHI_GL)
	// Hardware rendering: ask the frontend for an OpenGL|ES 3.0 context (the engine's ES target);
	// the engine initializes in context_reset, once that context actually exists
	_hwRender.context_type = RETRO_HW_CONTEXT_OPENGLES3;
	_hwRender.depth = true;
	_hwRender.stencil = true;
	_hwRender.bottom_left_origin = true;
	_hwRender.cache_context = true;
	_hwRender.context_reset = OnContextReset;
	_hwRender.context_destroy = OnContextDestroy;
	if (!_environCb(RETRO_ENVIRONMENT_SET_HW_RENDER, &_hwRender)) {
		return false;
	}
	_pendingInit = true;
	return true;
#else
	_gameInitialized = theLibretroApplication().Init();
	return _gameInitialized;
#endif
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info, size_t num_info)
{
	return false;
}

RETRO_API void retro_unload_game(void)
{
	if (_gameInitialized) {
		theLibretroApplication().Shutdown();
		_gameInitialized = false;
	}
}

RETRO_API unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

RETRO_API void* retro_get_memory_data(unsigned id) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return 0; }

#endif
