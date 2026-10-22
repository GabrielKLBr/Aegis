option casemap:none
.code
SET_GPR MACRO reg, displacement
    lea reg,[r12+displacement]
ENDM
CHECK_GPR MACRO reg, displacement
    lea rax,[r12+displacement]
    cmp reg,rax
    jne Failure
ENDM
SET_XMM MACRO reg
    movq reg,r12
ENDM
CHECK_XMM MACRO reg
    movq rax,reg
    cmp rax,r12
    jne Failure
ENDM
PUBLIC ThreadUserStart,ThreadUserEnd,ThreadIdle,ThreadBootstrap,ThreadApcDispatcher,ThreadApcCallback
ThreadUserStart LABEL BYTE
    mov r12,rdi
    add r12,12340000h
    movq xmm6,r12
    fld1
    cmp rsi,7
    je IdleTest
    cmp rsi,5
    je FaultWorker
    cmp rsi,4
    je ApcWorker
    cmp rsi,6
    je Controller
    cmp rsi,2
    jae LockWorker
    SET_GPR rbx,1
    SET_GPR rbp,2
    SET_GPR r13,3
    SET_GPR r14,4
    SET_GPR r15,5
    SET_GPR rcx,6
    SET_GPR rdx,7
    SET_GPR r8,8
    SET_GPR r9,9
    SET_GPR r10,10
    SET_GPR r11,11
    SET_XMM xmm0
    SET_XMM xmm1
    SET_XMM xmm2
    SET_XMM xmm3
    SET_XMM xmm4
    SET_XMM xmm5
    SET_XMM xmm7
    SET_XMM xmm8
    SET_XMM xmm9
    SET_XMM xmm10
    SET_XMM xmm11
    SET_XMM xmm12
    SET_XMM xmm13
    SET_XMM xmm14
    SET_XMM xmm15
    mov DWORD PTR gs:[32],1f80h
    mov WORD PTR gs:[40],037fh
    test edi,1
    jz SpinSettings
    mov DWORD PTR gs:[32],3f80h
    mov WORD PTR gs:[40],077fh
SpinSettings:
    ldmxcsr DWORD PTR gs:[32]
    fldcw WORD PTR gs:[40]
    std
    ; NT is writable by user POPFQ but unsafe for a long-mode IRETQ. A timer
    ; return must apply the same privileged-flag policy as a syscall return.
    pushfq
    or QWORD PTR [rsp],4000h
    popfq
SpinWorker:
    pushfq
    pop rax
    test eax,400h
    jz Failure
    inc QWORD PTR gs:[8]
    CHECK_GPR rbx,1
    CHECK_GPR rbp,2
    CHECK_GPR r13,3
    CHECK_GPR r14,4
    CHECK_GPR r15,5
    CHECK_GPR rcx,6
    CHECK_GPR rdx,7
    CHECK_GPR r8,8
    CHECK_GPR r9,9
    CHECK_GPR r10,10
    CHECK_GPR r11,11
    CHECK_XMM xmm0
    CHECK_XMM xmm1
    CHECK_XMM xmm2
    CHECK_XMM xmm3
    CHECK_XMM xmm4
    CHECK_XMM xmm5
    CHECK_XMM xmm6
    CHECK_XMM xmm7
    CHECK_XMM xmm8
    CHECK_XMM xmm9
    CHECK_XMM xmm10
    CHECK_XMM xmm11
    CHECK_XMM xmm12
    CHECK_XMM xmm13
    CHECK_XMM xmm14
    CHECK_XMM xmm15
    stmxcsr DWORD PTR gs:[36]
    mov eax,DWORD PTR gs:[32]
    cmp eax,DWORD PTR gs:[36]
    jne Failure
    fnstcw WORD PTR gs:[42]
    mov ax,WORD PTR gs:[40]
    cmp ax,WORD PTR gs:[42]
    jne Failure
    cmp QWORD PTR gs:[0],r12
    jne Failure
    lea rax,[r12+44440000h]
    cmp QWORD PTR fs:[0],rax
    jne Failure
    fst QWORD PTR gs:[16]
    mov rax,3ff0000000000000h
    cmp QWORD PTR gs:[16],rax
    jne Failure
    mov eax,620000h
    cmp QWORD PTR [rax],0
    je SpinWorker
    cld
    jmp ExitSuccess
LockWorker:
    mov ebp,16
LockLoop:
    mov ebx,620000h
    mov eax,ebp
    and eax,3
    mov r14,[rbx+40+rax*8]
    lea r15,[rbx+96+rax*8]
    mov rdx,r14
    mov r8,-1
    xor r9d,r9d
    mov eax,22h
    syscall
    test eax,eax
    jnz Failure
    mov r13,[r15]
    mov eax,20h
    syscall
    inc r13
    mov [r15],r13
    mov rdx,r14
    xor r8d,r8d
    mov eax,23h
    mov ecx,ebp
    and ecx,3
    cmp ecx,3
    jne ReleaseLock
    mov r8d,1
    mov eax,24h
