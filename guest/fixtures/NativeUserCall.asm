option casemap:none
.code
PUBLIC GuardianNativeUserCall
GuardianNativeUserCall PROC
    mov rdx,rcx
    mov eax,0abh
    syscall
    ret
GuardianNativeUserCall ENDP
END
