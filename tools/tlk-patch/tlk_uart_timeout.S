/* Copyright (C) 2026 Artem Bambalov
 * SPDX-License-Identifier: GPL-2.0-only
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License.
 */
.syntax unified
.arm
.cpu cortex-a15
.text
image_base:
.set original_putchar, image_base + 0x2a18
.set original_dputs, image_base + 0xa790
.set original_delay, image_base + 0xa730
.set original_delay_poll, image_base + 0xa748
.set original_delay_return, image_base + 0xa760
.set fuse_delay_call, image_base + 0x2b58
.set fuse_store_result, image_base + 0x2b7c
.set console_inhibit_literal, image_base + 0x2a8c
.set console_index_literal, image_base + 0x2a90
.set console_table_literal, image_base + 0x2a94
#ifdef TLK_ROUND3
.set uartd_literal, image_base + 0xecac
.set original_printf, image_base + 0xa7b0
.set original_memcpy, image_base + 0x0a6c
/* In-place replacement: same key-header arithmetic and 64 KiB TZRAM. */
.org 0x29c8
bounded_setup_keys:
    mov r3, #0x48000000
    ldr r1, [r3, #0x3a8]
    ldr r2, [r1]
    cmp r2, #0
    addne r2, r2, #7
    bicne r2, r2, #3
    moveq r2, #0x68
    add r1, r1, r2
    ldr r2, [r1], #4
    ldr r0, keys_destination_literal
    push {r0-r2, lr}
    mov r0, #'P'
    bl original_putchar
    ldr r3, keys_car_literal
    ldr r2, [r3, #0x360]
    orr r2, r2, #0x40000000
    str r2, [r3, #0x360]
    b keys_copy_tail
keys_destination_literal:
    .word 0x7c010000
keys_car_literal:
    .word 0x60006000
bounded_setup_keys_end:
#endif

/* Preserve console gates, UART selection, NUL suppression and CRLF. */
.org 0x2a18
bounded_putchar:
    ldr r3, console_inhibit_literal
    ldr r3, [r3]
    cmp r3, #0
    bxne lr
    ldr r3, console_index_literal
    ldr r3, [r3]
    cmp r3, #0
    bxeq lr
    cmp r0, #0
    bxeq lr
    push {r0, lr}
    ldr r1, console_table_literal
    ldr r1, [r1, r3, lsl #2]
    cmp r0, #10
    moveq r0, #13
    bleq bounded_tx
    ldr r0, [sp]
    bl bounded_tx
    pop {r0, pc}
/* r0 = character, r1 = UART base; no write occurs after a TX timeout. */
bounded_tx:
    mov r2, #65536
tx_poll:
    ldrb r3, [r1, #0x14]
    tst r3, #0x40
    strbne r0, [r1]
    bxne lr
    subs r2, r2, #1
    bne tx_poll
    bx lr
bounded_putchar_end:
/* These two previously unreachable instructions become the FUSE delay shim. */
fuse_delay_shim:
    mov r0, #1
    b original_delay
fuse_delay_shim_end:

/* The ONLY added storage is the audited 240-byte zero region ef10..efff. */
.org 0xef10
marker_dispatch:
#ifdef TLK_ROUND3
    push {r0-r5, ip, lr}
    mrs r4, cpsr
    ubfx ip, lr, #2, #14
    adr r2, marker_table
    mov r5, #0x48000000
marker_lookup:
    ldr r3, [r2], #4
    ubfx r1, r3, #14, #14
    cmp ip, r1
    bne marker_lookup
    mov r0, r3, lsr #28
    add r0, r0, #'A'
    ubfx r1, r3, #0, #14
    add r1, r5, r1, lsl #2
    str r1, [sp, #28]
    ldr r1, uartd_literal
    bl bounded_tx
    msr cpsr_f, r4
    add lr, r5, ip, lsl #2
    pop {r0-r5, ip, pc}
marker_table:
.macro ANCHOR character, return_pc, destination
    .word ((\character - 'A') << 28) | ((\return_pc >> 2) << 14) | (\destination >> 2)
.endm
    ANCHOR 'A', 0x4e9c, 0x8200
    ANCHOR 'B', 0x4e00, 0x4558
    ANCHOR 'I', 0x84c0, 0x8198
    ANCHOR 'L', 0x2640, 0x29c8
    ANCHOR 'M', 0x2644, 0x1a98
    ANCHOR 'N', 0x84c4, 0x2568
marker_table_end:
#elif defined(TLK_ROUND2)
    push {r0-r4, ip, lr}
    sub sp, sp, #4
    mrs r4, cpsr
    sub ip, lr, #0x48000000
    adr r2, marker_table
    mov r0, #'A'
marker_lookup:
    ldr r3, [r2], #4
    mov r1, r3, lsr #16
    cmp ip, r1
    addne r0, r0, #1
    bne marker_lookup
    uxth r1, r3
    orr r1, r1, #0x48000000
    str r1, [sp, #28]
    movw r1, #0x6300
    movt r1, #0x7000
    bl bounded_tx
    msr cpsr_f, r4
    /* bounded_tx does not touch ip; recover the original BL return PC. */
    orr lr, ip, #0x48000000
    add sp, sp, #4
    pop {r0-r4, ip}
    ldr pc, [sp], #4
#endif
#if defined(TLK_ROUND2) && !defined(TLK_ROUND3)
marker_table:
    /* High half = hook return PC; low half = displaced call target. */
    .word (0x4e9c << 16) | 0x8200
    .word (0x4e00 << 16) | 0x4558
    .word (0xc128 << 16) | 0xbffc
    .word (0xc130 << 16) | 0xbb14
    .word (0xbba0 << 16) | 0x0c84
    .word (0xbba4 << 16) | 0xc3e0
    .word (0xbbac << 16) | 0x0c5c
    .word (0xbbb0 << 16) | 0x0da4
    /* First yield after kmain becomes the priority-zero idle thread. */
    .word (0x84c0 << 16) | 0x8198
    .word (0xbbc4 << 16) | 0xb930
    .word (0xbbcc << 16) | (tail_replay - image_base)
    .word (0x2640 << 16) | 0x29c8
    .word (0x2644 << 16) | 0x1a98
    .word (0x84c4 << 16) | 0x2568
marker_table_end:
tail_replay:
    pop {r4-r7, lr}
    b image_base + 0xbbcc
#elif !defined(TLK_ROUND2)
    push {r0-r3, r4, lr}
    mrs r4, cpsr
    ldr r1, [lr]
    mov r0, r1, lsr #24
    bic r1, r1, #0xff000000
    orr r1, r1, #0x48000000
    ldr r2, [sp, #24]
    str r2, [sp, #20]
    str r1, [sp, #24]
    movw r1, #0x6300
    movt r1, #0x7000
    bl bounded_tx
    msr cpsr_f, r4
    ldr lr, [sp, #28]
    pop {r0-r4, ip}
    ldr pc, [sp], #8

.macro MARKER name, character, destination
\name:
    push {ip, lr}
    bl marker_dispatch
    .word (\destination) | ((\character) << 24)
.endm
MARKER mark_welcome, 'W', 0xa790
MARKER mark_post_welcome, 'A', 0x9064
MARKER mark_bootstrap, 'F', 0x7fe0
MARKER mark_init_complete, 'J', 0x2568
MARKER mark_fuse_begin, '1', (fuse_begin - image_base)
MARKER mark_fuse_ready, '2', (fuse_ready_replay - image_base)
#endif

/* B hooks preserve the original lr at these displaced non-BL sites. */
fuse_begin:
    mov r7, #8192
#ifndef TLK_ROUND3
    mov r0, #1
#endif
    b fuse_delay_call
#ifndef TLK_ROUND2
fuse_ready_replay:
    movw r3, #0xf000
    movt r3, #0x7000
    b image_base + 0x2b70
#endif

/* Entered only when FUSE_CTRL is not IDLE. Every iteration decrements r7. */
fuse_guard:
    subs r7, r7, #1
    bne fuse_delay_call
    ldr r0, fuse_message_literal
    bl original_dputs
    mov r3, #0
    cmp r4, #0
    b fuse_store_result
fuse_message_literal:
    .word 0x48000000 + (fuse_message - image_base)
fuse_message:
    .asciz "FUSE timeout\n"
.balign 4

/* The original delay uses r5=0 for the high-word comparison. The patch
 * changes that comparison to #0 and uses r5 as a consecutive-stall budget.
 * Its saved scratch-r3 stack slot holds the last elapsed low word. Progress
 * resets the budget, so a long healthy delay is not shortened.
 */
delay_guard:
    ldr r2, [sp]
    cmp r0, r2
    str r0, [sp]
    movne r5, #65536
    subseq r5, r5, #1
    bne original_delay_poll
    b original_delay_return
#ifdef TLK_ROUND3
/* Saved frame: {destination, source, length, LR}. */
keys_copy_tail:
    mov r0, #'Q'
    bl original_putchar
    adr r0, image_base + 0xe928
    ldr r1, [sp, #8]
    bl original_printf
    ldm sp, {r0-r2}
    cmp r2, #0x10000
    movhi r0, #'X'
    bhi keys_copy_result
    bl original_memcpy
    mov r0, #'R'
keys_copy_result:
    bl original_putchar
    pop {r0-r2, pc}
#endif
marker_dispatch_end:
