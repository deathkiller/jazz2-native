#include "UwpApplication.h"
#include "UwpGfxDevice.h"
#include "UwpInputManager.h"
#include "../../IAppEventHandler.h"
#include "../../Input/IInputManager.h"
#include "../../../Main.h"

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Graphics.Display.h>

#include <Utf8.h>
#include <IO/FileSystem.h>

using namespace Death;
using namespace Death::IO;
using namespace nCine::Backends;

namespace nCine
{
	static UwpApplication* _instance;
	static std::unique_ptr<IAppEventHandler>(*_createAppEventHandler)();

	winrtWUC::CoreDispatcher UwpApplication::_dispatcher = nullptr;

	Application& theApplication()
	{
		return *_instance;
	}

	int UwpApplication::Run(CreateAppEventHandlerDelegate createAppEventHandler)
	{
		if (createAppEventHandler == nullptr) {
			return EXIT_FAILURE;
		}

		winrt::init_apartment();

		// Force set current directory, so everything is loaded correctly, because it's not usually intended
		wchar_t path[fs::MaxPathLength];
		DWORD pathLength = ::GetModuleFileNameW(NULL, path, (DWORD)arraySize(path));
		if (pathLength > 0) {
			wchar_t* lastSlash = wcsrchr(path, L'\\');
			if (lastSlash == nullptr) {
				lastSlash = wcsrchr(path, L'/');
			}
			if (lastSlash != nullptr) {
				lastSlash++;
				*lastSlash = '\0';
				::SetCurrentDirectoryW(path);
			}
		}

		_createAppEventHandler = createAppEventHandler;

		winrtWAC::CoreApplication::Run(winrt::make<UwpApplication>());

		return EXIT_SUCCESS;
	}

	void UwpApplication::Initialize(const winrtWAC::CoreApplicationView& applicationView)
	{
		_instance = this;

		applicationView.Activated({ this, &UwpApplication::OnActivated });

		winrtWAC::CoreApplication::Suspending({ this, &UwpApplication::OnSuspending });
		winrtWAC::CoreApplication::Resuming({ this, &UwpApplication::OnResuming });
		winrtWAC::CoreApplication::EnteredBackground({ this, &UwpApplication::OnEnteredBackground });
		winrtWAC::CoreApplication::LeavingBackground({ this, &UwpApplication::OnLeavingBackground });
	}
	
	void UwpApplication::SetWindow(const winrtWUC::CoreWindow& window)
	{
		window.Closed({ this, &UwpApplication::OnWindowClosed });
		window.SizeChanged({ this, &UwpApplication::OnWindowSizeChanged });

		winrtWUC::SystemNavigationManager::GetForCurrentView().BackRequested([](const auto&, winrtWUC::BackRequestedEventArgs args) {
			args.Handled(true);
		});
	}

	void UwpApplication::Load(const winrt::hstring& entryPoint)
	{
		// Nothing to do
	}

	void UwpApplication::Run()
	{
		auto* gfxDevice = static_cast<UwpGfxDevice*>(gfxDevice_.get());
		gfxDevice->MakeCurrent();

		InitCommon();

		while (!shouldQuit_) {
			_dispatcher.ProcessEvents(winrtWUC::CoreProcessEventsOption::ProcessAllIfPresent);

			if (!ShouldSuspend()) {
				UwpInputManager::updateJoystickStates();
				Step();
			}
		}
	}

	void UwpApplication::Uninitialize()
	{
		ShutdownCommon();
	}

