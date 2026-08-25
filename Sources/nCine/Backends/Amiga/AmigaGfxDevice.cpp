#if defined(WITH_AMIGA)

#include "AmigaGfxDevice.h"
#include "AmigaPlatform.h"
#include "../../Graphics/RHI/Rhi.h"
#include "../../../Main.h"

#include <cstring>

#include <exec/exec.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/displayinfo.h>
#include <graphics/gfx.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>

using namespace Death::Containers::Literals;

namespace nCine::Backends
{
	namespace
	{
		/**
			@brief What one performance preset asks of the display, in screen pixels

			The budget is the present-copy cost the tier can afford per frame; the chosen mode is the one
			whose LOGICAL resolution (the 720x405-clamped aspect fit the game renders at, the same rule as
			LevelHandler::OnInitializeViewport) is highest within it. That is why a 640x400 mode beats a
			640x480 one where both exist: it yields MORE logical pixels (640x400 vs 540x405) for LESS
			present work - the game's box simply fits 16:10 better than 4:3.
		*/
		std::int32_t ScreenPixelBudget(AmigaPlatform::PerformanceClass performanceClass)
		{
			switch (performanceClass) {
				case AmigaPlatform::PerformanceClass::Ultra: return 640 * 512;
				case AmigaPlatform::PerformanceClass::High: return 640 * 360;
				case AmigaPlatform::PerformanceClass::Medium: return 384 * 300;
				default: return 320 * 220;
			}
		}

		void LogicalSizeFor(std::int32_t width, std::int32_t height, std::int32_t& logicalWidth, std::int32_t& logicalHeight)
		{
			// The game's viewport rule: clamp each axis to the 720x405 box, then restore the screen's
			// aspect ratio by shrinking the axis that overshoots it
			std::int32_t w = (width < 720 ? width : 720);
			std::int32_t h = (height < 405 ? height : 405);
			// Compare w/h against width/height without leaving integers: w*height vs width*h
			if (std::int64_t(w) * height > std::int64_t(width) * h) {
				w = std::int32_t((std::int64_t(h) * width) / height);
			} else {
				h = std::int32_t((std::int64_t(w) * height) / width);
			}
			logicalWidth = w;
			logicalHeight = h;
		}
	}

	AmigaGfxDevice::AmigaGfxDevice(const WindowMode& windowMode, const ContextInfo& contextInfo, const DisplayMode& displayMode)
		: IGfxDevice(windowMode, contextInfo, displayMode), _screen(nullptr), _window(nullptr), _screenTitle("nCine"_s),
			_screenBuffers{}, _safePort(nullptr), _dispPort(nullptr), _backBuffer(1),
			_safeToWrite(true), _doubleBuffered(false), _pixelFormat(0), _pixelFormatLogged(false)
	{
		const AmigaPlatform::PerformanceClass performanceClass = AmigaPlatform::GetPerformanceClass();
		FATAL_ASSERT_MSG(openDisplay(windowMode.width, windowMode.height),
			"Cannot open an RTG screen (a CyberGraphX/Picasso96 16-bit screen mode is required)");

		_drawableWidth = _width;
		_drawableHeight = _height;
		_isFullscreen = true;

		_currentVideoMode.width = std::uint32_t(_width);
		_currentVideoMode.height = std::uint32_t(_height);
		_currentVideoMode.refreshRate = 60.0f;
		_currentVideoMode.redBits = 5;
		_currentVideoMode.greenBits = 6;
		_currentVideoMode.blueBits = 5;

		updateMonitors();
		initDeviceViewport();

		LOGI("Video mode initialized: {}x{} 16-bit RTG ({} preset)", _width, _height,
			performanceClass == AmigaPlatform::PerformanceClass::Ultra ? "Ultra"
				: performanceClass == AmigaPlatform::PerformanceClass::High ? "High"
				: performanceClass == AmigaPlatform::PerformanceClass::Medium ? "Medium" : "Low");
	}

