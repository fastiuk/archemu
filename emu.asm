r1ltr2  mov r1, #20
        mov r2, #10
        cmp r1, r2
        blt r1ltr2
r2ltr1  mov r2, r1
        mov r1, #30
        cmp r2, r1
        blt r2ltr1
        mov r0, #0x20
        mov r3, #0x30
        cmp r0, r3
halt    blt halt