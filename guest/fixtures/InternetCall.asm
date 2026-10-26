option casemap:none
.code
PUBLIC GuardianInternetCall
GuardianInternetCall PROC
    mov rdx,rcx
    mov eax,0a9h
    syscall
    ret
GuardianInternetCall ENDP
END