	bool UwpApplication::OpenUrl(StringView url)
	{
		if (url.empty()) {
			return false;
		}

		try {
			auto urlW = Utf8::ToUtf16(url);
			auto asyncOp = winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri{
				winrt::hstring{urlW.data(), static_cast<winrt::hstring::size_type>(urlW.size())}});
			auto result = asyncOp.get(); // Wait for the operation to complete
			return result;
		} catch (...) {
			return false;
		}
	}

	void UwpApplication::OnActivated(const winrtWAC::CoreApplicationView& applicationView, const winrtWAA::IActivatedEventArgs& args)
	{
		/*const auto& kind = args.Kind();
		if (kind == winrtWAA::ActivationKind::Protocol) {
			auto protocolArgs = args.as<winrtWAA::ProtocolActivatedEventArgs>();
			init(protocolArgs.Uri());
		} else if (kind == winrtWAA::ActivationKind::CommandLineLaunch) {
			auto cmd = args.as<winrtWAA::CommandLineActivatedEventArgs>().Operation().Arguments();
			init(winrtWF::Uri(cmd));
		} else {
			init({ nullptr });
		}*/

#if defined(NCINE_PROFILING)
		profileStartTime_ = TimeStamp::now();
#endif
		//wasSuspended_ = shouldSuspend();

		// If we have a phone contract, hide the status bar
		if (winrtWF::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Phone.PhoneContract", 1, 0)) {
			auto statusBar = winrtWUV::StatusBar::GetForCurrentView();
			auto task = statusBar.HideAsync();
		}

		// Only `OnPreInitialize()` can modify the application configuration
		// TODO: Parse arguments from Uri
		//appCfg_.argc_ = argc;
		//appCfg_.argv_ = argv;
		PreInitCommon(_createAppEventHandler());

		winrtWUC::CoreWindow window = winrtWUC::CoreWindow::GetForCurrentThread();

		//auto windowTitleW = Utf8::ToUtf16(appCfg_.windowTitle);
		//winrtWUV::ApplicationView::GetForCurrentView().Title(winrt::hstring(windowTitleW.data(), windowTitleW.size()));

		winrtWAC::CoreApplication::GetCurrentView().TitleBar().ExtendViewIntoTitleBar(true);
		auto titleBar = winrtWUV::ApplicationView::GetForCurrentView().TitleBar();
		titleBar.ButtonBackgroundColor(winrtWU::Color{0, 0, 0, 0});
		titleBar.ButtonForegroundColor(winrtWU::Color{255, 255, 255, 255});
		titleBar.ButtonHoverBackgroundColor(winrtWU::Color{80, 0, 0, 0});
		titleBar.ButtonHoverForegroundColor(winrtWU::Color{255, 255, 255, 255});
		titleBar.ButtonInactiveBackgroundColor(winrtWU::Color{0, 0, 0, 0});
		titleBar.ButtonInactiveForegroundColor(winrtWU::Color{160, 255, 255, 255});

		auto displayInfo = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

		if (appCfg_.fullscreen) {
			winrtWUV::ApplicationView::PreferredLaunchWindowingMode(winrtWUV::ApplicationViewWindowingMode::FullScreen);
			window.Activate();
		} else if (appCfg_.resolution.X > 0 && appCfg_.resolution.Y > 0) {
			winrtWUV::ApplicationView::PreferredLaunchWindowingMode(winrtWUV::ApplicationViewWindowingMode::PreferredLaunchViewSize);
			winrtWF::Size desiredSize = winrtWF::Size(appCfg_.resolution.X, appCfg_.resolution.Y);
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
		const IGfxDevice::WindowMode windowMode(appCfg_.resolution.X, appCfg_.resolution.Y, appCfg_.windowPosition.X, appCfg_.windowPosition.Y, appCfg_.fullscreen, appCfg_.resizable, appCfg_.windowScaling);

		// Graphics device should always be created before the input manager!
		gfxDevice_ = std::make_unique<UwpGfxDevice>(windowMode, glContextInfo, displayMode, window);
		inputManager_ = std::make_unique<UwpInputManager>(window);

		displayInfo.DpiChanged([](const auto&, const auto& args) {
			auto& gfxDevice = static_cast<UwpGfxDevice&>(_instance->GetGfxDevice());
			gfxDevice.updateMonitors();
		});

		gfxDevice_->setWindowTitle(appCfg_.windowTitle.data());
		// TODO: Not supported
		//if (!appCfg_.windowIconFilename.empty()) {
		//	String windowIconFilePath = fs::CombinePath(GetDataPath(), appCfg_.windowIconFilename);
		//	if (fs::IsReadableFile(windowIconFilePath)) {
		//		gfxDevice_->setWindowIcon(windowIconFilePath);
		//	}
		//}

#if defined(NCINE_PROFILING)
		timings_[(std::int32_t)Timings::PreInit] = profileStartTime_.secondsSince();
#endif
	}

	void UwpApplication::OnSuspending(const winrtWF::IInspectable& sender, const winrtWA::SuspendingEventArgs& args)
	{
		if (!_isSuspended) {
			_isSuspended = true;
			Suspend();
		}
	}

	void UwpApplication::OnResuming(const winrtWF::IInspectable& sender, const winrtWF::IInspectable& args)
	{
		if (_isSuspended) {
			_isSuspended = false;
			Resume();
		}
	}

	void UwpApplication::OnEnteredBackground(const winrtWF::IInspectable& sender, const winrtWA::EnteredBackgroundEventArgs& args)
	{
		if (!_isSuspended) {
			_isSuspended = true;
			Suspend();
		}
	}

	void UwpApplication::OnLeavingBackground(const winrtWF::IInspectable& sender, const winrtWA::LeavingBackgroundEventArgs& args)
	{
		if (_isSuspended) {
			_isSuspended = false;
			Resume();
		}
	}

	void UwpApplication::OnWindowClosed(const winrtWF::IInspectable& sender, const winrtWUC::CoreWindowEventArgs& args)
	{
		shouldQuit_ = true;
	}

	void UwpApplication::OnWindowSizeChanged(const winrtWF::IInspectable& sender, winrtWUC::WindowSizeChangedEventArgs const& args)
	{
		auto* gfxDevice = static_cast<UwpGfxDevice*>(gfxDevice_.get());
		if (gfxDevice != nullptr) {
			gfxDevice->isFullscreen_ = winrtWUV::ApplicationView::GetForCurrentView().IsFullScreenMode();
			gfxDevice->_sizeChanged = 10;
		}
	}
}