#pragma once

#include "../../Application.h"

#include <CommonWindows.h>

#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.Core.h>

namespace winrtWA = winrt::Windows::ApplicationModel;
namespace winrtWAA = winrt::Windows::ApplicationModel::Activation;
namespace winrtWAC = winrt::Windows::ApplicationModel::Core;
namespace winrtWF = winrt::Windows::Foundation;
namespace winrtWU = winrt::Windows::UI;
namespace winrtWUC = winrt::Windows::UI::Core;
namespace winrtWUV = winrt::Windows::UI::ViewManagement;

namespace nCine
{
	/// Main entry point and handler for UWP (Universal Windows Platform) applications
	class UwpApplication : public Application, public winrt::implements<UwpApplication, winrtWAC::IFrameworkViewSource, winrtWAC::IFrameworkView>
	{
	public:
		/** @brief Entry point method to be called in the `wWinMain()` function */
		static int Run(CreateAppEventHandlerDelegate createAppEventHandler);

		UwpApplication() : Application(), _isSuspended(false) { }
		~UwpApplication() = default;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// IFrameworkViewSource methods
		winrtWAC::IFrameworkView CreateView() {
			return *this;
		}

		// IFrameworkView methods
		void Initialize(const winrtWAC::CoreApplicationView& applicationView);
		void SetWindow(const winrtWUC::CoreWindow& window);
		void Load(const winrt::hstring& entryPoint);
		void Run();
		void Uninitialize();
#endif

		bool OpenUrl(StringView url) override;
	
	private:
		void OnActivated(const winrtWAC::CoreApplicationView& applicationView, const winrtWAA::IActivatedEventArgs& args);
		void OnSuspending(const winrtWF::IInspectable& sender, const winrtWA::SuspendingEventArgs& args);
		void OnResuming(const winrtWF::IInspectable& sender, const winrtWF::IInspectable& args);
		void OnEnteredBackground(const winrtWF::IInspectable& sender, const winrtWA::EnteredBackgroundEventArgs& args);
		void OnLeavingBackground(const winrtWF::IInspectable& sender, const winrtWA::LeavingBackgroundEventArgs& args);
		void OnWindowClosed(const winrtWF::IInspectable& sender, const winrtWUC::CoreWindowEventArgs& args);
		void OnWindowSizeChanged(const winrtWF::IInspectable& sender, winrtWUC::WindowSizeChangedEventArgs const& args);

		static winrtWUC::CoreDispatcher _dispatcher;

		bool _isSuspended;

		UwpApplication(const UwpApplication&) = delete;
		UwpApplication& operator=(const UwpApplication&) = delete;

		friend Application& theApplication();
	};

	/// Returns application instance
	Application& theApplication();

}
