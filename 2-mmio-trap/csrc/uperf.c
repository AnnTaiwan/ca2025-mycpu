#include "uperf.h"

/* ============================================================
 * Internal Arithmetic Helpers (no M extension - no div/mod)
 * ============================================================ */

/* Unsigned division using repeated subtraction */
// static unsigned int udiv(unsigned int num, unsigned int den)
// {
//     if (den == 0)
//         return 0;

//     unsigned int quot = 0;
//     while (num >= den) {
//         num -= den;
//         quot++;
//     }
//     return quot;
// }

/* Unsigned modulo using repeated subtraction (no M extension) */
// static unsigned int umod(unsigned int num, unsigned int den)
// {
//     if (den == 0)
//         return 0;

//     while (num >= den)
//         num -= den;
//     return num;
// }
/* ============================================================
 * Internal Print Helper
 * ============================================================ */

/* Print unsigned integer in decimal (no hardware division) */
void print_uint(unsigned int val)
{
    /* Divisors for up to 10 digits (max 4,294,967,295) */
    static unsigned int divisors[] = {
        1000000000, 100000000, 10000000, 1000000, 100000,
        10000,      1000,      100,      10,      1};
    int started = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    for (int i = 0; i < 10; i++) {
        unsigned int d = divisors[i];
        int digit = 0;

        /* Repeated subtraction instead of division */
        while (val >= d) {
            val -= d;
            digit++;
        }

        if (digit > 0 || started) {
            uart_putc('0' + digit);
            started = 1;
        }
    }
}

/* Print unsigned long long (64-bit) in decimal (no hardware division) */
void print_ulong(unsigned long long val)
{
    /* Divisors for up to 20 digits (max 18,446,744,073,709,551,615) */
    static unsigned long long divisors[] = {
        10000000000000000000ULL, 1000000000000000000ULL, 100000000000000000ULL,
        10000000000000000ULL,    1000000000000000ULL,    100000000000000ULL,
        10000000000000ULL,       1000000000000ULL,       100000000000ULL,
        10000000000ULL,          1000000000ULL,          100000000ULL,
        10000000ULL,             1000000ULL,             100000ULL,
        10000ULL,                1000ULL,                100ULL,
        10ULL,                   1ULL};
    int started = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    for (int i = 0; i < 20; i++) {
        unsigned long long d = divisors[i];
        int digit = 0;

        /* Repeated subtraction instead of division */
        while (val >= d) {
            val -= d;
            digit++;
        }

        if (digit > 0 || started) {
            uart_putc('0' + digit);
            started = 1;
        }
    }
}

/* ============================================================
 * Public UART Helpers
 * ============================================================ */
/* Transmit a null-terminated string via UART */
void uart_puts(const char *s)
{
    while (*s)
        uart_putc((unsigned char) *s++);
}

/* Display performance counters: cycles, instructions, and CPI */
void do_perf(void)
{
    unsigned int cycles = read_mcycle();
    uart_puts("Performance Counters:");
    uart_puts("  Cycles:   ");
    print_uint(cycles);
    uart_puts("\r\n");
}

void show_perf_statistic(unsigned long long diff_cycles)
{
    uart_puts("Cycles:   ");
    print_ulong(diff_cycles);
    uart_puts("\r\n");
}