;坑爹的msvc longjmp的实现用了类似c++异常的方法，导致destructor在longjmp时会被调用，没办法只能自己实现一个了。
.386
.MODEL FLAT
.CODE
PUBLIC _popkcSetjmp
PUBLIC _popkcLongjmp

_popkcSetjmp PROC
mov ecx,[esp+4h]
mov [ecx],ebx
mov [ecx+4h],esi
mov [ecx+8h],edi
mov [ecx+0ch],ebp
lea edx,[esp+4h]
mov [ecx+10h],edx
mov edx,[esp]
mov [ecx+14h],edx
xor eax,eax
ret
_popkcSetjmp ENDP

_popkcLongjmp PROC
mov ecx,[esp+4h]
mov ebx,[ecx]
mov esi,[ecx+4h]
mov edi,[ecx+8h]
mov ebp,[ecx+0ch]
mov eax,[esp+8h]
test eax,eax
jnz bwl
inc eax
bwl:
mov esp,[ecx+10h]
mov edx,[ecx+14h]
jmp edx
_popkcLongjmp ENDP

END
