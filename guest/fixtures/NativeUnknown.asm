option casemap:none
.code
PUBLIC GuardianNativeUnknown
GuardianNativeUnknown PROC
    mov eax,1fffh
    syscall
    ret
GuardianNativeUnknown ENDP
PUBLIC GuardianNativeCollision
GuardianNativeCollision PROC
    mov eax,86h
    syscall
    ret
GuardianNativeCollision ENDP
END
