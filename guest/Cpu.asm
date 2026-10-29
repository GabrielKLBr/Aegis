option casemap:none
EXTERN GuardianTrapDispatch:PROC
PUBLIC CpuLoadTables, CpuEnterUser, CpuUserResume, CpuSyscallEntry, CpuProbeUd, CpuProbePf, CpuTriggerDoubleFault
PUBLIC CpuReadTables
PUBLIC CpuSavedKernelRsp, CpuSyscallStack, CpuSavedUserRsp
.data
CpuSavedKernelRsp QWORD 0
CpuSyscallStack QWORD 0
CpuSavedUserRsp QWORD 0
.code
CpuLoadTables PROC
    cli
    lgdt FWORD PTR [rcx]
    lidt FWORD PTR [rdx]
    mov ax, 10h
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov ax, 30h
    ltr ax
    fninit
    sub rsp, 8
    mov DWORD PTR [rsp], 1f80h
    ldmxcsr DWORD PTR [rsp]
    add rsp, 8
    ret
CpuLoadTables ENDP
CpuReadTables PROC
    sgdt FWORD PTR [rcx]
    sidt FWORD PTR [rdx]
    str ax
    movzx eax, ax
    ret
CpuReadTables ENDP

CpuEnterUser PROC
    push rbp
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 528
    mov rax, rsp
    and rax, -16
    fxsave64 [rax]
    mov CpuSavedKernelRsp, rsp
    mov ax, 23h
    mov ds, ax
    mov es, ax
    push 23h
    push rdx
    push 202h
    push 2bh
    push rcx
    swapgs
    iretq
CpuEnterUser ENDP
CpuUserResume PROC
    mov rax, rsp
    and rax, -16
    fxrstor64 [rax]
    add rsp, 528
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    mov ax, 10h
    mov ds, ax
    mov es, ax
    ret
CpuUserResume ENDP

ISR MACRO n, hasError
PUBLIC CpuIsr&n
CpuIsr&n PROC
    IF hasError EQ 0
        push 0
    ENDIF
    push n
    jmp CpuInterruptCommon
CpuIsr&n ENDP
ENDM
ISR 0,0
ISR 1,0
ISR 2,0
ISR 3,0
ISR 4,0
ISR 5,0
ISR 6,0
ISR 7,0
ISR 8,1
ISR 9,0
ISR 10,1
ISR 11,1
ISR 12,1
ISR 13,1
ISR 14,1
ISR 15,0
ISR 16,0
ISR 17,1
ISR 18,0
ISR 19,0
ISR 20,0
ISR 21,1
ISR 22,0
ISR 23,0
ISR 24,0
ISR 25,0
ISR 26,0
ISR 27,0
ISR 28,0
ISR 29,1
ISR 30,1
ISR 31,0
ISR 64,0
ISR 255,0
IFDEF GUARDIAN_COMPAT32
ISR 128,0
ENDIF

CpuSyscallEntry PROC
    swapgs
    mov CpuSavedUserRsp, rsp
    mov rsp, CpuSyscallStack
    push 23h
    push CpuSavedUserRsp
    push r11
    push 2bh
    push rcx
    push 0
    push 80h
    jmp CpuSaveContext
CpuSyscallEntry ENDP

CpuInterruptCommon PROC
    test BYTE PTR [rsp+24], 3
    jz CpuSaveContext
    swapgs
CpuSaveContext::
    cld
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rbx, rsp
    sub rsp, 560
    and rsp, -16
    fxsave64 [rsp+32]
    ; Kernel C++ runs with a defined FP environment even when a user thread
    ; changes its rounding/exception controls. Its full state remains above.
    fninit
    mov DWORD PTR [rsp+24],1f80h
    ldmxcsr DWORD PTR [rsp+24]
    mov rcx, rbx
    lea rdx, [rsp+32]
    call GuardianTrapDispatch
    fxrstor64 [rsp+32]
    mov rsp, rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    test BYTE PTR [rsp+24], 3
    jz CpuReturnKernel
    swapgs
CpuReturnKernel:
    add rsp, 16
    iretq
CpuInterruptCommon ENDP

CpuProbeUd PROC
PUBLIC CpuUdSite, CpuUdResume
CpuUdSite LABEL BYTE
    ud2
CpuUdResume LABEL BYTE
    ret
CpuProbeUd ENDP
CpuProbePf PROC
    mov rax, 100000000h
PUBLIC CpuPfSite, CpuPfResume
CpuPfSite LABEL BYTE
    mov rax, [rax]
CpuPfResume LABEL BYTE
    ret
CpuProbePf ENDP
CpuTriggerDoubleFault PROC
    mov rsp, 610800h
    ud2
    jmp $
CpuTriggerDoubleFault ENDP

