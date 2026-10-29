option casemap:none
.code
PUBLIC ProcessUserStart,ProcessUserEnd,ProcessIdle,ProcessBootstrap
EXPECT MACRO value,number
    cmp eax,value
    mov ebp,number
    jne ProcessFailure
ENDM
ProcessUserStart LABEL BYTE
    mov ebx,620000h
    mov r12,rdi
    movq xmm6,r12
    mov r13,[rbx]
    mov rax,[r13]
    cmp rax,[rbx+8]
    mov ebp,1
    jne ProcessFailure
    cmp QWORD PTR gs:[0],r12
    mov ebp,2
    jne ProcessFailure
    cmp rsi,1
    je Adversary
    cmp rsi,2
    je Child
    cmp rsi,3
    je OtherBadThread
Parent:
IFDEF GUARDIAN_STORAGE_KERNEL
    call StorageExercise
ENDIF
IFDEF GUARDIAN_IO_KERNEL
    call IoExercise
ENDIF
    xor edx,edx
    mov r8d,10000000h
    xor r9d,r9d
    mov eax,52h
    syscall
    EXPECT 0,33
    mov eax,48h
    syscall
    EXPECT 0,34
    mov rdx,[rbx+16]
    mov eax,4bh
    syscall
    EXPECT 0,3
    mov r14,rdx
    mov r15,r8
    mov rdx,40000000
    mov eax,50h
    syscall
    EXPECT 0,4
    mov rdx,r15
    mov r8,r13
    lea r9,[r13+8]
    mov r10d,8
    mov eax,46h
    syscall
    EXPECT 0,5
    cmp QWORD PTR [r13+8],3333h
    mov ebp,6
    jne ProcessFailure
    mov QWORD PTR [r13+16],0aaaah
    mov rdx,r15
    mov r8,r13
    lea r9,[r13+16]
    mov r10d,8
    mov eax,47h
    syscall
    EXPECT 0,7
    mov rdx,[rbx+24]
    mov eax,44h
    syscall
    EXPECT 0,8
    mov rdx,r15
    mov r8,-1
    mov eax,45h
    syscall
    EXPECT 0,9
    mov rdx,r15
    mov eax,4dh
    syscall
    EXPECT 0,10
    test rdx,rdx
    mov ebp,11
    jnz ProcessFailure
    mov rdx,r15
    mov eax,48h
    syscall
    EXPECT 0,12
    cmp QWORD PTR [r13],1111h
    mov ebp,13
    jne ProcessFailure
WaitForTicks:
    mov eax,51h
    syscall
    cmp rdx,4
    jae ProcessSuccess
    mov rdx,5000000
    mov eax,50h
    syscall
    EXPECT 0,32
    jmp WaitForTicks
Adversary:
    mov rdx,[rbx+32]
    mov r8d,16
    mov eax,42h
    syscall
    EXPECT 0c0000022h,14
    mov rdx,[rbx+24]
    mov eax,48h
    syscall
    EXPECT 0c0000008h,15
    lea rdx,[rbx+100h]
    mov r8d,3
    mov r9d,1
    mov eax,43h
    syscall
    EXPECT 0,16
    mov r14,rdx
    mov eax,44h
    syscall
    EXPECT 0c0000022h,17
    mov rdx,15000000
    mov eax,50h
    syscall
    EXPECT 0,18
    ; The page-table supervisor check must fault in the guest. Both threads
    ; belonging to this process are terminated; parent and child continue.
    mov eax,100000h
    mov rax,[rax]
    mov ebp,19
    jmp ProcessFailure
OtherBadThread:
    inc QWORD PTR [rbx+88]
    cmp QWORD PTR [r13],2222h
    mov ebp,20
    jne ProcessFailure
    movq rax,xmm6
    cmp rax,r12
    mov ebp,21
    jne ProcessFailure
    jmp OtherBadThread
Child:
    mov eax,4ch
    syscall
    EXPECT 0,22
    cmp edx,100
    mov ebp,23
    jne ProcessFailure
    cmp r8d,1
    mov ebp,24
    jne ProcessFailure
    test r9d,r9d
    mov ebp,25
    jnz ProcessFailure
    mov rdx,[rbx+24]
    mov eax,49h
    syscall
    EXPECT 0c0000008h,26
    mov rdx,[rbx+16]
    mov eax,44h
    syscall
    EXPECT 0c0000022h,27
    mov rdx,[rbx+32]
    mov r8d,32
    mov eax,42h
    syscall
    EXPECT 0c0000022h,28
    mov rdx,[rbx+16]
    mov r8,-1
    mov eax,45h
    syscall
    EXPECT 0,29
    cmp QWORD PTR [r13],0aaaah
    mov ebp,30
    jne ProcessFailure
    movq rax,xmm6
    cmp rax,r12
    mov ebp,31
    jne ProcessFailure
