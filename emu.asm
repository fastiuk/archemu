r1_less_than_r2: 
    mov r1, #20
    mov r2, #10
    cmp r1, r2
    blt r1_less_than_r2
r2_less_than_r1:
    mov r2, r1
    mov r1, #30
    cmp r2, r1
    blt r2_less_than_r1