#include "Main.h"

#if defined(WITH_LIBRETRO)

#include "libretro/libretro.h"

#include "nCine/IAppEventHandler.h"
#include "nCine/ServiceLocator.h"
#include "nCine/Backends/LibretroApplication.h"
#include "nCine/Backends/LibretroGfxDevice.h"
#include "nCine/Base/FrameTimer.h"

#include <IO/MemoryStream.h>

#include <cstring>
#include <memory>

using namespace Death;
using namespace Death::Containers;
using namespace Death::IO;
using namespace nCine;
using namespace nCine::Backends;

// Provided by the application, the same entry point the Android backend uses
std::unique_ptr<IAppEventHandler> CreateAppEventHandler();

static retro_audio_sample_t _audioCb;
static retro_audio_sample_batch_t _audioBatchCb;
static bool _gameInitialized = false;

static constexpr double AudioSampleRate = 48000.0;

// The game logic is frame-rate independent (it advances by GetTimeMult() units), so the core can run
// at the display rate of the frontend instead of a hardcoded 60 fps. The "jazz2_frame_rate" core
// option switches back to fixed 60 fps.
static double QueryTargetFps()
{
	retro_variable var = { "jazz2_frame_rate", nullptr };
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value != nullptr &&
		std::strcmp(var.value, "auto") != 0) {
		return 60.0;
	}
	float refreshRate = 0.0f;
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE, &refreshRate) &&
		refreshRate >= 50.0f && refreshRate <= 240.0f) {
		return (double)refreshRate;
	}
	return 60.0;
}

static void ApplyTargetFps(double fps)
{
	LibretroGfxDevice::SetTargetFps(fps);
	FrameTimer::FixedFrameDuration = (float)(1.0 / fps);
}
#if defined(WITH_RHI_GL)
// Hardware rendering: the frontend owns the GL context, which only exists once it calls
// context_reset - engine initialization is deferred until then
static retro_hw_render_callback _hwRender = {};
static bool _pendingInit = false;

static void OnContextReset()
{
	if (!LibretroGfxDevice::InitializeGraphicsLibrary()) {
		return;
	}
	if (_pendingInit) {
		_pendingInit = false;
		_gameInitialized = theLibretroApplication().Init(CreateAppEventHandler);
	} else if (_gameInitialized) {
		// A reset without a preceding context_destroy means the context was lost without notice:
		// every GPU resource the engine holds is gone and it cannot recreate them in place
		LibretroApplication::ShowMessage("Graphics context was lost, please restart the core");
		theLibretroApplication().Shutdown();
		_gameInitialized = false;
	}
}

