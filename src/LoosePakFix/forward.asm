EXTERN RealX3DAudioCalculate:QWORD
EXTERN RealX3DAudioInitialize:QWORD
EXTERN EnsureSystemAudio:PROC

.code
; Tail forwarding preserves the actual legacy API's return convention,
; all four register arguments (including XMM1 SpeedOfSound), and stack args.
FORWARD MACRO api, target
LOCAL ready
api PROC FRAME
    sub rsp, 88h
    .allocstack 88h
    .endprolog
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    movdqu [rsp+60h], xmm2
    movdqu [rsp+70h], xmm3
    cmp QWORD PTR [target], 0
    jne ready
    call EnsureSystemAudio
ready:
    mov rcx, [rsp+20h]
    mov rdx, [rsp+28h]
    mov r8, [rsp+30h]
    mov r9, [rsp+38h]
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    movdqu xmm2, [rsp+60h]
    movdqu xmm3, [rsp+70h]
    add rsp, 88h
    jmp QWORD PTR [target]
api ENDP
ENDM
FORWARD X3DAudioCalculate, RealX3DAudioCalculate
FORWARD X3DAudioInitialize, RealX3DAudioInitialize
END
