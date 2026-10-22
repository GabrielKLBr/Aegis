; Native LLVM's MSVC-compatible header declares this GS intrinsic but does
; not lower it. Same instruction used by MSVC, no dependency on a CRT.
.code
__outbyte PROC
    mov al, dl
    mov edx, ecx
    out dx, al
    ret
__outbyte ENDP
__outdword PROC
    mov eax, edx
    mov edx, ecx
    out dx, eax
    ret
__outdword ENDP
__indword PROC
    mov edx, ecx
    in eax, dx
    ret
__indword ENDP
__writegsdword PROC
    mov ecx, ecx
    mov DWORD PTR gs:[rcx], edx
    ret
__writegsdword ENDP
__writegsqword PROC
    mov ecx, ecx
    mov QWORD PTR gs:[rcx], rdx
    ret
__writegsqword ENDP
__readcr0 PROC
    mov rax, cr0
    ret
__readcr0 ENDP
__writecr0 PROC
    mov cr0, rcx
    ret
__writecr0 ENDP
__readcr2 PROC
    mov rax, cr2
    ret
__readcr2 ENDP
GuardianReadCr2 PROC
    mov rax, cr2
    ret
GuardianReadCr2 ENDP
__readcr3 PROC
    mov rax, cr3
    ret
__readcr3 ENDP
__writecr3 PROC
    mov cr3, rcx
    ret
__writecr3 ENDP
__readcr4 PROC
    mov rax, cr4
    ret
__readcr4 ENDP
__writecr4 PROC
    mov cr4, rcx
    ret
__writecr4 ENDP
__readmsr PROC
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
__readmsr ENDP
__writemsr PROC
    mov rax, rdx
    shr rdx, 32
    wrmsr
    ret
__writemsr ENDP
_enable PROC
    sti
    ret
_enable ENDP
_disable PROC
    cli
    ret
_disable ENDP
__invlpg PROC
    invlpg BYTE PTR [rcx]
    ret
__invlpg ENDP
END
