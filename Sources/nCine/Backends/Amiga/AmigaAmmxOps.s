; AMMX (Apollo 68080) implementations of the software rasterizer's two hottest scanline kernels,
; SwScanlineOps.h's BlendScanlineSrcAlpha and FusedLutBlendScanline. Assembled with vasm
; (-m68080 -Fhunk) and linked in only on the Amiga; dispatched at run time behind the 68080
; detection in AmigaPlatform.cpp (see SwRaster.cpp's entry-point wrappers), so the same binary
; stays 68060-safe.
;
; BIT-EXACTNESS CONTRACT (see the quantization notes at the top of SwRaster.cpp): every result
; byte must equal the scalar reference
;     r = (src * sA + dst * (255 - sA)) >> 8        (alpha lane: (sA * 255 + dstA * inv) >> 8)
; with the sA == 0 (skip) and sA == 255 (copy) pixels replayed as real skips/copies. PMULA
; cannot express this (its two per-operand ">> 8" truncations lose the carry between the
; products' low bytes), so the kernels widen to 16-bit lanes instead:
;     - VPERM widens the 4 pixel bytes to 4 words (zero byte from the constant register),
;     - two PMULLs form src*sA and dst*inv exactly (products <= 255*255 = 65025, so the low
;       16 bits ARE the product; sA + inv == 255 keeps the SUM <= 65025 as well, so a plain
;       PADDW cannot wrap),
;     - a final VPERM picks each word's high byte, which IS ">> 8" of a 16-bit value.
; The pair loop's both-opaque path is a single 64-bit load/store; both-transparent advances
; pointers only - the same shortcuts the scalar and SSE2/NEON variants take.
;
; Over-read note: the single-pixel paths use 64-bit AMMX loads of which only 4 bytes are
; consumed, so the last pixel of a buffer reads up to 4 bytes past it. AmigaOS has no memory
; protection and the rasterizer's surfaces are heap blocks with allocator metadata behind
; them, so the read is harmless; nothing is ever over-WRITTEN (stores go through STOREC with
; an exact byte count).
;
; Register conventions: m68k-amigaos-gcc passes arguments on the stack and expects d2-d7/a2-a6
; preserved; the E registers have no C convention (the compiler never touches them) and no
; state is held in them across calls. NOTE: E registers survive a task switch only under an
; AMMX-aware exec, which every Apollo/Vampire setup ships.

	machine	68080

	section	text,code

	xdef	_SwAmmxBlendScanlineSrcAlpha
	xdef	_SwAmmxFusedLutBlendScanline

; ---------------------------------------------------------------------------
; The two 64-bit constants every blend needs, loaded once per call:
;   e6 = $00FF000000000000 - byte 0 is the zero the widening VPERMs insert,
;                            byte 1 the 255 the alpha lane's multiplier uses
;   e5 = $00FF00FF00FF00FF - word-lane 255s, XORed with the splatted alpha to
;                            form inv = 255 - sA (exact for bytes: 255-x == x^255)
; ---------------------------------------------------------------------------
AmmxConsts:
	dc.l	$00FF0000,$00000000
	dc.l	$00FF00FF,$00FF00FF

; ---------------------------------------------------------------------------
; Blends the pixel at (a1) over the pixel at (a0) - bytes 0-3, mixed alpha only
; (the caller has already dispatched sA == 0 / sA == 255). Clobbers e0-e3.
; ---------------------------------------------------------------------------
BLENDPIX0	macro
	load	(a1),e0			; src pixel in bytes 0-3
	load	(a0),e1			; dst pixel in bytes 0-3
	vperm	#$80818283,e0,e6,e2	; SW    = [r, g, b, a] as words
	vperm	#$83838389,e0,e6,e3	; AW    = [sA, sA, sA, 255] as words
	pmull	e2,e3,e2		; SW * AW (exact 16-bit products)
	vperm	#$83838383,e0,e6,e3	; ASPL  = [sA, sA, sA, sA] as words
	peor	e5,e3,e3		; INVW  = [inv, inv, inv, inv]
	vperm	#$80818283,e1,e6,e0	; DW    = [dr, dg, db, da] as words
	pmull	e0,e3,e0		; DW * INVW
	paddw	e2,e0,e0		; sum (<= 65025 per lane, cannot wrap)
	vperm	#$02460246,e0,e0,e0	; each word's high byte == sum >> 8
	storec	e0,d4,(a0)		; write exactly the 4 result bytes (d4 == 4)
	endm

; Same for the second pixel of a pair - bytes 4-7 of (a1)/(a0)
BLENDPIX1	macro
	load	(a1),e0
	load	(a0),e1
	vperm	#$84858687,e0,e6,e2	; SW from bytes 4-7
	vperm	#$87878789,e0,e6,e3	; AW  ([sA1, sA1, sA1, 255])
	pmull	e2,e3,e2
	vperm	#$87878787,e0,e6,e3	; ASPL
	peor	e5,e3,e3
	vperm	#$84858687,e1,e6,e0	; DW from bytes 4-7
	pmull	e0,e3,e0
	paddw	e2,e0,e0
	vperm	#$02460246,e0,e0,e0
	storec	e0,d4,4(a0)
	endm

; ---------------------------------------------------------------------------
; void SwAmmxBlendScanlineSrcAlpha(uint8* dst, const uint8* src, int32 count)
; ---------------------------------------------------------------------------
_SwAmmxBlendScanlineSrcAlpha:
	movem.l	d2-d4/a2,-(sp)		; 16 bytes -> args at 20/24/28(sp)
	move.l	20(sp),a0		; dst
	move.l	24(sp),a1		; src
	move.l	28(sp),d0		; count
	lea	AmmxConsts(pc),a2
	load	(a2),e6
	load	8(a2),e5
	moveq	#4,d4			; STOREC byte count (no immediate form)

.pairLoop:
	subq.l	#2,d0
	bmi.s	.tail
	move.b	3(a1),d1		; sA of pixel 0
	move.b	7(a1),d2		; sA of pixel 1
	move.b	d1,d3
	or.b	d2,d3
	beq.s	.skip8			; both fully transparent: untouched
	move.b	d1,d3
	and.b	d2,d3
	cmp.b	#$ff,d3
	beq.s	.copy8			; both fully opaque: 64-bit copy
	; mixed pair: each pixel replays the scalar variant's per-pixel classes
	tst.b	d1
	beq.s	.pix1
	cmp.b	#$ff,d1
	beq.s	.copyPix0
	BLENDPIX0
	bra.s	.pix1
.copyPix0:
	move.l	(a1),(a0)
.pix1:
	tst.b	d2
	beq.s	.advance8
	cmp.b	#$ff,d2
	beq.s	.copyPix1
	BLENDPIX1
	bra.s	.advance8
.copyPix1:
	move.l	4(a1),4(a0)
.advance8:
	addq.l	#8,a0
	addq.l	#8,a1
	bra.s	.pairLoop
.skip8:
	addq.l	#8,a0
	addq.l	#8,a1
	bra.s	.pairLoop
.copy8:
	load	(a1),e0
	store	e0,(a0)
	addq.l	#8,a0
	addq.l	#8,a1
	bra.s	.pairLoop

.tail:					; d0 is -2 (nothing left) or -1 (one pixel)
	addq.l	#1,d0
	bmi.s	.done
	move.b	3(a1),d1
	beq.s	.done			; transparent: nothing to write
	cmp.b	#$ff,d1
	beq.s	.copyTail
	BLENDPIX0
	bra.s	.done
.copyTail:
	move.l	(a1),(a0)
.done:
	movem.l	(sp)+,d2-d4/a2
	rts

; ---------------------------------------------------------------------------
; void SwAmmxFusedLutBlendScanline(uint8* dst, const uint8* srcIdx, int32 count,
;                                  const uint8 (*packed)[4])
;
; Per-pixel like the scalar variant (two neighbouring indices rarely share an
; entry, so there is no pair shortcut to take); the LUT entry IS the final
; source pixel. The 64-bit entry load reads the next entry's bytes as well,
; which stays inside the caller's SwPaletteLut for every index including 255.
; ---------------------------------------------------------------------------
_SwAmmxFusedLutBlendScanline:
	movem.l	d2/d4/a2-a3,-(sp)	; 16 bytes -> args at 20/24/28/32(sp)
	move.l	20(sp),a0		; dst
	move.l	24(sp),a1		; srcIdx
	move.l	28(sp),d0		; count
	move.l	32(sp),a2		; packed[256][4]
	lea	AmmxConsts(pc),a3
	load	(a3),e6
	load	8(a3),e5
	moveq	#4,d4			; STOREC byte count (no immediate form)

.lutLoop:
	subq.l	#1,d0
	bmi.s	.lutDone
	moveq	#0,d1			; the previous iteration's <<2 left bits 8-9 set
	move.b	(a1)+,d1
	lsl.l	#2,d1			; entry byte offset
	move.b	3(a2,d1.l),d2		; entry alpha
	beq.s	.lutNext		; transparent: untouched
	cmp.b	#$ff,d2
	beq.s	.lutCopy
	; mixed: blend the entry over (a0) - the BLENDPIX0 body with the
	; source pixel loaded from the LUT instead of a source scanline
	load	(a2,d1.l),e0
	load	(a0),e1
	vperm	#$80818283,e0,e6,e2
	vperm	#$83838389,e0,e6,e3
	pmull	e2,e3,e2
	vperm	#$83838383,e0,e6,e3
	peor	e5,e3,e3
	vperm	#$80818283,e1,e6,e0
	pmull	e0,e3,e0
	paddw	e2,e0,e0
	vperm	#$02460246,e0,e0,e0
	storec	e0,d4,(a0)
	bra.s	.lutNext
.lutCopy:
	move.l	(a2,d1.l),(a0)
.lutNext:
	addq.l	#4,a0
	bra.s	.lutLoop

.lutDone:
	movem.l	(sp)+,d2/d4/a2-a3
	rts
