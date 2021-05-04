r1ltr2  MOV r1, #20
        MOV r2, #10
        CMP r1, r2
        BLT r1ltr2
r2ltr1  MOV r2, r1
        MOV r1, #30
        CMP r2, r1
        BLT r2ltr1
        MOV r0, #0x20
        MOV r3, #0x30
        CMP r0, r3
halt    BLT halt