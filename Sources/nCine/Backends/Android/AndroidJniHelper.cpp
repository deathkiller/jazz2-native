#include "AndroidJniHelper.h"
#include "AndroidApplication.h"
#include "../../../Main.h"
#include "../../Base/Algorithms.h"
#include "../../Base/Timer.h"

#include <cstring>
#include <utility>

#include <sys/system_properties.h>

namespace nCine::Backends
{
	JavaVM* AndroidJniHelper::javaVM_ = nullptr;
	JNIEnv* AndroidJniHelper::jniEnv = nullptr;
	unsigned int AndroidJniHelper::sdkVersion_ = 0;

	jclass AndroidJniClass_MotionRange::javaClass_ = nullptr;
	//jmethodID AndroidJniClass_MotionRange::midGetFlat_ = nullptr;
	//jmethodID AndroidJniClass_MotionRange::midGetFuzz_ = nullptr;
	//jmethodID AndroidJniClass_MotionRange::midGetMax_ = nullptr;
	jmethodID AndroidJniClass_MotionRange::midGetMin_ = nullptr;
	jmethodID AndroidJniClass_MotionRange::midGetRange_ = nullptr;
	jclass AndroidJniClass_VibrationEffect::javaClass_ = nullptr;
	jmethodID AndroidJniClass_VibrationEffect::midCreateOneShot_ = nullptr;
	jclass AndroidJniClass_Vibrator::javaClass_ = nullptr;
	jmethodID AndroidJniClass_Vibrator::midCancel_ = nullptr;
	jmethodID AndroidJniClass_Vibrator::midVibrate_ = nullptr;
	jclass AndroidJniClass_VibratorManager::javaClass_ = nullptr;
	jmethodID AndroidJniClass_VibratorManager::midCancel_ = nullptr;
	jmethodID AndroidJniClass_VibratorManager::midGetVibratorIds_ = nullptr;
	jmethodID AndroidJniClass_VibratorManager::midGetVibrator_ = nullptr;
	jclass AndroidJniClass_InputDevice::javaClass_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetDevice_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetDeviceIds_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetName_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetDescriptor_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetProductId_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetVendorId_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetMotionRange_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetSources_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midHasKeys_ = nullptr;
	jmethodID AndroidJniClass_InputDevice::midGetVibratorManager_ = nullptr;
	jclass AndroidJniClass_KeyCharacterMap::javaClass_ = nullptr;
	jmethodID AndroidJniClass_KeyCharacterMap::midDeviceHasKey_ = nullptr;
	jclass AndroidJniClass_KeyEvent::javaClass_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midConstructor2_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midConstructor_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midGetUnicodeCharMetaState_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midGetUnicodeChar_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midGetCharacters_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midIsPrintingKey_ = nullptr;
	jclass AndroidJniClass_Display::javaClass_ = nullptr;
	jmethodID AndroidJniClass_Display::midGetMode_ = nullptr;
	jmethodID AndroidJniClass_Display::midGetName_ = nullptr;
	jmethodID AndroidJniClass_Display::midGetSupportedModes_ = nullptr;
	jclass AndroidJniClass_DisplayMode::javaClass_ = nullptr;
	jmethodID AndroidJniClass_DisplayMode::midGetPhysicalHeight_ = nullptr;
	jmethodID AndroidJniClass_DisplayMode::midGetPhysicalWidth_ = nullptr;
	jmethodID AndroidJniClass_DisplayMode::midGetRefreshRate_ = nullptr;

	jobject AndroidJniWrap_Activity::activityObject_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midFinishAndRemoveTask_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midGetPreferredLanguage_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midIsScreenRound_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midHasExternalStoragePermission_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midRequestExternalStoragePermission_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midSetActivityEnabled_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midOpenUrl_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midGetWindow_ = nullptr;

	jmethodID AndroidJniWrap_Activity::midGetDecorView_ = nullptr;
	jclass AndroidJniWrap_Activity::rectClass_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midRectInit_ = nullptr;
	jfieldID AndroidJniWrap_Activity::fidRectLeft_ = nullptr;
	jfieldID AndroidJniWrap_Activity::fidRectTop_ = nullptr;
	jfieldID AndroidJniWrap_Activity::fidRectRight_ = nullptr;
	jfieldID AndroidJniWrap_Activity::fidRectBottom_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midGetWindowVisibleDisplayFrame_ = nullptr;

	jobject AndroidJniWrap_InputMethodManager::inputMethodManagerObject_ = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::midToggleSoftInput_ = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::midShowSoftInput_ = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::midHideSoftInput_ = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::midGetWindowToken_ = nullptr;

	jobject AndroidJniWrap_DisplayManager::displayManagerObject_ = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::midGetDisplay_ = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::midGetDisplays_ = nullptr;

	String AndroidJniWrap_Secure::androidId_;

	// ------------------- AndroidJniHelper -------------------

	bool AndroidJniHelper::CheckAndClearExceptions()
	{
		if DEATH_UNLIKELY(jniEnv->ExceptionCheck()) {
			if (jthrowable exception = jniEnv->ExceptionOccurred()) {
				jniEnv->ExceptionClear();
				String message = ExceptionToString(jniEnv, exception);
				if (!message.empty()) {
					LOGE("{}", message);
				}
				jniEnv->DeleteLocalRef(exception);
			} else {
				jniEnv->ExceptionDescribe();
				jniEnv->ExceptionClear();
			}
			return true;
		}

		return false;
	}