ProcessSuccess:
    xor edx,edx
    mov eax,41h
    syscall
    ud2
ProcessFailure:
    mov edx,ebp
    mov eax,41h
    syscall
    ud2
ProcessIdle LABEL NEAR
    pause
    jmp ProcessIdle
ProcessBootstrap LABEL BYTE
    mov eax,60h
    syscall
    ud2
IFDEF GUARDIAN_NATIVE_NT
PUBLIC ProcessNativeApc
ProcessNativeApc LABEL BYTE
    sub rsp,28h
    call r9
    mov eax,0a3h
    syscall
    ud2
ENDIF
IFDEF GUARDIAN_IO_KERNEL
IoComplete:
    mov rdx,r15
    mov eax,77h
    syscall
    EXPECT 0,100
    mov rdx,r15
    mov eax,72h
    syscall
    EXPECT 0,101
    ret
IoExercise:
    mov eax,76h
    syscall
    EXPECT 0,102
    cmp edx,3
    mov ebp,103
    jne ProcessFailure
    mov edx,3
    mov r8d,10000000h
    mov eax,70h
    syscall
    EXPECT 0,104
    mov r14,rdx
    mov eax,74h
    syscall
    EXPECT 0c000009eh,105
    mov QWORD PTR [rbx+400h],11223344h
    mov BYTE PTR [rbx+5ffh],5ah
    mov rdx,r14
    mov r8d,2
    lea r9,[rbx+400h]
    mov r10d,512
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 103h,106
    mov r15,rdx
    mov QWORD PTR [rbx+400h],0badch
    mov rdx,r14
    mov eax,75h
    syscall
    EXPECT 0,107
    xor r8d,r8d
    xor r9d,r9d
    call IoComplete
    mov edx,3
    mov r8d,18
    mov eax,70h
    syscall
    EXPECT 0,108
    mov r14,rdx
    mov r8d,2
    lea r9,[rbx+400h]
    mov r10d,512
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 0c0000022h,109
    mov rdx,r14
    mov r8d,1
    xor r9d,r9d
    mov r10d,512
    mov edi,10000h
    mov eax,71h
    syscall
    EXPECT 0c000000dh,110
    mov rdx,r14
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 103h,111
    mov r15,rdx
    mov eax,77h
    syscall
    EXPECT 0,112
    mov rdx,r15
    mov r8d,100000h
    mov r9d,512
    mov eax,72h
    syscall
    EXPECT 0c0000022h,113
    mov rdx,r15
    lea r8,[rbx+400h]
    mov r9d,512
    mov eax,72h
    syscall
    EXPECT 0,114
    cmp QWORD PTR [rbx+400h],11223344h
    mov ebp,115
    jne ProcessFailure
    cmp BYTE PTR [rbx+5ffh],5ah
    mov ebp,116
    jne ProcessFailure
    mov rdx,r14
    mov eax,75h
    syscall
    EXPECT 0,117
    mov edx,2
    mov r8d,10000000h
    mov eax,70h
    syscall
    EXPECT 0,118
    mov r14,rdx
    mov r8d,2
    lea r9,[rbx+400h]
    mov r10d,8
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 103h,119
    mov r15,rdx
    xor r8d,r8d
    xor r9d,r9d
    call IoComplete
    mov QWORD PTR [rbx+400h],0
    mov rdx,r14
    mov r8d,1
    xor r9d,r9d
    mov r10d,512
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 103h,120
    mov r15,rdx
    lea r8,[rbx+400h]
    mov r9d,512
    call IoComplete
    cmp edx,8
    mov ebp,121
    jne ProcessFailure
    cmp QWORD PTR [rbx+400h],11223344h
    mov ebp,122
    jne ProcessFailure
    mov rdx,r14
    mov eax,75h
    syscall
    EXPECT 0,123
    mov edx,1
    mov r8d,34
    mov eax,70h
    syscall
    EXPECT 0,124
    mov r14,rdx
    mov DWORD PTR [rbx+700h],4f204f49h
    mov WORD PTR [rbx+704h],0a4bh
    mov r8d,2
    lea r9,[rbx+700h]
    mov r10d,6
    xor edi,edi
    mov eax,71h
    syscall
    EXPECT 103h,125
    mov r15,rdx
    xor r8d,r8d
    xor r9d,r9d
    call IoComplete
    mov rdx,r14
    mov eax,75h
    syscall
    EXPECT 0,126
    mov edx,3
    mov r8d,10000000h
    mov eax,70h
    syscall
    EXPECT 0,127
    mov r14,rdx
    mov esi,16
