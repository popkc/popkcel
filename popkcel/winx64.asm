.CODE

PUBLIC popkcSetjmp
PUBLIC popkcLongjmp

popkcSetjmp PROC
mov [rcx],rbx
mov [rcx+8h],rsi
mov [rcx+10h],rdi
mov [rcx+18h],rbp
mov [rcx+20h],r12
mov [rcx+28h],r13
mov [rcx+30h],r14
mov [rcx+38h],r15
movdqa [rcx+40h],xmm6
movdqa [rcx+50h],xmm7
movdqa [rcx+60h],xmm8
movdqa [rcx+70h],xmm9
movdqa [rcx+80h],xmm10
movdqa [rcx+90h],xmm11
movdqa [rcx+0a0h],xmm12
movdqa [rcx+0b0h],xmm13
movdqa [rcx+0c0h],xmm14
movdqa [rcx+0d0h],xmm15
mov rdx,[rsp]
mov [rcx+0e0h],rdx
lea rdx,[rsp+8h]
mov [rcx+0e8h],rdx
xor rax,rax
ret
popkcSetjmp ENDP

popkcLongjmp PROC
mov rax,rdx
test rax,rax
jnz bwl
inc rax
bwl:
mov rbx,[rcx]
mov rsi,[rcx+8h]
mov rdi,[rcx+10h]
mov rbp,[rcx+18h]
mov r12,[rcx+20h]
mov r13,[rcx+28h]
mov r14,[rcx+30h]
mov r15,[rcx+38h]
movdqa xmm6,[rcx+40h]
movdqa xmm7,[rcx+50h]
movdqa xmm8,[rcx+60h]
movdqa xmm9,[rcx+70h]
movdqa xmm10,[rcx+80h]
movdqa xmm11,[rcx+90h]
movdqa xmm12,[rcx+0a0h]
movdqa xmm13,[rcx+0b0h]
movdqa xmm14,[rcx+0c0h]
movdqa xmm15,[rcx+0d0h]
mov rdx,[rcx+0e0h]
mov rsp,[rcx+0e8h]
jmp rdx
popkcLongjmp ENDP

END