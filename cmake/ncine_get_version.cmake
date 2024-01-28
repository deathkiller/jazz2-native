if(NCINE_VERSION_FROM_GIT)
	if(GIT_EXECUTABLE AND EXISTS ${GIT_EXECUTABLE})
		message(STATUS "Specified custom GIT path: ${GIT_EXECUTABLE}")
		set(GIT_FOUND 1)
	else()
		find_package(Git)
	endif()

	if(GIT_EXECUTABLE AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/.git)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE GIT_FAIL
			OUTPUT_VARIABLE GIT_REV_COUNT
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		execute_process(
			COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE GIT_FAIL
			OUTPUT_VARIABLE GIT_SHORT_HASH
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		execute_process(
			COMMAND ${GIT_EXECUTABLE} log -1 --format=%ad --date=short
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE GIT_FAIL
			OUTPUT_VARIABLE GIT_LAST_COMMIT_DATE
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		#execute_process(
		#	COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
		#	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		#	RESULT_VARIABLE GIT_FAIL
		#	OUTPUT_VARIABLE GIT_BRANCH_NAME
		#	ERROR_QUIET
		#	OUTPUT_STRIP_TRAILING_WHITESPACE
		#)

		execute_process(
			COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE GIT_NO_LAST_TAG
			OUTPUT_VARIABLE GIT_LAST_TAG_NAME
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		execute_process(
			COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE GIT_NO_TAG
			OUTPUT_VARIABLE GIT_TAG_NAME
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
	
		if(GIT_FAIL)
			message(STATUS "GIT failed with error ${GIT_FAIL}, cannot get current game version")
			set(GIT_NO_VERSION TRUE)
		else()
			if(GIT_NO_TAG)
				if(GIT_NO_LAST_TAG)
					# Fallback to last commit date if no tag found
					string(REPLACE "-" ";" GIT_LAST_COMMIT_DATE_LIST ${GIT_LAST_COMMIT_DATE})
					list(GET GIT_LAST_COMMIT_DATE_LIST 0 NCINE_VERSION_MAJOR)
					list(GET GIT_LAST_COMMIT_DATE_LIST 1 NCINE_VERSION_MINOR)
					set(NCINE_VERSION_PATCH "r${GIT_REV_COUNT}-${GIT_SHORT_HASH}")
				else()
					string(REGEX REPLACE "^v" "" GIT_LAST_TAG_NAME ${GIT_LAST_TAG_NAME})
					string(REPLACE "." ";" GIT_TAG_NAME_LIST ${GIT_LAST_TAG_NAME})
					list(LENGTH GIT_TAG_NAME_LIST GIT_TAG_NAME_LIST_LENGTH)
					if(GIT_TAG_NAME_LIST_LENGTH GREATER 0)
						# Use the last commit revision and hash for nightly builds,
						# but use NCINE_VERSION_PATCH_LAST for internal version numbers
						list(GET GIT_TAG_NAME_LIST 0 NCINE_VERSION_MAJOR)
						list(GET GIT_TAG_NAME_LIST 1 NCINE_VERSION_MINOR)
						set(NCINE_VERSION_PATCH "r${GIT_REV_COUNT}-${GIT_SHORT_HASH}")

						set(NCINE_VERSION_PATCH_LAST 0)
						if(GIT_TAG_NAME_LIST_LENGTH GREATER 2)
							list(GET GIT_TAG_NAME_LIST 2 NCINE_VERSION_PATCH_LAST)
						endif()
					else()
						# Fallback to last commit date if the last tag is not in valid format
						string(REPLACE "-" ";" GIT_LAST_COMMIT_DATE_LIST ${GIT_LAST_COMMIT_DATE})
						list(GET GIT_LAST_COMMIT_DATE_LIST 0 NCINE_VERSION_MAJOR)
						list(GET GIT_LAST_COMMIT_DATE_LIST 1 NCINE_VERSION_MINOR)
						set(NCINE_VERSION_PATCH "r${GIT_REV_COUNT}-${GIT_SHORT_HASH}")
					endif()
				endif()
			else()
				# The last commit is tagged, use it as version
				string(REGEX REPLACE "^v" "" GIT_TAG_NAME ${GIT_TAG_NAME})
				string(REPLACE "." ";" GIT_TAG_NAME_LIST ${GIT_TAG_NAME})
				list(LENGTH GIT_TAG_NAME_LIST GIT_TAG_NAME_LIST_LENGTH)
				if(GIT_TAG_NAME_LIST_LENGTH GREATER 0)
					list(GET GIT_TAG_NAME_LIST 0 NCINE_VERSION_MAJOR)
					list(GET GIT_TAG_NAME_LIST 1 NCINE_VERSION_MINOR)
					set(NCINE_VERSION_PATCH 0)
					if(GIT_TAG_NAME_LIST_LENGTH GREATER 2)
						list(GET GIT_TAG_NAME_LIST 2 NCINE_VERSION_PATCH)
					endif()
				endif()
			endif()
		endif()
	else()
		message(STATUS "GIT repository not found, cannot get current game version")
		set(GIT_NO_VERSION TRUE)
	endif()
else()
	set(GIT_NO_VERSION TRUE)
endif()

if(GIT_NO_VERSION)
	set(NCINE_VERSION_MAJOR "0")
	set(NCINE_VERSION_MINOR "0")
	set(NCINE_VERSION_PATCH "0")

	string(REPLACE "." ";" _versionComponents ${NCINE_VERSION})
	list(LENGTH _versionComponents _versionComponentsLength)
	if (${_versionComponentsLength} GREATER 0)
		list(GET _versionComponents 0 NCINE_VERSION_MAJOR)
	endif()
	if (${_versionComponentsLength} GREATER 1)
		list(GET _versionComponents 1 NCINE_VERSION_MINOR)
	endif()
	if (${_versionComponentsLength} GREATER 2)
		list(GET _versionComponents 2 NCINE_VERSION_PATCH)
	endif()
elseif(GIT_NO_TAG)
	set(NCINE_VERSION "${NCINE_VERSION_MAJOR}.${NCINE_VERSION_MINOR}.${NCINE_VERSION_PATCH}")
else()
	set(NCINE_VERSION "${GIT_TAG_NAME}")
endif()
message(STATUS "Game version: ${NCINE_VERSION}")

mark_as_advanced(NCINE_VERSION_MAJOR NCINE_VERSION_MINOR NCINE_VERSION_PATCH NCINE_VERSION_PATCH_LAST NCINE_VERSION)
