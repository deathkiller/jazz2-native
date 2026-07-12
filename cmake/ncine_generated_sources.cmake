# Has to be included after ncine_get_version.cmake

set(GENERATED_SOURCE_DIR "${CMAKE_BINARY_DIR}/Generated")
set(GENERATED_INCLUDE_DIR "${GENERATED_SOURCE_DIR}")

# Shader sources are preprocessed offline by ShaderCompiler into committed headers under
# "Sources/Shaders/Generated/" (see Sources/ShaderCompiler/GenerateAll.ps1), so no shader
# embedding or file installation happens at build time anymore

if(WIN32)
	if(DEDICATED_SERVER)
		if(EXISTS "${NCINE_SOURCE_DIR}/Icons/WindowImGui.ico")
			message(STATUS "Writing a resource file for executable icons")

			set(RESOURCE_RC_FILE "${GENERATED_SOURCE_DIR}/Resources.rc")
			set(RESOURCE_RC_CONTENT "GLFW_ICON ICON \"Window.ico\"")
			file(COPY "${NCINE_SOURCE_DIR}/Icons/Window.ico" DESTINATION ${GENERATED_INCLUDE_DIR})
			
			file(WRITE ${RESOURCE_RC_FILE} ${RESOURCE_RC_CONTENT})
			list(APPEND GENERATED_SOURCES ${RESOURCE_RC_FILE})
		endif()
	else()
		if(EXISTS "${NCINE_SOURCE_DIR}/Icons/Main.ico")
			message(STATUS "Writing a resource file for executable icons")

			set(RESOURCE_RC_FILE "${GENERATED_SOURCE_DIR}/Resources.rc")
			set(RESOURCE_RC_CONTENT "GLFW_ICON ICON \"Main.ico\"")
			file(COPY "${NCINE_SOURCE_DIR}/Icons/Main.ico" DESTINATION ${GENERATED_INCLUDE_DIR})
			if(DEATH_TRACE AND NOT (WINDOWS_PHONE OR WINDOWS_STORE) AND EXISTS "${NCINE_SOURCE_DIR}/Icons/Window.ico")
				set(RESOURCE_RC_CONTENT "${RESOURCE_RC_CONTENT}\nWINDOW_ICON ICON \"Window.ico\"")
				file(COPY "${NCINE_SOURCE_DIR}/Icons/Window.ico" DESTINATION ${GENERATED_INCLUDE_DIR})
			endif()
			if(NCINE_WITH_IMGUI AND EXISTS "${NCINE_SOURCE_DIR}/Icons/WindowImGui.ico")
				set(RESOURCE_RC_CONTENT "${RESOURCE_RC_CONTENT}\nIMGUI_ICON ICON \"WindowImGui.ico\"")
				file(COPY "${NCINE_SOURCE_DIR}/Icons/WindowImGui.ico" DESTINATION ${GENERATED_INCLUDE_DIR})
			endif()
			file(WRITE ${RESOURCE_RC_FILE} ${RESOURCE_RC_CONTENT})
			list(APPEND GENERATED_SOURCES ${RESOURCE_RC_FILE})
		endif()
	endif()

	message(STATUS "Writing a version info resource file")
	set(PACKAGE_VERSION_PATCH ${NCINE_VERSION_PATCH})
	if(NCINE_VERSION_FROM_GIT AND GIT_NO_TAG)
		if(DEFINED NCINE_VERSION_PATCH_LAST)
			set(PACKAGE_VERSION_PATCH ${NCINE_VERSION_PATCH_LAST})
		else()
			set(PACKAGE_VERSION_PATCH "0")
		endif()
	endif()
	set(PACKAGE_VERSION_REV "0")
	if(NOT "${GIT_REV_COUNT}" STREQUAL "")
		set(PACKAGE_VERSION_REV ${GIT_REV_COUNT})
	endif()

	set(PACKAGE_EXECUTABLE_NAME "Jazz2")
	if(TARGET ${NCINE_APP}) # Disabled if NCINE_BUILD_ANDROID
		get_target_property(DEATH_DEBUG_POSTFIX ${NCINE_APP} DEBUG_POSTFIX)
	endif()
	if(NOT DEATH_DEBUG_POSTFIX)
		set(DEATH_DEBUG_POSTFIX "d")
	endif()
	set(PACKAGE_EXTENSION ".exe")
	if(WINDOWS_PHONE OR WINDOWS_STORE)
		set(PACKAGE_EXTENSION ".msixbundle")
	endif()

	set(VERSION_RC_FILE "${GENERATED_SOURCE_DIR}/Version.rc")
	configure_file("${NCINE_SOURCE_DIR}/Resources.rc.in" ${VERSION_RC_FILE} @ONLY)
	list(APPEND GENERATED_SOURCES ${VERSION_RC_FILE})

	list(APPEND GENERATED_SOURCES "${NCINE_SOURCE_DIR}/App.manifest")
endif()

if(IS_DIRECTORY ${GENERATED_INCLUDE_DIR} AND TARGET ${NCINE_APP}) # Disabled if NCINE_BUILD_ANDROID
	get_filename_component(PARENT_GENERATED_INCLUDE_DIR ${GENERATED_INCLUDE_DIR} DIRECTORY)
	target_include_directories(${NCINE_APP}
		INTERFACE $<BUILD_INTERFACE:${PARENT_GENERATED_INCLUDE_DIR}>
		# Internal sources can access a generated header with or without the parent directory
		PRIVATE $<BUILD_INTERFACE:${PARENT_GENERATED_INCLUDE_DIR}>
		PRIVATE $<BUILD_INTERFACE:${GENERATED_INCLUDE_DIR}>)
endif()