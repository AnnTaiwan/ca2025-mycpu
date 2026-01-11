#include "uperf.h"
int main(void)
{
    // Initialize UART
    *UART_BAUDRATE = 115200;
    *UART_ENABLE = 1;
    uart_putc('H');
    unsigned int start_cycles = read_mcycle();
    print_uint(start_cycles);
    uart_putc('H');
    for (volatile int i = 0; i < 10000; i++)
        __asm__ volatile("nop");
    unsigned int end_cycles = read_mcycle();
    print_uint(end_cycles);
    uart_putc('H');
    print_uint(end_cycles - start_cycles);
}
