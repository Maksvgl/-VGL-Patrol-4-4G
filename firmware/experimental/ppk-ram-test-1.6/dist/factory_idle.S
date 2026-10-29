
        push {r4, r5, r6, lr}
        sub sp, #8
        ldr r4, =536936384
        ldr r0, [r4]
        ldr r1, =827607888
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #4]
        ldr r1, =2800752129
        cmp r0, r1
        bne normal_idle

        movs r0, #0
        str r0, [r4]
        str r0, [r4, #40]
        str r0, [r4, #20]
        movs r0, #1
        str r0, [r4, #8]
        movs r0, #0
        mvns r0, r0
        str r0, [r4, #12]
        str r0, [r4, #16]

        ldr r5, =536892268
        ldrb r0, [r5]
        cmp r0, #0
        bne fail_coil
        ldr r5, =536892272
        ldrb r0, [r5]
        cmp r0, #0
        bne fail_saving

        ldrb r0, [r4, #32]
        cmp r0, #59
        bhi fail_date
        ldrb r0, [r4, #33]
        cmp r0, #59
        bhi fail_date
        ldrb r0, [r4, #34]
        cmp r0, #23
        bhi fail_date
        ldrb r0, [r4, #35]
        cmp r0, #1
        blo fail_date
        cmp r0, #31
        bhi fail_date
        ldrb r0, [r4, #36]
        cmp r0, #1
        blo fail_date
        cmp r0, #12
        bhi fail_date
        ldrb r0, [r4, #37]
        cmp r0, #99
        bhi fail_date

        ldrb r0, [r4, #36]
        str r0, [sp]
        ldrb r0, [r4, #37]
        str r0, [sp, #4]
        ldrb r0, [r4, #32]
        ldrb r1, [r4, #33]
        ldrb r2, [r4, #34]
        ldrb r3, [r4, #35]
        bl 0x267fa
        str r0, [r4, #12]
        cmp r0, #0
        bne fail_write

        add r0, r4, #28
        str r0, [sp]
        add r0, r4, #29
        str r0, [sp, #4]
        add r0, r4, #24
        add r1, r4, #25
        add r2, r4, #26
        add r3, r4, #27
        bl 0x26a72
        str r0, [r4, #16]
        cmp r0, #0
        bne fail_read

        ldr r5, =536892271
        movs r0, #1
        strb r0, [r5]
        ldr r5, =536892192
        movw r0, #45875
        str r0, [r5]
        str r0, [r4, #20]
        bl 0x28d20
        ldr r0, =2800752129
        str r0, [r4, #40]
        movs r0, #2
        b set_status
    fail_write:
        movs r0, #225
        b set_status
    fail_read:
        movs r0, #226
        b set_status
    fail_coil:
        movs r0, #227
        b set_status
    fail_saving:
        movs r0, #228
        b set_status
    fail_date:
        movs r0, #229
    set_status:
        str r0, [r4, #8]
    normal_idle:
        add sp, #8
        pop {r4, r5, r6, lr}
        b.w 0x28158
        .align 2
        .ltorg
    