option casemap:none
.code
PUBLIC GuardianGuiCall
GuardianGuiCall PROC
    mov rdx,rcx
    mov eax,0a0h
    syscall
    ret
GuardianGuiCall ENDP
PUBLIC GuardianCompatibilityCall
GuardianCompatibilityCall PROC
    mov rdx,rcx
    mov eax,0a1h
    syscall
    ret
GuardianCompatibilityCall ENDP
END
