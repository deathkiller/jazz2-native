@echo off

if not defined DevEnvDir (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

cmake -D CMAKE_BUILD_TYPE=Release -G "Visual Studio 17 2022" -A x64 -B ".fake\_imgui-docking" -DCMAKE_SYSTEM_PROCESSOR=x64 -D NCINE_PREFERRED_BACKEND=GLFW -D NCINE_COPY_DEPENDENCIES=ON -D NCINE_WITH_IMGUI=ON -D NCINE_PROFILING=ON -D IMGUI_VERSION_TAG="docking" -D WITH_MULTIPLAYER=ON -D NCINE_WITH_ANGELSCRIPT=ON -D NCINE_ARCH_EXTENSIONS=AVX2 -D CMAKE_GENERATOR_TOOLSET=v143
if %ERRORLEVEL% GEQ 1 EXIT /B 1
cmake --open ".fake\_imgui-docking"