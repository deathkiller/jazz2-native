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
		window.Content(_panel);
		window.Activate();
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