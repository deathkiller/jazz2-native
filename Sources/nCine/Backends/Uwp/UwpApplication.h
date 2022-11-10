#pragma once

#include "../../Application.h"

#ifdef GetCurrentTime
#	undef GetCurrentTime
#endif
#include <winrt/base.h>
#include <winrt/Windows.UI.Xaml.Controls.h>

namespace winrtWAA = winrt::Windows::ApplicationModel::Activation;
namespace winrtWAC = winrt::Windows::ApplicationModel::Core;
namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWU = winrt::Windows::UI;
namespace winrtWUX = winrt::Windows::UI::Xaml;
namespace winrtWUXC = winrt::Windows::UI::Xaml::Controls;
namespace winrtWUC = winrt::Windows::UI::Core;
namespace winrtWUP = winrt::Windows::UI::Popups;
namespace winrtWUV = winrt::Windows::UI::ViewManagement;

namespace nCine
{
	/// Handler class for nCine applications on UWP
	class UwpApplication : public Application, public winrtWUX::ApplicationT<UwpApplication>
	{
	public:
		/// Entry point method to be called in the `main()` function
		static int start(std::unique_ptr<IAppEventHandler>(*createAppEventHandler)());

		UwpApplication() : Application() { }
		~UwpApplication() = default;

		winrt::hstring GetRuntimeClassName() const override { return winrt::hstring(L"App"); }

		void OnActivated(const winrtWAA::IActivatedEventArgs& args);
		void OnLaunched(winrtWAA::LaunchActivatedEventArgs const& args);

		static winrtWUC::CoreDispatcher GetDispatcher() {
			return _dispatcher;
		}

	private:
		static winrtWUC::CoreDispatcher _dispatcher;
		winrtWUXC::SwapChainPanel _panel;
		std::thread _renderLoopThread;

		/// Must be called at the beginning to initialize the application
		void init(const winrtWF::Uri& uri);

		void run();

		/// Deleted copy constructor
		UwpApplication(const UwpApplication&) = delete;
		/// Deleted assignment operator
		UwpApplication& operator=(const UwpApplication&) = delete;

		friend Application& theApplication();
	};

	/// Meyers' Singleton
	Application& theApplication();

}