IoCancelRetry:
    mov rdx,r14
    mov r8d,2
    lea r9,[rbx+400h]
    mov r10d,512
    mov edi,512
    mov eax,71h
    syscall
    EXPECT 103h,128
    mov r15,rdx
    mov eax,73h
    syscall
    test eax,eax
    jz IoCancelled
    EXPECT 0c000010bh,129
    mov rdx,r15
    xor r8d,r8d
    xor r9d,r9d
    mov eax,72h
    syscall
    EXPECT 0,130
    dec esi
    jnz IoCancelRetry
    mov ebp,131
    jmp ProcessFailure
IoCancelled:
    mov rdx,r15
    xor r8d,r8d
    xor r9d,r9d
    mov eax,72h
    syscall
    EXPECT 0c0000120h,132
    mov rdx,r14
    mov eax,75h
    syscall
    EXPECT 0,133
    mov rdi,r12
    ret
ENDIF
IFDEF GUARDIAN_STORAGE_KERNEL
FSRESET MACRO operation
    lea rdi,[rbx+800h]
    mov rsi,rdi
    xor eax,eax
    mov ecx,110
    cld
    rep stosq
    mov DWORD PTR [rsi],1
    mov DWORD PTR [rsi+4],880
    mov DWORD PTR [rsi+8],operation
ENDM
FSPATH MACRO
    mov rax,2e726573755c3a43h
    mov [rsi+80],rax
    mov DWORD PTR [rsi+88],006e6962h
ENDM
FSCALL MACRO number
    mov rdx,rsi
    mov eax,80h
    syscall
    EXPECT 0,number
ENDM
StorageExercise:
    FSRESET 2
    FSPATH
    mov DWORD PTR [rsi+32],48
    mov DWORD PTR [rsi+36],7
    mov DWORD PTR [rsi+40],2
    FSCALL 200
    mov r14,[rsi+16]
    FSRESET 4
    mov [rsi+16],r14
    mov DWORD PTR [rsi+52],4
    mov DWORD PTR [rsi+368],0a1b2c3d4h
    FSCALL 201
    FSRESET 3
    mov [rsi+16],r14
    mov DWORD PTR [rsi+52],512
    FSCALL 202
    cmp DWORD PTR [rsi+60],4
    mov ebp,203
    jne ProcessFailure
    cmp DWORD PTR [rsi+368],0a1b2c3d4h
    mov ebp,204
    jne ProcessFailure
    FSRESET 5
    mov [rsi+16],r14
    FSCALL 205
    FSRESET 11
    mov DWORD PTR [rsi+80],55434b48h
    FSCALL 206
    FSRESET 12
    mov DWORD PTR [rsi+80],55434b48h
    mov DWORD PTR [rsi+336],72657375h
    mov DWORD PTR [rsi+44],4
    mov DWORD PTR [rsi+52],4
    mov DWORD PTR [rsi+368],77h
    FSCALL 207
    FSRESET 13
    mov DWORD PTR [rsi+80],55434b48h
    mov DWORD PTR [rsi+336],72657375h
    mov DWORD PTR [rsi+52],512
    FSCALL 208
    cmp DWORD PTR [rsi+368],77h
    mov ebp,209
    jne ProcessFailure
    mov edx,100000h
    mov eax,80h
    syscall
    EXPECT 0c0000022h,210
    FSRESET 22
    mov DWORD PTR [rsi+80],005c3a43h
    FSCALL 211
    FSRESET 10
    FSCALL 212
    mov rdi,r12
    ret
ENDIF
IFDEF GUARDIAN_PE_KERNEL
PUBLIC PeProcessBootstrap
PeProcessBootstrap LABEL BYTE
    mov r12,rcx
    sub rsp,28h
IFDEF GUARDIAN_COMPAT32
    mov eax,0c0h
    syscall
    test eax,eax
    jz PeLoaderFailed
    sub rsp,8
    mov DWORD PTR [rsp],0
    mov DWORD PTR [rsp+4],r12d
    mov dx,43h
    mov fs,dx
    push 1bh
    push rax
    retfq
ENDIF
    xor ebx,ebx
IFDEF GUARDIAN_RUNTIME_KERNEL
    mov eax,83h
    syscall
    test eax,eax
    jnz PeLoaderFailed
ENDIF
PeAttach:
    cmp rbx,[r12+8]
    jae PeMain
    imul r13,rbx,24
    mov rcx,[r12+r13+24]
    mov edx,1
    xor r8d,r8d
    call QWORD PTR [r12+r13+16]
    cmp QWORD PTR [r12+r13+32],0
    je PeAttached
    test eax,eax
    jz PeAttachFailed
PeAttached:
    inc ebx
    jmp PeAttach
PeMain:
IFDEF GUARDIAN_RUNTIME_KERNEL
    mov eax,84h
    syscall
    test eax,eax
    jnz PeLoaderFailed