ReleaseLock:
    syscall
    test eax,eax
    jnz Failure
    lock inc QWORD PTR [rbx+8]
    inc QWORD PTR gs:[8]
    dec ebp
    jnz LockLoop
    jmp ExitSuccess
ApcWorker:
    mov rdx,50000000
    mov r8d,1
    mov eax,25h
    syscall
    cmp eax,0c0h
    jne Failure
    CHECK_XMM xmm6
    fst QWORD PTR gs:[16]
    mov rax,3ff0000000000000h
    cmp QWORD PTR gs:[16],rax
    jne Failure
    mov ebx,620000h
    mov rdx,[rbx+72]
    mov r8,-1
    xor r9d,r9d
    mov eax,22h
    syscall
    test eax,eax
    jnz Failure
    jmp ExitSuccess
FaultWorker:
    mov ebx,620000h
    mov rdx,[rbx+80]
    mov r8,-1
    xor r9d,r9d
    mov eax,22h
    syscall
    ud2
Controller:
    mov ebx,620000h
    mov rdx,[rbx+80]
    mov r8,-1
    xor r9d,r9d
    mov eax,22h
    syscall
    cmp eax,80h
    jne Failure
    mov rdx,[rbx+80]
    xor r8d,r8d
    mov eax,23h
    syscall
    ; Invalid user pointers must be rejected before any read or state change.
    mov rdx,-1
    mov r8d,2
    mov r9d,1
    mov r10,-1
    mov eax,27h
    syscall
    cmp eax,0c000000dh
    jne Failure
    lea rdx,[rbx+128]
    mov r8d,2
    mov r9d,1
    mov r10,-1
    mov eax,27h
    syscall
    test eax,eax
    jnz Failure
    mov rdx,[rbx+64]
    mov r8d,1
    mov eax,24h
    syscall
    test eax,eax
    jnz Failure
    mov rdx,40000000
    xor r8d,r8d
    mov eax,25h
    syscall
    test eax,eax
    jnz Failure
    mov QWORD PTR [rbx],1
    jmp ExitSuccess
IdleTest:
    mov edx,600000h
    mov r8d,3
    mov r9d,1
    mov eax,2fh
    syscall
    test eax,eax
    jnz Failure
    mov r14,rdx
    mov r15,r8
    mov eax,29h
    syscall
    test eax,eax
    jnz Failure
    cmp edx,1
    jne Failure
    mov rdx,r14
    mov eax,2ah
    syscall
    test eax,eax
    jnz Failure
    cmp edx,2
    jne Failure
    mov rdx,r14
    mov eax,2ah
    syscall
    test eax,eax
    jnz Failure
    cmp edx,1
    jne Failure
    mov rdx,r15
    mov r8,-1
    xor r9d,r9d
    mov eax,22h
    syscall
    test eax,eax
    jnz Failure
    mov rdx,10000000
    xor r8d,r8d
    mov eax,25h
    syscall
    test eax,eax
    jnz Failure
    jmp ExitSuccess
Failure:
    cld
    mov eax,620000h
    mov QWORD PTR [rax+24],1
ExitSuccess:
    cld
    xor edx,edx
    mov eax,21h
    syscall
    ud2
ThreadIdle LABEL NEAR
    pause
    jmp ThreadIdle
ThreadBootstrap LABEL BYTE
    mov eax,30h
    syscall
    ud2
ThreadApcDispatcher LABEL BYTE
    sub rsp,40
    call rdx
    add rsp,40
    mov eax,26h
    syscall
    ud2
ThreadApcCallback LABEL BYTE
    mov eax,620000h
    add [rax+16],rcx
    pxor xmm6,xmm6
    fninit
    ret
ThreadUserEnd LABEL BYTE
PUBLIC ThreadSaveSegments,ThreadRestoreSegments
ThreadSaveSegments PROC
    mov ax,ds
    mov [rcx],ax
    mov ax,es
    mov [rcx+2],ax
    mov ax,fs
    mov [rcx+4],ax
    mov ax,gs
    mov [rcx+6],ax
    ret
ThreadSaveSegments ENDP
ThreadRestoreSegments PROC
    mov ax,[rcx]
    mov ds,ax
    mov ax,[rcx+2]
    mov es,ax
    mov ax,[rcx+4]
    mov fs,ax
    mov ax,[rcx+6]
    mov gs,ax
    ret
ThreadRestoreSegments ENDP
END
