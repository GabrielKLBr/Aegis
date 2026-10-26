option casemap:none
.code
PUBLIC GuardianNativeGuiCall
GuardianNativeGuiCall PROC
    mov rdx,rcx
    mov eax,0aah
    syscall
    ret
GuardianNativeGuiCall ENDP
END