	void AndroidJniHelper::AttachJVM(struct android_app* state)
	{
		javaVM_ = state->activity->vm;

		// This is called before PreInitCommon(), so trace targets are usually not attached yet, only logcat
		if (javaVM_ == nullptr) {
			LOGE("JavaVM pointer is null");
		} else {
			const int getEnvStatus = javaVM_->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);
			if (getEnvStatus == JNI_EDETACHED) {
				if (javaVM_->AttachCurrentThread(&jniEnv, nullptr) != 0) {
					LOGW("AttachCurrentThread() cannot attach the JVM");
				} else {
					LOGI("AttachCurrentThread() was successful");
				}
			} else if (getEnvStatus == JNI_EVERSION) {
				LOGW("GetEnv() with unsupported version");
			} else if (getEnvStatus == JNI_OK) {
				LOGI("GetEnv() was successful");
			}
			
			if (jniEnv == nullptr) {
				LOGE("JNIEnv pointer is nullptr");
			} else {
				InitializeClasses();
				AndroidJniWrap_Activity::init(state);
				AndroidJniWrap_InputMethodManager::init(state);
				AndroidJniWrap_DisplayManager::init(state);
				AndroidJniWrap_Secure::init(state);

				// Cache the value of SDK version to avoid going through JNI in the future
				sdkVersion_ = AndroidJniClass_Version::sdkInt();
			}
		}
	}

	void AndroidJniHelper::DetachJVM()
	{
		if (javaVM_) {
			AndroidJniWrap_DisplayManager::shutdown();
			AndroidJniWrap_InputMethodManager::shutdown();

			javaVM_->DetachCurrentThread();
			LOGI("Thread detached");
			jniEnv = nullptr;
			javaVM_ = nullptr;
		}
	}

	void AndroidJniHelper::InitializeClasses()
	{
		AndroidJniClass_MotionRange::init();
		AndroidJniClass_VibrationEffect::init();
		AndroidJniClass_Vibrator::init();
		AndroidJniClass_VibratorManager::init();
		AndroidJniClass_InputDevice::init();
		AndroidJniClass_KeyCharacterMap::init();
		AndroidJniClass_KeyEvent::init();
		AndroidJniClass_Display::init();
		AndroidJniClass_DisplayMode::init();
	}

	String AndroidJniHelper::ExceptionToString(JNIEnv* env, jthrowable exception)
	{
		if (!exception) {
			return {};
		}

		auto checkAndClear = [env]() {
			if DEATH_UNLIKELY(env->ExceptionCheck()) {
				env->ExceptionClear();
				return true;
			}
			return false;
		};

		const jclass logClazz = env->FindClass("android/util/Log");
		if (checkAndClear() || !logClazz) {
			LOGW("Failed to fetch the last exception message");
			return {};
		}

		const jmethodID methodId = env->GetStaticMethodID(logClazz, "getStackTraceString", "(Ljava/lang/Throwable;)Ljava/lang/String;");
		if (checkAndClear() || !methodId) {
			LOGW("Failed to fetch the last exception message");
			return {};
		}

		jvalue value;
		value.l = static_cast<jobject>(exception);
		const jobject messageObj = env->CallStaticObjectMethodA(logClazz, methodId, &value);
		const jstring jmessage = static_cast<jstring>(messageObj);
		if (checkAndClear()) {
			return {};
		}

		char const* utf8Message = env->GetStringUTFChars(jmessage, nullptr);
		String result = utf8Message;
		env->ReleaseStringUTFChars(jmessage, utf8Message);

		return result;
	}

	// ------------------- AndroidJniClass -------------------

	AndroidJniClass::AndroidJniClass(jobject javaObject)
		: javaObject_(nullptr)
	{
		FATAL_ASSERT(AndroidJniHelper::jniEnv != nullptr);
		if (javaObject) {
			javaObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
			AndroidJniHelper::jniEnv->DeleteLocalRef(javaObject);
		}
	}

	AndroidJniClass::~AndroidJniClass()
	{
		if (javaObject_) {
			AndroidJniHelper::jniEnv->DeleteGlobalRef(javaObject_);
		}
	}

	AndroidJniClass::AndroidJniClass(AndroidJniClass&& other)
		: javaObject_(other.javaObject_)
	{
		other.javaObject_ = nullptr;
	}

	AndroidJniClass& AndroidJniClass::operator=(AndroidJniClass&& other)
	{
		std::swap(javaObject_, other.javaObject_);
		return *this;
	}

	jclass AndroidJniClass::findClass(const char* name)
	{
		ASSERT(name != nullptr);
		jclass javaClass = AndroidJniHelper::jniEnv->FindClass(name);
		if (AndroidJniHelper::CheckAndClearExceptions() || javaClass == nullptr) {
			LOGE("Cannot find Java class \"{}\"", name);
			return nullptr;
		}
		return javaClass;
	}

	jmethodID AndroidJniClass::getStaticMethodID(jclass javaClass, const char* name, const char* signature)
	{
		jmethodID mid = nullptr;

		ASSERT(name != nullptr && signature != nullptr);
		if (javaClass != nullptr) {
			mid = AndroidJniHelper::jniEnv->GetStaticMethodID(javaClass, name, signature);
			if (AndroidJniHelper::CheckAndClearExceptions() || mid == nullptr) {
				LOGE("Cannot get static method \"{}()\" with signature \"{}\"", name, signature);
				return nullptr;
			}
		} else {
			LOGE("Cannot get static methods before finding the Java class");
		}
		return mid;
	}

	jmethodID AndroidJniClass::getMethodID(jclass javaClass, const char* name, const char* signature)
	{
		jmethodID mid = nullptr;

		ASSERT(name != nullptr && signature != nullptr);
		if (javaClass != nullptr) {
			mid = AndroidJniHelper::jniEnv->GetMethodID(javaClass, name, signature);
			if (AndroidJniHelper::CheckAndClearExceptions() || mid == nullptr) {
				LOGE("Cannot get method \"{}()\" with signature \"{}\"", name, signature);
				return nullptr;
			}
		} else {
			LOGE("Cannot get methods before finding the Java class");
		}
		return mid;
	}

	jfieldID AndroidJniClass::getStaticFieldID(jclass javaClass, const char* name, const char* signature)
	{
		jfieldID fid = nullptr;

		ASSERT(name != nullptr && signature != nullptr);
		if (javaClass != nullptr) {
			fid = AndroidJniHelper::jniEnv->GetStaticFieldID(javaClass, name, signature);
			if (AndroidJniHelper::CheckAndClearExceptions() || fid == nullptr) {
				LOGE("Cannot get static field \"{}\" with signature \"{}\"", name, signature);
				return nullptr;
			}
		} else {
			LOGE("Cannot get static fields before finding the Java class");
		}
		return fid;
	}

	// ------------------- AndroidJniClass_Version -------------------

	int AndroidJniClass_Version::sdkInt()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.build.version.sdk", buffer);
		if (length <= 0) {
			return 0;
		}
		return atoi(buffer);
	}
	
	String AndroidJniClass_Version::deviceBrand()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.product.brand", buffer);
		if (length <= 0) {
			return {};
		}
		return String(buffer, (std::size_t)length);
	}
	
	String AndroidJniClass_Version::deviceManufacturer()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.product.manufacturer", buffer);
		if (length <= 0) {
			return {};
		}
		return String(buffer, (std::size_t)length);
	}
	
	String AndroidJniClass_Version::deviceModel()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.product.model", buffer);
		if (length <= 0) {
			return {};
		}
		return String(buffer, (std::size_t)length);
	}

	// ------------------- AndroidJniClass_MotionRange -------------------

	void AndroidJniClass_MotionRange::init()
	{
		javaClass_ = findClass("android/view/InputDevice$MotionRange");
		midGetMin_ = getMethodID(javaClass_, "getMin", "()F");
		midGetRange_ = getMethodID(javaClass_, "getRange", "()F");
	}

	AndroidJniClass_MotionRange::AndroidJniClass_MotionRange(jobject javaObject)
		: AndroidJniClass(javaObject)
	{
	}

	float AndroidJniClass_MotionRange::getMin() const
	{
		const jfloat minValue = AndroidJniHelper::jniEnv->CallFloatMethod(javaObject_, midGetMin_);
		return float(minValue);
	}

	float AndroidJniClass_MotionRange::getRange() const
	{
		const jfloat rangeValue = AndroidJniHelper::jniEnv->CallFloatMethod(javaObject_, midGetRange_);
		return float(rangeValue);
	}

	// ------------------- AndroidJniClass_VibrationEffect -------------------

	void AndroidJniClass_VibrationEffect::init()
	{
		javaClass_ = findClass("android/os/VibrationEffect");
		midCreateOneShot_ = getStaticMethodID(javaClass_, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
	}

	AndroidJniClass_VibrationEffect AndroidJniClass_VibrationEffect::createOneShot(long milliseconds, int amplitude)
	{
		jobject vibrationEffect = AndroidJniHelper::jniEnv->CallStaticObjectMethod(javaClass_, midCreateOneShot_, milliseconds, amplitude);
		return AndroidJniClass_VibrationEffect(vibrationEffect);
	}

	// ------------------- AndroidJniClass_Vibrator -------------------

	void AndroidJniClass_Vibrator::init()
	{
		javaClass_ = findClass("android/os/Vibrator");
		midCancel_ = getMethodID(javaClass_, "cancel", "()V");
		midVibrate_ = getMethodID(javaClass_, "vibrate", "(Landroid/os/VibrationEffect;)V");
	}

	void AndroidJniClass_Vibrator::cancel() const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(javaObject_, midCancel_);
	}

	void AndroidJniClass_Vibrator::vibrate(const AndroidJniClass_VibrationEffect& vibe) const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(javaObject_, midVibrate_, vibe.javaObject());
	}

	// ------------------- AndroidJniClass_VibratorManager -------------------

	void AndroidJniClass_VibratorManager::init()
	{
		javaClass_ = findClass("android/os/VibratorManager");
		midCancel_ = getMethodID(javaClass_, "cancel", "()V");
		midGetVibratorIds_ = getMethodID(javaClass_, "getVibratorIds", "()[I");
		midGetVibrator_ = getMethodID(javaClass_, "getVibrator", "(I)Landroid/os/Vibrator;");
	}

	AndroidJniClass_VibratorManager::AndroidJniClass_VibratorManager(jobject javaObject)
		: AndroidJniClass(javaObject)
	{
	}

	void AndroidJniClass_VibratorManager::cancel() const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(javaObject_, midCancel_);
	}

	int AndroidJniClass_VibratorManager::getVibratorIds(int* destination, int maxSize) const
	{
		jintArray arrVibratorIds = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetVibratorIds_));
		const jint length = AndroidJniHelper::jniEnv->GetArrayLength(arrVibratorIds);

		if (destination != nullptr && maxSize > 0) {
			jint* intsVibratorIds = AndroidJniHelper::jniEnv->GetIntArrayElements(arrVibratorIds, nullptr);
			for (int i = 0; i < length && i < maxSize; i++)
				destination[i] = int(intsVibratorIds[i]);
			AndroidJniHelper::jniEnv->ReleaseIntArrayElements(arrVibratorIds, intsVibratorIds, 0);
		}
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrVibratorIds);

		return int(length);
	}

	AndroidJniClass_Vibrator AndroidJniClass_VibratorManager::getVibrator(int vibratorId) const
	{
		jobject vibratorObject = AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetVibrator_, vibratorId);
		return AndroidJniClass_Vibrator(vibratorObject);
	}

	// ------------------- AndroidJniClass_InputDevice -------------------

	void AndroidJniClass_InputDevice::init()
	{
		javaClass_ = findClass("android/view/InputDevice");
		midGetDevice_ = getStaticMethodID(javaClass_, "getDevice", "(I)Landroid/view/InputDevice;");
		midGetDeviceIds_ = getStaticMethodID(javaClass_, "getDeviceIds", "()[I");
		midGetName_ = getMethodID(javaClass_, "getName", "()Ljava/lang/String;");
		midGetDescriptor_ = getMethodID(javaClass_, "getDescriptor", "()Ljava/lang/String;");
		midGetProductId_ = getMethodID(javaClass_, "getProductId", "()I");
		midGetVendorId_ = getMethodID(javaClass_, "getVendorId", "()I");
		midGetMotionRange_ = getMethodID(javaClass_, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;");
		midGetSources_ = getMethodID(javaClass_, "getSources", "()I");
		midHasKeys_ = getMethodID(javaClass_, "hasKeys", "([I)[Z");
		midGetVibratorManager_ = getMethodID(javaClass_, "getVibratorManager", "()Landroid/os/VibratorManager;");
	}

	AndroidJniClass_InputDevice AndroidJniClass_InputDevice::getDevice(int deviceId)
	{
		jobject inputDeviceObject = AndroidJniHelper::jniEnv->CallStaticObjectMethod(javaClass_, midGetDevice_, deviceId);
		return AndroidJniClass_InputDevice(inputDeviceObject);
	}

	int AndroidJniClass_InputDevice::getDeviceIds(int* destination, int maxSize)
	{
		jintArray arrDeviceIds = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallStaticObjectMethod(javaClass_, midGetDeviceIds_));
		int length = (int)AndroidJniHelper::jniEnv->GetArrayLength(arrDeviceIds);

		if (destination != nullptr && maxSize > 0) {
			jint* intsDeviceIds = AndroidJniHelper::jniEnv->GetIntArrayElements(arrDeviceIds, nullptr);
			for (int i = 0; i < length && i < maxSize; i++) {
				destination[i] = int(intsDeviceIds[i]);
			}
			AndroidJniHelper::jniEnv->ReleaseIntArrayElements(arrDeviceIds, intsDeviceIds, 0);
		}

		AndroidJniHelper::jniEnv->DeleteLocalRef(arrDeviceIds);

		return length;
	}

	int AndroidJniClass_InputDevice::getName(char* destination, int maxStringSize) const
	{
		jstring strDeviceName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetName_));
		int length;

		if (strDeviceName) {
			const char* deviceName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDeviceName, nullptr);
			length = copyStringFirst(destination, maxStringSize, deviceName);
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDeviceName, deviceName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDeviceName);
		} else {
			length = copyStringFirst(destination, maxStringSize, "Unknown");
		}

		return length;
	}

	int AndroidJniClass_InputDevice::getDescriptor(char* destination, int maxStringSize) const
	{
		jstring strDeviceDescriptor = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetDescriptor_));
		int length;

		if (strDeviceDescriptor) {
			const char* deviceName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDeviceDescriptor, nullptr);
			length = copyStringFirst(destination, maxStringSize, deviceName);
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDeviceDescriptor, deviceName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDeviceDescriptor);
		} else {
			if (maxStringSize > 0) {
				destination[0] = '\0';
			}
			length = 0;
		}

		return length;
	}

	int AndroidJniClass_InputDevice::getProductId() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 19) {
			return 0;
		}
		const jint productId = AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetProductId_);
		return int(productId);
	}

	int AndroidJniClass_InputDevice::getVendorId() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 19) {
			return 0;
		}
		const jint vendorID = AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetVendorId_);
		return int(vendorID);
	}

	AndroidJniClass_MotionRange AndroidJniClass_InputDevice::getMotionRange(int axis) const
	{
		jobject motionRangeObject = AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetMotionRange_, axis);
		return AndroidJniClass_MotionRange(motionRangeObject);
	}

	int AndroidJniClass_InputDevice::getSources() const
	{
		const jint sources = AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetSources_);
		return int(sources);
	}

	void AndroidJniClass_InputDevice::hasKeys(const int* buttons, const int length, bool* bools) const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 19) {
			return;
		}
		jintArray arrButtons = AndroidJniHelper::jniEnv->NewIntArray(length);

		jint* intsButtons = AndroidJniHelper::jniEnv->GetIntArrayElements(arrButtons, nullptr);
		for (int i = 0; i < length; i++) {
			intsButtons[i] = buttons[i];
		}
		AndroidJniHelper::jniEnv->ReleaseIntArrayElements(arrButtons, intsButtons, 0);

		jbooleanArray arrBooleans = static_cast<jbooleanArray>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midHasKeys_, arrButtons));
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrButtons);

		jboolean* booleans = AndroidJniHelper::jniEnv->GetBooleanArrayElements(arrBooleans, nullptr);
		for (int i = 0; i < length; i++) {
			bools[i] = bool(booleans[i]);
		}
		AndroidJniHelper::jniEnv->ReleaseBooleanArrayElements(arrBooleans, booleans, 0);
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrBooleans);
	}

	AndroidJniClass_VibratorManager AndroidJniClass_InputDevice::getVibratorManager() const
	{
		// Minimum SDK version should be 31 but early-out has not been implemented

		jobject vibratorManagerObject = AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetVibratorManager_);
		return AndroidJniClass_VibratorManager(vibratorManagerObject);
	}

	// ------------------- AndroidJniClass_KeyCharacterMap -------------------

	void AndroidJniClass_KeyCharacterMap::init()
	{
		javaClass_ = findClass("android/view/KeyCharacterMap");
		midDeviceHasKey_ = getStaticMethodID(javaClass_, "deviceHasKey", "(I)Z");
	}

	bool AndroidJniClass_KeyCharacterMap::deviceHasKey(int button)
	{
		const jboolean hasKey = AndroidJniHelper::jniEnv->CallStaticBooleanMethod(javaClass_, midDeviceHasKey_, button);
		return (hasKey == JNI_TRUE);
	}

	// ------------------- AndroidJniClass_KeyEvent -------------------

	void AndroidJniClass_KeyEvent::init()
	{
		javaClass_ = findClass("android/view/KeyEvent");
		midConstructor_ = getMethodID(javaClass_, "<init>", "(II)V");
		midConstructor2_ = getMethodID(javaClass_, "<init>", "(JJIIIIIIII)V");
		midGetUnicodeCharMetaState_ = getMethodID(javaClass_, "getUnicodeChar", "(I)I");
		midGetUnicodeChar_ = getMethodID(javaClass_, "getUnicodeChar", "()I");
		midGetCharacters_ = getMethodID(javaClass_, "getCharacters", "()Ljava/lang/String;");
		midIsPrintingKey_ = getMethodID(javaClass_, "isPrintingKey", "()Z");
	}

	AndroidJniClass_KeyEvent::AndroidJniClass_KeyEvent(int action, int code)
	{
		jobject javaObject = AndroidJniHelper::jniEnv->NewObject(javaClass_, midConstructor_, action, code);
		javaObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
	}

	AndroidJniClass_KeyEvent::AndroidJniClass_KeyEvent(long long int downTime, long long int eventTime, int action, int code, int repeat, int metaState, int deviceId, int scancode, int flags, int source)
	{
		jobject javaObject = AndroidJniHelper::jniEnv->NewObject(javaClass_, midConstructor2_, downTime, eventTime, action, code, repeat, metaState, deviceId, scancode, flags, source);
		javaObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
	}

	int AndroidJniClass_KeyEvent::getUnicodeChar(int metaState) const
	{
		if (metaState != 0) {
			return AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetUnicodeCharMetaState_, metaState);
		} else {
			return AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetUnicodeChar_);
		}
	}

	int AndroidJniClass_KeyEvent::getCharacters(char* destination, int maxStringSize) const
	{
		jstring strCharacters = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetCharacters_));
		int length;

		if (strCharacters) {
			const char* characters = AndroidJniHelper::jniEnv->GetStringUTFChars(strCharacters, nullptr);
			length = std::min((std::int32_t)strlen(characters), maxStringSize);
			std::memcpy(destination, characters, length);
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strCharacters, characters);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strCharacters);
		} else {
			length = 0;
		}

		return length;
	}

	bool AndroidJniClass_KeyEvent::isPrintingKey() const
	{
		return AndroidJniHelper::jniEnv->CallBooleanMethod(javaObject_, midIsPrintingKey_);
	}

	// ------------------- AndroidJniClass_DisplayMode -------------------

	void AndroidJniClass_DisplayMode::init()
	{
		javaClass_ = findClass("android/view/Display$Mode");
		midGetPhysicalHeight_ = getMethodID(javaClass_, "getPhysicalHeight", "()I");
		midGetPhysicalWidth_ = getMethodID(javaClass_, "getPhysicalWidth", "()I");
		midGetRefreshRate_ = getMethodID(javaClass_, "getRefreshRate", "()F");
	}

	int AndroidJniClass_DisplayMode::getPhysicalHeight() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jint physicalHeight = AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetPhysicalHeight_);
		return int(physicalHeight);
	}

	int AndroidJniClass_DisplayMode::getPhysicalWidth() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jint physicalWidth = AndroidJniHelper::jniEnv->CallIntMethod(javaObject_, midGetPhysicalWidth_);
		return int(physicalWidth);
	}

	float AndroidJniClass_DisplayMode::getRefreshRate() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jfloat refreshRate = AndroidJniHelper::jniEnv->CallFloatMethod(javaObject_, midGetRefreshRate_);
		return float(refreshRate);
	}

	// ------------------- AndroidJniClass_Display -------------------

	void AndroidJniClass_Display::init()
	{
		javaClass_ = findClass("android/view/Display");
		midGetMode_ = getMethodID(javaClass_, "getMode", "()Landroid/view/Display$Mode;");
		midGetName_ = getMethodID(javaClass_, "getName", "()Ljava/lang/String;");
		midGetSupportedModes_ = getMethodID(javaClass_, "getSupportedModes", "()[Landroid/view/Display$Mode;");
	}

	AndroidJniClass_DisplayMode AndroidJniClass_Display::getMode() const
	{
		jobject modeObject = static_cast<jobject>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetMode_));
		return AndroidJniClass_DisplayMode(modeObject);
	}

	int AndroidJniClass_Display::getName(char* destination, int maxStringSize) const
	{
		jstring strDisplayName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetName_));
		int length;

		if (strDisplayName) {
			const char* displayName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDisplayName, nullptr);
			length = copyStringFirst(destination, maxStringSize, displayName);
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDisplayName, displayName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDisplayName);
		} else {
			length = copyStringFirst(destination, maxStringSize, "Unknown");
		}

		return length;
	}

	int AndroidJniClass_Display::getSupportedModes(AndroidJniClass_DisplayMode* destination, int maxSize) const
	{
		jobjectArray arrModes = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetSupportedModes_));
		int length = (int)AndroidJniHelper::jniEnv->GetArrayLength(arrModes);

		for (int i = 0; i < length && i < maxSize; i++) {
			jobject modeObject = AndroidJniHelper::jniEnv->GetObjectArrayElement(arrModes, i);
			AndroidJniClass_DisplayMode mode(modeObject);
			destination[i] = std::move(mode);
			// The AndroidJniClass constructor deleted the local reference to `modeObject`
		}
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrModes);

		return length;
	}

	// ------------------- AndroidJniWrap_Activity -------------------

	void AndroidJniWrap_Activity::init(struct android_app* state)
	{
		// Retrieve `NativeActivity`
		activityObject_ = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(activityObject_);

		midFinishAndRemoveTask_ = AndroidJniClass::getMethodID(nativeActivityClass, "finishAndRemoveTask", "()V");
		midGetPreferredLanguage_ = AndroidJniClass::getMethodID(nativeActivityClass, "getPreferredLanguage", "()Ljava/lang/String;");
		midIsScreenRound_ = AndroidJniClass::getMethodID(nativeActivityClass, "isScreenRound", "()Z");
		midHasExternalStoragePermission_ = AndroidJniClass::getMethodID(nativeActivityClass, "hasExternalStoragePermission", "()Z");
		midRequestExternalStoragePermission_ = AndroidJniClass::getMethodID(nativeActivityClass, "requestExternalStoragePermission", "()V");
		midSetActivityEnabled_ = AndroidJniClass::getMethodID(nativeActivityClass, "setActivityEnabled", "(Ljava/lang/String;Z)V");
		midSetActivityEnabled_ = AndroidJniClass::getMethodID(nativeActivityClass, "setActivityEnabled", "(Ljava/lang/String;Z)V");
		midOpenUrl_ = AndroidJniClass::getMethodID(nativeActivityClass, "openUrl", "(Ljava/lang/String;)Z");
		midGetWindow_ = AndroidJniClass::getMethodID(nativeActivityClass, "getWindow", "()Landroid/view/Window;");

		jclass windowClass = AndroidJniClass::findClass("android/view/Window");
		midGetDecorView_ = AndroidJniClass::getMethodID(windowClass, "getDecorView", "()Landroid/view/View;");

		rectClass_ = AndroidJniClass::findClass("android/graphics/Rect");
		midRectInit_ = AndroidJniClass::getMethodID(rectClass_, "<init>", "()V");
		fidRectLeft_ = AndroidJniHelper::jniEnv->GetFieldID(rectClass_, "left", "I");
		fidRectTop_ = AndroidJniHelper::jniEnv->GetFieldID(rectClass_, "top", "I");
		fidRectRight_ = AndroidJniHelper::jniEnv->GetFieldID(rectClass_, "right", "I");
		fidRectBottom_ = AndroidJniHelper::jniEnv->GetFieldID(rectClass_, "bottom", "I");

		jclass viewClass = AndroidJniClass::findClass("android/view/View");
		midGetWindowVisibleDisplayFrame_ = AndroidJniClass::getMethodID(viewClass, "getWindowVisibleDisplayFrame", "(Landroid/graphics/Rect;)V");
	}

	void AndroidJniWrap_Activity::finishAndRemoveTask()
	{
		// Check if SDK version requirements are met
		if (AndroidJniHelper::SdkVersion() >= 21) {
			AndroidJniHelper::jniEnv->CallVoidMethod(activityObject_, midFinishAndRemoveTask_);
		}
	}

	String AndroidJniWrap_Activity::getPreferredLanguage()
	{
		jstring strLanguage = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(activityObject_, midGetPreferredLanguage_));
		if (!strLanguage) {
			return {};
		}

		jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strLanguage);
		const char* language = AndroidJniHelper::jniEnv->GetStringUTFChars(strLanguage, nullptr);
		String result = String(NoInit, length);
		std::memcpy(result.data(), language, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strLanguage, language);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strLanguage);
		return result;
	}

	bool AndroidJniWrap_Activity::isScreenRound()
	{
		if (AndroidJniHelper::SdkVersion() >= 23) {
			const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(activityObject_, midIsScreenRound_);
			return (result == JNI_TRUE);
		} else {
			return false;
		}
	}

	bool AndroidJniWrap_Activity::hasExternalStoragePermission()
	{
		if (AndroidJniHelper::SdkVersion() >= 30) {
			const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(activityObject_, midHasExternalStoragePermission_);
			return (result == JNI_TRUE);
		} else {
			return false;
		}
	}

	void AndroidJniWrap_Activity::requestExternalStoragePermission()
	{
		if (AndroidJniHelper::SdkVersion() >= 30) {
			AndroidJniHelper::jniEnv->CallVoidMethod(activityObject_, midRequestExternalStoragePermission_);
		}
	}
	
	void AndroidJniWrap_Activity::setActivityEnabled(StringView activity, bool enable)
	{
		if (midSetActivityEnabled_ == nullptr || activity.empty()) {
			return;
		}
		
		auto nullTerminatedName = String::nullTerminatedView(activity);
		jstring strName = AndroidJniHelper::jniEnv->NewStringUTF(nullTerminatedName.data());
		if (strName == nullptr) {
			return;
		}

		jboolean boolEnable = enable;
		AndroidJniHelper::jniEnv->CallVoidMethod(activityObject_, midSetActivityEnabled_, strName, boolEnable);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strName);
	}

	bool AndroidJniWrap_Activity::openUrl(StringView url)
	{
		if (midOpenUrl_ == nullptr || url.empty()) {
			return false;
		}

		auto nullTerminatedUrl = String::nullTerminatedView(url);
		jstring strUrl = AndroidJniHelper::jniEnv->NewStringUTF(nullTerminatedUrl.data());
		if (strUrl == nullptr) {
			return false;
		}

		jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(activityObject_, midOpenUrl_, strUrl);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strUrl);
		return result;
	}

	jobject AndroidJniWrap_Activity::getDecorView()
	{
		if (midGetWindow_ == nullptr || midGetDecorView_ == nullptr) {
			return nullptr;
		}

		jobject windowObject = AndroidJniHelper::jniEnv->CallObjectMethod(activityObject_, midGetWindow_);
		if (windowObject == nullptr) {
			return nullptr;
		}

		jobject decorViewObject = AndroidJniHelper::jniEnv->CallObjectMethod(windowObject, midGetDecorView_);
		AndroidJniHelper::jniEnv->DeleteLocalRef(windowObject);
		return decorViewObject;
	}

	Recti AndroidJniWrap_Activity::getVisibleBounds()
	{
		if (midRectInit_ == nullptr || midGetWindowVisibleDisplayFrame_ == nullptr ||
			fidRectLeft_ == nullptr || fidRectTop_ == nullptr || fidRectRight_ == nullptr || fidRectBottom_ == nullptr) {
			return {};
		}

		Recti result;
		jobject decorViewObject = getDecorView();
		if (decorViewObject != nullptr) {
			jobject rectObject = AndroidJniHelper::jniEnv->NewObject(rectClass_, midRectInit_);
			if (rectObject != nullptr) {
				AndroidJniHelper::jniEnv->CallVoidMethod(decorViewObject, midGetWindowVisibleDisplayFrame_, rectObject);

				std::int32_t left = AndroidJniHelper::jniEnv->GetIntField(rectObject, fidRectLeft_);
				std::int32_t top = AndroidJniHelper::jniEnv->GetIntField(rectObject, fidRectTop_);
				std::int32_t right = AndroidJniHelper::jniEnv->GetIntField(rectObject, fidRectRight_);
				std::int32_t bottom = AndroidJniHelper::jniEnv->GetIntField(rectObject, fidRectBottom_);

				result.X = left;
				result.Y = top;
				result.W = right - left;
				result.H = bottom - top;

				AndroidJniHelper::jniEnv->DeleteLocalRef(rectObject);
			}
			AndroidJniHelper::jniEnv->DeleteLocalRef(decorViewObject);
		}

		return result;
	}
	
	// ------------------- AndroidJniWrap_InputMethodManager -------------------

	void AndroidJniWrap_InputMethodManager::init(struct android_app* state)
	{
		// Retrieve `NativeActivity`
		jobject activityObject = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(activityObject);

		// Retrieve `Context.INPUT_METHOD_SERVICE`
		jclass contextClass = AndroidJniClass::findClass("android/content/Context");
		jfieldID fidInputMethodService = AndroidJniClass::getStaticFieldID(contextClass, "INPUT_METHOD_SERVICE", "Ljava/lang/String;");
		jobject inputMethodServiceObject = AndroidJniHelper::jniEnv->GetStaticObjectField(contextClass, fidInputMethodService);

		// Run `getSystemService(Context.INPUT_METHOD_SERVICE)`
		jclass inputMethodManagerClass = AndroidJniClass::findClass("android/view/inputmethod/InputMethodManager");
		jmethodID midGetSystemService = AndroidJniClass::getMethodID(nativeActivityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
		jobject inputMethodManagerObject = AndroidJniHelper::jniEnv->CallObjectMethod(activityObject, midGetSystemService, inputMethodServiceObject);
		inputMethodManagerObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(inputMethodManagerObject);

		midToggleSoftInput_ = AndroidJniClass::getMethodID(inputMethodManagerClass, "toggleSoftInput", "(II)V");
		midShowSoftInput_ = AndroidJniClass::getMethodID(inputMethodManagerClass, "showSoftInput", "(Landroid/view/View;I)Z");
		midHideSoftInput_ = AndroidJniClass::getMethodID(inputMethodManagerClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");

		jclass viewClass = AndroidJniClass::findClass("android/view/View");
		midGetWindowToken_ = AndroidJniClass::getMethodID(viewClass, "getWindowToken", "()Landroid/os/IBinder;");
	}

	void AndroidJniWrap_InputMethodManager::shutdown()
	{
		if (inputMethodManagerObject_) {
			AndroidJniHelper::jniEnv->DeleteGlobalRef(inputMethodManagerObject_);
		}
	}

	void AndroidJniWrap_InputMethodManager::toggleSoftInput()
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(inputMethodManagerObject_, midToggleSoftInput_, SHOW_IMPLICIT, HIDE_IMPLICIT_ONLY);
	}

	bool AndroidJniWrap_InputMethodManager::showSoftInput()
	{
		if (midShowSoftInput_ == nullptr) {
			return false;
		}

		bool result = false;
		jobject decorViewObject = AndroidJniWrap_Activity::getDecorView();
		if (decorViewObject != nullptr) {
			result = AndroidJniHelper::jniEnv->CallBooleanMethod(inputMethodManagerObject_, midShowSoftInput_, decorViewObject, 0);
			AndroidJniHelper::jniEnv->DeleteLocalRef(decorViewObject);
		}
		return result;
	}

	bool AndroidJniWrap_InputMethodManager::hideSoftInput()
	{
		if (midGetWindowToken_ == nullptr || midHideSoftInput_ == nullptr) {
			return false;
		}

		bool result = false;
		jobject decorViewObject = AndroidJniWrap_Activity::getDecorView();
		if (decorViewObject != nullptr) {
			jobject windowToken = AndroidJniHelper::jniEnv->CallObjectMethod(decorViewObject, midGetWindowToken_);
			if (windowToken != nullptr) {
				result = AndroidJniHelper::jniEnv->CallBooleanMethod(inputMethodManagerObject_, midHideSoftInput_, windowToken, 0);
				AndroidJniHelper::jniEnv->DeleteLocalRef(windowToken);
			}
			AndroidJniHelper::jniEnv->DeleteLocalRef(decorViewObject);
		}
		return result;
	}

	// ------------------- AndroidJniWrap_DisplayManager -------------------

	void AndroidJniWrap_DisplayManager::init(struct android_app* state)
	{
		// Retrieve `NativeActivity`
		jobject nativeActivityObject = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(nativeActivityObject);

		// Retrieve `Context.DISPLAY_SERVICE`
		jclass contextClass = AndroidJniClass::findClass("android/content/Context");
		jfieldID fidDisplayService = AndroidJniClass::getStaticFieldID(contextClass, "DISPLAY_SERVICE", "Ljava/lang/String;");
		jobject displayServiceObject = AndroidJniHelper::jniEnv->GetStaticObjectField(contextClass, fidDisplayService);

		// Run `getSystemService(Context.DISPLAY_SERVICE)`
		jclass displayManagerClass = AndroidJniClass::findClass("android/hardware/display/DisplayManager");
		jmethodID midGetSystemService = AndroidJniClass::getMethodID(nativeActivityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
		jobject displayManagerObject = AndroidJniHelper::jniEnv->CallObjectMethod(nativeActivityObject, midGetSystemService, displayServiceObject);
		displayManagerObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(displayManagerObject);
		AndroidJniHelper::jniEnv->DeleteLocalRef(displayManagerObject);

		midGetDisplay_ = AndroidJniClass::getMethodID(displayManagerClass, "getDisplay", "(I)Landroid/view/Display;");
		midGetDisplays_ = AndroidJniClass::getMethodID(displayManagerClass, "getDisplays", "()[Landroid/view/Display;");
	}

	void AndroidJniWrap_DisplayManager::shutdown()
	{
		if (displayManagerObject_) {
			AndroidJniHelper::jniEnv->DeleteGlobalRef(displayManagerObject_);
		}
	}

	AndroidJniClass_Display AndroidJniWrap_DisplayManager::getDisplay(int displayId)
	{
		jobject displayObject = static_cast<jobject>(AndroidJniHelper::jniEnv->CallObjectMethod(displayManagerObject_, midGetDisplay_, displayId));
		AndroidJniClass_Display display(displayObject);
		return display;
	}

	int AndroidJniWrap_DisplayManager::getDisplays(AndroidJniClass_Display* destination, int maxSize)
	{
		jobjectArray arrDisplays = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(displayManagerObject_, midGetDisplays_));
		int length = (int)AndroidJniHelper::jniEnv->GetArrayLength(arrDisplays);

		if (destination != nullptr && maxSize > 0) {
			for (int i = 0; i < length && i < maxSize; i++) {
				jobject displayObject = AndroidJniHelper::jniEnv->GetObjectArrayElement(arrDisplays, i);
				AndroidJniClass_Display display(displayObject);
				destination[i] = std::move(display);
				// The AndroidJniClass constructor deleted the local reference to `displayObject`
			}
		}
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrDisplays);

		return length;
	}
	
	// --------------------- AndroidJniWrap_Secure ---------------------

	void AndroidJniWrap_Secure::init(struct android_app* state)
	{
		jclass contextClass = AndroidJniClass::findClass("android/content/Context");
		jclass settingsSecureClass = AndroidJniClass::findClass("android/provider/Settings$Secure");
		if (contextClass == nullptr || settingsSecureClass == nullptr) {
			return;
		}

		jmethodID midGetContentResolver = AndroidJniClass::getMethodID(contextClass, "getContentResolver", "()Landroid/content/ContentResolver;");
		jmethodID midGetString = AndroidJniClass::getStaticMethodID(settingsSecureClass, "getString", "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
		jfieldID fidAndroidId = AndroidJniClass::getStaticFieldID(settingsSecureClass, "ANDROID_ID", "Ljava/lang/String;");
		if (midGetContentResolver == nullptr || midGetString == nullptr || fidAndroidId == nullptr) {
			return;
		}

		jstring sAndroidId = static_cast<jstring>(AndroidJniHelper::jniEnv->GetStaticObjectField(settingsSecureClass, fidAndroidId));
		jobject contentResolverObject = AndroidJniHelper::jniEnv->CallObjectMethod(state->activity->clazz, midGetContentResolver);
		if (sAndroidId == nullptr || contentResolverObject == nullptr) {
			return;
		}

		jstring strAndroidId = static_cast<jstring>(AndroidJniHelper::jniEnv->CallStaticObjectMethod(settingsSecureClass, midGetString, contentResolverObject, sAndroidId));
		AndroidJniHelper::jniEnv->DeleteLocalRef(contentResolverObject);
		if (strAndroidId == nullptr) {
			return;
		}
		
		jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strAndroidId);
		const char* androidId = AndroidJniHelper::jniEnv->GetStringUTFChars(strAndroidId, nullptr);
		androidId_ = String(NoInit, length);
		std::memcpy(androidId_.data(), androidId, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strAndroidId, androidId);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strAndroidId);
	}

	StringView AndroidJniWrap_Secure::getAndroidId()
	{
		return androidId_;
	}
}
