find_path(LIBOPENMPT_INCLUDE_DIR DOC "Path to libopenmpt include directory"
  NAMES libopenmpt.h
  PATHS
  /usr/include/
  /usr/local/include/
  /usr/include/libopenmpt/
  /usr/local/include/libopenmpt/
  ${EXTERNAL_INCLUDES_DIR}/libopenmpt/
)
mark_as_advanced(LIBOPENMPT_INCLUDE_DIR)

find_library(LIBOPENMPT_LIBRARY DOC "Path to libopenmpt library"
  NAMES libopenmpt openmpt
  NAMES_PER_DIR
  PATH_SUFFIXES lib64 lib libx32 lib/x64 x86_64-w64-mingw32/lib
  PATHS /sw /opt/local /opt/csw /opt ${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
)
mark_as_advanced(LIBOPENMPT_LIBRARY)

# handle the QUIETLY and REQUIRED arguments and set WEBFOUND_FOUND to TRUE if
# all listed variables are TRUE
include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(libopenmpt REQUIRED_VARS LIBOPENMPT_LIBRARY LIBOPENMPT_INCLUDE_DIR)

if(NOT TARGET libopenmpt::libopenmpt)
	add_library(libopenmpt::libopenmpt SHARED IMPORTED)
	set_target_properties(libopenmpt::libopenmpt PROPERTIES
		IMPORTED_LOCATION ${LIBOPENMPT_LIBRARY}
		INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})
endif()