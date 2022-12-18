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
	PATH_SUFFIXES
	lib64
	lib
	libx32
	lib/x64
	x86_64-w64-mingw32/lib
	PATHS
	/sw
	/opt/local
	/opt/csw
	/opt
	${NCINE_LIBS}/Linux/${CMAKE_SYSTEM_PROCESSOR}/
)
mark_as_advanced(LIBOPENMPT_LIBRARY)

if(NOT TARGET libopenmpt::libopenmpt)
	if(LIBOPENMPT_INCLUDE_DIR AND LIBOPENMPT_LIBRARY)
		add_library(libopenmpt::libopenmpt SHARED IMPORTED)
		set_target_properties(libopenmpt::libopenmpt PROPERTIES
			IMPORTED_LOCATION ${LIBOPENMPT_LIBRARY}
			INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})
	elseif(NCINE_DOWNLOAD_DEPENDENCIES)
		# Build `libopenmpt` from source
		set(LIBOPENMPT_URL "https://github.com/OpenMPT/openmpt/archive/libopenmpt-0.6.6.tar.gz")
		message(STATUS "Downloading dependencies from \"${LIBOPENMPT_URL}\"...")
		
		include(FetchContent)
		FetchContent_Declare(
			libopenmpt_git
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${LIBOPENMPT_URL}
		)
		FetchContent_MakeAvailable(libopenmpt_git)
		
		add_library(libopenmpt_src STATIC)
		target_compile_features(libopenmpt_src PUBLIC cxx_std_20)
		set_target_properties(libopenmpt_src PROPERTIES CXX_EXTENSIONS OFF)
		set(LIBOPENMPT_INCLUDE_DIR "${libopenmpt_git_SOURCE_DIR}/libopenmpt/")
		set_target_properties(libopenmpt_src PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${LIBOPENMPT_INCLUDE_DIR})
		target_compile_definitions(libopenmpt_src PRIVATE "LIBOPENMPT_BUILD")
		
		set(LIBOPENMPT_SOURCES)
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/common/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/sounddsp/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		file(GLOB_RECURSE _files "${libopenmpt_git_SOURCE_DIR}/soundlib/*.cpp")
		list(APPEND LIBOPENMPT_SOURCES ${_files})
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_c.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_cxx.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_impl.cpp")
		list(APPEND LIBOPENMPT_SOURCES "${libopenmpt_git_SOURCE_DIR}/libopenmpt/libopenmpt_ext_impl.cpp")
		target_sources(libopenmpt_src PRIVATE ${LIBOPENMPT_SOURCES})
		target_include_directories(libopenmpt_src PRIVATE "${libopenmpt_git_SOURCE_DIR}" "${libopenmpt_git_SOURCE_DIR}/common" "${libopenmpt_git_SOURCE_DIR}/src")
		file(WRITE "${libopenmpt_git_SOURCE_DIR}/common/svn_version.h" "")

		add_library(libopenmpt::libopenmpt ALIAS libopenmpt_src)
		set(LIBOPENMPT_STATIC TRUE)
		mark_as_advanced(LIBOPENMPT_STATIC)
	endif()
endif()

if(NOT LIBOPENMPT_STATIC)
	include(${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake)
	find_package_handle_standard_args(libopenmpt REQUIRED_VARS LIBOPENMPT_LIBRARY LIBOPENMPT_INCLUDE_DIR)
endif()