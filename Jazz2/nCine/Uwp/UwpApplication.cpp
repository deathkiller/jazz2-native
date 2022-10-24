#include "UwpApplication.h"
#include "AngleGfxDevice.h"
#include "XinputInputManager.h"
#include "../IAppEventHandler.h"
#include "../Input/IInputManager.h"
#include "../IO/FileSystem.h"
#include "../../Common.h"

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Popups.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.ApplicationModel.Core.h>

#include <Utf8.h>

namespace nCine
{
	static UwpApplication* _instance;

	static std::unique_ptr<IAppEventHandler>(*_createAppEventHandler)();

	Application& theApplication()
	{
		return *_instance;
	}

	static void RaiseExceptionDialog(const winrtWUC::CoreDispatcher& dispatcher, winrt::hstring const& msg)
	{
		dispatcher.RunIdleAsync([msg](auto args) {
			winrtWUP::MessageDialog dialog(msg);
			dialog.ShowAsync();
		});
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	int UwpApplication::start(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)())
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

		winrt::init_apartment();

		// Force set current directory, so everything is loaded correctly, because it's not usually intended
		wchar_t pBuf[MAX_PATH];
		DWORD pBufLength = ::GetModuleFileName(nullptr, pBuf, _countof(pBuf));
		if (pBufLength > 0) {
			wchar_t* lastSlash = wcsrchr(pBuf, L'\\');
			if (lastSlash == nullptr) {
				lastSlash = wcsrchr(pBuf, L'/');
			}
			if (lastSlash != nullptr) {
				lastSlash++;
				*lastSlash = '\0';
				::SetCurrentDirectory(pBuf);
			}
		}

		_createAppEventHandler = createAppEventHandler;

		winrtWUX::Application::Start([](auto&&) { auto app = winrt::make<UwpApplication>(); });

