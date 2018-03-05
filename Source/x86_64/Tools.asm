%include "Common.inc"

GLOBAL DECORATE(uint64AddExchangeInterlocked)
GLOBAL DECORATE(uint64AddInterlockedMem)
GLOBAL DECORATE(uint64AddInterlocked)
GLOBAL DECORATE(uint64IncInterlocked)
GLOBAL DECORATE(uint64DecInterlocked)
GLOBAL DECORATE(uint64CompareMem)
GLOBAL DECORATE(uint64Test)

GLOBAL DECORATE(bitSetInterlockedMem)
GLOBAL DECORATE(bitSetInterlocked)
GLOBAL DECORATE(bitSet)
GLOBAL DECORATE(bitSet2InterlockedMem)
GLOBAL DECORATE(bitResetInterlocked)
GLOBAL DECORATE(bitReset)
GLOBAL DECORATE(bitTestMem)
GLOBAL DECORATE(bitTest)
GLOBAL DECORATE(bitSet2InterlockedMem)
GLOBAL DECORATE(bitSet2Interlocked)
GLOBAL DECORATE(bitTest2Mem)
GLOBAL DECORATE(bitTest2)
GLOBAL DECORATE(bitGet2)
GLOBAL DECORATE(bitTestRangeMem)
GLOBAL DECORATE(bitTestRange)
GLOBAL DECORATE(bitTestRange2Mem)
GLOBAL DECORATE(bitTestRange2)

GLOBAL DECORATE(uint8BitScanForward)
GLOBAL DECORATE(uint8BitScanReverse)
GLOBAL DECORATE(uint32BitScanReverse)
GLOBAL DECORATE(uint64BitScanReverse)
GLOBAL DECORATE(uint32FusedMulAdd)
GLOBAL DECORATE(uint64FusedMulAdd)
GLOBAL DECORATE(uint64FusedMul)

%ifdef WINDOWS_64

ALIGN 16
DECORATE(uint64AddExchangeInterlocked):
    mov rax, rdx
    lock xadd [rcx], rax
    ret

ALIGN 16
DECORATE(uint64AddInterlockedMem):
    mov rdx, [rdx]
    lock add [rcx], rdx
    ret

ALIGN 16
DECORATE(uint64AddInterlocked):
    lock add [rcx], rdx
    ret

ALIGN 16
DECORATE(uint64IncInterlocked):
    lock add qword [rcx], 1
    ret

ALIGN 16
DECORATE(uint64DecInterlocked):
    lock sub qword [rcx], 1
    ret

ALIGN 16
DECORATE(uint64CompareMem):
    mov rcx, [rcx]
    cmp rcx, [rdx]
    setnb al
    ret

ALIGN 16
DECORATE(uint64Test):
    mov rcx, [rcx]
    test rcx, rcx
    setnz al
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

ALIGN 16
DECORATE(bitSetInterlockedMem):
    mov rdx, [rdx]
DECORATE(bitSetInterlocked):
    mov r8, rdx
    shr r8, 3
    add r8, rcx
    movzx ecx, dl
    and cl, 7
    mov eax, 1
    shl al, cl
    lock or [r8], al
    ret

ALIGN 16
DECORATE(bitSet):
    mov r8, rdx
    shr r8, 3
    add r8, rcx
    movzx ecx, dl
    and cl, 7
    mov eax, 1
    shl al, cl
    or [r8], al
    ret

ALIGN 16
DECORATE(bitResetInterlocked):
    mov r8, rdx
    shr r8, 3
    add r8, rcx
    movzx ecx, dl
    and cl, 7
    mov eax, 1
    shl al, cl
    not al
    lock and [r8], al
    ret

ALIGN 16
DECORATE(bitReset):
    mov r8, rdx
    shr r8, 3
    add r8, rcx
    movzx ecx, dl
    and cl, 7
    mov eax, 1
    shl al, cl
    not al
    and [r8], al
    ret

ALIGN 16
DECORATE(bitTestMem):
    mov rdx, [rdx]
DECORATE(bitTest):
    mov r8, rdx
    shr r8, 3
    add r8, rcx
    movzx ecx, dl
    and cl, 7
    movzx eax, byte [r8]
    shr al, cl
    and al, 1
    ret

ALIGN 16
DECORATE(bitSet2InterlockedMem):
    mov rdx, [rdx]
DECORATE(bitSet2Interlocked):
    mov r8, rcx
    movzx ecx, dl
    shr rdx, 3
    and cl, 7
    cmp cl, 7
    je .overflow
    mov eax, 3
    shl al, cl
    lock or byte [rdx + r8], al
    ret
