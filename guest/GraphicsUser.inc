; Two project-owned applications, executed in the isolated CPL3 page.
; The small explicit API is not the Windows USER32/GDI binary ABI.
; Command at 620100h (160 bytes), returned event follows it.
UserGraphics:
    xor r15d, r15d
    mov rsi, 620100h
    mov eax, 0a0h
    mov rdx, 100000h
    syscall
    cmp eax, 2
    jne GuiFail
    mov eax, 0a0h
    mov rdx, -32
    syscall
    cmp eax, 2
    jne GuiFail
    call GuiClear
    mov dword ptr [rsi], 1
    mov dword ptr [rsi+8], 12
    mov dword ptr [rsi+12], 12
    mov dword ptr [rsi+16], 184
    mov dword ptr [rsi+20], 132
    mov dword ptr [rsi+28], 5
    mov dword ptr [rsi+32], 4e494150h
    mov byte ptr [rsi+36], 54h
    call GuiOk
    mov r12d, [rsi+4]
    call GuiClear
    mov dword ptr [rsi], 3
    mov [rsi+4], r12d
    mov dword ptr [rsi+16], 184
    mov dword ptr [rsi+20], 132
    mov dword ptr [rsi+24], 00e1edf5h
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 4
    mov [rsi+4], r12d
    mov dword ptr [rsi+8], 8
    mov dword ptr [rsi+12], 8
    mov dword ptr [rsi+24], 00253644h
    mov dword ptr [rsi+28], 5
    mov dword ptr [rsi+32], 43494c43h
    mov byte ptr [rsi+36], 4bh
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 5
    mov [rsi+4], r12d
    mov dword ptr [rsi+8], 10
    mov dword ptr [rsi+12], 30
    mov dword ptr [rsi+16], 2
    mov dword ptr [rsi+20], 2
    mov dword ptr [rsi+28], 16
    mov dword ptr [rsi+32], 00ff0000h
    mov dword ptr [rsi+36], 0000ff00h
    mov dword ptr [rsi+40], 000000ffh
    mov dword ptr [rsi+44], 00ffffffh
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 1
    mov dword ptr [rsi+8], 126
    mov dword ptr [rsi+12], 92
    mov dword ptr [rsi+16], 180
    mov dword ptr [rsi+20], 136
    mov dword ptr [rsi+28], 5
    mov dword ptr [rsi+32], 45544f4eh
    mov byte ptr [rsi+36], 53h
    call GuiOk
    mov r13d, [rsi+4]
    call GuiClear
    mov dword ptr [rsi], 8
    mov [rsi+4], r13d
    mov dword ptr [rsi+24], 144
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 4
    mov [rsi+4], r13d
    mov dword ptr [rsi+8], 8
    mov dword ptr [rsi+12], 8
    mov dword ptr [rsi+24], 00253644h
    mov dword ptr [rsi+28], 9
    mov rax, 5245482045505954h
    mov [rsi+32], rax
    mov byte ptr [rsi+40], 45h
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 7
    mov dword ptr [rsi+24], 1
    call GuiOk
    ; Every external bridge has an independent refusal.
    xor ebx, ebx
GuiBridges:
    call GuiClear
    mov dword ptr [rsi], 11
    mov [rsi+24], ebx
    call GuiCall
    cmp eax, 3
    jne GuiFail
    inc ebx
    cmp ebx, 6
    jb GuiBridges
    call GuiClear
    mov dword ptr [rsi], 1
    mov [rsi+4], r13d
    mov dword ptr [rsi+8], 68
    mov dword ptr [rsi+12], 58
    mov dword ptr [rsi+16], 180
    mov dword ptr [rsi+20], 104
    mov dword ptr [rsi+24], 1
    mov dword ptr [rsi+28], 6
    mov dword ptr [rsi+32], 4c414944h
    mov word ptr [rsi+36], 474fh
    call GuiOk
    mov r14d, [rsi+4]
    call GuiClear
    mov dword ptr [rsi], 4
    mov [rsi+4], r14d
    mov dword ptr [rsi+8], 8
    mov dword ptr [rsi+12], 16
    mov dword ptr [rsi+24], 00253644h
    mov dword ptr [rsi+28], 11
    mov rax, 4f54205245544e45h
    mov [rsi+32], rax
    mov word ptr [rsi+40], 4f20h
    mov byte ptr [rsi+42], 4bh
    call GuiOk
    call GuiClear
    mov dword ptr [rsi], 6
    mov [rsi+4], r12d
    call GuiCall
    cmp eax, 3
    jne GuiFail
    call GuiPresent
