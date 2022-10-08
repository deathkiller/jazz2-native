#!/usr/bin/env bash

BuildType=Release
#BuildType=Debug

export ANDROID_HOME="$(pwd)/../android-sdk"
export GRADLE_HOME="$(pwd)/../gradle"

# Generate keystore
#keytool -genkey -v -keystore Keystore.jks -keyalg RSA -keysize 2048 -validity 18250 -alias Main

echo 'Aligning APK file...'
$ANDROID_HOME/build-tools/33.0.0/zipalign -p 4 ../Jazz2-Android-${BuildType}/android/app/build/outputs/apk/release/app-universal-release-unsigned.apk ../Jazz2-Android-${BuildType}/app-universal-release.apk

echo 'Signing APK file...'
$ANDROID_HOME/build-tools/33.0.0/apksigner sign --ks-key-alias Main --ks Keystore.jks ../Jazz2-Android-${BuildType}/app-universal-release.apk

echo 'Aligning APK file (x86-64)...'
$ANDROID_HOME/build-tools/33.0.0/zipalign -p 4 ../Jazz2-Android-${BuildType}_x86-64/android/app/build/outputs/apk/release/app-x86_64-release-unsigned.apk ../Jazz2-Android-${BuildType}_x86-64/app-x86_64-release.apk

echo 'Signing APK file (x86-64)...'
$ANDROID_HOME/build-tools/33.0.0/apksigner sign --ks-key-alias Main --ks Keystore.jks ../Jazz2-Android-${BuildType}_x86-64/app-x86_64-release.apk
