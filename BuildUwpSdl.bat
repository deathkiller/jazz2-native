@echo off

if not defined DevEnvDir (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

cmake -G "Visual Studio 17 2022" -A x64 -B ".\.fake\_uwp" -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION="10.0" -DCMAKE_SYSTEM_PROCESSOR=x64 -DNCINE_UWP_CERTIFICATE_PASSWORD="{19D890AD-5353-4B25-A85B-34D9A713B6AC}" -DNCINE_DOWNLOAD_DEPENDENCIES=OFF
if %ERRORLEVEL% GEQ 1 EXIT /B 1
cmake --open ".\.fake\_uwp\"
