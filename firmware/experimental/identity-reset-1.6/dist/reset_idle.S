
        push {r4, r5, r6, r7, r8, lr}
        ldr r4, =536936384
        ldr r0, [r4]
        ldr r1, =827478854
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #4]
        ldr r1, =2800752130
        cmp r0, r1
        bne normal_idle
        ldr r0, [r4, #8]
        cmp r0, #0
        bne reject_consumed_request
        mrs r7, basepri
        movs r0, #192
        msr basepri_max, r0
        dsb
        isb
        movs r0, #0
        str r0, [r4]
        str r0, [r4, #24]
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
        ldr r5, =536892271
        movs r0, #0
        strb r0, [r5]
        adr r5, reset_defaults
        movs r0, #4
        mov r1, r5
        movs r2, #23
        bl 0x28b5e
        str r0, [r4, #12]
        cmp r0, #0
        bne fail_write
        add r5, r4, #32
        movs r6, #23
        movs r0, #165
    prefill_readback:
        strb r0, [r5], #1
        subs r6, #1
        bne prefill_readback
        add r0, r4, #32
        movs r1, #4
        movs r2, #23
        bl 0x2675c
        str r0, [r4, #16]
        cmp r0, #0
        bne fail_read
        add r5, r4, #32
        adr r6, reset_defaults
        movs r8, #23
    compare_readback:
        ldrb r0, [r5], #1
        ldrb r1, [r6], #1
        cmp r0, r1
        bne fail_compare
        subs r8, #1
        bne compare_readback
        ldr r5, =536881800
        movs r6, #22
        movs r0, #48
    reset_ram_identity:
        strb r0, [r5], #1
        subs r6, #1
        bne reset_ram_identity
        movs r0, #23
        str r0, [r4, #20]
        ldr r0, =2800752130
        str r0, [r4, #24]
        movs r0, #2
        b set_status
    fail_write:
        movs r0, #225
        b set_status
    fail_read:
        movs r0, #226
        b set_status
    fail_compare:
        movs r0, #227
        b set_status
    fail_coil:
        movs r0, #228
        b set_status
    fail_saving:
        movs r0, #229
    set_status:
        str r0, [r4, #8]
        msr basepri, r7
        dsb
        isb
        b normal_idle
    reject_consumed_request:
        movs r0, #0
        str r0, [r4]
    normal_idle:
        pop {r4, r5, r6, r7, r8, lr}
        b.w 0x28158
        .align 2
    reset_defaults:
        .byte 0,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48,48
        .align 2
        .ltorg
    