GuiModalPoll:
    call GuiPoll
    cmp eax, 1
    je GuiModalPoll
    test eax, eax
    jnz GuiFail
    cmp dword ptr [rsi+160], 5
    jne GuiModalTarget
    mov r15d, 1
    jmp GuiDismiss
GuiModalTarget:
    cmp dword ptr [rsi+160], 1
    je GuiModalPoll
    cmp dword ptr [rsi+160], 2
    jne GuiModalKey
    cmp dword ptr [rsi+180], 1
    jne GuiModalPoll
GuiModalKey:
    mov eax, [rsi+164]
    cmp eax, r14d
    jne GuiFail
GuiDismiss:
    call GuiClear
    mov dword ptr [rsi], 2
    mov [rsi+4], r14d
    call GuiOk
    call GuiPresent
    test r15d, r15d
    jnz GuiDone
GuiLoop:
    call GuiPoll
    cmp eax, 1
    je GuiLoop
    test eax, eax
    jnz GuiFail
    cmp dword ptr [rsi+160], 5
    je GuiDone
    cmp dword ptr [rsi+160], 1
    je GuiPainted
    cmp dword ptr [rsi+160], 3
    je GuiKey
    cmp dword ptr [rsi+160], 2
    jne GuiLoop
    cmp dword ptr [rsi+180], 1
    jne GuiLoop
    mov eax, [rsi+164]
    cmp eax, r12d
    jne GuiLoop
    mov ebx, [rsi+168]
    mov ebp, [rsi+172]
    call GuiClear
    mov dword ptr [rsi], 3
    mov [rsi+4], r12d
    mov [rsi+8], ebx
    mov [rsi+12], ebp
    mov dword ptr [rsi+16], 12
    mov dword ptr [rsi+20], 12
    mov dword ptr [rsi+24], 00d04a32h
    call GuiCall
    test eax, eax
    jz GuiPainted
    cmp eax, 4
    jne GuiFail
    ; Full display list: repaint the client background before continuing.
    mov dword ptr [rsi+8], 0
    mov dword ptr [rsi+12], 0
    mov dword ptr [rsi+16], 184
    mov dword ptr [rsi+20], 132
    mov dword ptr [rsi+24], 00e1edf5h
    call GuiOk
GuiPainted:
    call GuiPresent
    jmp GuiLoop
GuiKey:
    mov ebx, [rsi+176]
    cmp ebx, 27
    je GuiDone
    cmp [rsi+164], r13d
    jne GuiLoop
    call GuiClear
    mov dword ptr [rsi], 3
    mov [rsi+4], r13d
    mov dword ptr [rsi+16], 180
    mov dword ptr [rsi+20], 136
    mov dword ptr [rsi+24], 00f5f3e8h
    call GuiOk
    cmp ebx, 32
    jb GuiPainted
    call GuiClear
    mov dword ptr [rsi], 4
    mov [rsi+4], r13d
    mov dword ptr [rsi+8], 10
    mov dword ptr [rsi+12], 20
    mov dword ptr [rsi+28], 1
    mov dword ptr [rsi+24], 00253644h
    mov [rsi+32], bl
    call GuiOk
    call GuiPresent
    jmp GuiLoop
GuiDone:
    call GuiPresent
    xor edx, edx
    mov eax, 3
    syscall
    ud2
GuiFail:
    mov edx, 1906h
    mov eax, 3
    syscall
    ud2
GuiClear:
    push rdi
    mov rdi, rsi
    xor eax, eax
    mov ecx, 24
    rep stosq
    pop rdi
    ret
GuiCall:
    mov eax, 0a0h
    mov rdx, rsi
    syscall
    ret
GuiOk:
    call GuiCall
    test eax, eax
    jnz GuiFail
    ret
GuiPoll:
    call GuiClear
    mov dword ptr [rsi], 9
    jmp GuiCall
GuiPresent:
    call GuiClear
    mov dword ptr [rsi], 10
    jmp GuiOk
