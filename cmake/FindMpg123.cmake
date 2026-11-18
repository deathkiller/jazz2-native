# - Find libmpg123
# Find the native libmpg123 includes and library
#
#  MPG123_INCLUDE_DIR - where to find mpg123.h
#  MPG123_LIBRARY     - Library to link against
#  MPG123_FOUND       - True if libmpg123 found, `Mpg123::Mpg123` is then defined

if(MPG123_INCLUDE_DIR)
	# Already in cache, be silent
	set(MPG123_FIND_QUIETLY TRUE)
endif()

find_path(MPG123_INCLUDE_DIR mpg123.h
	PATHS
	~/Library/Frameworks
	/Library/Frameworks
	/sw # Fink
	/opt/local # DarwinPorts
	/opt/csw # Blastwave
	/opt
	${EXTERNAL_INCLUDES_DIR}
)

# The bundled Linux library has only the versioned name
find_library(MPG123_LIBRARY NAMES mpg123 libmpg123 libmpg123.so.0
	PATHS
	/sw
	/opt/local
	/opt/csw
	/opt
	${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Mpg123 DEFAULT_MSG MPG123_LIBRARY MPG123_INCLUDE_DIR)

if(MPG123_FOUND AND NOT TARGET Mpg123::Mpg123)
	add_library(Mpg123::Mpg123 UNKNOWN IMPORTED)
	set_target_properties(Mpg123::Mpg123 PROPERTIES
		IMPORTED_LOCATION "${MPG123_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${MPG123_INCLUDE_DIR}")
endif()

mark_as_advanced(MPG123_INCLUDE_DIR MPG123_LIBRARY)