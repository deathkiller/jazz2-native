/*
	Layout of the GameCube apploader (see GameCubeApploader.c). The disc carries a 32-byte header at 0x2440
	and the code right behind it at 0x2460; BS2 loads the code to 0x81200000 and nothing else, so the header is
	linked 32 bytes below that and `objcopy -O binary` turns the two into exactly the bytes the disc holds from
	0x2440 on. Whatever has no initial value comes after the code and is not part of the image at all.
*/
OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(ApploaderEntry)

SECTIONS
{
	.header 0x811FFFE0 : {
		KEEP(*(.apploader.header))
	}

	.text 0x81200000 : {
		KEEP(*(.apploader.entry))
		*(.text .text.*)
		*(.rodata .rodata.* .sdata2 .sdata2.*)
		*(.data .data.* .sdata .sdata.*)
		. = ALIGN(32);
	}
	_apploaderCodeSize = SIZEOF(.text);

	.bss (NOLOAD) : ALIGN(32) {
		*(.sbss .sbss.* .bss .bss.* COMMON)
	}

	/DISCARD/ : {
		*(.comment .eh_frame .note .note.* .gnu.attributes)
	}
}
