option casemap:none
.code
PUBLIC GuardianNativeCall
GuardianNativeCall PROC
    mov rdx,rcx
    mov eax,0a2h
    syscall
    ret
GuardianNativeCall ENDP
END
