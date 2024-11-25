#pragma once

#include "../../Application.h"

struct android_app;
struct AInputEvent;

namespace nCine
{
	/// Main entry point and handler for nCine Android applications
	class AndroidApplication : public Application
	{
	public:
		/// Entry point method to be called in the `android_main()` function
		static void Run(struct android_app* state, std::unique_ptr<IAppEventHandler>(*createAppEventHandler)());

		/// Processes an Android application command
		static void ProcessCommand(struct android_app* state, int32_t cmd);

		/// Returns true if the application has already called `init()`
		inline bool IsInitialized() const {
			return isInitialized_;
		}

		/// Returns the package name for the Android application
		inline const char* GetPackageName() const {
			return packageName_.data();
		}

		/// Toggles the software keyboard
		void ToggleSoftInput();

	private:
		bool isInitialized_;
		String packageName_;

		struct android_app* state_;
		std::unique_ptr<IAppEventHandler>(*createAppEventHandler_)();
		
		void PreInit();
		/// Must be called at the beginning to initialize the application
		void Init();
		/// Must be called before exiting to shut down the application
		void Shutdown();

		AndroidApplication()
			: Application(), isInitialized_(false), state_(nullptr), createAppEventHandler_(nullptr) {}
		~AndroidApplication() {}

		AndroidApplication(const AndroidApplication&) = delete;
		AndroidApplication& operator=(const AndroidApplication&) = delete;

		/// Returns the singleton reference to the Android application
		static AndroidApplication& theAndroidApplication() {
			return static_cast<AndroidApplication&>(theApplication());
		}

		friend Application& theApplication();
	};

	/// Meyers' Singleton
	Application& theApplication();

}
