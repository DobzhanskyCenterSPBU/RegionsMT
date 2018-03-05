%include "Common.inc"

GLOBAL DECORATE(float64CompDscAbsStable)
GLOBAL DECORATE(float64CompDscStable)
GLOBAL DECORATE(float64CompDscNaNStable)
GLOBAL DECORATE(float64CompDsc)
GLOBAL DECORATE(float64CompDscNaN)

%ifdef WINDOWS_64

ALIGN 16
DECORATE(float64CompDscAbsStable):
    movsd xmm0, qword [rcx]
    andpd xmm0, [.mask wrt rip]
    movsd xmm1, qword [rdx]
    andpd xmm1, [.mask wrt rip]
    ucomisd xmm0, xmm1
    seta dl
    setb al
    sub al, dl
    ret
ALIGN 16
.mask
    dq 0x7fffffffffffffff, 0

ALIGN 16
DECORATE(float64CompDscStable):
    movsd xmm0, qword [rcx]
    movsd xmm1, qword [rdx]
    ucomisd xmm0, xmm1
    seta dl
    setb al
    sub al, dl
    ret

ALIGN 16
DECORATE(float64CompDscNaNStable):
    movsd xmm0, qword [rcx]
    movsd xmm1, qword [rdx]
    ucomisd xmm0, xmm1
    jp .nan0
    seta dl
    setb al
    sub al, dl
    ret
.nan0
    ucomisd xmm0, xmm0
    jp .nan1
    mov al, -1
    ret
.nan1
    ucomisd xmm1, xmm1
    setnp al    
    ret

ALIGN 16
DECORATE(float64CompDsc):
    movsd xmm0, qword [rcx]
    movsd xmm1, qword [rdx]
    ucomisd xmm0, xmm1
    setb al
    ret

ALIGN 16
DECORATE(float64CompDscNaN):
    movsd xmm0, qword [rcx]
    movsd xmm1, qword [rdx]
    ucomisd xmm0, xmm1
    jp .nan0
    setb al
    ret
.nan0
    ucomisd xmm0, xmm0
    jp .nan1
    xor eax, eax
    ret
.nan1
    ucomisd xmm1, xmm1
    setnp al    
    ret

ALIGN 16

%elifdef SYSTEM_V

ALIGN 16
DECORATE(float64CompDscAbsStable):
    movsd xmm0, qword [rdi]
    andpd xmm0, [.mask wrt rip]
    movsd xmm1, qword [rsi]
    andpd xmm1, [.mask wrt rip]
    ucomisd xmm0, xmm1
    seta dl
    setb al
    sub al, dl
    ret
ALIGN 16
.mask
    dq 0x7fffffffffffffff, 0

ALIGN 16
DECORATE(float64CompDscStable):
    movsd xmm0, qword [rdi]
    movsd xmm1, qword [rsi]
    ucomisd xmm0, xmm1
    seta dl
    setb al
    sub al, dl
    ret

ALIGN 16
DECORATE(float64CompDscNaNStable):
    movsd xmm0, qword [rdi]
    movsd xmm1, qword [rsi]
    ucomisd xmm0, xmm1
    jp .nan0
    seta dl
    setb al
    sub al, dl
    ret
.nan0
    ucomisd xmm0, xmm0
    jp .nan1
    mov al, -1
    ret
.nan1
    ucomisd xmm1, xmm1
    setnp al    
    ret

ALIGN 16
DECORATE(float64CompDsc):
    movsd xmm0, qword [rdi]
    movsd xmm1, qword [rsi]
    ucomisd xmm0, xmm1
    setb al
    ret

ALIGN 16
DECORATE(float64CompDscNaN):
    movsd xmm0, qword [rdi]
    movsd xmm1, qword [rsi]
    ucomisd xmm0, xmm1
    jp .nan0
    setb al
    ret
.nan0
    ucomisd xmm0, xmm0
    jp .nan1
    xor eax, eax
    ret
.nan1
    ucomisd xmm1, xmm1
    setnp al    
    ret

ALIGN 16

%endif