		return EXIT_SUCCESS;
	}

	void UwpApplication::OnActivated(const winrtWAA::IActivatedEventArgs& args)
	{
		try {
			const auto& kind = args.Kind();
			if (kind == winrtWAA::ActivationKind::Protocol) {
				auto protocolArgs = args.as<winrtWAA::ProtocolActivatedEventArgs>();
				init(protocolArgs.Uri());
			} else if (kind == winrtWAA::ActivationKind::CommandLineLaunch) {
				auto cmd = args.as<winrtWAA::CommandLineActivatedEventArgs>().Operation().Arguments();
				init(winrtWF::Uri(cmd));
			} else {
				init({ nullptr });
			}
		} catch (std::exception const& ex) {
			LOGE_X("Critical exception: %s", ex.what());
			RaiseExceptionDialog(_dispatcher, winrt::to_hstring(ex.what()));
		}
	}

	void UwpApplication::OnLaunched(winrtWAA::LaunchActivatedEventArgs const& args)
	{
		auto arguments = args.Arguments();
		if (!arguments.empty()) {
			auto uri = winrtWF::Uri(L"jazz2:" + (std::wstring)arguments);
			init(uri);
		} else {
			init({ nullptr });
		}
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void UwpApplication::init(const winrtWF::Uri& uri)
	{
		_instance = this;

		profileStartTime_ = TimeStamp::now();
		//wasSuspended_ = shouldSuspend();
		appEventHandler_ = _createAppEventHandler();

		// If we have a phone contract, hide the status bar
		if (winrtWF::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Phone.PhoneContract", 1, 0)) {
			auto statusBar = winrt::Windows::UI::ViewManagement::StatusBar::GetForCurrentView();
			auto task = statusBar.HideAsync();
		}

		// Only `onPreInit()` can modify the application configuration
		AppConfiguration& modifiableAppCfg = const_cast<AppConfiguration&>(appCfg_);
		// TODO: Parse arguments from Uri
		//modifiableAppCfg.argc_ = argc;
		//modifiableAppCfg.argv_ = argv;
		appEventHandler_->onPreInit(modifiableAppCfg);
		LOGI("IAppEventHandler::onPreInit() invoked");

		// Graphics device should always be created before the input manager!
		auto window = winrtWUX::Window::Current();

		window.SizeChanged([](const auto&, const winrtWUC::WindowSizeChangedEventArgs& args) {
			auto& gfxDevice = dynamic_cast<AngleGfxDevice&>(theApplication().gfxDevice());
			gfxDevice._sizeChanged = 2;
		});

		window.Content(_panel);

		winrtWUV::ApplicationView::TerminateAppOnFinalViewClose(true);
		//auto windowTitleW = Death::Utf8::ToUtf16(appCfg_.windowTitle);
		//winrtWUV::ApplicationView::GetForCurrentView().Title(winrt::hstring(windowTitleW.data(), windowTitleW.size()));

		winrt::Windows::ApplicationModel::Core::CoreApplication::GetCurrentView().TitleBar().ExtendViewIntoTitleBar(true);
		auto titleBar = winrtWUV::ApplicationView::GetForCurrentView().TitleBar();
		titleBar.ButtonBackgroundColor(winrt::Windows::UI::Color { 0, 0, 0, 0 });
		titleBar.ButtonForegroundColor(winrt::Windows::UI::Color { 255, 255, 255, 255 });
		titleBar.ButtonHoverBackgroundColor(winrt::Windows::UI::Color { 80, 0, 0, 0 });
		titleBar.ButtonHoverForegroundColor(winrt::Windows::UI::Color { 255, 255, 255, 255 });
		titleBar.ButtonInactiveBackgroundColor(winrt::Windows::UI::Color { 0, 0, 0, 0 });
		titleBar.ButtonInactiveForegroundColor(winrt::Windows::UI::Color { 160, 255, 255, 255 });

		if (appCfg_.fullscreen) {
			winrtWUV::ApplicationView::PreferredLaunchWindowingMode(winrtWUV::ApplicationViewWindowingMode::FullScreen);
			window.Activate();
		} else if (appCfg_.resolution.X > 0 && appCfg_.resolution.Y > 0) {
			float dpi = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().LogicalDpi();
			winrtWUV::ApplicationView::PreferredLaunchWindowingMode(winrtWUV::ApplicationViewWindowingMode::PreferredLaunchViewSize);
			winrtWF::Size desiredSize = winrtWF::Size((appCfg_.resolution.X * IGfxDevice::DefaultDPI / dpi), (appCfg_.resolution.Y * IGfxDevice::DefaultDPI / dpi));
			winrtWUV::ApplicationView::PreferredLaunchViewSize(desiredSize);
			window.Activate();
			winrtWUV::ApplicationView::GetForCurrentView().TryResizeView(desiredSize);
		} else {
			window.Activate();
		}

		_dispatcher = window.Dispatcher();

		IGfxDevice::GLContextInfo glContextInfo(appCfg_);
		const DisplayMode::VSync vSyncMode = (appCfg_.withVSync ? DisplayMode::VSync::Enabled : DisplayMode::VSync::Disabled);
		DisplayMode displayMode(8, 8, 8, 8, 24, 8, DisplayMode::DoubleBuffering::Enabled, vSyncMode);
		const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.fullscreen, appCfg_.resizable);

		gfxDevice_ = std::make_unique<AngleGfxDevice>(windowMode, glContextInfo, displayMode, _panel);
		inputManager_ = std::make_unique<XinputInputManager>();

		_renderLoopThread = std::thread([this] { this->run(); });

		gfxDevice_->setWindowTitle(appCfg_.windowTitle.data());
		if (!appCfg_.windowIconFilename.empty()) {
			String windowIconFilePath = fs::JoinPath(fs::GetDataPath(), appCfg_.windowIconFilename);
			if (fs::IsReadableFile(windowIconFilePath)) {
				gfxDevice_->setWindowIcon(windowIconFilePath);
			}
		}

		timings_[Timings::PreInit] = profileStartTime_.secondsSince();
	}

	void UwpApplication::run()
	{
		auto gfx = dynamic_cast<AngleGfxDevice*>(gfxDevice_.get());
		gfx->MakeCurrent();

		initCommon();

		while (!shouldQuit_) {
			step();
		}

		shutdownCommon();
	}

}