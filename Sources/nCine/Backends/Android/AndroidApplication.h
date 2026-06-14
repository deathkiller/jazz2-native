#pragma once

#include "../../Application.h"

struct android_app;
struct AInputEvent;

namespace nCine
{
	/**
		@brief Main entry point and handler for Android applications
		
		Specializes @ref Application for the Android platform, wiring the engine to the
		`android_native_app_glue` lifecycle and dispatching the platform commands it receives.
	*/
	class AndroidApplication : public Application
	{
	public:
		/**
		 * @brief Entry point method to be called in the `android_main()` function
		 *
		 * @param state					Native application state provided by the glue layer
		 * @param createAppEventHandler	Factory delegate that creates the application's event handler
		 */
		static void Run(struct android_app* state, CreateAppEventHandlerDelegate createAppEventHandler);

		/**
		 * @brief Processes an Android application lifecycle command
		 *
		 * @param state	Native application state provided by the glue layer
		 * @param cmd	One of the `APP_CMD_*` commands dispatched by the glue layer
		 */
		static void ProcessCommand(struct android_app* state, std::int32_t cmd);

		/** @brief Returns `true` if the application has already called @ref Init() */
		inline bool IsInitialized() const {
			return isInitialized_;
		}

		/** @brief Returns `true` if the main screen is round */
		inline bool IsScreenRound() const {
			return isScreenRound_;
		}

		bool OpenUrl(StringView url) override;

		/** @brief Handles invocation of the system Back gesture */
		void HandleBackInvoked();

		/**
		 * @brief Handles the intent sent to the application activity
		 *
		 * @param action	Intent action string
		 * @param uri		Intent data URI
		 */
		void HandleIntent(StringView action, StringView uri);

		/**
		 * @brief Handles changes of content bounds
		 *
		 * @param bounds	New content bounds, accounting for insets such as display cutouts
		 */
		void HandleContentBoundsChanged(Recti bounds);

		bool CanShowScreenKeyboard() override;
		bool ToggleScreenKeyboard() override;
		bool ShowScreenKeyboard() override;
		bool HideScreenKeyboard() override;
		void Vibrate(std::int32_t milliseconds) override;
		void ShowStatusBar() override;
		void HideStatusBar() override;

	private:
		bool isInitialized_;
		bool isBackInvoked_;
		bool isScreenRound_;

		struct android_app* state_;
		CreateAppEventHandlerDelegate createAppEventHandler_;
		
		void PreInit();
		/** @brief Must be called at the beginning to initialize the application */
		void Init();
		/** @brief Must be called before exiting to shut down the application */
		void Shutdown();

		AndroidApplication();
		~AndroidApplication();

		AndroidApplication(const AndroidApplication&) = delete;
		AndroidApplication& operator=(const AndroidApplication&) = delete;

		/** @brief Returns the singleton reference to the Android application */
		static AndroidApplication& theAndroidApplication() {
			return static_cast<AndroidApplication&>(theApplication());
		}

		friend Application& theApplication();
	};

	/** @brief Returns the application instance */
	Application& theApplication();

}
