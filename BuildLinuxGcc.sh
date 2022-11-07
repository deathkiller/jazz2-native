#!/usr/bin/env bash

BuildType=Release
#BuildType=Debug

export OS=linux
export CC=gcc
export CXX=g++

cmake -B ../Jazz2-LinuxGcc-${BuildType} -D CMAKE_BUILD_TYPE=${BuildType} -D NCINE_LINKTIME_OPTIMIZATION=ON
make -j8 -C ../Jazz2-LinuxGcc-${BuildType}