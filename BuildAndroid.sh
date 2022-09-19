#!/usr/bin/env bash

BuildType=Release
#BuildType=Debug

cmake -B ../Jazz2-Android-${BuildType} -D CMAKE_BUILD_TYPE=${BuildType} -D CMAKE_PREFIX_PATH=$(pwd)/../nCine-android-external -D NCINE_BUILD_ANDROID=ON
make -j2 -C ../Jazz2-Android-${BuildType}