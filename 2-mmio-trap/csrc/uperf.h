#ifndef UPERF_H
#define UPERF_H
#include "mmio.h"

/* ============================================================
 * Performance Counter Utilities for RISC-V
 * 
 * Provides functions to read performance counters and display
 * cycle count, instruction count, and CPI (Cycles Per Instruction).
 * 
 * Note: Does not use division/modulo hardware (M extension).
 * ============================================================ */

/* ============================================================
 * Inline Helper Functions (defined in header for inlining)
 * ============================================================ */

/* Transmit a single byte via UART (inline for performance) */
static inline void uart_putc(unsigned char byte)
{
    /* Wait for TX ready - prevents dropped characters */
    // while (!(*UART_STATUS & 0x01))
    //     ;  // Wait for TX buffer ready
    
    *UART_SEND = (unsigned int) byte;
}

/* Read cycle CSR (0xC00) - returns CPU cycle counter low 32 bits */
static inline unsigned int read_mcycle(void)
{
    unsigned int val;
    __asm__ volatile("csrr %0, 0xC00" : "=r"(val));
    return val;
}

/* Read cycleh CSR (0xC80) - returns CPU cycle counter high 32 bits */
static inline unsigned int read_mcycleh(void)
{
    unsigned int val;
    __asm__ volatile("csrr %0, 0xC80" : "=r"(val));
    return val;
}

/* ============================================================
 * Regular Functions (implemented in uperf.c)
 * ============================================================ */

/* Transmit a null-terminated string via UART */
void uart_puts(const char *s);

/* Print unsigned integer in decimal format (no division hardware) */
void print_uint(unsigned int val);

/* Print unsigned long long (64-bit) in decimal format (no division hardware) */
void print_ulong(unsigned long long val);

/* Display current performance counters: mcycle, minstret, and CPI */
void do_perf(void);

/* Display performance statistics for a measured code section */
void show_perf_statistic(unsigned long long diff_cycles);

#endif