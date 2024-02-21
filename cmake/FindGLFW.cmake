# Locate the glfw library
# This module defines the following variables:
# GLFW_LIBRARY, the name of the library;
# GLFW_INCLUDE_DIR, where to find glfw include files.
# GLFW_FOUND, true if both the GLFW_LIBRARY and GLFW_INCLUDE_DIR have been found.
#
# To help locate the library and include file, you could define an environment variable called
# GLFW_ROOT which points to the root of the glfw library installation. This is pretty useful
# on a Windows platform.
#
#
# Usage example to compile an "executable" target to the glfw library:
#
# FIND_PACKAGE (glfw REQUIRED)
# INCLUDE_DIRECTORIES (${GLFW_INCLUDE_DIR})
# ADD_EXECUTABLE (executable ${EXECUTABLE_SRCS})
# TARGET_LINK_LIBRARIES (executable ${GLFW_LIBRARY})
#
# TODO:
# Allow the user to select to link to a shared library or to a static library.

#Search for the include file...
find_path(GLFW_INCLUDE_DIR NAMES glfw3.h DOC "Path to GLFW include directory"
  HINTS
  $ENV{GLFW_ROOT}
  PATH_SUFFIXES
  include # For finding the include file under the root of the glfw expanded archive, typically on Windows.
  PATHS
  /usr/include/
  /usr/local/include/
  # By default headers are under GL subfolder
  /include/GLFW/
  /usr/include/GLFW
  /usr/local/include/GLFW
  ${GLFW_ROOT_DIR}/include/ # added by ptr
  ${EXTERNAL_INCLUDES_DIR}/GL/
)
mark_as_advanced(GLFW_INCLUDE_DIR)

find_library(GLFW_LIBRARY DOC "Absolute path to GLFW library"
  NAMES glfw glfw3 glfw3.lib libglfw3
  HINTS
  $ENV{GLFW_ROOT}
  PATH_SUFFIXES lib/win32 #For finding the library file under the root of the glfw expanded archive, typically on Windows.
  PATHS
  /usr/local/lib
  /usr/lib
  ${GLFW_ROOT_DIR}/lib-msvc100/release # added by ptr
  ${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
)
mark_as_advanced(GLFW_LIBRARY)

# handle the QUIETLY and REQUIRED arguments and set WEBFOUND_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLFW DEFAULT_MSG GLFW_LIBRARY GLFW_INCLUDE_DIR)

# On Mac OS X GLFW depends on the Cocoa framework
if(APPLE)
	set(GLFW_LIBRARY "-framework Cocoa -framework IOKit" ${GLFW_LIBRARY})
endif()

set(GLFW_LIBRARIES ${GLFW_LIBRARY})
set(GLFW_INCLUDE_DIRS ${GLFW_INCLUDE_DIR})

