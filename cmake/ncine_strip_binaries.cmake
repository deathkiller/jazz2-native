# `NCINE_DEBUG_SYMBOLS_FILE` means the debug symbols were split into a separate file instead of being thrown
# away (see "ncine_compiler_options.cmake"), which is mutually exclusive with stripping here: the executable
# is already stripped in place by the split step and carries a ".gnu_debuglink" section pointing at that file,
# which `strip --strip-all` would silently remove again, and stripping the static libraries would drop their
# debug info before the executable even links them, leaving every dependency frame unnamed in a crash report.
if(NCINE_STRIP_BINARIES AND NCINE_DEBUG_SYMBOLS_FILE)
	message(STATUS "Not stripping any binaries, the debug symbols are split into a separate file instead")
endif()

if(NCINE_STRIP_BINARIES AND NOT NCINE_DEBUG_SYMBOLS_FILE AND CMAKE_BUILD_TYPE MATCHES "Release" AND NOT EMSCRIPTEN AND EXISTS ${CMAKE_STRIP})
	message(STATUS "Strip command: " ${CMAKE_STRIP})

	get_directory_property(NCINE_BUILD_TARGETS ${CMAKE_BUILD_DIR} BUILDSYSTEM_TARGETS)
	foreach(BUILD_TARGET ${NCINE_BUILD_TARGETS})
		get_target_property(BUILD_TARGET_TYPE ${BUILD_TARGET} TYPE)
		if(BUILD_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
			set(STRIP_MESSAGE "Stripping debug symbols from static library")
			set(STRIP_ARGUMENTS "--strip-debug")
			if(APPLE)
				set(STRIP_ARGUMENTS "-S")
			endif()
		elseif(BUILD_TARGET_TYPE STREQUAL "SHARED_LIBRARY")
			set(STRIP_MESSAGE "Stripping unneeded symbols from shared library")
			set(STRIP_ARGUMENTS "--strip-unneeded")
			if(APPLE)
				set(STRIP_ARGUMENTS "-urS")
			endif()

		elseif(BUILD_TARGET_TYPE STREQUAL "EXECUTABLE")
			set(STRIP_MESSAGE "Stripping all symbols from executable")
			set(STRIP_ARGUMENTS "--strip-all")
			if(APPLE)
				set(STRIP_ARGUMENTS "-Sx")
			endif()
		endif()

		if(BUILD_TARGET_TYPE STREQUAL "STATIC_LIBRARY" OR BUILD_TARGET_TYPE STREQUAL "SHARED_LIBRARY" OR
		   BUILD_TARGET_TYPE STREQUAL "EXECUTABLE")
			add_custom_command(TARGET ${BUILD_TARGET} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E echo "${STRIP_MESSAGE} ${BUILD_TARGET}"
				COMMAND ${CMAKE_STRIP} ${STRIP_ARGUMENTS} $<TARGET_FILE:${BUILD_TARGET}>)
		endif()
	endforeach()
endif()
