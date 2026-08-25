#pragma once

#include "../../Primitives/Rect.h"

#include <jni.h>
#include <android/api-level.h>
#include <android_native_app_glue.h>

#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	class AndroidApplication;
}

namespace nCine::Backends
{
	/**
		@brief Sets up the JNI environment and initializes the wrapper classes
		
		Attaches the Java virtual machine, queries the SDK version and initializes the `AndroidJniClass_*`
		and `AndroidJniWrap_*` wrapper classes for the lifetime of the application.
	*/
	class AndroidJniHelper
	{
		friend class nCine::AndroidApplication;

	public:
		inline static unsigned int SdkVersion() { return _sdkVersion; }

		static JNIEnv *jniEnv;

		AndroidJniHelper() = delete;
		~AndroidJniHelper() = delete;

		static bool CheckAndClearExceptions();

	private:
		static JavaVM* _javaVM;
		static unsigned int _sdkVersion;

		/** @brief Attaches the Java virtual machine to make use of JNI */
		static void AttachJVM(struct android_app* state);
		/** @brief Detaches the Java virtual machine */
		static void DetachJVM();
		static void InitializeClasses();

		static String ExceptionToString(JNIEnv* env, jthrowable exception);
	};

	/**
		@brief Base class for the JNI wrappers that hold a reference to a Java object
		
		Owns an underlying `jobject` and exposes helpers for resolving classes, methods and fields through JNI.
	*/
	class AndroidJniClass
	{
	public:
		AndroidJniClass()
			: _javaObject(nullptr) {}
		explicit AndroidJniClass(jobject javaObject);
		virtual ~AndroidJniClass();

		/** @brief Move constructor */
		AndroidJniClass(AndroidJniClass&& other);
		/** @brief Move assignment operator */
		AndroidJniClass& operator=(AndroidJniClass&& other);

		/** @brief Deleted copy constructor */
		AndroidJniClass(const AndroidJniClass&) = delete;
		/** @brief Deleted assignment operator */
		AndroidJniClass& operator=(const AndroidJniClass&) = delete;

		inline bool IsNull() const {
			return _javaObject == nullptr;
		}
		inline jobject javaObject() const {
			return _javaObject;
		}

		static jclass findClass(const char* name);
		static jmethodID getStaticMethodID(jclass javaClass, const char* name, const char* signature);
		static jmethodID getMethodID(jclass javaClass, const char* name, const char* signature);
		static jfieldID getStaticFieldID(jclass javaClass, const char* name, const char* signature);

	protected:
		jobject _javaObject;

		friend class AndroidJniHelper;
	};

	/** @brief Wraps JNI access to `android.os.Build.VERSION` */
	class AndroidJniClass_Version
	{
	public:
		static int sdkInt();
		static String deviceBrand();
		static String deviceManufacturer();
		static String deviceModel();
	};

	/** @brief Wraps JNI access to `android.view.InputDevice.MotionRange` */
	class AndroidJniClass_MotionRange : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_MotionRange(jobject javaObject);
		float getMin() const;
		float getRange() const;
		
