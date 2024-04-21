#include "AndroidJniHelper.h"
#include "AndroidApplication.h"
#include "../../../Common.h"
#include "../../Base/Algorithms.h"
#include "../../Base/Timer.h"

#include <cstring>
#include <utility>

#include <sys/system_properties.h>

namespace nCine
{
	JavaVM* AndroidJniHelper::javaVM_ = nullptr;
	JNIEnv* AndroidJniHelper::jniEnv = nullptr;
	unsigned int AndroidJniHelper::sdkVersion_ = 0;

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
	jclass AndroidJniClass_KeyCharacterMap::javaClass_ = nullptr;
	jmethodID AndroidJniClass_KeyCharacterMap::midDeviceHasKey_ = nullptr;
	jclass AndroidJniClass_MotionRange::javaClass_ = nullptr;
	jmethodID AndroidJniClass_MotionRange::midGetMin_ = nullptr;
	jmethodID AndroidJniClass_MotionRange::midGetRange_ = nullptr;
	jclass AndroidJniClass_KeyEvent::javaClass_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midConstructor_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midGetUnicodeCharMetaState_ = nullptr;
	jmethodID AndroidJniClass_KeyEvent::midGetUnicodeChar_ = nullptr;
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
	jmethodID AndroidJniWrap_Activity::midHasExternalStoragePermission_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midRequestExternalStoragePermission_ = nullptr;
	jmethodID AndroidJniWrap_Activity::midSetActivityEnabled_ = nullptr;

	jobject AndroidJniWrap_InputMethodManager::inputMethodManagerObject_ = nullptr;
	jmethodID AndroidJniWrap_InputMethodManager::midToggleSoftInput_ = nullptr;

	jobject AndroidJniWrap_DisplayManager::displayManagerObject_ = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::midGetDisplay_ = nullptr;
	jmethodID AndroidJniWrap_DisplayManager::midGetDisplays_ = nullptr;

	String AndroidJniWrap_Secure::androidId_;

	// ------------------- AndroidJniHelper -------------------

	void AndroidJniHelper::AttachJVM(struct android_app* state)
	{
		javaVM_ = state->activity->vm;

		if (javaVM_ == nullptr) {
			LOGE("JavaVM pointer is null");
		} else {
			const int getEnvStatus = javaVM_->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);
			if (getEnvStatus == JNI_EDETACHED) {
				if (javaVM_->AttachCurrentThread(&jniEnv, nullptr) != 0) {
					LOGW("AttachCurrentThread() cannot attach the JVM");
				} else {
					LOGI("AttachCurrentThread() successful");
				}
			} else if (getEnvStatus == JNI_EVERSION) {
				LOGW("GetEnv() with unsupported version");
			} else if (getEnvStatus == JNI_OK) {
				LOGI("GetEnv() successful");
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
				LOGI("Android API version - NDK: %d, JNI: %d", __ANDROID_API__, sdkVersion_);
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
		AndroidJniClass_InputDevice::init();
		AndroidJniClass_KeyCharacterMap::init();
		AndroidJniClass_KeyEvent::init();
		AndroidJniClass_Display::init();
		AndroidJniClass_DisplayMode::init();
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
		if (javaClass == nullptr) {
			LOGE("Cannot find Java class \"%s\"", name);
		}
		return javaClass;
	}

	jmethodID AndroidJniClass::getStaticMethodID(jclass javaClass, const char* name, const char* signature)
	{
		jmethodID mid = nullptr;

		ASSERT(name != nullptr && signature != nullptr);
		if (javaClass != nullptr) {
			mid = AndroidJniHelper::jniEnv->GetStaticMethodID(javaClass, name, signature);
			if (mid == nullptr) {
				LOGE("Cannot get static method \"%s()\" with signature \"%s\"", name, signature);
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
			if (mid == nullptr) {
				LOGE("Cannot get method \"%s()\" with signature \"%s\"", name, signature);
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
			if (fid == nullptr) {
				LOGE("Cannot get static field \"%s\" with signature \"%s\"", name, signature);
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
			return { };
		}
		return String(buffer);
	}
	
	String AndroidJniClass_Version::deviceManufacturer()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.product.manufacturer", buffer);
		if (length <= 0) {
			return { };
		}
		return String(buffer);
	}
	
	String AndroidJniClass_Version::deviceModel()
	{
		char buffer[PROP_VALUE_MAX];
		int length = __system_property_get("ro.product.model", buffer);
		if (length <= 0) {
			return { };
		}
		return String(buffer);
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
	}

	AndroidJniClass_InputDevice AndroidJniClass_InputDevice::getDevice(int deviceId)
	{
		jobject inputDeviceObject = AndroidJniHelper::jniEnv->CallStaticObjectMethod(javaClass_, midGetDevice_, deviceId);
		return AndroidJniClass_InputDevice(inputDeviceObject);
	}

	int AndroidJniClass_InputDevice::getDeviceIds(int* destination, int maxSize)
	{
		jintArray arrDeviceIds = static_cast<jintArray>(AndroidJniHelper::jniEnv->CallStaticObjectMethod(javaClass_, midGetDeviceIds_));
		const jint length = AndroidJniHelper::jniEnv->GetArrayLength(arrDeviceIds);

		jint* intsDeviceIds = AndroidJniHelper::jniEnv->GetIntArrayElements(arrDeviceIds, nullptr);
		for (int i = 0; i < length && i < maxSize; i++) {
			destination[i] = int(intsDeviceIds[i]);
		}
		AndroidJniHelper::jniEnv->ReleaseIntArrayElements(arrDeviceIds, intsDeviceIds, 0);
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrDeviceIds);

		return int(length);
	}

	int AndroidJniClass_InputDevice::getName(char* destination, int maxStringSize) const
	{
		jstring strDeviceName = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetName_));
		const jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strDeviceName);

