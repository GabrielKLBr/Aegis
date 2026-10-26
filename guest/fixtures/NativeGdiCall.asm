option casemap:none
.code
PUBLIC GuardianNativeGdiCall
GuardianNativeGdiCall PROC
    mov rdx,rcx
    mov eax,0ach
    syscall
    ret
GuardianNativeGdiCall ENDP
END
