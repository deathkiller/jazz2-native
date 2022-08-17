#!/bin/bash

BuildType=Release
#BuildType=Debug

export OS=emscripten
export CC=emcc

source ../emsdk/emsdk_env.sh

emcmake cmake -B ../Jazz2-Emscripten-${BuildType} -D CMAKE_BUILD_TYPE=${BuildType}
make -j2 -C ../Jazz2-Emscripten-${BuildType}