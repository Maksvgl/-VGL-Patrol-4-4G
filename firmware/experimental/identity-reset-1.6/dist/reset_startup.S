
        push {r4, r5, r6, lr}
        ldr r4, =536936384
        movs r0, #0
        movs r1, #16
    clear_mailbox:
        str r0, [r4], #4
        subs r1, #1
        bne clear_mailbox
        ldr r4, =536936384
        mrs r6, basepri
        movs r0, #192
        msr basepri_max, r0
        dsb
        isb
        ldr r5, =536892271
        movs r0, #0
        strb r0, [r5]
        movs r0, #9
        bl 0x2e752
        movs r0, #3
        bl 0x2e752
        ldr r0, =536891752
        bl 0x2b86a
        str r0, [r4, #12]
        cmp r0, #0
        bne startup_failed
        ldr r0, =536891816
        bl 0x2b86a
        str r0, [r4, #12]
        cmp r0, #0
        bne startup_failed
        bl 0x2cc02
        b startup_done
    startup_failed:
        movs r0, #230
        str r0, [r4, #8]
    startup_done:
        msr basepri, r6
        dsb
        isb
        ldr r0, =827478850
        str r0, [r4, #60]
        pop {r4, r5, r6, pc}
        .align 2
        .ltorg
    