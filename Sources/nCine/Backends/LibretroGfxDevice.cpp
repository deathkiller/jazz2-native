#include "LibretroGfxDevice.h"

#if defined(WITH_LIBRETRO)

#include "LibretroApplication.h"
#include "../Graphics/RHI/Rhi.h"

#if defined(WITH_RHI_GL)
#	include "../Graphics/RHI/GL/GLDevice.h"
#	include "../Graphics/RHI/GL/GLFramebuffer.h"
#endif

#include <cstring>

namespace nCine::Backends
{
	namespace
	{
		// Largest framebuffer advertised to the frontend so far - the engine resolution comes from the
		// game configuration, so it can exceed the default the core starts with
		std::int32_t _maxWidth = 1920;
		std::int32_t _maxHeight = 1080;
#if defined(WITH_RHI_GL)
		retro_hw_get_current_framebuffer_t _currentFramebufferCb = nullptr;
#endif
	}

	LibretroGfxDevice::LibretroGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
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

	void LibretroGfxDevice::FillSystemAvInfo(retro_system_av_info& info, std::int32_t width, std::int32_t height)
	{
		std::memset(&info, 0, sizeof(info));
		info.geometry.base_width = (unsigned)width;
		info.geometry.base_height = (unsigned)height;
		info.geometry.max_width = (unsigned)_maxWidth;
		info.geometry.max_height = (unsigned)_maxHeight;
		info.geometry.aspect_ratio = (float)width / (float)height;
		info.timing.fps = 60.0;
		// Audio goes straight out through OpenAL, this rate only satisfies the frontend
		info.timing.sample_rate = 48000.0;
	}

	bool LibretroGfxDevice::InitializeGraphicsLibrary()
	{
#if defined(WITH_RHI_GL) && defined(WITH_GLEW)
		glewExperimental = GL_TRUE;
		const GLenum glewErr = glewInit();
		if (glewErr != GLEW_OK && glewErr != GLEW_ERROR_NO_GLX_DISPLAY) {
			return false;
		}
		glGetError();	// glewInit() can leave a stray GL error behind
#endif
		return true;
	}

	void LibretroGfxDevice::SetCurrentFramebufferCallback(retro_hw_get_current_framebuffer_t callback)
	{
#if defined(WITH_RHI_GL)
		_currentFramebufferCb = callback;
#endif
	}

	void LibretroGfxDevice::beginFrame()
	{
#if defined(WITH_RHI_GL)
		// The frontend renders its own UI with the same GL context between two frames: repoint the
		// "screen" at its FBO (the id can change every frame) and re-sync every cached GL state.
		// get_current_framebuffer is obsolete upstream, so a frontend is not obliged to provide it -
		// without it the default framebuffer is the only target left
		GLuint defaultHandle = (_currentFramebufferCb != nullptr ? (GLuint)_currentFramebufferCb() : 0);
		RHI::GL::GLFramebuffer::SetDefaultHandle(defaultHandle);
		RHI::GL::GLDevice::ResyncExternalStateChanges();
#endif
	}

	void LibretroGfxDevice::update()
	{
#if defined(WITH_RHI_GL)
		// Hardware rendering: the frame was drawn straight into the frontend's FBO
		// (GLFramebuffer::SetDefaultHandle), just tell the frontend it is ready
		if (LibretroApplication::IsInsideFrame) {
			LibretroApplication::VideoRefreshCallback(RETRO_HW_FRAME_BUFFER_VALID, (unsigned)drawableWidth_, (unsigned)drawableHeight_, 0);
		}
#else
		// Same presentation sequence as SdlGfxDevice::presentSoftware()
		RHI::Device::FlushSoftwareRenderer();
		RHI::Device::EndFrame();
		if (!LibretroApplication::IsInsideFrame) {
			return;
		}
		const auto fb = RHI::Device::GetScreenFramebuffer();
		if (fb.pixels == nullptr || fb.width <= 0 || fb.height <= 0) {
			LibretroApplication::VideoRefreshCallback(nullptr, _lastWidth, _lastHeight, _lastWidth * 4);
			return;
		}

		if (fb.width != _lastWidth || fb.height != _lastHeight) {
			_lastWidth = fb.width;
			_lastHeight = fb.height;
			if (fb.width > _maxWidth || fb.height > _maxHeight) {
				// SET_GEOMETRY ignores max_width/max_height, only SET_SYSTEM_AV_INFO can raise
				// the advertised maximum (at the cost of reinitializing the frontend's drivers)
				_maxWidth = (fb.width > _maxWidth ? fb.width : _maxWidth);
				_maxHeight = (fb.height > _maxHeight ? fb.height : _maxHeight);
				retro_system_av_info avInfo;
				FillSystemAvInfo(avInfo, fb.width, fb.height);
				LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &avInfo);
			} else {
				retro_game_geometry geometry = {};
				geometry.base_width = (unsigned)fb.width;
				geometry.base_height = (unsigned)fb.height;
				geometry.max_width = (unsigned)_maxWidth;
				geometry.max_height = (unsigned)_maxHeight;
				geometry.aspect_ratio = (float)fb.width / (float)fb.height;
				LibretroApplication::EnvironmentCallback(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
			}
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
		LibretroApplication::VideoRefreshCallback(_converted.data(), fb.width, fb.height, fb.width * 4);
#endif
	}
}

#endif
