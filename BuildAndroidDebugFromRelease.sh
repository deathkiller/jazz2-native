#!/usr/bin/env bash

export ANDROID_HOME="$(pwd)/../android-sdk"
export GRADLE_HOME="$(pwd)/../gradle"

cd ../Jazz2-Android-Release/android/
../../gradle/bin/gradle assembleDebug