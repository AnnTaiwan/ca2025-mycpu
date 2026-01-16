// SPDX-License-Identifier: MIT
// QRCODE image program for VGA peripheral
// When QRCODE_VGA=1, allow vga to show the qrcode image.
// When USE_HARDCODED_DATA=1, uses version3 hardcoded qrcode data.

#include <stdbool.h>
#include <stdint.h>
#define QRCODE_VGA 1 // allow using vga to generate and show qrcode
#define USE_HARDCODED_DATA 0 // Enable hardcoded qrcode data to test the vga result is fine for debuging.

#if !QRCODE_VGA
// Custom memory copy for bare-metal environment (no libc)
static inline void copy_buffer(uint8_t *dest, const uint8_t *src, int n)
{
    for (int i = 0; i < n; i++)
        dest[i] = src[i];
}
#endif
// VGA MMIO register addresses (base: 0x30000000)
#define VGA_BASE 0x30000000u
#define VGA_ID (VGA_BASE + 0x00)
#define VGA_CTRL (VGA_BASE + 0x04)
#define VGA_STATUS (VGA_BASE + 0x08)
#define VGA_UPLOAD_ADDR (VGA_BASE + 0x10)
#define VGA_STREAM_DATA (VGA_BASE + 0x14)
#define VGA_PALETTE(n) (VGA_BASE + 0x20 + ((n) << 2))

#define QRCODE_VGA_SCALE 2 // means qrcode bitmap will scale to 2 times
// Animation constants
#define WIDTH 64    
#define LENGTH 64  
#define FRAME_SIZE WIDTH * LENGTH    // 64×64=4096 pixels
#define FRAME_COUNT 1     // Total animation frames
#define PIXELS_PER_WORD 8  // 8 4-bit pixels per 32-bit word
#define PALETTE_SIZE 14    // pallette color count
#define PALETTE_MAX 16     // VGA palette entries

// Opcode format constants
#define OPCODE_MASK 0xF0   // Extract opcode type
#define PARAM_MASK 0x0F    // Extract opcode parameter
#define END_OF_FRAME 0xFF  // Frame terminator

// Opcode types
#define OP_SET_COLOR 0x00        // Set current color
#define OP_SKIP_1 0x10           // Skip (1-16) - delta only
#define OP_REPEAT_1 0x20         // Repeat (1-16)
#define OP_SKIP_16 0x30          // Skip (16-256) - delta only
#define OP_REPEAT_16 0x30        // Repeat (16-256) - baseline only
#define OP_REPEAT_16_DELTA 0x40  // Repeat (16-256) - delta only
#define OP_SKIP_64 0x50          // Skip (64-1024) - delta only

// QR code color palette (6-bit RRGGBB values)
static const uint8_t qrcode_palette[PALETTE_SIZE] = {
    0x01,  //  0: Dark blue background
    0x3F,  //  1: White
    0x00,  //  2: Black
    0x3E,  //  3: Light pink/beige
    0x3B,  //  4: Pink
    0x36,  //  5: Hot pink
    0x30,  //  6: Red
    0x38,  //  7: Orange
    0x3C,  //  8: Yellow
    0x0C,  //  9: Green
    0x0B,  // 10: Light blue
    0x17,  // 11: Purple
    0x2A,  // 12: Gray
    0x3A,  // 13: Peach
};

// Include compressed frame data (delta-RLE)
#include "qrcode_data.h"

// MMIO access functions
static inline void vga_write32(uint32_t addr, uint32_t val)
{
    *(volatile uint32_t *) addr = val;
}

static inline uint32_t vga_read32(uint32_t addr)
{
    return *(volatile uint32_t *) addr;
}

// Pack 8 4-bit pixels into a 32-bit word
static inline uint32_t pack8_pixels(const uint8_t *pixels)
{
    return (uint32_t) (pixels[0] & 0xF) | ((uint32_t) (pixels[1] & 0xF) << 4) |
           ((uint32_t) (pixels[2] & 0xF) << 8) |
           ((uint32_t) (pixels[3] & 0xF) << 12) |
           ((uint32_t) (pixels[4] & 0xF) << 16) |
           ((uint32_t) (pixels[5] & 0xF) << 20) |
           ((uint32_t) (pixels[6] & 0xF) << 24) |
           ((uint32_t) (pixels[7] & 0xF) << 28);
}

// Initialize VGA palette with qrcode colors
void vga_init_palette(void)
{
    for (int i = 0; i < PALETTE_SIZE; i++) {
        vga_write32(VGA_PALETTE(i), qrcode_palette[i] & 0x3F);
    }
    // Fill remaining palette entries with black
    for (int i = PALETTE_SIZE; i < PALETTE_MAX; i++) {
        vga_write32(VGA_PALETTE(i), 0x00);
    }
}
#if QRCODE_VGA
// Frame buffers for delta decompression
static uint8_t frame_buffer[FRAME_SIZE];       // Current frame buffer
// static uint8_t prev_frame_buffer[FRAME_SIZE];  // Previous frame for delta
#endif