static void OnContextDestroy()
{
	// Called by the frontend with the context still current - the only safe point to free GL resources
	if (_gameInitialized) {
		theLibretroApplication().Shutdown();
		_gameInitialized = false;
	}
	// cache_context is only a request, the frontend may recreate the context at any time: re-arm
	// initialization so the next context_reset brings the engine back up
	_pendingInit = true;
}
#endif

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	LibretroApplication::EnvironmentCallback = cb;

	// The core can start without content if "<system>/jazz2/Content" exists
	bool supportNoGame = true;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &supportNoGame);

	// "auto" follows the target refresh rate of the frontend, "60" forces the original fixed rate
	static const retro_variable variables[] = {
		{ "jazz2_frame_rate", "Frame rate; auto|60" },
		{ nullptr, nullptr }
	};
	cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)variables);

	// Save states are the game's own level-resume snapshot written in host byte order: usable for
	// manual save/load, but the frontend must not build rewind, run-ahead or netplay on top of it,
	// and the resulting file cannot travel to a machine with a different endianness or word size
	std::uint64_t quirks = RETRO_SERIALIZATION_QUIRK_INCOMPLETE | RETRO_SERIALIZATION_QUIRK_MUST_INITIALIZE |
		RETRO_SERIALIZATION_QUIRK_ENDIAN_DEPENDENT | RETRO_SERIALIZATION_QUIRK_PLATFORM_DEPENDENT;
	cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &quirks);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { LibretroApplication::VideoRefreshCallback = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { _audioCb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { _audioBatchCb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { LibretroApplication::InputPollCallback = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { LibretroApplication::InputStateCallback = cb; }

RETRO_API void retro_init(void) {}

RETRO_API void retro_deinit(void) {}

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info* info)
{
	std::memset(info, 0, sizeof(*info));
	// Plain ASCII on purpose - frontends derive core info files, playlist entries and per-core
	// config/save paths from this name
	info->library_name = NCINE_APP;
	info->library_version = NCINE_VERSION;
	// Load any file inside the game directory (the one containing Content/ and Source/)
	info->valid_extensions = "pak|j2a|j2l|j2e";
	info->need_fullpath = true;
	info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info)
{
	ApplyTargetFps(QueryTargetFps());
	LibretroGfxDevice::FillSystemAvInfo(*info, 720, 405);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {}

RETRO_API void retro_reset(void)
{
	// The application has no reset state to return to and the engine cannot be restarted in place,
	// so say it instead of leaving the frontend's Reset command looking like a hang
	LibretroApplication::ShowMessage("Reset is not supported, quit to the main menu instead");
}

RETRO_API void retro_run(void)
{
	if (!_gameInitialized) {
		return;
	}
	// Follow the "Frame rate" core option: on a change, retime the game and tell the frontend
	// (SET_SYSTEM_AV_INFO reinitializes its audio/video drivers, so only when it actually changed)
	bool variablesUpdated = false;
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &variablesUpdated) && variablesUpdated) {
		double fps = QueryTargetFps();
		if (fps != LibretroGfxDevice::GetTargetFps()) {
			ApplyTargetFps(fps);
			LibretroGfxDevice::ReannounceAvInfo();
		}
	}

	// The internal frame limiter is only a backstop for unthrottled frontends; lift it while
	// the frontend fast-forwards so the feature actually speeds the application up
	bool fastForwarding = false;
	LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_FASTFORWARDING, &fastForwarding);
	theLibretroApplication().SetFrameLimiter(!fastForwarding);

	LibretroApplication::IsInsideFrame = true;
	theLibretroApplication().RunFrame();
	LibretroApplication::IsInsideFrame = false;

	// One frame of the mixed OpenAL output (48000 Hz / fps) is pulled from the loopback device and
	// handed to the frontend; it also feeds the frontend's audio sync so retro_run stays paced at
	// the announced rate even with vsync off. A fractional accumulator keeps the long-run sample
	// count exact for rates that don't divide 48000 (e.g. 144 fps)
	if (_audioBatchCb != nullptr) {
		constexpr std::int32_t MaxAudioFramesPerRun = 48000 / 50 + 1;	// QueryTargetFps() floors at 50 fps
		static std::int16_t audioBuffer[MaxAudioFramesPerRun * 2];
		static double audioFramesAcc = 0.0;
		audioFramesAcc += AudioSampleRate / LibretroGfxDevice::GetTargetFps();
		std::int32_t numFrames = (std::int32_t)audioFramesAcc;
		audioFramesAcc -= numFrames;
		if (numFrames > MaxAudioFramesPerRun) {
			numFrames = MaxAudioFramesPerRun;
		}
		if (!theServiceLocator().GetAudioDevice().renderSamples(audioBuffer, numFrames)) {
			// No audio device available, keep the frontend's pacing fed with silence
			std::memset(audioBuffer, 0, (std::size_t)numFrames * 2 * sizeof(std::int16_t));
		}
		_audioBatchCb(audioBuffer, numFrames);
	}
	if (theLibretroApplication().ShouldQuit()) {
		LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
	}
}

// Save states piggyback on the application's own resumable session snapshot: it is restored on the
// next frame instead of the current one, so it is fine for manual save/load but unsuitable for
// netplay/run-ahead/rewind.
// Fixed generous cap, the snapshot is deflate-compressed and typically tens of KB
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
	// Fails outside a resumable session (menus, cinematics) - the frontend just reports that
	// saving failed
	if (!theLibretroApplication().SaveState(ms) || (std::size_t)ms.GetSize() > size) {
		return false;
	}
	std::memcpy(data, ms.GetBuffer(), (std::size_t)ms.GetSize());
	std::memset((std::uint8_t*)data + ms.GetSize(), 0, size - (std::size_t)ms.GetSize());
	return true;
}

RETRO_API bool retro_unserialize(const void* data, size_t size)
{
	if (!_gameInitialized || size == 0) {
		return false;
	}
	auto ms = std::make_shared<MemoryStream>(Containers::InPlaceInit,
		Containers::ArrayView<const std::uint8_t>((const std::uint8_t*)data, (std::size_t)size));
	return theLibretroApplication().LoadState(std::move(ms));
}

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char* code) {}

RETRO_API bool retro_load_game(const struct retro_game_info* game)
{
	// The engine may initialize right below, so the game rate must be known before that
	ApplyTargetFps(QueryTargetFps());

	// A libretro core shares its process with the frontend, so the working directory is not the
	// core's to change - the application resolves its data directories from these paths instead
	LibretroApplication::HostPaths hostPaths;
	const char* systemDir = nullptr;
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemDir) && systemDir != nullptr) {
		hostPaths.System = systemDir;
	}
	// Frontends that separate engine data from the user's files provide it separately
	// (Recalbox: core_assets = bios/jazz2)
	const char* assetsDir = nullptr;
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY, &assetsDir) && assetsDir != nullptr) {
		hostPaths.CoreAssets = assetsDir;
	}
	// Everything the application writes (configuration, progress, highscores) belongs here, the
	// frontend backs this directory up and lets the user relocate it
	const char* saveDir = nullptr;
	if (LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &saveDir) && saveDir != nullptr) {
		hostPaths.Save = saveDir;
	}
	if (game != nullptr && game->path != nullptr) {
		hostPaths.Content = game->path;
	}
	theLibretroApplication().SetHostPaths(std::move(hostPaths));

	retro_pixel_format pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
	if (!LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixelFormat)) {
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
	if (!LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_SET_HW_RENDER, &_hwRender)) {
		return false;
	}
	LibretroGfxDevice::SetCurrentFramebufferCallback(_hwRender.get_current_framebuffer);
	_pendingInit = true;
	return true;
#else
	_gameInitialized = theLibretroApplication().Init(CreateAppEventHandler);
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