		if (strDeviceName) {
			const char* deviceName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDeviceName, 0);
			copyStringFirst(destination, maxStringSize, deviceName);
			destination[maxStringSize - 1] = '\0';
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDeviceName, deviceName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDeviceName);
		} else {
			copyStringFirst(destination, maxStringSize, static_cast<const char*>("Unknown"));
		}
		return (int(length) < maxStringSize) ? int(length) : maxStringSize;
	}

	int AndroidJniClass_InputDevice::getDescriptor(char* destination, int maxStringSize) const
	{
		jstring strDeviceDescriptor = static_cast<jstring>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetDescriptor_));
		const jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strDeviceDescriptor);

		if (strDeviceDescriptor) {
			const char* deviceName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDeviceDescriptor, 0);
			copyStringFirst(destination, maxStringSize, deviceName);
			destination[maxStringSize - 1] = '\0';
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDeviceDescriptor, deviceName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDeviceDescriptor);
		} else if (maxStringSize > 0) {
			destination[0] = '\0';
		}
		return (int(length) < maxStringSize) ? int(length) : maxStringSize;
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

	// ------------------- AndroidJniClass_MotionRange -------------------

	AndroidJniClass_MotionRange::AndroidJniClass_MotionRange(jobject javaObject)
		: AndroidJniClass(javaObject)
	{
		javaClass_ = findClass("android/view/InputDevice$MotionRange");
		midGetMin_ = getMethodID(javaClass_, "getMin", "()F");
		midGetRange_ = getMethodID(javaClass_, "getRange", "()F");
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

	// ------------------- AndroidJniClass_KeyEvent -------------------

	void AndroidJniClass_KeyEvent::init()
	{
		javaClass_ = findClass("android/view/KeyEvent");
		midConstructor_ = getMethodID(javaClass_, "<init>", "(II)V");
		midGetUnicodeCharMetaState_ = getMethodID(javaClass_, "getUnicodeChar", "(I)I");
		midGetUnicodeChar_ = getMethodID(javaClass_, "getUnicodeChar", "()I");
		midIsPrintingKey_ = getMethodID(javaClass_, "isPrintingKey", "()Z");
	}

	AndroidJniClass_KeyEvent::AndroidJniClass_KeyEvent(int action, int code)
	{
		jobject javaObject = AndroidJniHelper::jniEnv->NewObject(javaClass_, midConstructor_, action, code);
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
		const jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strDisplayName);

		if (strDisplayName) {
			const char* displayName = AndroidJniHelper::jniEnv->GetStringUTFChars(strDisplayName, 0);
			copyStringFirst(destination, maxStringSize, displayName);
			destination[maxStringSize - 1] = '\0';
			AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strDisplayName, displayName);
			AndroidJniHelper::jniEnv->DeleteLocalRef(strDisplayName);
		} else {
			copyStringFirst(destination, maxStringSize, static_cast<const char*>("Unknown"));
		}
		return (int(length) < maxStringSize) ? int(length) : maxStringSize;
	}

	int AndroidJniClass_Display::getSupportedModes(AndroidJniClass_DisplayMode* destination, int maxSize) const
	{
		jobjectArray arrModes = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(javaObject_, midGetSupportedModes_));
		const jint length = AndroidJniHelper::jniEnv->GetArrayLength(arrModes);

		for (int i = 0; i < length && i < maxSize; i++) {
			jobject modeObject = AndroidJniHelper::jniEnv->GetObjectArrayElement(arrModes, i);
			AndroidJniClass_DisplayMode mode(modeObject);
			destination[i] = std::move(mode);
			// The AndroidJniClass constructor deleted the local reference to `modeObject`
		}
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrModes);

		return int(length);
	}

	// ------------------- AndroidJniWrap_Activity -------------------

	void AndroidJniWrap_Activity::init(struct android_app* state)
	{
		// Retrieve `NativeActivity`
		activityObject_ = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(activityObject_);

		midFinishAndRemoveTask_ = AndroidJniClass::getMethodID(nativeActivityClass, "finishAndRemoveTask", "()V");
		midGetPreferredLanguage_ = AndroidJniClass::getMethodID(nativeActivityClass, "getPreferredLanguage", "()Ljava/lang/String;");
		midHasExternalStoragePermission_ = AndroidJniClass::getMethodID(nativeActivityClass, "hasExternalStoragePermission", "()Z");
		midRequestExternalStoragePermission_ = AndroidJniClass::getMethodID(nativeActivityClass, "requestExternalStoragePermission", "()V");
		midSetActivityEnabled_ = AndroidJniClass::getMethodID(nativeActivityClass, "setActivityEnabled", "(Ljava/lang/String;Z)V");
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
		if (strLanguage == nullptr) {
			return { };
		}

		const jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strLanguage);
		const char* language = AndroidJniHelper::jniEnv->GetStringUTFChars(strLanguage, 0);
		String result = String(NoInit, length);
		std::memcpy(result.data(), language, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strLanguage, language);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strLanguage);
		return result;
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
	
	// ------------------- AndroidJniWrap_InputMethodManager -------------------

	void AndroidJniWrap_InputMethodManager::init(struct android_app* state)
	{
		// Retrieve `NativeActivity`
		jobject nativeActivityObject = state->activity->clazz;
		jclass nativeActivityClass = AndroidJniHelper::jniEnv->GetObjectClass(nativeActivityObject);

		// Retrieve `Context.INPUT_METHOD_SERVICE`
		jclass contextClass = AndroidJniClass::findClass("android/content/Context");
		jfieldID fidInputMethodService = AndroidJniClass::getStaticFieldID(contextClass, "INPUT_METHOD_SERVICE", "Ljava/lang/String;");
		jobject inputMethodServiceObject = AndroidJniHelper::jniEnv->GetStaticObjectField(contextClass, fidInputMethodService);

		// Run `getSystemService(Context.INPUT_METHOD_SERVICE)`
		jclass inputMethodManagerClass = AndroidJniClass::findClass("android/view/inputmethod/InputMethodManager");
		jmethodID midGetSystemService = AndroidJniClass::getMethodID(nativeActivityClass, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
		jobject inputMethodManagerObject = AndroidJniHelper::jniEnv->CallObjectMethod(nativeActivityObject, midGetSystemService, inputMethodServiceObject);
		inputMethodManagerObject_ = AndroidJniHelper::jniEnv->NewGlobalRef(inputMethodManagerObject);

		midToggleSoftInput_ = AndroidJniClass::getMethodID(inputMethodManagerClass, "toggleSoftInput", "(II)V");
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

	int AndroidJniWrap_DisplayManager::getNumDisplays()
	{
		jobjectArray arrDisplays = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(displayManagerObject_, midGetDisplays_));
		const jint length = AndroidJniHelper::jniEnv->GetArrayLength(arrDisplays);
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrDisplays);

		return int(length);
	}

	int AndroidJniWrap_DisplayManager::getDisplays(AndroidJniClass_Display* destination, int maxSize)
	{
		jobjectArray arrDisplays = static_cast<jobjectArray>(AndroidJniHelper::jniEnv->CallObjectMethod(displayManagerObject_, midGetDisplays_));
		const jint length = AndroidJniHelper::jniEnv->GetArrayLength(arrDisplays);

		for (int i = 0; i < length && i < maxSize; i++) {
			jobject displayObject = AndroidJniHelper::jniEnv->GetObjectArrayElement(arrDisplays, i);
			AndroidJniClass_Display display(displayObject);
			destination[i] = std::move(display);
			// The AndroidJniClass constructor deleted the local reference to `displayObject`
		}
		AndroidJniHelper::jniEnv->DeleteLocalRef(arrDisplays);

		return int(length);
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
		
		const jsize length = AndroidJniHelper::jniEnv->GetStringUTFLength(strAndroidId);
		const char* androidId = AndroidJniHelper::jniEnv->GetStringUTFChars(strAndroidId, 0);
		androidId_ = String(NoInit, length);
		std::memcpy(androidId_.data(), androidId, length);
		AndroidJniHelper::jniEnv->ReleaseStringUTFChars(strAndroidId, androidId);
		AndroidJniHelper::jniEnv->DeleteLocalRef(strAndroidId);
	}
}
