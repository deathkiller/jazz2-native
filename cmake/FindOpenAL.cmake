# Modified version of https://gitlab.kitware.com/cmake/cmake/-/blob/master/Modules/FindOpenAL.cmake
# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

find_path(OPENAL_INCLUDE_DIR al.h
  HINTS
    ENV OPENALDIR
  PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /opt
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\Creative\ Labs\\OpenAL\ 1.1\ Software\ Development\ Kit\\1.00.0000;InstallDir]
    ${EXTERNAL_INCLUDES_DIR}/AL/
  PATH_SUFFIXES include/AL include/OpenAL include AL OpenAL
  )

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_OpenAL_ARCH_DIR libs/Win64)
else()
  set(_OpenAL_ARCH_DIR libs/Win32)
endif()

find_library(OPENAL_LIBRARY
  NAMES OpenAL al openal OpenAL32
  HINTS
    ENV OPENALDIR
  PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /opt
    [HKEY_LOCAL_MACHINE\\SOFTWARE\\Creative\ Labs\\OpenAL\ 1.1\ Software\ Development\ Kit\\1.00.0000;InstallDir]
    ${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
  PATH_SUFFIXES libx32 lib64 lib libs64 libs ${_OpenAL_ARCH_DIR}
  )

unset(_OpenAL_ARCH_DIR)

find_package_handle_standard_args(
  OpenAL
  REQUIRED_VARS OPENAL_LIBRARY OPENAL_INCLUDE_DIR
  VERSION_VAR OPENAL_VERSION_STRING
  )

mark_as_advanced(OPENAL_LIBRARY OPENAL_INCLUDE_DIR)

if(OPENAL_INCLUDE_DIR AND OPENAL_LIBRARY)
  if(NOT TARGET OpenAL::OpenAL)
    if(EXISTS "${OPENAL_LIBRARY}")
      add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
      set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_LOCATION "${OPENAL_LIBRARY}")
    else()
      add_library(OpenAL::OpenAL INTERFACE IMPORTED)
      set_target_properties(OpenAL::OpenAL PROPERTIES
        IMPORTED_LIBNAME "${OPENAL_LIBRARY}")
    endif()
    set_target_properties(OpenAL::OpenAL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
  endif()
endif()