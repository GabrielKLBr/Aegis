.code
PUBLIC GuardianFaultUd,GuardianFaultDivide
GuardianFaultUd PROC
    mov eax,55h
    ud2
    ret
GuardianFaultUd ENDP
GuardianFaultDivide PROC
    mov eax,1
    xor edx,edx
    xor ecx,ecx
    div ecx
    ret
GuardianFaultDivide ENDP
END
