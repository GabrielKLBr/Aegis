option casemap:none
.code
PUBLIC GuardianDesktopCall
GuardianDesktopCall PROC
    mov rdx,rcx
    mov eax,0a7h
    syscall
    ret
GuardianDesktopCall ENDP
END
