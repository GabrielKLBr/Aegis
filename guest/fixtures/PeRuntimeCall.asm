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
IFDEF GUARDIAN_CRT_RUNTIME
    mov r10,r9
ENDIF
    mov r9,r8
    mov r8,rdx
    mov rdx,rcx
    mov eax,87h
    syscall
    ud2
GuardianUnwindCall ENDP
IFDEF GUARDIAN_CRT_RUNTIME
PUBLIC GuardianCrtConsolidateReply
GuardianCrtConsolidateReply PROC
    mov r8,rdx
    mov rdx,rcx
    mov eax,8ah
    syscall
    ud2
GuardianCrtConsolidateReply ENDP
ENDIF
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
