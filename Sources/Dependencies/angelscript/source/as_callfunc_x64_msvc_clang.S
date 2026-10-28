#  AngelCode Scripting Library
#  Copyright (c) 2003-2025 Andreas Jonsson
# 
#  This software is provided 'as-is', without any express or implied 
#  warranty. In no event will the authors be held liable for any 
#  damages arising from the use of this software.
# 
#  Permission is granted to anyone to use this software for any 
#  purpose, including commercial applications, and to alter it and 
#  redistribute it freely, subject to the following restrictions:
# 
#  1. The origin of this software must not be misrepresented; you 
#     must not claim that you wrote the original software. If you use
#     this software in a product, an acknowledgment in the product 
#     documentation would be appreciated but is not required.
# 
#  2. Altered source versions must be plainly marked as such, and 
#     must not be misrepresented as being the original software.
# 
#  3. This notice may not be removed or altered from any source 
#     distribution.
# 
#  The original version of this library can be located at:
#  http://www.angelcode.com/angelscript/
# 
#  Andreas Jonsson
#  andreas@angelcode.com

# Ported to gnu / at&t assembly from AngelScript's as_callfunc_x64_msvc_asm.asm by OctalCodes
# The modifications I made are available under public domain.
# Aside from my modifications, follow the licence and terms of the original software.

#if !defined(AS_MAX_PORTABILITY)

#if defined(__clang__)

.section .text
.globl CallX64
.globl GetReturnedFloat
.globl GetReturnedDouble

CallX64:
    # PROLOGUE
    # Save the preserved registers that are used
    pushq %rbp
    pushq %rsi
    pushq %r11
    pushq %rdi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rbx
    subq $0x50, %rsp
    movq %rsp, %rbp
    # End of prologue

    # Move function param to non-scratch register
    movq %r9, %r14   # r14 = function

    # Allocate space for arguments on the stack
    movq %r8, %rdi
    addq $0x20, %rdi

    # Ensure the stack is 16-byte aligned
    movq %rsp, %rsi
    subq %rdi, %rsi
    andq $0x8, %rsi
    addq %rsi, %rdi
    subq %rdi, %rsp

    # Check if paramSize == 0
    cmpq $0, %r8
    je .callfunc

    # Move parameters to non-scratch registers
    movq %rcx, %rsi   # rsi = pArgs
    movq %rdx, %r11   # r11 = pFloatArgs
    movl %r8d, %r12d  # r12 = paramSize

    # Copy arguments to stack
    movq 0(%rsi), %rcx
    movq 8(%rsi), %rdx
    movq 16(%rsi), %r8
    movq 24(%rsi), %r9

    # Adjust r12 to reflect the size of remaining params
    subq $0x20, %r12
    js .copyfloat
    jz .copyfloat

    # Copy remaining parameters onto the stack
    addq $0x20, %rsi
    movq %rsp, %r13
    addq $0x20, %r13

.copyoverflow:
    movq 0(%rsi), %r15
    movq %r15, 0(%r13)
    addq $8, %r13
    addq $8, %rsi
    subq $8, %r12
    jnz .copyoverflow

.copyfloat:
    # Check for floating-point parameters
    cmpq $0, %r11
    je .callfunc

    movlpd 0(%r11), %xmm0
    movlpd 8(%r11), %xmm1
    movlpd 16(%r11), %xmm2
    movlpd 24(%r11), %xmm3

.callfunc:
    # Call function
    call *%r14

    # Restore the stack and registers
    movq %rbp, %rsp
    addq $0x50, %rsp
    popq %rbx
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rdi
    popq %r11
    popq %rsi
    popq %rbp

    ret

GetReturnedFloat:
    # Prologue: Store registers and allocate stack space
    subq $8, %rsp
    # End prologue

    # Move float value from XMM0 to RAX
    movss %xmm0, (%rsp)
    movl (%rsp), %eax

    # Epilogue: Clean up
    addq $8, %rsp
    ret

GetReturnedDouble:
    # Prologue: Store registers and allocate stack space
    subq $8, %rsp
    # End prologue

    # Move double value from XMM0 to RAX
    movlpd %xmm0, (%rsp)
    movq (%rsp), %rax

    # Epilogue: Clean up
    addq $8, %rsp
    ret

#endif /* __clang__ */

#endif /* !AS_MAX_PORTABILITY */