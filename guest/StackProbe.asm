option casemap:none
.code
; MSVC x64 stack-probe ABI: RAX is the requested frame size. Preserve RAX/RCX
; and touch each intervening page before the compiler adjusts RSP. A guard
; failure is real; this is deliberately not a dummy CRT replacement.
PUBLIC __chkstk
__chkstk PROC
    push rcx
    push rax
    lea rcx,[rsp+24]
ProbePages:
    cmp rax,1000h
    jb ProbeTail
    sub rcx,1000h
    test BYTE PTR [rcx],0
    sub rax,1000h
    jmp ProbePages
ProbeTail:
    sub rcx,rax
    test BYTE PTR [rcx],0
    pop rax
    pop rcx
    ret
__chkstk ENDP
END