.overflow
    lock or word [rdx + r8], 0x180
    ret

ALIGN 16
DECORATE(bitTest2Mem):
    mov rdx, [rdx]
DECORATE(bitTest2):
    mov r8, rcx
    movzx ecx, dl
    shr rdx, 3
    and cl, 7
    cmp cl, 7
    je .overflow
    movzx eax, byte [rdx + r8]
    shr al, cl
    not al
    test al, 3
    setz al 
    ret
.overflow
    movzx eax, word [rdx + r8]
    not ax
    test ax, 0x180
    setz al
    ret

ALIGN 16
DECORATE(bitGet2):
    mov r8, rcx
    movzx ecx, dl
    shr rdx, 3
    and cl, 7
    cmp cl, 7
    je .overflow
    movzx eax, byte [rdx + r8]
    shr al, cl
    and al, 3
    ret
.overflow
    movzx eax, word [rdx + r8]
    shr ax, 7
    and al, 3
    ret

ALIGN 16
DECORATE(bitTestRangeMem):
    mov rdx, [rdx]
DECORATE(bitTestRange):
    mov r8, rcx
    movzx ecx, dl
    shr rdx, 3
    add rdx, r8
.loop:
    cmp r8, rdx              
    jae .rem 
    movzx eax, byte [r8]
    add r8, 1
    add al, 1
    jz .loop
    xor eax, eax
    ret
.rem:
    mov eax, 1
    and cl, 7
    shl al, cl
    sub al, 1
    movzx ecx, byte [rdx]
    not cl
    test cl, al 
    setz al
    ret

ALIGN 16
DECORATE(bitTestRange2Mem):
    mov rdx, [rdx]
DECORATE(bitTestRange2):
    mov r8, rcx
    movzx ecx, dl
    shr rdx, 2
    add rdx, r8
.loop:
    cmp r8, rdx              
    jae .rem 
    movzx eax, byte [r8]
    add r8, 1
    not al
    test al, 01010101b
    jz .loop
    xor eax, eax
    ret
.rem:
    mov eax, 1
    and cl, 3
    shl cl, 1
    shl al, cl
    sub al, 1
    and al, 01010101b
    movzx ecx, byte [rdx]
    not cl
    test cl, al
    setz al
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

ALIGN 16
DECORATE(uint8BitScanForward):
    movzx ecx, cl
    bsf ax, cx
    jz .null
    ret
.null
    mov al, -1
    ret

ALIGN 16
DECORATE(uint8BitScanReverse):
    movzx ecx, cl
    bsr ax, cx
    jz .null
    ret
.null
    mov al, -1
    ret

ALIGN 16
DECORATE(uint32BitScanReverse):
    bsr eax, ecx
    jz .null
    ret 
.null:
    mov eax, -1
    ret

ALIGN 16
DECORATE(uint64BitScanReverse):
    bsr rax, rcx
    jz .null
    ret 
.null:
    mov rax, -1
    ret

ALIGN 16
DECORATE(uint32FusedMulAdd)
    mov eax, [rcx]
    mul edx
    setnc dl
    add eax, r8d
    mov [rcx], eax
    setnc al
    and al, dl
    ret

ALIGN 16
DECORATE(uint64FusedMulAdd)
    mov rax, [rcx]
    mul rdx
    setnc dl
    add rax, r8
    mov [rcx], rax
    setnc al
    and al, dl
    ret

ALIGN 16
DECORATE(uint64FusedMul)
    mov rax, [rcx]
    mul rdx
    mov [rcx], rax
    setnc al
    ret

ALIGN 16

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

%elifdef SYSTEM_V

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

ALIGN 16
DECORATE(uint64AddExchangeInterlocked):
    mov rax, rsi
    lock xadd [rdi], rax
    ret

ALIGN 16
DECORATE(uint64AddInterlockedMem):
    mov rsi, [rsi]
    lock add [rdi], rsi
    ret

ALIGN 16
DECORATE(uint64AddInterlocked):
    lock add [rdi], rsi
    ret

ALIGN 16
DECORATE(uint64IncInterlocked):
    lock add qword [rdi], 1
    ret

ALIGN 16
DECORATE(uint64DecInterlocked):
    lock sub qword [rdi], 1
    ret

ALIGN 16
DECORATE(uint64CompareMem):
    mov rdi, [rdi]
    cmp rdi, [rsi]
    setnb al
    ret

ALIGN 16
DECORATE(uint64Test):
    mov rdi, [rdi]
    test rdi, rdi
    setnz al
    ret
          
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

ALIGN 16
DECORATE(bitSetInterlockedMem):
    mov rsi, [rsi]
