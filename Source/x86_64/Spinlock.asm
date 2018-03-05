%include "Common.inc"

GLOBAL DECORATE(spinlockRelease)
GLOBAL DECORATE(spinlockAcquire)
GLOBAL DECORATE(spinlockTest)

%ifdef WINDOWS_64

ALIGN 16
DECORATE(spinlockRelease):
    mov dword [rcx], 0
    ret

ALIGN 16
DECORATE(spinlockAcquire):
    mov edx, 1
.retry:
    pause
    xor eax, eax
    lock cmpxchg dword [rcx], edx
    jnz .retry
    ret

ALIGN 16
DECORATE(spinlockTest):
    mov eax, dword [rcx]
    ret

ALIGN 16

%elifdef SYSTEM_V

ALIGN 16
DECORATE(spinlockRelease):
    mov dword [rdi], 0
    ret

ALIGN 16
DECORATE(spinlockAcquire):
    mov esi, 1
.retry:
    pause
    xor eax, eax
    lock cmpxchg dword [rdi], esi
    jnz .retry
    ret

ALIGN 16
DECORATE(spinlockTest):
    mov eax, dword [rdi]
    ret

ALIGN 16

%endif