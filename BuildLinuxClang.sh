#!/usr/bin/env bash

BuildType=Release
#BuildType=Debug

export OS=linux
export CC=clang
export CXX=clang++

cmake -B ../Jazz2-LinuxClang-${BuildType} -D CMAKE_BUILD_TYPE=${BuildType} -D CMAKE_PREFIX_PATH=$(pwd)/../nCine-external
make -j2 -C ../Jazz2-LinuxClang-${BuildType}