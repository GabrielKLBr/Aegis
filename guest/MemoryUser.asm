option casemap:none
.code
PUBLIC MemoryUserStart, MemoryUserEnd
; Only these project-owned instructions run in CPL3 in the memory tests.
MemoryUserStart LABEL BYTE
    mov rdi, 620000h
    mov ax, cs
    movzx eax, ax
    mov [rdi+8], rax
    mov rbx, [rdi+24]
    mov rdx, [rdi+32]
    cmp QWORD PTR [rdi], 1
    je MemWrite
    cmp QWORD PTR [rdi], 2
    je MemExecute
    mov rax, [rbx]
    jmp MemDone
MemWrite:
    mov [rbx], rdx
    mov rax, [rbx]
    jmp MemDone
MemExecute:
    call rbx
MemDone:
    mov [rdi+40], rax
    xor edx, edx
    mov [rdi+16], rdx
    mov eax, 3
    syscall
    ud2
MemoryUserEnd LABEL BYTE
END