ENDIF
    call QWORD PTR [r12]
IFDEF GUARDIAN_LIFETIME_KERNEL
    mov edx,eax
    mov rcx,r12
    mov rsp,QWORD PTR gs:[8]
    sub rsp,28h
    jmp PeProcessExitEntry
ENDIF
    mov r14d,eax
IFDEF GUARDIAN_RUNTIME_KERNEL
    mov eax,83h
    syscall
    test eax,eax
    jnz PeLoaderFailed
ENDIF
PeDetach:
    test ebx,ebx
    jz PeFinished
    dec ebx
    imul r13,rbx,24
    mov rcx,[r12+r13+24]
    xor edx,edx
    xor r8d,r8d
    call QWORD PTR [r12+r13+16]
    jmp PeDetach
PeAttachFailed:
IFDEF GUARDIAN_SCALABLE_PE
    mov rcx,[r12+r13+24]
    xor edx,edx
    xor r8d,r8d
    call QWORD PTR [r12+r13+16]
ENDIF
    mov r14d,3
    jmp PeDetach
PeFinished:
IFDEF GUARDIAN_RUNTIME_KERNEL
    mov eax,84h
    syscall
    test eax,eax
    jnz PeLoaderFailed
ENDIF
    mov edx,r14d
    mov eax,41h
    syscall
    ud2
IFDEF GUARDIAN_RUNTIME_KERNEL
PeLoaderFailed:
    mov edx,0feh
    mov eax,41h
    syscall
    ud2
PUBLIC PeThreadBootstrap
PeThreadBootstrap LABEL BYTE
    mov r12,rcx
    sub rsp,28h
    mov eax,83h
    syscall
    test eax,eax
    jnz PeLoaderFailed
IFDEF GUARDIAN_COMPLETE_KERNEL
    mov rdx,r12
    mov eax,89h
    syscall
    test eax,eax
    jnz PeLoaderFailed
ENDIF
    xor ebx,ebx
PeThreadAttach:
    cmp rbx,[r12+8]
    jae PeThreadMain
    imul r13,rbx,24
    mov rcx,[r12+r13+24]
    mov edx,2
    xor r8d,r8d
    call QWORD PTR [r12+r13+16]
    inc ebx
    jmp PeThreadAttach
PeThreadMain:
    mov eax,84h
    syscall
    test eax,eax
    jnz PeLoaderFailed
IFDEF GUARDIAN_SCALABLE_PE
    mov rcx,[r12+3472]
ELSE
IFDEF GUARDIAN_COMPLETE_KERNEL
    mov rcx,[r12+304]
ELSE
    mov rcx,[r12+112]
ENDIF
ENDIF
    call QWORD PTR [r12]
    mov r14d,eax
    jmp PeThreadTakeExitLock
IFDEF GUARDIAN_LIFETIME_KERNEL
PUBLIC PeThreadExitEntry
PeThreadExitEntry:
    mov r12,rcx
    mov r14d,edx
    sub rsp,28h
    mov rbx,[r12+8]
ENDIF
PeThreadTakeExitLock:
    mov eax,83h
    syscall
    test eax,eax
    jnz PeLoaderFailed
IFDEF GUARDIAN_COMPLETE_KERNEL
    mov rdx,r12
    mov eax,89h
    syscall
    test eax,eax
    jnz PeLoaderFailed
    mov rbx,[r12+8]
ENDIF
PeThreadDetach:
    test ebx,ebx
    jz PeThreadFinished
    dec ebx
    imul r13,rbx,24
    mov rcx,[r12+r13+24]
    mov edx,3
    xor r8d,r8d
    call QWORD PTR [r12+r13+16]
    jmp PeThreadDetach
PeThreadFinished:
    mov eax,84h
    syscall
    test eax,eax
    jnz PeLoaderFailed
    mov edx,r14d
    mov eax,82h
    syscall
    ud2
IFDEF GUARDIAN_LIFETIME_KERNEL
PUBLIC PeProcessExitEntry
PeProcessExitEntry:
    mov r12,rcx
    mov r14d,edx
    sub rsp,28h
    mov rbx,[r12+8]
    mov eax,83h
    syscall
    test eax,eax
    jnz PeLoaderFailed
    mov edx,r14d
    mov eax,85h
    syscall
    test eax,eax
    jnz PeLoaderFailed
PeProcessTerminating:
    test ebx,ebx
    jz PeFinished
    dec ebx
    imul r13,rbx,24
    mov rcx,[r12+r13+24]
    xor edx,edx
    mov r8d,1
    call QWORD PTR [r12+r13+16]
    jmp PeProcessTerminating
ENDIF
ENDIF
ENDIF
ProcessUserEnd LABEL BYTE
END