#if QRCODE_VGA
/* FRAME size is 64*64 pixels
    qrcode version 1: 21*21 modules, with quiet zone: 23*23
    qrcode version 2: 25*25 modules, with quiet zone: 27*27
    qrcode version 3: 29*29 modules, with quiet zone: 31*31
So, it can extend to two times:
Pixels scale:
    qrcode version 1: 42*42 pixels, with quiet zone: 46*46
    qrcode version 2: 50*50 pixels, with quiet zone: 54*54
    qrcode version 3: 58*58 pixels, with quiet zone: 62*62
*/
static inline bool qr_getdot(qr_ctx *ctx, uint x, uint y)
{
    return ctx->bmp[y] << x >> 31;
}

// Write QR code data to memory: bitmap first, then ASCII visualization
void write_qr_data_to_memory(qr_ctx *ctx, int ret_value)
{
    // Write bitmap data starting at mem[4] (0x10)
    volatile uint32_t *mem32 = (volatile uint32_t *)0x00000010;
    
    // Write return value at mem[4]
    *mem32++ = (uint32_t)ret_value;
    
    // Write QR size at mem[5]
    *mem32++ = (uint32_t)ctx->size;
    
    // Write raw bitmap data at mem[6] onwards (29 words for version 3)
    for (int y = 0; y < ctx->size; y++) {
        *mem32++ = ctx->bmp[y];
    }
    
    // Write completion marker at 0x00000090
    mem32 = (volatile uint32_t *)0x00000090;
    *mem32 = 0xDEADBEEF;
    
    // Write ASCII QR code starting at mem[64] (0x100)
    // volatile char *mem8 = (volatile char *)0x00000100;
    
    // // Write top border
    // for (int i = 0; i < ctx->size + 2; i++) {
    //     *mem8++ = '#';
    //     *mem8++ = '#';
    // }
    // *mem8++ = '\n';
    
    // // Write QR code rows with borders
    // for (int y = 0; y < ctx->size; y++) {
    //     *mem8++ = '#';
    //     *mem8++ = '#';
    //     for (int x = 0; x < ctx->size; x++) {
    //         bool is_black = qr_getdot(ctx, x, y);
    //         if (is_black) {
    //             *mem8++ = ' ';
    //             *mem8++ = ' ';
    //         } else {
    //             *mem8++ = '#';
    //             *mem8++ = '#';
    //         }
    //     }
    //     *mem8++ = '#';
    //     *mem8++ = '#';
    //     *mem8++ = '\n';
    // }
    
    // // Write bottom border
    // for (int i = 0; i < ctx->size + 2; i++) {
    //     *mem8++ = '#';
    //     *mem8++ = '#';
    // }
    // *mem8++ = '\n';
    // *mem8++ = '\0';
}



void vga_upload_frame_qrcode(int frame_index)
{
    // Set upload address to start of frame
    vga_write32(VGA_UPLOAD_ADDR, ((uint32_t) (frame_index & 0xF) << 16) | 0);

    int output_index = 0;
    uint8_t padding_color = 0; // background
    uint qr_size_with_quiet = ctx[0].size + 2;    // QR size + quiet zone (1 module each side)
    uint scaled_size = qr_size_with_quiet * QRCODE_VGA_SCALE; // Total scaled size

    // Generate 64x64 frame (iterating over frame pixels)
    for (int frame_y = 0; frame_y < LENGTH; frame_y++) {
        for (int frame_x = 0; frame_x < WIDTH; frame_x++) {
            // Map frame pixel to QR module coordinates
            int qr_x = frame_x / QRCODE_VGA_SCALE;
            int qr_y = frame_y / QRCODE_VGA_SCALE;

            uint8_t pixel_color;

            // Check if we're in the QR code area (including quiet zone)
            if (frame_x < scaled_size && frame_y < scaled_size) {
                // Quiet zone (1 module border = white)
                if (qr_x == 0 || qr_y == 0 || qr_x >= ctx[0].size + 1 || qr_y >= ctx[0].size + 1) {
                    pixel_color = 1; // White quiet zone
                } else {
                    // Inside QR code (adjust for quiet zone offset)
                    bool is_black = qr_getdot(&ctx[0], qr_x - 1, qr_y - 1);
                    pixel_color = is_black ? 2 : 1; // Black or White
                }
            } else {
                // Outside QR code area = padding
                pixel_color = padding_color;
            }

            frame_buffer[output_index++] = pixel_color;
        }
    }


    // Upload decompressed frame to VGA
    for (int i = 0; i < FRAME_SIZE; i += PIXELS_PER_WORD) {
        uint32_t packed = pack8_pixels(&frame_buffer[i]);
        vga_write32(VGA_STREAM_DATA, packed);
    }
}

