#pragma once

#include "../../Application.h"

struct android_app;
struct AInputEvent;

namespace nCine
{
	/// Main entry point and handler for Android applications
	class AndroidApplication : public Application
	{
	public:
		/// Entry point method to be called in the `android_main()` function
		static void Run(struct android_app* state, CreateAppEventHandlerDelegate createAppEventHandler);

		/// Processes an Android application command
		static void ProcessCommand(struct android_app* state, std::int32_t cmd);

		/// Returns true if the application has already called `Init()`
		inline bool IsInitialized() const {
			return isInitialized_;
		}

		/// Returns true if the main screen is round
		inline bool IsScreenRound() const {
			return isScreenRound_;
		}

		bool OpenUrl(StringView url) override;

		/// Handles invocation of the Back gesture
		void HandleBackInvoked();

		/// Handles the intent sent to the application activity
		void HandleIntent(StringView action, StringView uri);

		/// Handles changes of content bounds
		void HandleContentBoundsChanged(Recti bounds);

		bool CanShowScreenKeyboard() override;
		bool ToggleScreenKeyboard() override;
		bool ShowScreenKeyboard() override;
		bool HideScreenKeyboard() override;

	private:
		bool isInitialized_;
		bool isScreenRound_;

		struct android_app* state_;
		CreateAppEventHandlerDelegate createAppEventHandler_;
		
		void PreInit();
		/// Must be called at the beginning to initialize the application
		void Init();
		/// Must be called before exiting to shut down the application
		void Shutdown();

		AndroidApplication();
		~AndroidApplication();

		AndroidApplication(const AndroidApplication&) = delete;
		AndroidApplication& operator=(const AndroidApplication&) = delete;

		/// Returns the singleton reference to the Android application
		static AndroidApplication& theAndroidApplication() {
			return static_cast<AndroidApplication&>(theApplication());
		}

		friend Application& theApplication();
	};

	/// Returns application instance
	Application& theApplication();

}
