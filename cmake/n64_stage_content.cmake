# Stages the content tree that goes into a Nintendo 64 ROM image.
#
# A tree prepared with `AssetPacker convert --target=n64` is already exactly what the console plays: sound effects
# as ".wav64" beside the package, music as ".xm64"/".wav64" and the cinematics as MPEG-1 video with a ".wav64"
# soundtrack (see Sources/Utilities/AssetPacker/N64Content.cpp). Any other console tree still boots, so the files
# the console cannot play at all - original music formats, which it has no decoder for, and the original cinematic
# where the video replaces it - are left out here rather than taking cartridge space for nothing. So is anything a
# failed conversion left behind (its ".n64-temp" scratch directory).
#
# Invoked from the packaging step in ncine_extra_sources.cmake as:
#   cmake -DSOURCE=<content tree> -DDESTINATION=<staging dir> -P cmake/n64_stage_content.cmake

if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION)
	message(FATAL_ERROR "SOURCE and DESTINATION have to be defined")
endif()

# Rebuilt from scratch: copy_directory keeps destination files that are no longer in the source
file(REMOVE_RECURSE "${DESTINATION}")
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN ".n64-temp" EXCLUDE)

file(GLOB _n64UnplayableMusic LIST_DIRECTORIES false
	"${DESTINATION}/Music/*.j2b" "${DESTINATION}/Music/*.it" "${DESTINATION}/Music/*.s3m" "${DESTINATION}/Music/*.mod"
	"${DESTINATION}/Music/*.mo3" "${DESTINATION}/Music/*.xm" "${DESTINATION}/Music/*.ogg" "${DESTINATION}/Music/*.umx")
if(_n64UnplayableMusic)
	list(LENGTH _n64UnplayableMusic _n64UnplayableCount)
	message(STATUS "Leaving out ${_n64UnplayableCount} music files the console cannot decode (prepare the tree with --target=n64 to convert them)")
	file(REMOVE ${_n64UnplayableMusic})
endif()

file(GLOB _n64Videos LIST_DIRECTORIES false "${DESTINATION}/Cinematics/*.m1v" "${DESTINATION}/Cinematics/*.h264")
foreach(_n64Video IN LISTS _n64Videos)
	get_filename_component(_n64VideoName "${_n64Video}" NAME_WE)
	file(REMOVE "${DESTINATION}/Cinematics/${_n64VideoName}.j2v")
endforeach()

# The ".po" translation sources are not read at run time (only the compiled ".mo" are)
file(GLOB_RECURSE _n64TranslationSources LIST_DIRECTORIES false "${DESTINATION}/Translations/*.po")
if(_n64TranslationSources)
	file(REMOVE ${_n64TranslationSources})
endif()

# The console has no writable cache to convert into, so a package named for the desktop is taken as prebaked
if(EXISTS "${DESTINATION}/Source.pak")
	file(RENAME "${DESTINATION}/Source.pak" "${DESTINATION}/Prebaked.pak")
endif()
