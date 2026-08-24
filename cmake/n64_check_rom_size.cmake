# Verifies that a packaged Nintendo 64 ROM still fits the cartridge address space.
#
# The PI bus maps the cartridge domain at 0x10000000...0x14000000, so 64 MiB is a hard ceiling for
# everything n64tool concatenates (the executable, the symbol table and the DragonFS image). An
# oversized image is not rejected by the tools and boots in a forgiving emulator, but the part past
# the mapping is simply unreachable on hardware - which surfaces as content that cannot be opened
# rather than as a build error, so it is worth failing here instead.
#
# Invoked from the packaging step in ncine_extra_sources.cmake as:
#   cmake -DN64_ROM=<path> -P cmake/n64_check_rom_size.cmake

if(NOT DEFINED N64_ROM)
	message(FATAL_ERROR "N64_ROM has to be defined")
endif()

file(SIZE "${N64_ROM}" _romSize)
math(EXPR _romSizeMiB "${_romSize} / 1048576")
set(_romLimit 67108864)

if(_romSize GREATER _romLimit)
	math(EXPR _romExcess "(${_romSize} - ${_romLimit} + 1048575) / 1048576")
	message(FATAL_ERROR
		"The ROM is ${_romSizeMiB} MiB, which exceeds the 64 MiB cartridge address space by about "
		"${_romExcess} MiB - the content past the mapping would be unreachable on hardware. Drop or "
		"re-encode something in the staged content tree (see the packaging step for what is already "
		"left out on this platform).")
endif()

math(EXPR _romFreeMiB "(${_romLimit} - ${_romSize}) / 1048576")
message(STATUS "ROM image: ${_romSizeMiB} MiB (${_romFreeMiB} MiB left of the 64 MiB cartridge space)")
