// SPDX-License-Identifier: MIT
// MyCPU is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

/**
 * Memory-Mapped I/O Register Definitions
 *
 * This header defines MMIO peripheral base addresses and register offsets
 * for the MyCPU RISC-V processor implementation.
 */

/* Video RAM base address and access pointer */
#define VRAM_BASE 0x20000000
#define VRAM ((volatile unsigned char *) VRAM_BASE)

/* Timer peripheral registers (base: 0x80000000) */
#define TIMER_BASE 0x80000000
/* +0x04: Timer limit register */
#define TIMER_LIMIT ((volatile unsigned int *) (TIMER_BASE + 4))
/* +0x08: Timer enable register */
#define TIMER_ENABLED ((volatile unsigned int *) (TIMER_BASE + 8))

/**
 * UART peripheral registers (base: 0x40000000)
 *
 * Hardware Architecture:
 *   The UART uses ready/valid handshaking internally (Chisel DecoupledIO)
 *   with BufferedTx (single-byte buffer) for transmission and Rx with
 *   interrupt signaling for reception. Status registers are NOT exposed
 *   to software via MMIO, requiring careful driver design.
 *
 * Register Map:
 *   +0x00: Reserved
 *   +0x04: UART_BAUDRATE - Baud rate configuration (read/write)
 *   +0x08: UART_ENABLE   - Enable control and interrupt clear (write)
 *   +0x0C: UART_RECV     - Receive data register (read, clears RX interrupt)
 *   +0x10: UART_SEND     - Transmit data register (write)
 *
 * TX Hardware Behavior and Limitations:
 *   - Architecture: CPU → Buffer (1 byte) → Tx (shift register) → txd pin
 *   - Buffer state: empty (ready) or full (busy)
 *   - CRITICAL LIMITATION: No TX ready status exposed to software
 *   - Write behavior: Writes to UART_SEND when buffer is full are DROPPED
 *   - Hardware does not stall writes or queue beyond single buffer
 *
 *   Software must use one of these strategies:
 *     1. Conservative pacing: Add inter-character delays (see UART_TX_DELAY)
 *     2. Slow baud rates: Ensure buffer drains faster than writes arrive
 *     3. Short messages: Verify total message fits within buffer drain time
 *     4. Hardware modification: Expose tx.io.channel.ready as status bit
 *
 *   For test environments (fast simulation, 8-byte messages), no delay needed.
 *   For production (slow baud, long messages), use UART_TX_DELAY or modify HW.
 *
 * RX Hardware Behavior and Limitations:
 *   - Architecture: rxd pin → Rx (shift register) → rxData register → interrupt
 *   - Interrupt flag set when data received, cleared when UART_RECV read
 *   - CRITICAL LIMITATION: No RX data valid flag exposed to software
 *   - Read behavior: Returns last received byte, or 0 if no data received yet
 *
 *   This creates an ambiguity problem:
 *     - Reading UART_RECV with no data: returns 0
 *     - Reading UART_RECV after receiving 0x00: returns 0
 *     - After first read, hardware clears valid flag, subsequent reads return 0
 *     - Software cannot distinguish "no data" from "valid 0x00 byte"
 *
 *   Polling mode (uart_getc):
 *     - Treats non-zero as valid data, times out on zero
 *     - CANNOT reliably receive 0x00 bytes
 *     - Suitable ONLY for ASCII text transmission (0x01-0xFF)
 *
 *   Interrupt mode (uart_rx_interrupt_handler + uart_getc_nonblocking):
 *     - ISR reads hardware immediately when interrupt fires
 *     - Software buffer stores byte (including 0x00) with separate valid flag
 *     - CAN reliably receive all byte values 0x00-0xFF
 *     - Recommended for binary data transmission
 *
 *   Hardware modification for reliable polling:
 *     Option A: Return valid bit in bit 31, data in bits [7:0]
 *     Option B: Make read block until data valid (requires bus stall support)
 *
 * Read Side Effects:
 *   - UART_RECV: Clears RX interrupt flag on read
 *   - UART_BAUDRATE: No side effects
 *   - Reading when no data available: Returns 0, no interrupt cleared
 *
 * Error Handling Strategy:
 *   - Initialize with uart_init() before any operations
 *   - Check return codes from all UART functions
 *   - Use uart_is_initialized() to verify initialization state
 *   - For ASCII text: Use polling mode (uart_getc)
 *   - For binary data: Use interrupt mode (uart_rx_interrupt_handler)
 *   - For production TX: Configure UART_TX_DELAY or use slow baud rates
 *
 * Recommended Hardware Improvements:
 *   1. Expose tx.io.channel.ready as UART_STATUS bit 0 (TX ready)
 *   2. Expose rx.io.channel.valid as UART_STATUS bit 1 (RX valid)
 *   3. Add UART_STATUS register at offset +0x00
 *   4. Make UART_RECV return 0x100 | data when valid, 0x000 when not
 *   5. Make MMIO bus stall writes when TX buffer full
 *
 * ============================================================
 * UPDATED UART IMPLEMENTATION WITH STATUS REGISTER
 * ============================================================
 *
 * The hardware has been updated to expose TX/RX status via UART_STATUS.
 * This eliminates the previous limitations and enables reliable operation.
 *
 * Register Map (UPDATED):
 *   +0x00: UART_STATUS   - Status register (read-only)
 *                          bit 0: TX ready (1 = buffer can accept data)
 *                          bit 1: RX valid (1 = data available to read)
 *                          bits 2-31: Reserved (always 0)
 *   +0x04: UART_BAUDRATE - Baud rate (read-only, compile-time constant)
 *   +0x08: UART_ENABLE   - Enable control and interrupt clear (write-only)
 *   +0x0C: UART_RECV     - Receive data register (read, clears RX interrupt)
 *   +0x10: UART_SEND     - Transmit data register (write-only)
 *
 * TX Behavior (UPDATED):
 *   - UART_STATUS bit 0 indicates TX buffer ready state
 *   - When bit 0 = 1: Buffer empty, safe to write to UART_SEND
 *   - When bit 0 = 0: Buffer full, writing to UART_SEND may drop data
 *   - Software MUST poll UART_STATUS before each write for reliability
 *   - No character dropping when STATUS polling is used correctly
 *
 *   Recommended TX pattern:
 *     while ((*UART_STATUS & 0x01) == 0) ;  // Wait for TX ready
 *     *UART_SEND = byte;                     // Safe write
 *
 * RX Behavior (UPDATED):
 *   - UART_STATUS bit 1 indicates RX data valid state
 *   - When bit 1 = 1: Valid data available in UART_RECV
 *   - When bit 1 = 0: No new data, reading UART_RECV returns stale value
 *   - Can now reliably receive ALL byte values including 0x00
 *   - Software should check UART_STATUS before reading UART_RECV
 *
 *   Recommended RX pattern:
 *     while ((*UART_STATUS & 0x02) == 0) ;  // Wait for RX valid
 *     unsigned char byte = *UART_RECV;       // Safe read
 *
 * Status Register Bit Definitions:
 *   bit 0 (0x01): TX ready
 *                 - Set when BufferedTx can accept new data
 *                 - Cleared when TX buffer is full or transmitting
 *   bit 1 (0x02): RX valid  
 *                 - Set when new data received and available
 *                 - Cleared when UART_RECV is read
 *   bits 2-31:    Reserved, always read as 0
 *
 * Benefits of STATUS Register:
 *   ✓ Eliminates dropped TX characters (no more blind writes)
 *   ✓ Reliable RX polling for all byte values (0x00-0xFF)
 *   ✓ No need for conservative timing delays
 *   ✓ Works at any baud rate with any message length
 *   ✓ Deterministic behavior for testing and debugging
 */
