option casemap:none
.code
PUBLIC GuardianCrtMetaCall,RtlCaptureContext
GuardianCrtMetaCall PROC
    mov rdx,rcx
    mov eax,0a5h
    syscall
    ret
GuardianCrtMetaCall ENDP
RtlCaptureContext PROC
    mov [rcx+120],rax
    mov [rcx+128],rcx
    mov [rcx+136],rdx
    mov [rcx+144],rbx
    lea rax,[rsp+8]
    mov [rcx+152],rax
    mov [rcx+160],rbp
    mov [rcx+168],rsi
    mov [rcx+176],rdi
    mov [rcx+184],r8
    mov [rcx+192],r9
    mov [rcx+200],r10
    mov [rcx+208],r11
    mov [rcx+216],r12
    mov [rcx+224],r13
    mov [rcx+232],r14
    mov [rcx+240],r15
    mov rax,[rsp]
    mov [rcx+248],rax
    pushfq
    pop rax
    mov [rcx+68],eax
    mov [rcx+56],cs
    mov [rcx+58],ds
    mov [rcx+60],es
    mov [rcx+62],fs
    mov [rcx+64],gs
    mov [rcx+66],ss
    mov dword ptr [rcx+48],10000fh
    stmxcsr [rcx+52]
    fxsave64 [rcx+256]
    ret
RtlCaptureContext ENDP
END
