/* Generic Cortex-M0 vector stub for link measurement, not a board BSP. */
.syntax unified
.cpu cortex-m0
.thumb
.section .isr_vector,"a",%progbits
.global xgl_boot_probe_vectors
xgl_boot_probe_vectors:
    .word __stack_top
    .word Reset_Handler
