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
	/// Class for setting up JNI and initialize requests classes
	class AndroidJniHelper
	{
		friend class nCine::AndroidApplication;

	public:
		inline static unsigned int SdkVersion() { return sdkVersion_; }

		static JNIEnv *jniEnv;

		AndroidJniHelper() = delete;
		~AndroidJniHelper() = delete;

		static bool CheckAndClearExceptions();

	private:
		static JavaVM* javaVM_;
		static unsigned int sdkVersion_;

		/// Attaches the Java virtual machine to make use of JNI
		static void AttachJVM(struct android_app* state);
		/// Detaches the Java virtual machine
		static void DetachJVM();
		static void InitializeClasses();

		static String ExceptionToString(JNIEnv* env, jthrowable exception);
	};

	/// Base class for handling of JNI requests to the Android API
	class AndroidJniClass
	{
	public:
		AndroidJniClass()
			: javaObject_(nullptr) {}
		explicit AndroidJniClass(jobject javaObject);
		virtual ~AndroidJniClass();

		/// Move constructor
		AndroidJniClass(AndroidJniClass&& other);
		/// Move assignment operator
		AndroidJniClass& operator=(AndroidJniClass&& other);

		/// Deleted copy constructor
		AndroidJniClass(const AndroidJniClass&) = delete;
		/// Deleted assignment operator
		AndroidJniClass& operator=(const AndroidJniClass&) = delete;

		inline bool IsNull() const {
			return javaObject_ == nullptr;
		}

		static jclass findClass(const char* name);
		static jmethodID getStaticMethodID(jclass javaClass, const char* name, const char* signature);
		static jmethodID getMethodID(jclass javaClass, const char* name, const char* signature);
		static jfieldID getStaticFieldID(jclass javaClass, const char* name, const char* signature);

	protected:
		jobject javaObject_;

		friend class AndroidJniHelper;
	};

	/// Handles JNI requests to `android.os.Build.VERSION`
	class AndroidJniClass_Version
	{
	public:
		static int sdkInt();
		static String deviceBrand();
		static String deviceManufacturer();
		static String deviceModel();
	};

	/// Handles JNI requests to `android.view.InputDevice.MotionRange`
	class AndroidJniClass_MotionRange : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_MotionRange(jobject javaObject);
		float getMin() const;
		float getRange() const;
		
	private:
		static jclass javaClass_;
		static jmethodID midGetMin_;
		static jmethodID midGetRange_;
	};

	/// A class to handle JNI requests to `android.os.VibrationEffect`
	class AndroidJniClass_VibrationEffect : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_VibrationEffect(jobject javaObject)
			: AndroidJniClass(javaObject) {}
		static AndroidJniClass_VibrationEffect createOneShot(long milliseconds, int amplitude);

	private:
		static jclass javaClass_;
		static jmethodID midCreateOneShot_;
	};

	/// A class to handle JNI requests to `android.os.Vibrator`
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
		static jclass javaClass_;
		static jmethodID midCancel_;
		static jmethodID midVibrate_;
	};

	/// A class to handle JNI requests to `android.os.VibratorManager`
	class AndroidJniClass_VibratorManager : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_VibratorManager(jobject javaObject)
			: AndroidJniClass(javaObject) {}

		void cancel() const;
		int getVibratorIds(int* destination, int maxSize) const;
		AndroidJniClass_Vibrator getVibrator(int vibratorId) const;

	private:
		static jclass javaClass_;
		static jmethodID midCancel_;
		static jmethodID midGetVibratorIds_;
		static jmethodID midGetVibrator_;
	};

	/// Handles JNI requests to `android.view.InputDevice`
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
		static jclass javaClass_;
		static jmethodID midGetDevice_;
		static jmethodID midGetDeviceIds_;
		static jmethodID midGetName_;
		static jmethodID midGetDescriptor_;
		static jmethodID midGetVendorId_;
		static jmethodID midGetProductId_;
		static jmethodID midGetMotionRange_;
		static jmethodID midGetSources_;
		static jmethodID midHasKeys_;
		static jmethodID midGetVibratorManager_;
	};

	/// Handles JNI requests to `android.view.KeyCharacterMap`
	class AndroidJniClass_KeyCharacterMap : public AndroidJniClass
	{
	public:
		static void init();
		explicit AndroidJniClass_KeyCharacterMap(jobject javaObject)
			: AndroidJniClass(javaObject) {}
		static bool deviceHasKey(int button);

	private:
		static jclass javaClass_;
		static jmethodID midDeviceHasKey_;
	};

	/// Handles JNI requests to `android.view.KeyEvent`
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
		static jclass javaClass_;
		static jmethodID midConstructor_;
		static jmethodID midConstructor2_;
		static jmethodID midGetUnicodeCharMetaState_;
		static jmethodID midGetUnicodeChar_;
		static jmethodID midGetCharacters_;
		static jmethodID midIsPrintingKey_;
	};
	
	/// Handles JNI requests to `android.view.Display.Mode`
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
		static jclass javaClass_;
		static jmethodID midGetPhysicalHeight_;
		static jmethodID midGetPhysicalWidth_;
		static jmethodID midGetRefreshRate_;
	};

	/// Handles JNI requests to `android.view.Display`
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
		static jclass javaClass_;
		static jmethodID midGetMode_;
		static jmethodID midGetName_;
		static jmethodID midGetSupportedModes_;
	};

	/// Handles JNI requests to `android.app.Activity`
	class AndroidJniWrap_Activity
	{
	public:
		static void init(struct android_app* state);
		static void finishAndRemoveTask();
		static String getPreferredLanguage();
		static bool hasExternalStoragePermission();
		static void requestExternalStoragePermission();
		static void setActivityEnabled(StringView activity, bool enable);
		static bool openUrl(StringView url);
		static jobject getDecorView();
		static Recti getVisibleBounds();

	private:
		static jobject activityObject_;
		static jmethodID midFinishAndRemoveTask_;
		static jmethodID midGetPreferredLanguage_;
		static jmethodID midHasExternalStoragePermission_;
		static jmethodID midRequestExternalStoragePermission_;
		static jmethodID midSetActivityEnabled_;
		static jmethodID midOpenUrl_;
		static jmethodID midGetWindow_;

		static jmethodID midGetDecorView_;
		static jclass rectClass_;
		static jmethodID midRectInit_;
		static jfieldID fidRectLeft_;
		static jfieldID fidRectTop_;
		static jfieldID fidRectRight_;
		static jfieldID fidRectBottom_;
		static jmethodID midGetWindowVisibleDisplayFrame_;
	};

	/// Handles JNI requests to `android.view.inputmethod.InputMethodManager`
	class AndroidJniWrap_InputMethodManager
	{
	public:
		static void init(struct android_app* state);
		static void shutdown();

		static void toggleSoftInput();
		static bool showSoftInput();
		static bool hideSoftInput();

	private:
		static jobject inputMethodManagerObject_;
		static jmethodID midToggleSoftInput_;
		static jmethodID midShowSoftInput_;
		static jmethodID midHideSoftInput_;
		static jmethodID midGetWindowToken_;

		static const int SHOW_IMPLICIT = 1;
		static const int HIDE_IMPLICIT_ONLY = 1;
	};

	/// Handles JNI requests to `android.hardware.display.DisplayManager`
	class AndroidJniWrap_DisplayManager
	{
	public:
		static void init(struct android_app* state);
		static void shutdown();

		static AndroidJniClass_Display getDisplay(int displayId);
		static int getDisplays(AndroidJniClass_Display* destination, int maxSize);

	private:
		static jobject displayManagerObject_;
		static jmethodID midGetDisplay_;
		static jmethodID midGetDisplays_;
	};

	/// Handles JNI requests to `android.provider.Settings.Secure`
	class AndroidJniWrap_Secure
	{
	public:
		static void init(struct android_app* state);

		static StringView getAndroidId();

	private:
		static String androidId_;
	};
}
