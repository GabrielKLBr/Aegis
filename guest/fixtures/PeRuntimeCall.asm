option casemap:none
.code
PUBLIC GuardianRuntimeCall
GuardianRuntimeCall PROC
    mov rdx,rcx
    mov eax,81h
    syscall
    ret
GuardianRuntimeCall ENDP
PUBLIC GuardianExceptionReply,GuardianUnwindCall,GuardianRaiseCall,GuardianLoaderEnter,GuardianLoaderLeave
GuardianExceptionReply PROC
    mov r9,r8
    mov r8,rdx
    mov rdx,rcx
    mov eax,86h
    syscall
    ud2
GuardianExceptionReply ENDP
GuardianUnwindCall PROC
    mov r9,r8
    mov r8,rdx
    mov rdx,rcx
    mov eax,87h
    syscall
    ud2
GuardianUnwindCall ENDP
GuardianRaiseCall PROC
    mov rdx,rcx
    mov eax,88h
    syscall
    ret
GuardianRaiseCall ENDP
GuardianLoaderEnter PROC
    mov eax,83h
    syscall
    ret
GuardianLoaderEnter ENDP
GuardianLoaderLeave PROC
    mov eax,84h
    syscall
    ret
GuardianLoaderLeave ENDP
END
