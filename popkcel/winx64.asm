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
movdqu [rcx+40h],xmm6
movdqu [rcx+50h],xmm7
movdqu [rcx+60h],xmm8
movdqu [rcx+70h],xmm9
movdqu [rcx+80h],xmm10
movdqu [rcx+90h],xmm11
movdqu [rcx+0a0h],xmm12
movdqu [rcx+0b0h],xmm13
movdqu [rcx+0c0h],xmm14
movdqu [rcx+0d0h],xmm15
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
movdqu xmm6,[rcx+40h]
movdqu xmm7,[rcx+50h]
movdqu xmm8,[rcx+60h]
movdqu xmm9,[rcx+70h]
movdqu xmm10,[rcx+80h]
movdqu xmm11,[rcx+90h]
movdqu xmm12,[rcx+0a0h]
movdqu xmm13,[rcx+0b0h]
movdqu xmm14,[rcx+0c0h]
movdqu xmm15,[rcx+0d0h]
mov rdx,[rcx+0e0h]
mov rsp,[rcx+0e8h]
jmp rdx
popkcLongjmp ENDP

END
