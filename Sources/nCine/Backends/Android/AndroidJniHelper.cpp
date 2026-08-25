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
	JavaVM* AndroidJniHelper::_javaVM = nullptr;
	JNIEnv* AndroidJniHelper::jniEnv = nullptr;
	unsigned int AndroidJniHelper::_sdkVersion = 0;

	jclass AndroidJniClass_MotionRange::_javaClass = nullptr;
	//jmethodID AndroidJniClass_MotionRange::_midGetFlat = nullptr;
	//jmethodID AndroidJniClass_MotionRange::_midGetFuzz = nullptr;
	//jmethodID AndroidJniClass_MotionRange::_midGetMax = nullptr;
	jmethodID AndroidJniClass_MotionRange::_midGetMin = nullptr;
	jmethodID AndroidJniClass_MotionRange::_midGetRange = nullptr;
	jclass AndroidJniClass_VibrationEffect::_javaClass = nullptr;
	jmethodID AndroidJniClass_VibrationEffect::_midCreateOneShot = nullptr;
	jclass AndroidJniClass_Vibrator::_javaClass = nullptr;
	jmethodID AndroidJniClass_Vibrator::_midCancel = nullptr;
	jmethodID AndroidJniClass_Vibrator::_midVibrate = nullptr;
	jclass AndroidJniClass_VibratorManager::_javaClass = nullptr;
	jmethodID AndroidJniClass_VibratorManager::_midCancel = nullptr;
	jmethodID AndroidJniClass_VibratorManager::_midGetVibratorIds = nullptr;
	jmethodID AndroidJniClass_VibratorManager::_midGetVibrator = nullptr;
	jclass AndroidJniClass_InputDevice::_javaClass = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetDevice = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetDeviceIds = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetName = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetDescriptor = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetProductId = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetVendorId = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetMotionRange = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetSources = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midHasKeys = nullptr;
	jmethodID AndroidJniClass_InputDevice::_midGetVibratorManager = nullptr;
	jclass AndroidJniClass_KeyCharacterMap::_javaClass = nullptr;
	jmethodID AndroidJniClass_KeyCharacterMap::_midDeviceHasKey = nullptr;
	jclass AndroidJniClass_KeyEvent::_javaClass = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midConstructor2 = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midConstructor = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midGetUnicodeCharMetaState = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midGetUnicodeChar = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midGetCharacters = nullptr;
	jmethodID AndroidJniClass_KeyEvent::_midIsPrintingKey = nullptr;
	jclass AndroidJniClass_Display::_javaClass = nullptr;
	jmethodID AndroidJniClass_Display::_midGetMode = nullptr;
	jmethodID AndroidJniClass_Display::_midGetName = nullptr;
	jmethodID AndroidJniClass_Display::_midGetSupportedModes = nullptr;
	jclass AndroidJniClass_DisplayMode::_javaClass = nullptr;
	jmethodID AndroidJniClass_DisplayMode::_midGetPhysicalHeight = nullptr;
	jmethodID AndroidJniClass_DisplayMode::_midGetPhysicalWidth = nullptr;
	jmethodID AndroidJniClass_DisplayMode::_midGetRefreshRate = nullptr;

	jobject AndroidJniWrap_Activity::_activityObject = nullptr;
	jmethodID AndroidJniWrap_Activity::_midFinishAndRemoveTask = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetPackageName = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetPreferredLanguage = nullptr;
	jmethodID AndroidJniWrap_Activity::_midIsScreenRound = nullptr;
	jmethodID AndroidJniWrap_Activity::_midHasExternalStoragePermission = nullptr;
	jmethodID AndroidJniWrap_Activity::_midRequestExternalStoragePermission = nullptr;
	jmethodID AndroidJniWrap_Activity::_midSetActivityEnabled = nullptr;
	jmethodID AndroidJniWrap_Activity::_midOpenUrl = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetWindow = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetSystemService = nullptr;
	jobject AndroidJniWrap_Activity::_vibratorObject = nullptr;
	jobject AndroidJniWrap_Activity::_insetsControllerObject = nullptr;
	jmethodID AndroidJniWrap_Activity::_midInsetsShow = nullptr;
	jmethodID AndroidJniWrap_Activity::_midInsetsHide = nullptr;

	jmethodID AndroidJniWrap_Activity::_midGetDecorView = nullptr;
	jclass AndroidJniWrap_Activity::_rectClass = nullptr;
	jmethodID AndroidJniWrap_Activity::_midRectInit = nullptr;
	jfieldID AndroidJniWrap_Activity::_fidRectLeft = nullptr;
	jfieldID AndroidJniWrap_Activity::_fidRectTop = nullptr;
	jfieldID AndroidJniWrap_Activity::_fidRectRight = nullptr;
	jfieldID AndroidJniWrap_Activity::_fidRectBottom = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetWindowVisibleDisplayFrame = nullptr;
	jmethodID AndroidJniWrap_Activity::_midGetVisibleBounds = nullptr;

	jobject AndroidJniWrap_InputMethodManager::_activityObject = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::_midIsSoftInputAvailable = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::_midIsSoftInputVisible = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::_midShowSoftInput = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::_midHideSoftInput = nullptr;

	jobject AndroidJniWrap_DisplayManager::_displayManagerObject = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::_midGetDisplay = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::_midGetDisplays = nullptr;

	String AndroidJniWrap_Secure::_androidId;

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
		_javaVM = state->activity->vm;

		// This is called before PreInitCommon(), so trace targets are usually not attached yet, only logcat
		if (_javaVM == nullptr) {
			LOGE("JavaVM pointer is null");
		} else {
			const int getEnvStatus = _javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);
			if (getEnvStatus == JNI_EDETACHED) {
				if (_javaVM->AttachCurrentThread(&jniEnv, nullptr) != 0) {
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
				_sdkVersion = AndroidJniClass_Version::sdkInt();
			}
		}
	}

	void AndroidJniHelper::DetachJVM()
	{
		if (_javaVM) {
			AndroidJniWrap_DisplayManager::shutdown();

			_javaVM->DetachCurrentThread();
			LOGI("Thread detached");
			jniEnv = nullptr;
			_javaVM = nullptr;
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
		: _javaObject(nullptr)
	{
		FATAL_ASSERT(AndroidJniHelper::jniEnv != nullptr);
		if (javaObject) {
			_javaObject = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
			AndroidJniHelper::jniEnv->DeleteLocalRef(javaObject);
		}
	}

	AndroidJniClass::~AndroidJniClass()
	{
		if (_javaObject) {
			AndroidJniHelper::jniEnv->DeleteGlobalRef(_javaObject);
		}
	}

	AndroidJniClass::AndroidJniClass(AndroidJniClass&& other)
		: _javaObject(other._javaObject)
	{
		other._javaObject = nullptr;
	}

	AndroidJniClass& AndroidJniClass::operator=(AndroidJniClass&& other)
	{
		std::swap(_javaObject, other._javaObject);
		return *this;
	}

	jclass AndroidJniClass::findClass(const char* name)
	{
		DEATH_ASSERT(name != nullptr);
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

		DEATH_ASSERT(name != nullptr && signature != nullptr);
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

		DEATH_ASSERT(name != nullptr && signature != nullptr);
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

		DEATH_ASSERT(name != nullptr && signature != nullptr);
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
		_javaClass = findClass("android/view/InputDevice$MotionRange");
		_midGetMin = getMethodID(_javaClass, "getMin", "()F");
		_midGetRange = getMethodID(_javaClass, "getRange", "()F");
	}

	AndroidJniClass_MotionRange::AndroidJniClass_MotionRange(jobject javaObject)
		: AndroidJniClass(javaObject)
	{
	}

	float AndroidJniClass_MotionRange::getMin() const
	{
		const jfloat minValue = AndroidJniHelper::jniEnv->CallFloatMethod(_javaObject, _midGetMin);
		return float(minValue);
	}

	float AndroidJniClass_MotionRange::getRange() const
	{
		const jfloat rangeValue = AndroidJniHelper::jniEnv->CallFloatMethod(_javaObject, _midGetRange);
		return float(rangeValue);
	}

	// ------------------- AndroidJniClass_VibrationEffect -------------------

	void AndroidJniClass_VibrationEffect::init()
	{
		_javaClass = findClass("android/os/VibrationEffect");
		_midCreateOneShot = getStaticMethodID(_javaClass, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
	}

	AndroidJniClass_VibrationEffect AndroidJniClass_VibrationEffect::createOneShot(long milliseconds, int amplitude)
	{
		jobject vibrationEffect = AndroidJniHelper::jniEnv->CallStaticObjectMethod(_javaClass, _midCreateOneShot, milliseconds, amplitude);
		return AndroidJniClass_VibrationEffect(vibrationEffect);
	}

	// ------------------- AndroidJniClass_Vibrator -------------------

	void AndroidJniClass_Vibrator::init()
	{
		_javaClass = findClass("android/os/Vibrator");
		_midCancel = getMethodID(_javaClass, "cancel", "()V");
		_midVibrate = getMethodID(_javaClass, "vibrate", "(Landroid/os/VibrationEffect;)V");
	}

	void AndroidJniClass_Vibrator::cancel() const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(_javaObject, _midCancel);
	}

	void AndroidJniClass_Vibrator::vibrate(const AndroidJniClass_VibrationEffect& vibe) const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(_javaObject, _midVibrate, vibe.javaObject());
	}

	// ------------------- AndroidJniClass_VibratorManager -------------------

	void AndroidJniClass_VibratorManager::init()
	{
		_javaClass = findClass("android/os/VibratorManager");
		_midCancel = getMethodID(_javaClass, "cancel", "()V");
		_midGetVibratorIds = getMethodID(_javaClass, "getVibratorIds", "()[I");
		_midGetVibrator = getMethodID(_javaClass, "getVibrator", "(I)Landroid/os/Vibrator;");
	}

	AndroidJniClass_VibratorManager::AndroidJniClass_VibratorManager(jobject javaObject)
		: AndroidJniClass(javaObject)
	{
	}

	void AndroidJniClass_VibratorManager::cancel() const
	{
		AndroidJniHelper::jniEnv->CallVoidMethod(_javaObject, _midCancel);
	}

	int AndroidJniClass_VibratorManager::getVibratorIds(int* destination, int maxSize) const
	{
		jintArray arrVibratorIds = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetVibratorIds));
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
		jobject vibratorObject = AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetVibrator, vibratorId);
		return AndroidJniClass_Vibrator(vibratorObject);
	}

	// ------------------- AndroidJniClass_InputDevice -------------------

	void AndroidJniClass_InputDevice::init()
	{
		_javaClass = findClass("android/view/InputDevice");
		_midGetDevice = getStaticMethodID(_javaClass, "getDevice", "(I)Landroid/view/InputDevice;");
		_midGetDeviceIds = getStaticMethodID(_javaClass, "getDeviceIds", "()[I");
		_midGetName = getMethodID(_javaClass, "getName", "()Ljava/lang/String;");
		_midGetDescriptor = getMethodID(_javaClass, "getDescriptor", "()Ljava/lang/String;");
		_midGetProductId = getMethodID(_javaClass, "getProductId", "()I");
		_midGetVendorId = getMethodID(_javaClass, "getVendorId", "()I");
		_midGetMotionRange = getMethodID(_javaClass, "getMotionRange", "(I)Landroid/view/InputDevice$MotionRange;");
		_midGetSources = getMethodID(_javaClass, "getSources", "()I");
		_midHasKeys = getMethodID(_javaClass, "hasKeys", "([I)[Z");
		_midGetVibratorManager = getMethodID(_javaClass, "getVibratorManager", "()Landroid/os/VibratorManager;");
	}

	AndroidJniClass_InputDevice AndroidJniClass_InputDevice::getDevice(int deviceId)
	{
		jobject inputDeviceObject = AndroidJniHelper::jniEnv->CallStaticObjectMethod(_javaClass, _midGetDevice, deviceId);
		return AndroidJniClass_InputDevice(inputDeviceObject);
	}

	int AndroidJniClass_InputDevice::getDeviceIds(int* destination, int maxSize)
	{
		jintArray arrDeviceIds = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallStaticObjectMethod(_javaClass, _midGetDeviceIds));
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
		jstring strDeviceName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetName));
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
		jstring strDeviceDescriptor = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetDescriptor));
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
		const jint productId = AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetProductId);
		return int(productId);
	}

	int AndroidJniClass_InputDevice::getVendorId() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 19) {
			return 0;
		}
		const jint vendorID = AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetVendorId);
		return int(vendorID);
	}

	AndroidJniClass_MotionRange AndroidJniClass_InputDevice::getMotionRange(int axis) const
	{
		jobject motionRangeObject = AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetMotionRange, axis);
		return AndroidJniClass_MotionRange(motionRangeObject);
	}

	int AndroidJniClass_InputDevice::getSources() const
	{
		const jint sources = AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetSources);
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

		jbooleanArray arrBooleans = static_cast<jbooleanArray>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midHasKeys, arrButtons));
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

		jobject vibratorManagerObject = AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetVibratorManager);
		return AndroidJniClass_VibratorManager(vibratorManagerObject);
	}

	// ------------------- AndroidJniClass_KeyCharacterMap -------------------

	void AndroidJniClass_KeyCharacterMap::init()
	{
		_javaClass = findClass("android/view/KeyCharacterMap");
		_midDeviceHasKey = getStaticMethodID(_javaClass, "deviceHasKey", "(I)Z");
	}

	bool AndroidJniClass_KeyCharacterMap::deviceHasKey(int button)
	{
		const jboolean hasKey = AndroidJniHelper::jniEnv->CallStaticBooleanMethod(_javaClass, _midDeviceHasKey, button);
		return (hasKey == JNI_TRUE);
	}

	// ------------------- AndroidJniClass_KeyEvent -------------------

	void AndroidJniClass_KeyEvent::init()
	{
		_javaClass = findClass("android/view/KeyEvent");
		_midConstructor = getMethodID(_javaClass, "<init>", "(II)V");
		_midConstructor2 = getMethodID(_javaClass, "<init>", "(JJIIIIIIII)V");
		_midGetUnicodeCharMetaState = getMethodID(_javaClass, "getUnicodeChar", "(I)I");
		_midGetUnicodeChar = getMethodID(_javaClass, "getUnicodeChar", "()I");
		_midGetCharacters = getMethodID(_javaClass, "getCharacters", "()Ljava/lang/String;");
		_midIsPrintingKey = getMethodID(_javaClass, "isPrintingKey", "()Z");
	}

	AndroidJniClass_KeyEvent::AndroidJniClass_KeyEvent(int action, int code)
	{
		jobject javaObject = AndroidJniHelper::jniEnv->NewObject(_javaClass, _midConstructor, action, code);
		_javaObject = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
	}

	AndroidJniClass_KeyEvent::AndroidJniClass_KeyEvent(long long int downTime, long long int eventTime, int action, int code, int repeat, int metaState, int deviceId, int scancode, int flags, int source)
	{
		jobject javaObject = AndroidJniHelper::jniEnv->NewObject(_javaClass, _midConstructor2, downTime, eventTime, action, code, repeat, metaState, deviceId, scancode, flags, source);
		_javaObject = AndroidJniHelper::jniEnv->NewGlobalRef(javaObject);
	}

	int AndroidJniClass_KeyEvent::getUnicodeChar(int metaState) const
	{
		if (metaState != 0) {
			return AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetUnicodeCharMetaState, metaState);
		} else {
			return AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetUnicodeChar);
		}
	}

	int AndroidJniClass_KeyEvent::getCharacters(char* destination, int maxStringSize) const
	{
		jstring strCharacters = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetCharacters));
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
		return AndroidJniHelper::jniEnv->CallBooleanMethod(_javaObject, _midIsPrintingKey);
	}

	// ------------------- AndroidJniClass_DisplayMode -------------------

	void AndroidJniClass_DisplayMode::init()
	{
		_javaClass = findClass("android/view/Display$Mode");
		_midGetPhysicalHeight = getMethodID(_javaClass, "getPhysicalHeight", "()I");
		_midGetPhysicalWidth = getMethodID(_javaClass, "getPhysicalWidth", "()I");
		_midGetRefreshRate = getMethodID(_javaClass, "getRefreshRate", "()F");
	}

	int AndroidJniClass_DisplayMode::getPhysicalHeight() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jint physicalHeight = AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetPhysicalHeight);
		return int(physicalHeight);
	}

	int AndroidJniClass_DisplayMode::getPhysicalWidth() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jint physicalWidth = AndroidJniHelper::jniEnv->CallIntMethod(_javaObject, _midGetPhysicalWidth);
		return int(physicalWidth);
	}

	float AndroidJniClass_DisplayMode::getRefreshRate() const
	{
		// Early-out if SDK version requirements are not met
		if (AndroidJniHelper::SdkVersion() < 23) {
			return 0;
		}
		const jfloat refreshRate = AndroidJniHelper::jniEnv->CallFloatMethod(_javaObject, _midGetRefreshRate);
		return float(refreshRate);
	}

	// ------------------- AndroidJniClass_Display -------------------

	void AndroidJniClass_Display::init()
	{
		_javaClass = findClass("android/view/Display");
		_midGetMode = getMethodID(_javaClass, "getMode", "()Landroid/view/Display$Mode;");
		_midGetName = getMethodID(_javaClass, "getName", "()Ljava/lang/String;");
		_midGetSupportedModes = getMethodID(_javaClass, "getSupportedModes", "()[Landroid/view/Display$Mode;");
	}

	AndroidJniClass_DisplayMode AndroidJniClass_Display::getMode() const
	{
		jobject modeObject = static_cast<jobject>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetMode));
		return AndroidJniClass_DisplayMode(modeObject);
	}

	int AndroidJniClass_Display::getName(char* destination, int maxStringSize) const
	{
		jstring strDisplayName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetName));
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
		jobjectArray arrModes = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(_javaObject, _midGetSupportedModes));
		if (AndroidJniHelper::CheckAndClearExceptions() || arrModes == nullptr) {
			// A display that is being removed reports no modes, and passing null to GetArrayLength() below
			// would abort the whole process with a JNI error instead
			return 0;
		}

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
		_activityObject = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(_activityObject);

		_midFinishAndRemoveTask = AndroidJniClass::getMethodID(nativeActivityClass, "finishAndRemoveTask", "()V");
		_midGetPackageName = AndroidJniClass::getMethodID(nativeActivityClass, "getPackageName", "()Ljava/lang/String;");
		_midGetPreferredLanguage = AndroidJniClass::getMethodID(nativeActivityClass, "getPreferredLanguage", "()Ljava/lang/String;");
		_midIsScreenRound = AndroidJniClass::getMethodID(nativeActivityClass, "isScreenRound", "()Z");
		_midHasExternalStoragePermission = AndroidJniClass::getMethodID(nativeActivityClass, "hasExternalStoragePermission", "()Z");
		_midRequestExternalStoragePermission = AndroidJniClass::getMethodID(nativeActivityClass, "requestExternalStoragePermission", "()V");
		_midSetActivityEnabled = AndroidJniClass::getMethodID(nativeActivityClass, "setActivityEnabled", "(Ljava/lang/String;Z)V");
		_midSetActivityEnabled = AndroidJniClass::getMethodID(nativeActivityClass, "setActivityEnabled", "(Ljava/lang/String;Z)V");
		_midOpenUrl = AndroidJniClass::getMethodID(nativeActivityClass, "openUrl", "(Ljava/lang/String;)Z");
		_midGetWindow = AndroidJniClass::getMethodID(nativeActivityClass, "getWindow", "()Landroid/view/Window;");
		_midGetSystemService = AndroidJniClass::getMethodID(nativeActivityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");

		jclass windowClass = AndroidJniClass::findClass("android/view/Window");
		_midGetDecorView = AndroidJniClass::getMethodID(windowClass, "getDecorView", "()Landroid/view/View;");

		_rectClass = AndroidJniClass::findClass("android/graphics/Rect");
		_midRectInit = AndroidJniClass::getMethodID(_rectClass, "<init>", "()V");
		_fidRectLeft = AndroidJniHelper::jniEnv->GetFieldID(_rectClass, "left", "I");
		_fidRectTop = AndroidJniHelper::jniEnv->GetFieldID(_rectClass, "top", "I");
		_fidRectRight = AndroidJniHelper::jniEnv->GetFieldID(_rectClass, "right", "I");
		_fidRectBottom = AndroidJniHelper::jniEnv->GetFieldID(_rectClass, "bottom", "I");

		jclass viewClass = AndroidJniClass::findClass("android/view/View");
		_midGetWindowVisibleDisplayFrame = AndroidJniClass::getMethodID(viewClass, "getWindowVisibleDisplayFrame", "(Landroid/graphics/Rect;)V");

		_midGetVisibleBounds = AndroidJniClass::getMethodID(nativeActivityClass, "getVisibleBounds", "()[I");

		// Cache the device vibrator for reuse
		{
			jstring svcName = AndroidJniHelper::jniEnv->NewStringUTF("vibrator");
			jobject vibObj = AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetSystemService, svcName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(svcName);
			if (vibObj != nullptr) {
				_vibratorObject = AndroidJniHelper::jniEnv->NewGlobalRef(vibObj);
				AndroidJniHelper::jniEnv->DeleteLocalRef(vibObj);
			}
		}

		// Set up transparent status bar and cache WindowInsetsController (API 30+)
		if (_midGetWindow != nullptr && AndroidJniHelper::SdkVersion() >= 30) {
			jobject windowObj = AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetWindow);
			if (windowObj != nullptr) {
				jclass windowClass = AndroidJniHelper::jniEnv->GetObjectClass(windowObj);

				// Edge-to-edge: content draws behind system bars
				jmethodID midSetDecorFitsSystemWindows = AndroidJniClass::getMethodID(windowClass, "setDecorFitsSystemWindows", "(Z)V");
				if (midSetDecorFitsSystemWindows != nullptr) {
					AndroidJniHelper::jniEnv->CallVoidMethod(windowObj, midSetDecorFitsSystemWindows, JNI_FALSE);
				}

				// Fully transparent status bar color
				jmethodID midSetStatusBarColor = AndroidJniClass::getMethodID(windowClass, "setStatusBarColor", "(I)V");
				if (midSetStatusBarColor != nullptr) {
					AndroidJniHelper::jniEnv->CallVoidMethod(windowObj, midSetStatusBarColor, (jint)0x00000000);
				}

				// Cache WindowInsetsController
				jmethodID midGetInsetsController = AndroidJniClass::getMethodID(windowClass, "getInsetsController", "()Landroid/view/WindowInsetsController;");
				if (midGetInsetsController != nullptr) {
					jobject controller = AndroidJniHelper::jniEnv->CallObjectMethod(windowObj, midGetInsetsController);
					if (controller != nullptr) {
						_insetsControllerObject = AndroidJniHelper::jniEnv->NewGlobalRef(controller);
						AndroidJniHelper::jniEnv->DeleteLocalRef(controller);

						jclass controllerClass = AndroidJniHelper::jniEnv->GetObjectClass(_insetsControllerObject);
						_midInsetsShow = AndroidJniClass::getMethodID(controllerClass, "show", "(I)V");
						_midInsetsHide = AndroidJniClass::getMethodID(controllerClass, "hide", "(I)V");
						AndroidJniHelper::jniEnv->DeleteLocalRef(controllerClass);
					}
				}

				AndroidJniHelper::jniEnv->DeleteLocalRef(windowClass);
				AndroidJniHelper::jniEnv->DeleteLocalRef(windowObj);
			}
		}
	}

	void AndroidJniWrap_Activity::finishAndRemoveTask()
	{
		// Check if SDK version requirements are met
		if (AndroidJniHelper::SdkVersion() >= 21) {
			AndroidJniHelper::jniEnv->CallVoidMethod(_activityObject, _midFinishAndRemoveTask);
		}
	}

	String AndroidJniWrap_Activity::getPackageName()
	{
		jstring strPackageName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetPackageName));
		if (!strPackageName) {
			return {};
		}

		jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strPackageName);
		const char* packageName = AndroidJniHelper::jniEnv->GetStringUTFChars(strPackageName, nullptr);
		String result(NoInit, length);
		std::memcpy(result.data(), packageName, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strPackageName, packageName);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strPackageName);
		return result;
	}

	String AndroidJniWrap_Activity::getPreferredLanguage()
	{
		jstring strLanguage = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetPreferredLanguage));
		if (!strLanguage) {
			return {};
		}

		jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strLanguage);
		const char* language = AndroidJniHelper::jniEnv->GetStringUTFChars(strLanguage, nullptr);
		String result(NoInit, length);
		std::memcpy(result.data(), language, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strLanguage, language);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strLanguage);
		return result;
	}

	bool AndroidJniWrap_Activity::isScreenRound()
	{
		if (AndroidJniHelper::SdkVersion() >= 23) {
			const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midIsScreenRound);
			return (result == JNI_TRUE);
		} else {
			return false;
		}
	}

	bool AndroidJniWrap_Activity::hasExternalStoragePermission()
	{
		if (AndroidJniHelper::SdkVersion() >= 30) {
			const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midHasExternalStoragePermission);
			return (result == JNI_TRUE);
		} else {
			return false;
		}
	}

	void AndroidJniWrap_Activity::requestExternalStoragePermission()
	{
		if (AndroidJniHelper::SdkVersion() >= 30) {
			AndroidJniHelper::jniEnv->CallVoidMethod(_activityObject, _midRequestExternalStoragePermission);
		}
	}
	
	void AndroidJniWrap_Activity::setActivityEnabled(StringView activity, bool enable)
	{
		if (_midSetActivityEnabled == nullptr || activity.empty()) {
			return;
		}
		
		auto nullTerminatedName = String::nullTerminatedView(activity);
		jstring strName = AndroidJniHelper::jniEnv->NewStringUTF(nullTerminatedName.data());
		if (strName == nullptr) {
			return;
		}

		jboolean boolEnable = enable;
		AndroidJniHelper::jniEnv->CallVoidMethod(_activityObject, _midSetActivityEnabled, strName, boolEnable);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strName);
	}

	bool AndroidJniWrap_Activity::openUrl(StringView url)
	{
		if (_midOpenUrl == nullptr || url.empty()) {
			return false;
		}

		auto nullTerminatedUrl = String::nullTerminatedView(url);
		jstring strUrl = AndroidJniHelper::jniEnv->NewStringUTF(nullTerminatedUrl.data());
		if (strUrl == nullptr) {
			return false;
		}

		jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midOpenUrl, strUrl);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strUrl);
		return result;
	}

	jobject AndroidJniWrap_Activity::getDecorView()
	{
		if (_midGetWindow == nullptr || _midGetDecorView == nullptr) {
			return nullptr;
		}

		jobject windowObject = AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetWindow);
		if (windowObject == nullptr) {
			return nullptr;
		}

		jobject decorViewObject = AndroidJniHelper::jniEnv->CallObjectMethod(windowObject, _midGetDecorView);
		AndroidJniHelper::jniEnv->DeleteLocalRef(windowObject);
		return decorViewObject;
	}

	Recti AndroidJniWrap_Activity::getVisibleBounds()
	{
		// Prefer the activity of the Java bridge, because it also accounts for the screen keyboard, which
		// the visible display frame doesn't report for a window drawn edge-to-edge
		if (_midGetVisibleBounds != nullptr) {
			jintArray boundsArray = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallObjectMethod(_activityObject, _midGetVisibleBounds));
			if (!AndroidJniHelper::CheckAndClearExceptions() && boundsArray != nullptr) {
				Recti result;
				if (AndroidJniHelper::jniEnv->GetArrayLength(boundsArray) >= 4) {
					jint bounds[4];
					AndroidJniHelper::jniEnv->GetIntArrayRegion(boundsArray, 0, 4, bounds);
					result = Recti(bounds[0], bounds[1], bounds[2], bounds[3]);
				}
				AndroidJniHelper::jniEnv->DeleteLocalRef(boundsArray);
				return result;
			}
		}

		if (_midRectInit == nullptr || _midGetWindowVisibleDisplayFrame == nullptr ||
			_fidRectLeft == nullptr || _fidRectTop == nullptr || _fidRectRight == nullptr || _fidRectBottom == nullptr) {
			return {};
		}

		Recti result;
		jobject decorViewObject = getDecorView();
		if (decorViewObject != nullptr) {
			jobject rectObject = AndroidJniHelper::jniEnv->NewObject(_rectClass, _midRectInit);
			if (rectObject != nullptr) {
				AndroidJniHelper::jniEnv->CallVoidMethod(decorViewObject, _midGetWindowVisibleDisplayFrame, rectObject);

				std::int32_t left = AndroidJniHelper::jniEnv->GetIntField(rectObject, _fidRectLeft);
				std::int32_t top = AndroidJniHelper::jniEnv->GetIntField(rectObject, _fidRectTop);
				std::int32_t right = AndroidJniHelper::jniEnv->GetIntField(rectObject, _fidRectRight);
				std::int32_t bottom = AndroidJniHelper::jniEnv->GetIntField(rectObject, _fidRectBottom);

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

	void AndroidJniWrap_Activity::vibrate(std::int32_t milliseconds)
	{
		if (_vibratorObject == nullptr || milliseconds <= 0) {
			return;
		}

		if (AndroidJniHelper::SdkVersion() >= 26) {
			// Android 8+ - use VibrationEffect::createOneShot
			const AndroidJniClass_VibrationEffect effect = AndroidJniClass_VibrationEffect::createOneShot(
				static_cast<long>(milliseconds), -1 /* DEFAULT_AMPLITUDE */);
			// _vibratorObject is a global ref; wrap it via a local ref so AndroidJniClass ctor can DeleteLocalRef safely
			jobject localRef = AndroidJniHelper::jniEnv->NewLocalRef(_vibratorObject);
			AndroidJniClass_Vibrator vibrator(localRef);
			vibrator.vibrate(effect);
		}
		AndroidJniHelper::CheckAndClearExceptions();
	}

	void AndroidJniWrap_Activity::showStatusBar()
	{
		if (_insetsControllerObject == nullptr || _midInsetsShow == nullptr) {
			return;
		}

		// WindowInsets.Type.statusBars() == 1
		AndroidJniHelper::jniEnv->CallVoidMethod(_insetsControllerObject, _midInsetsShow, (jint)1);
		AndroidJniHelper::CheckAndClearExceptions();
	}

	void AndroidJniWrap_Activity::hideStatusBar()
	{
		if (_insetsControllerObject == nullptr || _midInsetsHide == nullptr) {
			return;
		}

		// WindowInsets.Type.statusBars() == 1
		AndroidJniHelper::jniEnv->CallVoidMethod(_insetsControllerObject, _midInsetsHide, (jint)1);
		AndroidJniHelper::CheckAndClearExceptions();
	}

	// ------------------- AndroidJniWrap_InputMethodManager -------------------

	void AndroidJniWrap_InputMethodManager::init(struct android_app* state)
	{
		// The activity of the Java bridge owns the text editor the keyboard attaches to, so everything is
		// delegated to it - it also takes care of running the requests on the UI thread
		_activityObject = state->activity->clazz;
		jclass activityClass = AndroidJniHelper::jniEnv->GetObjectClass(_activityObject);

		_midIsSoftInputAvailable = AndroidJniClass::getMethodID(activityClass, "isSoftInputAvailable", "()Z");
		_midIsSoftInputVisible = AndroidJniClass::getMethodID(activityClass, "isSoftInputVisible", "()Z");
		_midShowSoftInput = AndroidJniClass::getMethodID(activityClass, "showSoftInput", "()Z");
		_midHideSoftInput = AndroidJniClass::getMethodID(activityClass, "hideSoftInput", "()Z");

		AndroidJniHelper::jniEnv->DeleteLocalRef(activityClass);
	}

	bool AndroidJniWrap_InputMethodManager::isSoftInputAvailable()
	{
		if (_midIsSoftInputAvailable == nullptr) {
			return false;
		}

		const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midIsSoftInputAvailable);
		AndroidJniHelper::CheckAndClearExceptions();
		return (result == JNI_TRUE);
	}

	bool AndroidJniWrap_InputMethodManager::isSoftInputVisible()
	{
		if (_midIsSoftInputVisible == nullptr) {
			return false;
		}

		const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midIsSoftInputVisible);
		AndroidJniHelper::CheckAndClearExceptions();
		return (result == JNI_TRUE);
	}

	bool AndroidJniWrap_InputMethodManager::showSoftInput()
	{
		if (_midShowSoftInput == nullptr) {
			return false;
		}

		const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midShowSoftInput);
		AndroidJniHelper::CheckAndClearExceptions();
		return (result == JNI_TRUE);
	}

	bool AndroidJniWrap_InputMethodManager::hideSoftInput()
	{
		if (_midHideSoftInput == nullptr) {
			return false;
		}

		const jboolean result = AndroidJniHelper::jniEnv->CallBooleanMethod(_activityObject, _midHideSoftInput);
		AndroidJniHelper::CheckAndClearExceptions();
		return (result == JNI_TRUE);
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
		_displayManagerObject = AndroidJniHelper::jniEnv->NewGlobalRef(displayManagerObject);
		AndroidJniHelper::jniEnv->DeleteLocalRef(displayManagerObject);

		_midGetDisplay = AndroidJniClass::getMethodID(displayManagerClass, "getDisplay", "(I)Landroid/view/Display;");
		_midGetDisplays = AndroidJniClass::getMethodID(displayManagerClass, "getDisplays", "()[Landroid/view/Display;");
	}

	void AndroidJniWrap_DisplayManager::shutdown()
	{
		if (_displayManagerObject) {
			AndroidJniHelper::jniEnv->DeleteGlobalRef(_displayManagerObject);
		}
	}

	AndroidJniClass_Display AndroidJniWrap_DisplayManager::getDisplay(int displayId)
	{
		jobject displayObject = static_cast<jobject>(AndroidJniHelper::jniEnv->CallObjectMethod(_displayManagerObject, _midGetDisplay, displayId));
		AndroidJniClass_Display display(displayObject);
		return display;
	}

	int AndroidJniWrap_DisplayManager::getDisplays(AndroidJniClass_Display* destination, int maxSize)
	{
		jobjectArray arrDisplays = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(_displayManagerObject, _midGetDisplays));
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
		_androidId = String(NoInit, length);
		std::memcpy(_androidId.data(), androidId, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strAndroidId, androidId);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strAndroidId);
	}

	StringView AndroidJniWrap_Secure::getAndroidId()
	{
		return _androidId;
	}
}
