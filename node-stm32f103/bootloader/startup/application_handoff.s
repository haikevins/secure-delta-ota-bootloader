.syntax unified
.cpu cortex-m3
.fpu softvfp
.thumb

.section .text.ApplicationJump_SetStackAndBranch,"ax",%progbits
.global ApplicationJump_SetStackAndBranch
.type ApplicationJump_SetStackAndBranch, %function
.thumb_func
ApplicationJump_SetStackAndBranch:
    /* r0 = application initial MSP, r1 = application reset handler. */
    msr     msp, r0
    movs    r2, #0
    msr     psp, r2
    msr     control, r2
    msr     basepri, r2
    msr     faultmask, r2
    msr     primask, r2
    dsb
    isb
    bx      r1
.size ApplicationJump_SetStackAndBranch, .-ApplicationJump_SetStackAndBranch