; Copied as one position-independent block to the only user executable page.
PUBLIC CpuUserStart, CpuUserEnd
CpuUserStart LABEL BYTE
    mov rdi, 620000h
    mov ax, cs
    movzx rax, ax
    mov [rdi+8], rax
    mov r10, [rdi]
IFDEF GUARDIAN_GRAPHICS_KERNEL
    cmp r10, 10
    je UserGraphics
ENDIF
IFDEF GUARDIAN_NETWORK_KERNEL
    cmp r10, 9
    je UserNetwork
ENDIF
    cmp r10, 1
    je UserUd
    cmp r10, 2
    je UserGp
    cmp r10, 3
    je UserPf
    cmp r10, 4
    je UserGuard
    cmp r10, 5
    je UserBadReturn
    cmp r10, 7
    je UserIo
    cmp r10, 8
    je UserGate
    mov rbx, 1122334455667788h
    mov rbp, 123456789abcdefh
    mov r12, 8877665544332211h
    mov r13, 3344556677881122h
    mov r14, 5566778811223344h
    mov r15, 7788112233445566h
    mov rsi, 2233445566778811h
    mov rax, 0123456789abcdefh
    movq xmm6, rax
    fld1
    std
    mov rdx, 1234h
    mov eax, 1
    syscall
    pushfq
    pop rcx
    test ecx, 400h
    jz UserFail
    cld
    mov rcx, 11223344556665bch ; Echo XOR constant (0x1122334455667788 ^ 0x1234).
    cmp rax, rcx
    jne UserFail
    mov rcx, 1122334455667788h
    cmp rbx, rcx
    jne UserFail
    mov rcx, 123456789abcdefh
    cmp rbp, rcx
    jne UserFail
    mov rcx, 8877665544332211h
    cmp r12, rcx
    jne UserFail
    mov rcx, 3344556677881122h
    cmp r13, rcx
    jne UserFail
    mov rcx, 5566778811223344h
    cmp r14, rcx
    jne UserFail
    mov rcx, 7788112233445566h
    cmp r15, rcx
    jne UserFail
    mov rcx, 2233445566778811h
    cmp rsi, rcx
    jne UserFail
    cmp rdi, 620000h
    jne UserFail
    mov eax, 99
    syscall
    mov ecx, 0c0000002h
    cmp rax, rcx
    jne UserFail
    cmp QWORD PTR [rdi], 6
    jne UserContextCheck
UserWait:
    inc QWORD PTR [rdi+24]
    mov eax, 2
    syscall
    cmp rax, [rdi+32]
    jb UserWait
    cmp rdx, 2
    jb UserWait
UserContextCheck:
    movq rax, xmm6
    mov rcx, 0123456789abcdefh
    cmp rax, rcx
    jne UserFail
    fstp QWORD PTR [rdi+40]
    mov rax, 3ff0000000000000h
    cmp [rdi+40], rax
    jne UserFail
    xor edx, edx
    jmp UserFinish
UserFail:
    mov edx, 1
UserFinish:
    mov [rdi+16], rdx
    mov eax, 3
    syscall
    ud2
UserUd:
    ud2
UserGp:
    cli
    ud2
UserPf:
    mov eax, 100000h
    mov rax, [rax]
    ud2
UserGuard:
    mov rsp, 611000h
    push rax
    ud2
UserBadReturn:
    mov rsp, 800000000000h
    mov eax, 1
    syscall
    ud2
UserIo:
    out 0e9h, al
    ud2
UserGate:
    int 40h
    ud2
IFDEF GUARDIAN_NETWORK_KERNEL
UserNetwork:
    mov rax, [rdi+24]
    mov rdx, [rdi+32]
    mov r8, [rdi+40]
    syscall
    mov [rdi+16], rax
    mov [rdi+24], rdx
    xor edx, edx
    mov eax, 3
    syscall
    ud2
ENDIF
IFDEF GUARDIAN_GRAPHICS_KERNEL
INCLUDE GraphicsUser.inc
ENDIF
CpuUserEnd LABEL BYTE

.const
PUBLIC CpuIsrTable
CpuIsrTable QWORD CpuIsr0,CpuIsr1,CpuIsr2,CpuIsr3,CpuIsr4,CpuIsr5,CpuIsr6,CpuIsr7
    QWORD CpuIsr8,CpuIsr9,CpuIsr10,CpuIsr11,CpuIsr12,CpuIsr13,CpuIsr14,CpuIsr15
    QWORD CpuIsr16,CpuIsr17,CpuIsr18,CpuIsr19,CpuIsr20,CpuIsr21,CpuIsr22,CpuIsr23
    QWORD CpuIsr24,CpuIsr25,CpuIsr26,CpuIsr27,CpuIsr28,CpuIsr29,CpuIsr30,CpuIsr31
    QWORD CpuIsr64,CpuIsr255
END
