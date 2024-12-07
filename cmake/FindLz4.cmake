include(ncine_helpers)

if(NOT TARGET Lz4)
	if(NCINE_DOWNLOAD_DEPENDENCIES)
		# Try to build `lz4` from source
		set(LZ4_URL "https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz")
		message(STATUS "Downloading dependencies from \"${LZ4_URL}\"...")
		
		include(FetchContent)
		FetchContent_Declare(
			Lz4Git
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${LZ4_URL}
		)
		FetchContent_MakeAvailable(Lz4Git)

		ncine_add_dependency(Lz4 STATIC)

		set(LZ4_DIR "${lz4git_SOURCE_DIR}/lib/")
		set(LZ4_INCLUDE_DIR "${LZ4_DIR}")
		set_target_properties(Lz4 PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${LZ4_INCLUDE_DIR})

		set(LZ4_HEADERS
			${LZ4_INCLUDE_DIR}/lz4.h
			${LZ4_INCLUDE_DIR}/lz4file.h
			${LZ4_INCLUDE_DIR}/lz4frame.h
			${LZ4_INCLUDE_DIR}/lz4hc.h
			${LZ4_INCLUDE_DIR}/xxhash.h
		)

		set(LZ4_SOURCES
			${LZ4_DIR}/lz4.c
			${LZ4_DIR}/lz4file.c
			${LZ4_DIR}/lz4frame.c
			${LZ4_DIR}/lz4hc.c
			${LZ4_DIR}/xxhash.c
		)

		target_sources(Lz4 PRIVATE ${LZ4_SOURCES} ${LZ4_HEADERS})
		target_include_directories(Lz4 PRIVATE ${LZ4_INCLUDE_DIR})

		if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE OR CMAKE_POSITION_INDEPENDENT_CODE)
			set_target_properties(Lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
		endif()

		set(LZ4_FOUND TRUE)
		set(LZ4_STATIC TRUE)
		mark_as_advanced(LZ4_STATIC)
	else()
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(Lz4 REQUIRED IMPORTED_TARGET liblz4)
		if(TARGET PkgConfig::Lz4)
			message(STATUS "Using Lz4 from PkgConfig")
			add_library(Lz4 ALIAS PkgConfig::Lz4)
		endif()
	endif()
endif()