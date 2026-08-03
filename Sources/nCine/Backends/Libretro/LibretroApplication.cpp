#include "LibretroApplication.h"

#if defined(WITH_LIBRETRO)

#include "LibretroGfxDevice.h"
#include "LibretroInputManager.h"
#include "../../IAppEventHandler.h"

namespace nCine
{
	Application& theApplication()
	{
		static Backends::LibretroApplication instance;
		return instance;
	}
}

namespace nCine::Backends
{
	retro_environment_t LibretroApplication::EnvironmentCallback = nullptr;
	retro_video_refresh_t LibretroApplication::VideoRefreshCallback = nullptr;
	retro_input_poll_t LibretroApplication::InputPollCallback = nullptr;
	retro_input_state_t LibretroApplication::InputStateCallback = nullptr;
	bool LibretroApplication::IsInsideFrame = false;

	LibretroApplication& theLibretroApplication()
	{
		return static_cast<LibretroApplication&>(theApplication());
	}

	void LibretroApplication::ShowMessage(const char* text)
	{
		if (EnvironmentCallback == nullptr) {
			return;
		}

		retro_message message = {};
		message.msg = text;
		message.frames = 180;
		EnvironmentCallback(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
	}

	bool LibretroApplication::Init(CreateAppEventHandlerDelegate createAppEventHandler)
	{
		PreInitCommon(createAppEventHandler());
		if (shouldQuit_) {
			return false;
		}

		// Pacing is normally the frontend's job (vsync or audio sync), but the engine steps a
		// fixed 1/fps per retro_run, so an unthrottled frontend would fast-forward the game:
		// keep the engine's own frame limiter as a backstop. It is bypassed automatically
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

	void LibretroApplication::RunFrame()
	{
		static_cast<LibretroGfxDevice&>(*gfxDevice_).beginFrame();
		static_cast<LibretroInputManager&>(*inputManager_).processFrame();
		Step();
	}

	void LibretroApplication::SetFrameLimiter(bool enabled)
	{
		appCfg_.frameLimit = (enabled ? (std::uint32_t)(LibretroGfxDevice::GetTargetFps() + 0.5) : 0);
	}

	void LibretroApplication::Shutdown()
	{
		ShutdownCommon();
	}

	bool LibretroApplication::SaveState(Death::IO::Stream& dest)
	{
		return (appEventHandler_ != nullptr && appEventHandler_->OnSaveState(dest));
	}

	bool LibretroApplication::LoadState(std::shared_ptr<Death::IO::Stream> src)
	{
		return (appEventHandler_ != nullptr && appEventHandler_->OnLoadState(std::move(src)));
	}
}

#endif