#define UART_BASE 0x40000000
/* +0x00: UART status */
#define UART_STATUS ((volatile unsigned int *) (UART_BASE + 0))
/* +0x04: Baud rate (R/W) */
#define UART_BAUDRATE ((volatile unsigned int *) (UART_BASE + 4))
/* +0x08: Enable/IRQ clear (W) */
#define UART_ENABLE ((volatile unsigned int *) (UART_BASE + 8))
#define UART_RECV \
    ((volatile unsigned int *) (UART_BASE + 12)) /* +0x0C: RX data (R) */
#define UART_SEND \
    ((volatile unsigned int *) (UART_BASE + 16)) /* +0x10: TX data (W) */

/**
 * UART Usage Examples
 *
 * Example 1: Simple TX (test environment, short ASCII messages)
 *
 *   *UART_BAUDRATE = 115200;
 *   *UART_ENABLE = 1;
 *   *UART_SEND = 'H';
 *   *UART_SEND = 'i';
 *   *UART_SEND = '\n';
 *
 * Example 2: RX polling mode (ASCII only, cannot receive 0x00)
 *
 *   *UART_BAUDRATE = 115200;
 *   *UART_ENABLE = 1;
 *   unsigned int data;
 *   for (int i = 0; i < 1000; i++) {
 *     data = *UART_RECV;
 *     if (data != 0) {
 *       char c = (char)(data & 0xFF);
 *       // Process received character
 *       break;
 *     }
 *   }
 *
 * Example 3: Echo server (loopback test)
 *
 *   *UART_BAUDRATE = 115200;
 *   *UART_ENABLE = 1;
 *   *UART_SEND = 'X';  // Send test character
 *   for (volatile int i = 0; i < 20; i++) ;  // Small delay
 *   unsigned int received = *UART_RECV;  // Read back (loopback)
 *   if ((received & 0xFF) == 'X') {
 *     // Loopback successful
 *   }
 *
 * For comprehensive RX implementations (polling + interrupt-driven),
 * see uart_rx.c reference implementation in csrc/ directory.
 *
 * ============================================================
 * UART_STATUS Register Usage (New Hardware Implementation)
 * ============================================================
 *
 * The UART_STATUS register provides reliable TX/RX status checking:
 *   Bit 0: TX ready (1 = buffer can accept data, 0 = buffer full)
 *   Bit 1: RX valid (1 = received data available, 0 = no data)
 *
 * Example 4: Reliable TX with UART_STATUS polling (no dropped bytes)
 *
 *   *UART_ENABLE = 1;
 *   const char *msg = "Hello World\n";
 *   while (*msg) {
 *     // Wait for TX buffer ready
 *     while ((*UART_STATUS & 0x01) == 0)
 *       ;
 *     *UART_SEND = *msg++;
 *   }
 *
 * Example 5: Reliable RX with UART_STATUS polling (works for 0x00-0xFF)
 *
 *   *UART_ENABLE = 1;
 *   unsigned char buffer[256];
 *   int count = 0;
 *   
 *   // Receive up to 256 bytes with timeout
 *   for (int timeout = 0; timeout < 10000 && count < 256; timeout++) {
 *     if (*UART_STATUS & 0x02) {  // Check RX valid
 *       buffer[count++] = (unsigned char)(*UART_RECV & 0xFF);
 *       timeout = 0;  // Reset timeout on successful read
 *     }
 *   }
 *
 * Example 6: Echo server with UART_STATUS (production quality)
 *
 *   *UART_ENABLE = 1;
 *   while (1) {
 *     // Wait for RX data available
 *     while ((*UART_STATUS & 0x02) == 0)
 *       ;
 *     unsigned char ch = (unsigned char)(*UART_RECV & 0xFF);
 *     
 *     // Wait for TX ready before echoing
 *     while ((*UART_STATUS & 0x01) == 0)
 *       ;
 *     *UART_SEND = ch;
 *   }
 *
 * Example 7: Helper function for reliable uart_putc
 *
 *   static inline void uart_putc_safe(unsigned char byte) {
 *     while ((*UART_STATUS & 0x01) == 0)  // Wait for TX ready
 *       ;
 *     *UART_SEND = byte;
 *   }
 *
 *   static inline int uart_getc_safe(unsigned char *byte) {
 *     if (*UART_STATUS & 0x02) {  // Check RX valid
 *       *byte = (unsigned char)(*UART_RECV & 0xFF);
 *       return 1;  // Success
 *     }
 *     return 0;  // No data available
 *   }
 */
