option casemap:none
.code
PUBLIC GuardianWin32Call
GuardianWin32Call PROC
    mov rdx,rcx
    mov eax,0a4h
    syscall
    ret
GuardianWin32Call ENDP
END
