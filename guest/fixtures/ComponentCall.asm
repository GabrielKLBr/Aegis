option casemap:none
.code
PUBLIC GuardianComponentCall
GuardianComponentCall PROC
    mov rdx,rcx
    mov eax,0a8h
    syscall
    ret
GuardianComponentCall ENDP
END
