@echo off

if not defined DevEnvDir (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

cmake -G "Visual Studio 16 2019" -A x64 -B "..\Jazz2-Uwp" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DCMAKE_SYSTEM_PROCESSOR=x64
cmake --open "..\Jazz2-Uwp"