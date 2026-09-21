# Turns "Sources/Icons/Dreamcast/Icon.ico" into the palette and bitmap a VMS file header carries, so the
# icon the console's file manager shows for a save is editable as an image instead of as a wall of hex.
#
# Runs at configure time rather than as a build rule, for the same reason the PS2's embedded IOP modules do
# (see ncine_dreamcast_icon.cmake's neighbour, ncine_ps2_embed_irx.cmake): the result is a plain header the
# compiler finds on the include path, with no custom-command ordering to get wrong.
#
# The conversion is string surgery on the file's hex, with no external tool: an .ico is an uncompressed
# paletted bitmap, so every field needed here can be sliced straight out of it. That is also why the source
# has to be the .ico and not a .png - undoing a PNG's deflate and row filters is not something CMake can do,
# and every other way of decoding one would put a host image tool between the build and fifteen
# cross-compiled targets.

# Generates the icon header into the build tree and returns its path in `outHeader`.
function(ncine_generate_dreamcast_icon outHeader)
	set(_ico "${NCINE_SOURCE_DIR}/Icons/Dreamcast/Icon.ico")
	if(NOT EXISTS "${_ico}")
		message(FATAL_ERROR "Cannot generate the Dreamcast save icon - \"${_ico}\" is missing")
	endif()
	# So that editing the icon reconfigures instead of leaving a stale header behind
	set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ico}")

	file(READ "${_ico}" _hex HEX)

	# ICONDIR: a reserved zero, type 1 ("icon"), and the number of images. Only one is read, because the
	# header written by PreferencesCache declares a single icon frame - the VMU allows up to three, but all
	# of them would have to share one palette, which an .ico does not guarantee.
	string(SUBSTRING "${_hex}" 4 4 _type)
	string(SUBSTRING "${_hex}" 8 4 _count)
	if(NOT _type STREQUAL "0100" OR NOT _count STREQUAL "0100")
		message(FATAL_ERROR "\"${_ico}\" is not a single-image .ico file (type ${_type}, ${_count} images)")
	endif()
	# ICONDIRENTRY: width, height, colour count, reserved, planes, bits per pixel
	string(SUBSTRING "${_hex}" 12 2 _width)
	string(SUBSTRING "${_hex}" 14 2 _height)
	string(SUBSTRING "${_hex}" 24 4 _bpp)
	if(NOT _width STREQUAL "20" OR NOT _height STREQUAL "20" OR NOT _bpp STREQUAL "0400")
		message(FATAL_ERROR "\"${_ico}\" must be 32x32 at 4 bits per pixel, not ${_width}x${_height} at ${_bpp} "
			"- a VMU icon has no other shape")
	endif()

	# Two hex digits per byte, so every offset below is twice the offset in the file: 6 bytes of ICONDIR and
	# 16 of ICONDIRENTRY put the image at 22, its BITMAPINFOHEADER is 40 bytes, then the 16-colour palette,
	# the 4-bit pixels and the 1-bit transparency mask
	set(_paletteAt 124)
	set(_pixelsAt 252)
	set(_maskAt 1276)

	# A .ico palette entry is blue, green, red and a padding byte; the VMU wants ARGB4444, which keeps only
	# the top nibble of each channel - and the top nibble of a byte is simply the first of its two hex
	# digits, so the conversion is a concatenation rather than any arithmetic. Alpha is always opaque here
	# because transparency is index 15 instead, which is what the mask below selects.
	set(_palette "")
	foreach(_i RANGE 14)
		math(EXPR _at "${_paletteAt} + ${_i} * 8")
		string(SUBSTRING "${_hex}" ${_at} 1 _blue)
		math(EXPR _at "${_at} + 2")
		string(SUBSTRING "${_hex}" ${_at} 1 _green)
		math(EXPR _at "${_at} + 2")
		string(SUBSTRING "${_hex}" ${_at} 1 _red)
		list(APPEND _palette "0xf${_red}${_green}${_blue}")
	endforeach()
	list(APPEND _palette "0x0000")		# The sixteenth entry is the transparent one

	# The rows of an .ico run bottom-up and those of a VMS icon top-down, so the source row is mirrored.
	# Both store two pixels per byte with the left one in the high nibble, which means a row is already the
	# 32 hex digits wanted - one digit per pixel - and only the mask has to be applied over it: a set bit
	# there is a transparent pixel, which becomes the palette's last entry.
	set(_rows "")
	foreach(_y RANGE 31)
		math(EXPR _sourceRow "31 - ${_y}")
		math(EXPR _at "${_pixelsAt} + ${_sourceRow} * 32")
		string(SUBSTRING "${_hex}" ${_at} 32 _row)
		math(EXPR _maskRowAt "${_maskAt} + ${_sourceRow} * 8")
		foreach(_byte RANGE 3)
			math(EXPR _at "${_maskRowAt} + ${_byte} * 2")
			string(SUBSTRING "${_hex}" ${_at} 2 _mask)
			if(NOT _mask STREQUAL "00")
				foreach(_bit RANGE 7)
					math(EXPR _transparent "(0x${_mask} >> (7 - ${_bit})) & 1")
					if(_transparent)
						math(EXPR _x "${_byte} * 8 + ${_bit}")
						string(SUBSTRING "${_row}" 0 ${_x} _before)
						math(EXPR _x "${_x} + 1")
						string(SUBSTRING "${_row}" ${_x} -1 _after)
						set(_row "${_before}f${_after}")
					endif()
				endforeach()
			endif()
		endforeach()
		list(APPEND _rows "${_row}")
	endforeach()

	# Eight palette entries per line, and sixteen pixel bytes - half an icon row - on each of its own.
	# CMake's regular expressions have no counted repetition, so the pixels are wrapped by matching a
	# literal run of thirty-two hex digits BEFORE they are spelled out as C++ literals, the same way
	# ncine_ps2_embed_irx.cmake wraps a module.
	set(_paletteOut "")
	foreach(_i RANGE 15)
		list(GET _palette ${_i} _entry)
		if(_i EQUAL 0)
			set(_paletteOut "${_entry},")
		elseif(_i EQUAL 8)
			set(_paletteOut "${_paletteOut}\n\t${_entry},")
		else()
			set(_paletteOut "${_paletteOut} ${_entry},")
		endif()
	endforeach()

	string(REPLACE ";" "" _pixels "${_rows}")
	string(REGEX REPLACE "(................................)" "\\1\n\t" _pixels "${_pixels}")
	string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " _pixels "${_pixels}")
	string(REPLACE " \n" "\n" _pixels "${_pixels}")
	string(REGEX REPLACE "[ \t\n]+$" "" _pixels "${_pixels}")

	set(_generatedDir "${CMAKE_BINARY_DIR}/Generated")
	set(_generatedHeader "${_generatedDir}/DreamcastIcon.h")
	file(MAKE_DIRECTORY "${_generatedDir}")
	file(WRITE "${_generatedHeader}" "// Generated by cmake/ncine_dreamcast_icon.cmake from \"${_ico}\". Do not edit manually.
#pragma once

// The 32x32 icon the Dreamcast's file manager shows for this game's saves: sixteen ARGB4444 colours, the
// last of them fully transparent, and four bits per pixel with the left pixel of a pair in the high
// nibble - what a VMS header carries verbatim.
static const unsigned short DreamcastIconPalette[16] = {
	${_paletteOut}
};

static const unsigned char DreamcastIconData[512] = {
	${_pixels}
};
")

	set(${outHeader} "${_generatedHeader}" PARENT_SCOPE)
endfunction()