DECORATE(bitSetInterlocked):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    mov eax, 1
    shl al, cl
    lock or [rdi + rsi], al
    ret

ALIGN 16
DECORATE(bitSet):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    mov eax, 1
    shl al, cl
    or [rdi + rsi], al
    ret

ALIGN 16
DECORATE(bitResetInterlocked):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    mov eax, 1
    shl al, cl
    not al
    lock and [rdi + rsi], al
    ret

ALIGN 16
DECORATE(bitReset):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    mov eax, 1
    shl al, cl
    not al
    and [rdi + rsi], al
    ret

ALIGN 16
DECORATE(bitTestMem):
    mov rsi, [rsi]
DECORATE(bitTest):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    movzx eax, byte [rdi + rsi]
    shr al, cl
    and al, 1
    ret

ALIGN 16
DECORATE(bitSet2InterlockedMem):
    mov rsi, [rsi]
DECORATE(bitSet2Interlocked):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    cmp cl, 7
    je .overflow
    mov eax, 3
    shl al, cl
    lock or [rdi + rsi], al
    ret
.overflow
    lock or word [rdi + rsi], 0x180
    ret

ALIGN 16
DECORATE(bitTest2Mem):
    mov rsi, [rsi]
DECORATE(bitTest2):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    cmp cl, 7
    je .overflow                                      
    movzx eax, byte [rdi + rsi]
    shr al, cl
    not al
    test al, 3
    setz al 
    ret
.overflow
    movzx eax, word [rdi + rsi]
    not ax
    test ax, 0x180
    setz al
    ret

ALIGN 16
DECORATE(bitGet2):
    movzx ecx, sil
    shr rsi, 3
    and cl, 7
    cmp cl, 7
    je .overflow
    movzx eax, byte [rdi + rsi]
    shr al, cl
    and al, 3
    ret
.overflow
    movzx eax, word [rdi + rsi]
    shr ax, 7
    and al, 3
    ret

ALIGN 16
DECORATE(bitTestRangeMem):
    mov rsi, [rsi]
DECORATE(bitTestRange):
    movzx ecx, sil
    shr rsi, 3
    add rsi, rdi
.loop:
    cmp rdi, rsi          
    jae .rem
    movzx eax, byte [rdi]
    add rdi, 1
    add al, 1
    jz .loop
    xor eax, eax
    ret
.rem:
    mov eax, 1
    and cl, 7
    shl al, cl
    sub al, 1
    movzx ecx, byte [rsi]
    not cl
    test cl, al
    setz al
    ret    

ALIGN 16
DECORATE(bitTestRange2Mem):
    mov rsi, [rsi]
DECORATE(bitTestRange2):
    movzx ecx, sil
    shr rsi, 2
    add rsi, rdi
.loop:
    cmp rdi, rsi              
    jae .rem 
    movzx eax, byte [rdi]
    add rdi, 1
    not al
    test al, 01010101b
    jz .loop
    xor eax, eax
    ret
.rem:
    mov eax, 1
    and cl, 3
    shl cl, 1
    shl al, cl
    sub al, 1
    and al, 01010101b
    movzx ecx, byte [rsi]
    not cl
    test cl, al
    setz al
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

ALIGN 16
DECORATE(uint8BitScanForward):
    movzx edi, dil
    bsf ax, di
    jz .null
    ret
.null:
    mov al, -1
    ret

ALIGN 16
DECORATE(uint8BitScanReverse):
    movzx edi, dil
    bsr ax, di
    jz .null
    ret
.null:
    mov al, -1
    ret

ALIGN 16
DECORATE(uint32BitScanReverse):
    bsr eax, edi
    jz .null
    ret 
.null:
    mov eax, -1
    ret

ALIGN 16
DECORATE(uint64BitScanReverse):
    bsr rax, rdi
    jz .null
    ret 
.null:
    mov rax, -1
    ret

ALIGN 16
DECORATE(uint32FusedMulAdd)
    mov eax, [rdi]
    mov r8d, edx
    mul esi
    setnc cl
    add eax, r8d
    mov [rdi], eax
    setnc al
    and al, cl
    ret

ALIGN 16
DECORATE(uint64FusedMulAdd)
    mov rax, [rdi]
    mov r8, rdx
    mul rsi
    setnc cl
    add rax, r8
    mov [rdi], rax
    setnc al
    and al, cl
    ret

ALIGN 16
DECORATE(uint64FusedMul)
    mov rax, [rdi]
    mul rsi
    mov [rdi], rax
    setnc al
    ret

ALIGN 16

%endif