	private:
		static jclass _javaClass;
		static jmethodID _midGetMin;
		static jmethodID _midGetRange;
	};

	/** @brief Wraps JNI access to `android.os.VibrationEffect` */
	class AndroidJniClass_VibrationEffect : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_VibrationEffect(jobject javaObject)
			: AndroidJniClass(javaObject) {}
		static AndroidJniClass_VibrationEffect createOneShot(long milliseconds, int amplitude);

	private:
		static jclass _javaClass;
		static jmethodID _midCreateOneShot;
	};

	/** @brief Wraps JNI access to `android.os.Vibrator` */
	class AndroidJniClass_Vibrator : public AndroidJniClass
	{
	public:
		static void init();
		AndroidJniClass_Vibrator()
			: AndroidJniClass() {}
		explicit AndroidJniClass_Vibrator(jobject javaObject)
			: AndroidJniClass(javaObject) {}

		void cancel() const;
		void vibrate(const AndroidJniClass_VibrationEffect& vibe) const;

	private:
		static jclass _javaClass;
		static jmethodID _midCancel;
		static jmethodID _midVibrate;
	};

	/** @brief Wraps JNI access to `android.os.VibratorManager` */
	class AndroidJniClass_VibratorManager : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_VibratorManager(jobject javaObject);

		void cancel() const;
		int getVibratorIds(int* destination, int maxSize) const;
		AndroidJniClass_Vibrator getVibrator(int vibratorId) const;

	private:
		static jclass _javaClass;
		static jmethodID _midCancel;
		static jmethodID _midGetVibratorIds;
		static jmethodID _midGetVibrator;
	};

	/** @brief Wraps JNI access to `android.view.InputDevice` */
	class AndroidJniClass_InputDevice : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_InputDevice(jobject javaObject)
			: AndroidJniClass(javaObject) {}
		static AndroidJniClass_InputDevice getDevice(int deviceId);
		static int getDeviceIds(int* destination, int maxSize);
		int getName(char* destination, int maxStringSize) const;
		int getDescriptor(char* destination, int maxStringSize) const;
		int getProductId() const;
		int getVendorId() const;
		AndroidJniClass_MotionRange getMotionRange(int axis) const;
		int getSources() const;
		void hasKeys(const int* buttons, const int length, bool* bools) const;
		AndroidJniClass_VibratorManager getVibratorManager() const;

	private:
		static jclass _javaClass;
		static jmethodID _midGetDevice;
		static jmethodID _midGetDeviceIds;
		static jmethodID _midGetName;
		static jmethodID _midGetDescriptor;
		static jmethodID _midGetVendorId;
		static jmethodID _midGetProductId;
		static jmethodID _midGetMotionRange;
		static jmethodID _midGetSources;
		static jmethodID _midHasKeys;
		static jmethodID _midGetVibratorManager;
	};

	/** @brief Wraps JNI access to `android.view.KeyCharacterMap` */
	class AndroidJniClass_KeyCharacterMap : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_KeyCharacterMap(jobject javaObject)
			: AndroidJniClass(javaObject) {}
		static bool deviceHasKey(int button);

	private:
		static jclass _javaClass;
		static jmethodID _midDeviceHasKey;
	};

	/** @brief Wraps JNI access to `android.view.KeyEvent` */
	class AndroidJniClass_KeyEvent : public AndroidJniClass
	{
	public:
		static void init();

		AndroidJniClass_KeyEvent(int action, int code);
		AndroidJniClass_KeyEvent(long long int downTime, long long int eventTime, int action, int code, int repeat, int metaState, int deviceId, int scancode, int flags, int source);

		int getUnicodeChar(int metaState) const;
		inline int getUnicodeChar() const { return getUnicodeChar(0); }
		int getCharacters(char* destination, int maxStringSize) const;
		bool isPrintingKey() const;

	private:
		static jclass _javaClass;
		static jmethodID _midConstructor;
		static jmethodID _midConstructor2;
		static jmethodID _midGetUnicodeCharMetaState;
		static jmethodID _midGetUnicodeChar;
		static jmethodID _midGetCharacters;
		static jmethodID _midIsPrintingKey;
	};
	
	/** @brief Wraps JNI access to `android.view.Display.Mode` */
	class AndroidJniClass_DisplayMode : public AndroidJniClass
	{
	public:
		static void init();

		AndroidJniClass_DisplayMode()
			: AndroidJniClass() {}
		explicit AndroidJniClass_DisplayMode(jobject javaObject)
			: AndroidJniClass(javaObject) {}

		int getPhysicalHeight() const;
		int getPhysicalWidth() const;
		float getRefreshRate() const;

	private:
		static jclass _javaClass;
		static jmethodID _midGetPhysicalHeight;
		static jmethodID _midGetPhysicalWidth;
		static jmethodID _midGetRefreshRate;
	};

	/** @brief Wraps JNI access to `android.view.Display` */
	class AndroidJniClass_Display : public AndroidJniClass
	{
	public:
		static void init();

		AndroidJniClass_Display()
			: AndroidJniClass() {}
		explicit AndroidJniClass_Display(jobject javaObject)
			: AndroidJniClass(javaObject) {}

		AndroidJniClass_DisplayMode getMode() const;
		int getName(char* destination, int maxStringSize) const;
		int getSupportedModes(AndroidJniClass_DisplayMode* destination, int maxSize) const;

	private:
		static jclass _javaClass;
		static jmethodID _midGetMode;
		static jmethodID _midGetName;
		static jmethodID _midGetSupportedModes;
	};

	/** @brief Wraps JNI access to `android.app.Activity` */
	class AndroidJniWrap_Activity
	{
	public:
		static void init(struct android_app* state);
		static void finishAndRemoveTask();
		static String getPackageName();
		static String getPreferredLanguage();
		static bool isScreenRound();
		static bool hasExternalStoragePermission();
		static void requestExternalStoragePermission();
		static void setActivityEnabled(StringView activity, bool enable);
		static bool openUrl(StringView url);
		static jobject getDecorView();
		static Recti getVisibleBounds();
		static void vibrate(std::int32_t milliseconds);
		static void showStatusBar();
		static void hideStatusBar();

	private:
		static jobject _activityObject;
		static jmethodID _midFinishAndRemoveTask;
		static jmethodID _midGetPackageName;
		static jmethodID _midGetPreferredLanguage;
		static jmethodID _midIsScreenRound;
		static jmethodID _midHasExternalStoragePermission;
		static jmethodID _midRequestExternalStoragePermission;
		static jmethodID _midSetActivityEnabled;
		static jmethodID _midOpenUrl;
		static jmethodID _midGetWindow;
		static jmethodID _midGetSystemService;

		static jobject _vibratorObject;
		static jobject _insetsControllerObject;
		static jmethodID _midInsetsShow;
		static jmethodID _midInsetsHide;

		static jmethodID _midGetDecorView;
		static jclass _rectClass;
		static jmethodID _midRectInit;
		static jfieldID _fidRectLeft;
		static jfieldID _fidRectTop;
		static jfieldID _fidRectRight;
		static jfieldID _fidRectBottom;
		static jmethodID _midGetWindowVisibleDisplayFrame;
		static jmethodID _midGetVisibleBounds;
	};

	/**
		@brief Wraps JNI access to the screen (software) keyboard

		`android.app.NativeActivity` contains no view that accepts text, so the activity of the Java bridge
		owns an invisible one and drives `android.view.inputmethod.InputMethodManager` on the UI thread.
		Software keyboards open only for a focused text editor, which is why talking to the input method
		manager directly used to fail on many devices and never showed anything on Android TV.
	*/
	class AndroidJniWrap_InputMethodManager
	{
	public:
		static void init(struct android_app* state);

		/** @brief Returns `true` if any input method that can provide a software keyboard is enabled */
		static bool isSoftInputAvailable();
		/** @brief Returns `true` if the software keyboard is currently shown */
		static bool isSoftInputVisible();
		/** @brief Requests the software keyboard to be shown */
		static bool showSoftInput();
		/** @brief Requests the software keyboard to be hidden */
		static bool hideSoftInput();

	private:
		static jobject _activityObject;
		static jmethodID _midIsSoftInputAvailable;
		static jmethodID _midIsSoftInputVisible;
		static jmethodID _midShowSoftInput;
		static jmethodID _midHideSoftInput;
	};

	/** @brief Wraps JNI access to `android.hardware.display.DisplayManager` */
	class AndroidJniWrap_DisplayManager
	{
	public:
		static void init(struct android_app* state);
		static void shutdown();

		static AndroidJniClass_Display getDisplay(int displayId);
		static int getDisplays(AndroidJniClass_Display* destination, int maxSize);

	private:
		static jobject _displayManagerObject;
		static jmethodID _midGetDisplay;
		static jmethodID _midGetDisplays;
	};

	/** @brief Wraps JNI access to `android.provider.Settings.Secure` */
	class AndroidJniWrap_Secure
	{
	public:
		static void init(struct android_app* state);

		static StringView getAndroidId();

	private:
		static String _androidId;
	};
}
