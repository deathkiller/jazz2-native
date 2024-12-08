include(ncine_helpers)

if(NOT TARGET Zstd)
	if(NCINE_DOWNLOAD_DEPENDENCIES)
		# Try to build `zstd` from source
		set(ZSTD_URL "https://github.com/facebook/zstd/archive/refs/tags/v1.5.6.tar.gz")
		message(STATUS "Downloading dependencies from \"${ZSTD_URL}\"...")
		
		include(FetchContent)
		FetchContent_Declare(
			ZstdGit
			DOWNLOAD_EXTRACT_TIMESTAMP TRUE
			URL ${ZSTD_URL}
		)
		FetchContent_MakeAvailable(ZstdGit)

		ncine_add_dependency(Zstd STATIC)

		set(ZSTD_DIR "${zstdgit_SOURCE_DIR}/lib")
		set(ZSTD_INCLUDE_DIR "${ZSTD_DIR}")
		set_target_properties(Zstd PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES ${ZSTD_INCLUDE_DIR})
		if(MSVC)
			target_compile_definitions(Zstd PRIVATE "_CRT_SECURE_NO_WARNINGS" "ZSTD_HEAPMODE=0")
		endif()
		
		file(GLOB CommonSources "${ZSTD_DIR}/common/*.c")
		file(GLOB CompressSources "${ZSTD_DIR}/compress/*.c")
		file(GLOB DecompressSources "${ZSTD_DIR}/decompress/*.c")
		if (MSVC)
			target_compile_definitions(Zstd PRIVATE "ZSTD_DISABLE_ASM")
		else()
			if(CMAKE_SYSTEM_PROCESSOR MATCHES "amd64.*|AMD64.*|x86_64.*|X86_64.*")
				set(DecompressSources ${DecompressSources} "${ZSTD_DIR}/decompress/huf_decompress_amd64.S")
			else()
				target_compile_definitions(Zstd PRIVATE "ZSTD_DISABLE_ASM")
			endif()
		endif()
		file(GLOB DictBuilderSources "${ZSTD_DIR}/dictBuilder/*.c")
		file(GLOB DeprecatedSources "${ZSTD_DIR}/deprecated/*.c")

		file(GLOB PublicHeaders "${ZSTD_DIR}/*.h")
		file(GLOB CommonHeaders "${ZSTD_DIR}/common/*.h")
		file(GLOB CompressHeaders "${ZSTD_DIR}/compress/*.h")
		file(GLOB DecompressHeaders "${ZSTD_DIR}/decompress/*.h")
		file(GLOB DictBuilderHeaders "${ZSTD_DIR}/dictBuilder/*.h")
		file(GLOB DeprecatedHeaders "${ZSTD_DIR}/deprecated/*.h")

		set(ZSTD_SOURCES ${CommonSources} ${CompressSources} ${DecompressSources})
		set(ZSTD_HEADERS ${PublicHeaders} ${CommonHeaders} ${CompressHeaders} ${DecompressHeaders})

		#if (ZSTD_BUILD_DICTBUILDER)
		#	set(ZSTD_SOURCES ${ZSTD_SOURCES} ${DictBuilderSources})
		#	set(ZSTD_HEADERS ${ZSTD_HEADERS} ${DictBuilderHeaders})
		#endif()
		#if (ZSTD_BUILD_DEPRECATED)
		#	set(ZSTD_SOURCES ${ZSTD_SOURCES} ${DeprecatedSources})
		#	set(ZSTD_HEADERS ${ZSTD_HEADERS} ${DeprecatedHeaders})
		#endif()
		
		if(NOT CMAKE_ASM_COMPILER STREQUAL CMAKE_C_COMPILER)
			set_source_files_properties(${ZSTD_SOURCES} PROPERTIES LANGUAGE C)
		endif()

		ncine_assign_source_group(PATH_PREFIX ${ZSTD_DIR} FILES ${ZSTD_HEADERS} ${ZSTD_SOURCES} SKIP_EXTERNAL)
		target_sources(Zstd PRIVATE ${ZSTD_SOURCES} ${ZSTD_HEADERS})
		target_include_directories(Zstd PRIVATE ${ZSTD_INCLUDE_DIR})

		set(ZSTD_FOUND TRUE)
		set(ZSTD_STATIC TRUE)
		mark_as_advanced(ZSTD_STATIC)
	else()
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(Zstd REQUIRED IMPORTED_TARGET libzstd)
		if(TARGET PkgConfig::Zstd)
			message(STATUS "Using Zstd from PkgConfig")
			add_library(Zstd ALIAS PkgConfig::Zstd)
		endif()
	endif()
endif()