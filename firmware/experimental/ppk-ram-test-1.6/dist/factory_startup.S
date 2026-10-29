
        push {r4, lr}
        ldr r4, =536936384
        movs r0, #0
        movs r1, #16
    clear_mailbox:
        str r0, [r4], #4
        subs r1, #1
        bne clear_mailbox
        ldr r4, =536936384
        ldr r0, =827609158
        str r0, [r4, #60]
        bl 0x28d20
        pop {r4, pc}
        .align 2
        .ltorg
    