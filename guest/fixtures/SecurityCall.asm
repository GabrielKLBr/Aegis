option casemap:none
.code
PUBLIC GuardianSecurityCall
GuardianSecurityCall PROC
    mov rdx,rcx
    mov eax,0a6h
    syscall
    ret
GuardianSecurityCall ENDP
END