// Upload hardcoded QR code to VGA (without running generation)
void vga_upload_hardcoded_qrcode(int frame_index)
{
    // Set upload address to start of frame
    vga_write32(VGA_UPLOAD_ADDR, ((uint32_t) (frame_index & 0xF) << 16) | 0);

    int output_index = 0;
    uint8_t padding_color = 0; // background
    const uint qr_size = 29;   // Hardcoded size
    uint qr_size_with_quiet = qr_size + 2;    // QR size + quiet zone (1 module each side)
    uint scaled_size = qr_size_with_quiet * QRCODE_VGA_SCALE; // Total scaled size

    // Generate 64x64 frame (iterating over frame pixels)
    for (int frame_y = 0; frame_y < LENGTH; frame_y++) {
        for (int frame_x = 0; frame_x < WIDTH; frame_x++) {
            // Map frame pixel to QR module coordinates
            int qr_x = frame_x / QRCODE_VGA_SCALE;
            int qr_y = frame_y / QRCODE_VGA_SCALE;

            uint8_t pixel_color;

            // Check if we're in the QR code area (including quiet zone)
            if (frame_x < scaled_size && frame_y < scaled_size) {
                // Quiet zone (1 module border = white)
                if (qr_x == 0 || qr_y == 0 || qr_x >= qr_size + 1 || qr_y >= qr_size + 1) {
                    pixel_color = 1; // White quiet zone
                } else {
                    // Inside QR code (adjust for quiet zone offset)
                    bool is_black = qr_getdot_hardcoded(qr_x - 1, qr_y - 1);
                    pixel_color = is_black ? 2 : 1; // Black or White
                }
            } else {
                // Outside QR code area = padding
                pixel_color = padding_color;
            }

            frame_buffer[output_index++] = pixel_color;
        }
    }

    // Upload frame to VGA
    for (int i = 0; i < FRAME_SIZE; i += PIXELS_PER_WORD) {
        uint32_t packed = pack8_pixels(&frame_buffer[i]);
        vga_write32(VGA_STREAM_DATA, packed);
    }
}
#endif
// Simple delay function (~20Hz frame rate)
// Use inline assembly to prevent compiler optimization
static inline void delay(uint32_t cycles)
{
    for (uint32_t i = 0; i < cycles; i++)
        __asm__ volatile("nop");
}

// extern int generate_qrcode_opt_v3(void);
// extern int generate_qrcode_opt_v2(void);
extern int generate_qrcode_opt(void);

#include "uperf.h" // for doing perf command
#define DO_PERF true

int main(void)
{
#if DO_PERF
    // Initialize UART
    *UART_BAUDRATE = 115200;
    *UART_ENABLE = 1;
    // Debug: Try reading without checking first
    // *UART_SEND = 'D';  // Blind write - should work if STATUS not implemented
    // *UART_SEND = 'E';
    // *UART_SEND = 'B';
    // *UART_SEND = 'U';
    // *UART_SEND = 'G';
    // *UART_SEND = '\n';
    
    // // Now check status - print the actual value
    // unsigned int status = *UART_STATUS;
    
    // // Print status value as hex (manual, no printf)
    // *UART_SEND = 'S';
    // *UART_SEND = '=';
    // const char *hex = "0123456789ABCDEF";
    // for (int i = 7; i >= 0; i--) {
    //     *UART_SEND = hex[(status >> (i * 4)) & 0xF];
    // }
    // *UART_SEND = '\n';
    
    // if ((status & 0x01) == 0) {
    //     // TX not ready - print error and halt
    //     *UART_SEND = 'E';
    //     *UART_SEND = 'R';
    //     *UART_SEND = 'R';
    //     *UART_SEND = '\n';
    //     while(1);  // Halt
    // }
    
    // *UART_SEND = 'O';
    // *UART_SEND = 'K';
    // *UART_SEND = '\n';
    unsigned long long start_cycles = ((unsigned long long)read_mcycleh() << 32) | read_mcycle();
#endif
    // generate qrcode first
#if !USE_HARDCODED_DATA
    int ret = 1;
    // ret = generate_qrcode_opt_v3();
    // ret = generate_qrcode_opt_v2();
    ret = generate_qrcode_opt();
    #if DO_PERF
        unsigned long long end_cycles = ((unsigned long long)read_mcycleh() << 32) | read_mcycle();
        show_perf_statistic(end_cycles - start_cycles);
    #endif
    // Write all QR data to memory (bitmap + ASCII)
    // write_qr_data_to_memory(&ctx[0], ret);
    if(ret < 0)
        return -1;
#endif    
    
    // Verify VGA peripheral presence
    uint32_t id = vga_read32(VGA_ID);
    if (id != 0x56474131)
        return 1;

    // Initialize palette and enable display
    vga_init_palette();
    vga_write32(VGA_CTRL, 0x01);

#if QRCODE_VGA
// Upload all frames (baseline RLE)
    for (int frame = 0; frame < FRAME_COUNT; frame++) {
        
        #if !USE_HARDCODED_DATA
            vga_upload_frame_qrcode(frame); // setting one frame data
        #else
            vga_upload_hardcoded_qrcode(frame);
        #endif
            vga_write32(VGA_CTRL,
                    (frame << 4) | 0x01);  // Display frame as we upload
    }

    // Animate: cycle through frames infinitely
    for (uint32_t frame = 0;;) {
        vga_write32(VGA_CTRL, (frame << 4) | 0x01);
        delay(100000);
        frame = (frame + 1 < FRAME_COUNT) ? frame + 1 : 0;
    }

#endif
}
