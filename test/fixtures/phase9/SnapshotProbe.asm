.code
PUBLIC GuardianSnapshotProbe
PUBLIC GuardianSnapshotIrq
GuardianSnapshotProbe PROC
    mov r11,310800h
    lidt fword ptr [r11]
    add r11,16
    lgdt fword ptr [r11]
    mov ecx,0C0000100h
    mov eax,320000h
    xor edx,edx
    wrmsr
    inc ecx
    mov eax,321000h
    wrmsr
    inc ecx
    mov eax,322000h
    wrmsr
    mov r11,300000h
    mov eax,1
    cpuid
    mov [r11+224],ebx
    mov eax,0Bh
    xor ecx,ecx
    cpuid
    mov [r11+232],edx
    movdqu xmm0,xmmword ptr [SnapshotPattern]
    movdqu xmm15,xmmword ptr [SnapshotPattern]
    fld1
    mov rax,1122334455667788h
    mov rcx,2233445566778899h
    mov rdx,33445566778899AAh
    mov rbx,445566778899AABBh
    mov rbp,5566778899AABBCCh
    mov rsi,66778899AABBCCDDh
    mov rdi,778899AABBCCDDEEh
    mov r8,8899AABBCCDDEEFFh
    mov r9,99AABBCCDDEEFF00h
    mov r10,0AABBCCDDEEFF0011h
    mov r11,300000h
    mov r12,0BBCCDDEEFF001122h
    mov r13,0CCDDEEFF00112233h
    mov r14,0DDEEFF0011223344h
    mov r15,0EEFF001122334455h
    stc
    out 0EEh,al
    mov [r11],rax
    mov [r11+8],rcx
    mov [r11+16],rdx
    mov [r11+24],rbx
    mov [r11+32],rsp
    mov [r11+40],rbp
    mov [r11+48],rsi
    mov [r11+56],rdi
    mov [r11+64],r8
    mov [r11+72],r9
    mov [r11+80],r10
    mov [r11+88],r11
    mov [r11+96],r12
    mov [r11+104],r13
    mov [r11+112],r14
    mov [r11+120],r15
    pushfq
    pop qword ptr [r11+128]
    movdqu xmmword ptr [r11+144],xmm0
    movdqu xmmword ptr [r11+160],xmm15
    fstp qword ptr [r11+176]
    fstcw word ptr [r11+184]
    stmxcsr dword ptr [r11+188]
    mov eax,1
    cpuid
    mov [r11+240],ebx
    mov eax,0Bh
    xor ecx,ecx
    cpuid
    mov [r11+248],edx
    xor esi,esi
    mov rax,fs:[rsi]
    mov [r11+192],rax
    mov rax,gs:[rsi]
    mov [r11+200],rax
    swapgs
    mov rax,gs:[rsi]
    mov [r11+208],rax
    swapgs
    sti
    nop
    nop
    out 0EFh,al
    cli
    hlt
GuardianSnapshotProbe ENDP
GuardianSnapshotIrq PROC
    inc qword ptr [r11+216]
    iretq
GuardianSnapshotIrq ENDP
SnapshotPattern dq 0807060504030201h,100F0E0D0C0B0A09h
END