	AmigaGfxDevice::~AmigaGfxDevice()
	{
		closeDisplay();
	}

	bool AmigaGfxDevice::openDisplay(std::int32_t requestedWidth, std::int32_t requestedHeight)
	{
		// Walk the display database for RTG modes at 2 bytes per pixel and pick by logical fidelity
		// within the preset's budget; the requested size (usually unset on this platform) only raises
		// the budget so an explicit "-w/-h" from the command line is honored when such a mode exists.
		std::int32_t budget = ScreenPixelBudget(AmigaPlatform::GetPerformanceClass());
		if (requestedWidth > 0 && requestedHeight > 0 && requestedWidth * requestedHeight > budget) {
			budget = requestedWidth * requestedHeight;
		}

		ULONG bestMode = INVALID_ID;
		std::int32_t bestWidth = 0, bestHeight = 0;
		std::int64_t bestScore = 0;
		ULONG smallestMode = INVALID_ID;
		std::int32_t smallestWidth = 0, smallestHeight = 0;
		std::int64_t smallestPixels = 0;

		ULONG modeId = INVALID_ID;
		while ((modeId = NextDisplayInfo(modeId)) != ULONG(INVALID_ID)) {
			if (!IsCyberModeID(modeId)) {
				continue;
			}
			const ULONG bytesPerPixel = GetCyberIDAttr(CYBRIDATTR_BPPIX, modeId);
			if (bytesPerPixel != 2) {
				continue;
			}
			const std::int32_t width = std::int32_t(GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId));
			const std::int32_t height = std::int32_t(GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId));
			if (width < 320 || height < 200) {
				continue;
			}
			const std::int64_t pixels = std::int64_t(width) * height;
			if (smallestMode == ULONG(INVALID_ID) || pixels < smallestPixels) {
				smallestMode = modeId;
				smallestWidth = width;
				smallestHeight = height;
				smallestPixels = pixels;
			}
			if (pixels > budget) {
				continue;
			}
			std::int32_t logicalWidth, logicalHeight;
			LogicalSizeFor(width, height, logicalWidth, logicalHeight);
			// Logical fidelity first; among equals the cheaper present (fewer screen pixels) wins
			const std::int64_t score = std::int64_t(logicalWidth) * logicalHeight * 16 - pixels;
			if (bestMode == ULONG(INVALID_ID) || score > bestScore) {
				bestMode = modeId;
				bestWidth = width;
				bestHeight = height;
				bestScore = score;
			}
		}

		if (bestMode == ULONG(INVALID_ID)) {
			// Nothing inside the budget: take the smallest mode there is rather than refusing to run
			bestMode = smallestMode;
			bestWidth = smallestWidth;
			bestHeight = smallestHeight;
		}
		if (bestMode == ULONG(INVALID_ID)) {
			LOGE("No 16-bit RTG screen mode found in the display database");
			return false;
		}

		_pixelFormat = GetCyberIDAttr(CYBRIDATTR_PIXFMT, bestMode);
		LOGI("Chosen screen mode 0x{:x}: {}x{}, pixel format {}", ULONG(bestMode), bestWidth, bestHeight, _pixelFormat);

		_screen = OpenScreenTags(nullptr,
			SA_DisplayID, ULONG(bestMode),
			SA_Width, ULONG(bestWidth),
			SA_Height, ULONG(bestHeight),
			SA_Depth, 16,
			SA_Type, CUSTOMSCREEN,
			SA_Quiet, TRUE,
			SA_ShowTitle, FALSE,
			SA_Title, ULONG(reinterpret_cast<std::uintptr_t>(_screenTitle.data())),
			TAG_DONE);
		if (_screen == nullptr) {
			LOGE("OpenScreenTags() failed for mode 0x{:x}", ULONG(bestMode));
			return false;
		}

		// Double buffering through Intuition's screen-buffer protocol: the first buffer wraps the
		// screen's own bitmap, the second is allocated; the safe/display message ports tell when the
		// previous frame has left the back buffer so writing into it cannot tear. If the second buffer
		// does not fit in video memory the game still runs single-buffered.
		_safePort = CreateMsgPort();
		_dispPort = CreateMsgPort();
		_screenBuffers[0] = AllocScreenBuffer(_screen, nullptr, SB_SCREEN_BITMAP);
		_screenBuffers[1] = AllocScreenBuffer(_screen, nullptr, 0);
		if (_safePort != nullptr && _dispPort != nullptr && _screenBuffers[0] != nullptr && _screenBuffers[1] != nullptr) {
			for (std::int32_t i = 0; i < 2; i++) {
				_screenBuffers[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = _safePort;
				_screenBuffers[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = _dispPort;
			}
			_doubleBuffered = true;
			_backBuffer = 1;
			_safeToWrite = true;
		} else {
			LOGW("Double buffering unavailable (video memory?), running single-buffered");
			if (_screenBuffers[1] != nullptr) {
				FreeScreenBuffer(_screen, _screenBuffers[1]);
				_screenBuffers[1] = nullptr;
			}
			_doubleBuffered = false;
			_backBuffer = 0;
		}

		// A borderless backdrop window covering the screen: it exists to receive IDCMP input (RAWKEY,
		// mouse) for the input manager, and to own the mouse pointer state
		_window = OpenWindowTags(nullptr,
			WA_CustomScreen, ULONG(reinterpret_cast<std::uintptr_t>(_screen)),
			WA_Left, 0,
			WA_Top, 0,
			WA_Width, ULONG(bestWidth),
			WA_Height, ULONG(bestHeight),
			WA_Backdrop, TRUE,
			WA_Borderless, TRUE,
			WA_Activate, TRUE,
			WA_RMBTrap, TRUE,
			WA_ReportMouse, TRUE,
			WA_SimpleRefresh, TRUE,
			WA_NoCareRefresh, TRUE,
			WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE,
			TAG_DONE);
		if (_window == nullptr) {
			LOGE("OpenWindowTags() failed");
			return false;
		}

		AmigaPlatform::GameScreen = _screen;
		AmigaPlatform::GameWindow = _window;

		_width = bestWidth;
		_height = bestHeight;
		return true;
	}

	void AmigaGfxDevice::closeDisplay()
	{
		AmigaPlatform::GameWindow = nullptr;
		AmigaPlatform::GameScreen = nullptr;

		if (_window != nullptr) {
			CloseWindow(_window);
			_window = nullptr;
		}
		if (_screen != nullptr) {
			// Drain any outstanding buffer messages before their storage goes away
			if (_safePort != nullptr) {
				while (GetMsg(_safePort) != nullptr) {}
			}
			if (_dispPort != nullptr) {
				while (GetMsg(_dispPort) != nullptr) {}
			}
			for (std::int32_t i = 0; i < 2; i++) {
				if (_screenBuffers[i] != nullptr) {
					FreeScreenBuffer(_screen, _screenBuffers[i]);
					_screenBuffers[i] = nullptr;
				}
			}
			CloseScreen(_screen);
			_screen = nullptr;
		}
		if (_dispPort != nullptr) {
			DeleteMsgPort(_dispPort);
			_dispPort = nullptr;
		}
		if (_safePort != nullptr) {
			DeleteMsgPort(_safePort);
			_safePort = nullptr;
		}
	}

	void AmigaGfxDevice::update()
	{
		// Render any draws the tile renderer deferred this frame into the screen buffer before reading it,
		// and drop unconsumed lighting entries - the same sequence as the SDL software present
		RHI::Device::FlushSoftwareRenderer();
		RHI::Device::EndFrame();
		presentFramebuffer();
	}

	void AmigaGfxDevice::presentFramebuffer()
	{
		const auto fb = RHI::Device::GetScreenFramebuffer();
		if (fb.pixels == nullptr || fb.width <= 0 || fb.height <= 0) {
			return;
		}

		if (_doubleBuffered && !_safeToWrite) {
			// The back buffer may still be scanned out from the previous flip; its safe message says
			// when writing cannot tear. This blocks at most until the next vertical blank.
			while (GetMsg(_safePort) == nullptr) {
				WaitPort(_safePort);
			}
			_safeToWrite = true;
		}

		struct BitMap* bitmap = (_screenBuffers[_backBuffer] != nullptr ? _screenBuffers[_backBuffer]->sb_BitMap : _screen->RastPort.BitMap);

		APTR baseAddress = nullptr;
		ULONG bytesPerRow = 0;
		ULONG pixelFormat = 0;
		APTR lock = LockBitMapTags(bitmap,
			LBMI_BASEADDRESS, ULONG(reinterpret_cast<std::uintptr_t>(&baseAddress)),
			LBMI_BYTESPERROW, ULONG(reinterpret_cast<std::uintptr_t>(&bytesPerRow)),
			LBMI_PIXFMT, ULONG(reinterpret_cast<std::uintptr_t>(&pixelFormat)),
			TAG_DONE);
		if (lock == nullptr) {
			return;
		}
		if (!_pixelFormatLogged) {
			LOGI("Screen bitmap: pixel format {}, {} bytes per row", pixelFormat, bytesPerRow);
			_pixelFormatLogged = true;
		}

		// The logical buffer is at most as large as the screen (the viewport rule guarantees it), so a
		// smaller one is centered; the borders are memset once per lock - cheap next to the copy itself
		const std::int32_t copyWidth = (fb.width < _width ? fb.width : _width);
		const std::int32_t copyHeight = (fb.height < _height ? fb.height : _height);
		const std::int32_t offsetX = (_width - copyWidth) / 2;
		const std::int32_t offsetY = (_height - copyHeight) / 2;

		std::uint8_t* screenBase = reinterpret_cast<std::uint8_t*>(baseAddress);
		if (offsetX != 0 || offsetY != 0) {
			for (std::int32_t y = 0; y < _height; y++) {
				if (y < offsetY || y >= offsetY + copyHeight) {
					std::memset(screenBase + std::size_t(y) * bytesPerRow, 0, std::size_t(_width) * 2);
				} else {
					std::uint8_t* row = screenBase + std::size_t(y) * bytesPerRow;
					std::memset(row, 0, std::size_t(offsetX) * 2);
					std::memset(row + std::size_t(offsetX + copyWidth) * 2, 0, std::size_t(_width - offsetX - copyWidth) * 2);
				}
			}
		}

		// The rasterizer's rows are bottom-up (OpenGL convention): screen row y reads source row
		// (copyHeight-1-y). The FB16 buffer holds native-endian RGB565, which on this big-endian CPU is
		// the RTG PIXFMT_RGB16 layout - the overwhelmingly common one, a straight row copy. The other
		// 2-byte layouts some cards scan out are converted in place per pixel.
		for (std::int32_t y = 0; y < copyHeight; y++) {
			const std::uint16_t* source = reinterpret_cast<const std::uint16_t*>(fb.pixels + std::size_t(copyHeight - 1 - y) * fb.strideBytes);
			std::uint16_t* dest = reinterpret_cast<std::uint16_t*>(screenBase + std::size_t(y + offsetY) * bytesPerRow) + offsetX;
			switch (pixelFormat) {
				default:
				case PIXFMT_RGB16:
					std::memcpy(dest, source, std::size_t(copyWidth) * 2);
					break;
				case PIXFMT_RGB16PC:
					for (std::int32_t x = 0; x < copyWidth; x++) {
						const std::uint16_t p = source[x];
						dest[x] = std::uint16_t((p >> 8) | (p << 8));
					}
					break;
				case PIXFMT_BGR16:
					for (std::int32_t x = 0; x < copyWidth; x++) {
						const std::uint16_t p = source[x];
						dest[x] = std::uint16_t(((p & 0x1F) << 11) | (p & 0x7E0) | ((p >> 11) & 0x1F));
					}
					break;
				case PIXFMT_BGR16PC:
					for (std::int32_t x = 0; x < copyWidth; x++) {
						const std::uint16_t p = source[x];
						const std::uint16_t q = std::uint16_t(((p & 0x1F) << 11) | (p & 0x7E0) | ((p >> 11) & 0x1F));
						dest[x] = std::uint16_t((q >> 8) | (q << 8));
					}
					break;
				case PIXFMT_RGB15:
				case PIXFMT_RGB15PC:
				case PIXFMT_BGR15:
				case PIXFMT_BGR15PC: {
					for (std::int32_t x = 0; x < copyWidth; x++) {
						const std::uint16_t p = source[x];
						std::uint16_t r = std::uint16_t(p >> 11);
						const std::uint16_t g = std::uint16_t((p >> 6) & 0x1F);
						std::uint16_t b = std::uint16_t(p & 0x1F);
						if (pixelFormat == PIXFMT_BGR15 || pixelFormat == PIXFMT_BGR15PC) {
							const std::uint16_t t = r; r = b; b = t;
						}
						std::uint16_t q = std::uint16_t((r << 10) | (g << 5) | b);
						if (pixelFormat == PIXFMT_RGB15PC || pixelFormat == PIXFMT_BGR15PC) {
							q = std::uint16_t((q >> 8) | (q << 8));
						}
						dest[x] = q;
					}
					break;
				}
			}
		}

		UnLockBitMap(lock);

		if (_doubleBuffered) {
			if (ChangeScreenBuffer(_screen, _screenBuffers[_backBuffer]) != 0) {
				_backBuffer ^= 1;
				_safeToWrite = false;
			}
			// A failed change (Intuition was busy) leaves the frame in the back buffer; the next present
			// simply overwrites it and tries again - dropping a flip beats blocking the game loop
		}
	}

	void AmigaGfxDevice::setSwapInterval(int interval)
	{
		// The screen-buffer flip paces to the display's vertical blank by itself
		static_cast<void>(interval);
	}

	void AmigaGfxDevice::setResolution(bool fullscreen, int width, int height)
	{
		static_cast<void>(fullscreen);
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void AmigaGfxDevice::setWindowPosition(int x, int y)
	{
		static_cast<void>(x);
		static_cast<void>(y);
	}

	void AmigaGfxDevice::setWindowSize(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void AmigaGfxDevice::setWindowTitle(StringView windowTitle)
	{
		// The screen is opened with SA_ShowTitle FALSE, so this is not drawn anywhere - but it is what
		// screen-listing tools and the Workbench screen menu show, so it is worth keeping accurate.
		// Intuition stores the pointer rather than a copy, hence the member.
		if (windowTitle.empty()) {
			return;
		}
		_screenTitle = windowTitle;
		if (_window != nullptr) {
			// (STRPTR)~0 leaves the window's own title alone
			SetWindowTitles(_window, reinterpret_cast<STRPTR>(~std::uintptr_t(0)), reinterpret_cast<STRPTR>(_screenTitle.data()));
		}
	}

	void AmigaGfxDevice::setWindowIcon(StringView windowIconFilename)
	{
		static_cast<void>(windowIconFilename);
	}

	const IGfxDevice::VideoMode& AmigaGfxDevice::currentVideoMode(unsigned int monitorIndex) const
	{
		static_cast<void>(monitorIndex);
		return _currentVideoMode;
	}

	void AmigaGfxDevice::setResolutionInternal(int width, int height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void AmigaGfxDevice::updateMonitors()
	{
		_numMonitors = 1;
		_monitors[0].name = "RTG";
		_monitors[0].position = Vector2i(0, 0);
		_monitors[0].scale = Vector2f(1.0f, 1.0f);
		_monitors[0].numVideoModes = 1;
		_monitors[0].videoModes[0] = _currentVideoMode;
	}
}

#endif
