.386
.model flat,c
.data
CompatRelocationAnchor DD OFFSET GuardianNativeUserCall
.code
GuardianCompatCall PROC
    mov edx,[esp+4]
    mov eax,0c1h
    int 80h
    ret
GuardianCompatCall ENDP
GuardianNativeUserCall PROC
    mov edx,[esp+4]
    mov eax,0abh
    int 80h
    ret
GuardianNativeUserCall ENDP
END
