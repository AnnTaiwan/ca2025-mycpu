/*
 * QR123: minimal fast QR encoder for version 1, 2, 3.
 *
 * Copyright (c) 2019 Ling LI <lix2ng@gmail.com>.
 * Copyright (c) 2026 An Chou <ann20021007@gmail.com>.
 *
 * Modified by An Chou, 2026:
 * - Optimized _rs_mul into RV32I assembly code.
 * - Removed M-extension instructions (*, /, %)
 * - Optimized _reed_solomon by converting `res` array to a circular buffer,
 *   eliminating the need for element shifting.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Verify results here:
 *    https://www.nayuki.io/page/creating-a-qr-code-step-by-step
 */

#include <stdbool.h>
#include <stdint.h>
#include "qrcode_data.h"
#define PARM_SOURCE_TYPE 2
#define DEBUG_REED_SOLOMON false
// Global definitions (declared as extern in qrcode_data.h)
qr_ctx ctx[1];

/* QR Code Input String Selection
 * 
 * Priority:
 * 1. If QRCODE_INPUT_STRING is defined at compile time (via -DQRCODE_INPUT_STRING="..."),
 *    use that string (allows make-time customization)
 * 2. Otherwise, use the default hardcoded string below
 * 
 * Usage (from csrc/ directory):
 *   make qrcode_vga.asmbin QRSTR='"https://your-custom-url.com"'
 *   
 * Usage (from parent directory - quotes handled automatically):
 *   make demo-qrcode QRSTR=https://your-custom-url.com
 *   make demo-qrcode QRSTR="Text with spaces"
 *   
 * Technical Details:
 *   - csrc/Makefile: Receives QRSTR with quotes, passes as -DQRCODE_INPUT_STRING='$(QRSTR)'
 *   - Parent Makefile: Auto-wraps QRSTR in quotes via QRSTR='"$(QRSTR)"'
 *   - Final preprocessor: Gets -DQRCODE_INPUT_STRING="your string"
 */
#ifdef QRCODE_INPUT_STRING
const char str[BUFFLEN] = QRCODE_INPUT_STRING; // the maximum capacity for input string is 53 bytes for version3
#else
/* Default string - used when QRCODE_INPUT_STRING is not defined */
const char str[BUFFLEN] = "https://github.com/AnnTaiwan/ca2025-mycpu";
#endif

/* Available Link Examples (for manual testing):
 * const char str[] = "https://github.com/AnnTaiwan/ca2025-mycpu";
 * const char str[] = "https://github.com/sysprog21/rv32emu";
 * const char str[] = "https://www.thonky.com/qr-code-tutorial/";
 * const char str[] = "https://www.youtube.com/watch?v=1pQJkt7-R4Q";
 */

/*
 * Get dots for display.
 */
static inline bool qr_getdot(qr_ctx *ctx, uint x, uint y)
{
    return ctx->bmp[y] << x >> 31;
}

/*
 * Draw finders, timing pattern, alignment pattern, and the dark dot.
 * And now the format bits for fixed mask 0.
 */
static void _init_bmp(uint32_t A[], uint size)
{
    /* Draw top-left finder. */
    A[0] = A[6] = 0xFE000000;
    A[1] = A[5] = 0x82000000;
    A[2] = A[3] = A[4] = 0xBA000000;

    /* Replicate to bottom-left then top-right. */
    int y;
    for (y = 0; y < 7; y++) {
        A[size - 1 - y] = A[y]; // for bottom-left
        A[y] |= A[y] >> (size - 7); // for top-right
    }

    /* Horizontal timing pattern. */
    A[6] |= 0xAAA800;

    /* Vertical timing pattern. */
    for (y = 9; y < size - 7; y++)
        A[y] = ((y + 1) & 1) << 25;

    /* Version and format string
    * Assume the level is L, and the mask pattern is 0.
    * L : `01`
    * Mask pattern 5: `000`
    * First five bits: `01000`
    * Error correction bits: `1111010110`
    * Combined string: `010001111010110` (XOR with `101010000010010`)
    * Format Information Strings: `111011111000100`
    */
    /* The dark dot, then some format bits. */
    // at right side of bottom-left finder pattern
    y -= 1;  // size-8
    for (int i = 0; i < 8; i++) {
        if (i == 4)
            continue;
        A[y + i] |= 0x800000;
    }
 
    /* More format bits. */
    // at right side of top-left finder bottom
    A[2] |= 0x800000; // format string[12]=1
    A[7] = 0x800000; // separator and format string[8]=1 at row 7
    A[8] = 0xEF800000 | 0x31 << (34 - size); // format_string[0-5](111011) + one timing pattern(1) + format_string[6-7](11) + 00000000 + format_string[7-14](11000100)
    // so A[8] is 1110111110000000011000100(0000000) if size is 25

    /* Alignment pattern for version 2 & 3. */
    if (size > 21) {
        uint shift = 36 - size;
        uint pat = 0x1F << shift;
        A[size - 9] |= pat;
        A[size - 5] |= pat;
        pat = 0x11 << shift;
        A[size - 8] |= pat;
        A[size - 6] |= pat;
        A[size - 7] |= 0x15 << shift;
    }
}

typedef struct qr_params {
    uint8_t capa;   /* total capacity in bytes. */
    uint8_t eccdeg; /* ECC degree/byte count. */
    uint8_t gen[];  /* ECC generator polynomial. */
} qr_params;

/*
 * Check capacity then setup parameters.
 *
 * Return false if version number is invalid or input exceeds the capacity of
 * specified version.
 *  - Parameters are written to the context.
 *  - Must evaluate before encoding.
 *  - Must fail if evaluation fails.
 *  Capacity: V1 17B, V2 32B, V3 53B. for byte mode
 *  reference: https://www.thonky.com/qr-code-tutorial/character-capacities
 */
static bool qr_eval(qr_ctx *ctx, uint ver, const uint8_t *data, uint len)
{
    static const uint8_t _params_blob[] = {
        // total code words(data + EC), ECC code words, ECC generator polynomial...(The highest term's coefficient is always 1, so ignore here.)
        26,   7,    0x7f, 0x7a, 0x9a, 0xa4, 0x0b, 0x44, 0x75,  // V1
        44,   10,   0xd8, 0xc2, 0x9f, 0x6f, 0xc7, 0x5e, 0x5f,
        0x71, 0x9d, 0xc1,  // V2
        70,   15,   0x1d, 0xc4, 0x6f, 0xa3, 0x70, 0x4a, 0x0a,
        0x69, 0x69, 0x8b,  // V3
        0x84, 0x97, 0x20, 0x86, 0x1a};

    if (!ctx)
        return false;
    ctx->data = data;
    ctx->len = len;

    uintptr_t params = (uintptr_t) _params_blob; /* intentional */
    /* Skip-overs, cross check with the blob layout. */
    switch (ver) {
    case 1:
        break;
    case 2:
        params += 9;
        break;
    case 3:
        params += 21;
        break;
    default:
        return false;
    }

    uint size = (ver << 2) + 17;
    ctx->params = (void *) params;
    /* 4b mode, 8b count, 4b terminator. */
    uint usable =
        ((qr_params *) params)->capa - ((qr_params *) params)->eccdeg - 2;
    if (usable < len)
        return false;

    ctx->size = size;
    _init_bmp(ctx->bmp, ctx->size);
    return true;
}

/*
 * Prepare all the data bits before ECC.
 */
static void _serialize_data(qr_ctx *ctx, uint8_t *buf)
{
    /* Mode bits and length bits. */
    // From Versions 1 through 9, the specific required number of bit is 8 for byte mode.
    uint b = 4 << 8 | ctx->len; // byte mode
    buf[0] = b >> 4; // first code word(8-bit)
    uint i = 0;
    while (i < ctx->len) {
        b <<= 8;
        b |= ctx->data[i++]; // append next code word
        buf[i] = b >> 4;
    }

    /* Final 4 bits with terminator. */
    i++;
    buf[i++] = b << 4;

    /* Byte padding. */
    b = 0xEC; // 1110 1100 = 8'd236 => 1110 1100 ^ 1111 1101 = 0001 0001 = 8'd17
    qr_params *para = (qr_params *) ctx->params;
    while (i < para->capa - para->eccdeg) {
        buf[i++] = b;
        b ^= 0xFD; /* alternating EC, 11. */
    }

    /* Clear out the rest bytes for ECC; also clear 1 extra byte for the spare
     * bits (v2 & v3 have 7 unused).
     */
    while (i <= para->capa)
        buf[i++] = 0;
}

/*
 * QR_OPT: Reed-Solomon GF(2^8) multiplication optimization modes
 * 
 * Performance Results (generate_qrcode_opt_v2 cycle count):
 * ┌──────┬─────────────────────┬──────────────┬─────────────────────────┐
 * │ OPT  │ Implementation      │ Cycle Count  │ Normalized (LUT = 1.0x) │
 * ├──────┼─────────────────────┼──────────────┼─────────────────────────┤
 * │  0   │ LUT (Log/Antilog)   │   155,781    │         1.000x          │
 * │  1   │ Iterative (C)       │   324,969    │         2.086x          │
 * │  2   │ Iterative (ASM)     │   199,280    │         1.279x          │
 * └──────┴─────────────────────┴──────────────┴─────────────────────────┘
 * 
 * Speedup Analysis (relative to baseline OPT=1):
 *   OPT=0 vs OPT=1: 324969/155781 = 2.086x speedup (51.1% fewer cycles)
 *   OPT=2 vs OPT=1: 324969/199280 = 1.631x speedup (38.7% fewer cycles)
 *   OPT=0 vs OPT=2: 199280/155781 = 1.279x speedup (21.8% fewer cycles)
 * 
 * OPT=0 (LUT): Lookup Table Method
 *   - Uses pre-computed log/antilog tables (512 bytes total)
 *   - Converts multiplication to addition in log space: x*y = 2^(log(x)+log(y))
 *   - FASTEST: 2.09x faster than C, 1.28x faster than assembly
 *   - Trade-off: Requires 512 bytes of ROM/flash for lookup tables
 *   - Best for: Performance-critical applications with available memory
 * 
 * OPT=1 (C): Iterative C Implementation
 *   - Pure C bitwise multiplication using Galois Field (GF) 2^8 rules
 *   - No lookup tables, minimal memory footprint
 *   - SLOWEST: 2.09x slower than LUT, 1.63x slower than assembly
 *   - Best for: Memory-constrained systems, educational purposes
 * 
 * OPT=2 (ASM): Inline Assembly Optimization
 *   - Hand-optimized RISC-V assembly with branchless conditionals
 *   - Eliminates C compiler overhead and reduces branch penalties
 *   - MIDDLE GROUND: 1.28x slower than LUT, 1.63x faster than C
 *   - No lookup tables required (0 bytes extra memory)
 *   - Best for: Balance between performance and memory usage
 * 
 * Recommendation:
 *   - Use OPT=0 for production (fastest, 512B overhead acceptable)
 *   - Use OPT=2 for tight memory constraints (no tables, 63% faster than C)
 *   - Use OPT=1 for portability/debugging only
 */
#if QR_OPT == 0
/*
 * The GF(2^8, 285) finite field element multiplication.
 * Basic iterative, unrolled, and log/exp LUT versions are implemented.
 * table: https://www.thonky.com/qr-code-tutorial/log-antilog-table
 */
static inline uint _rs_mul(uint x, uint y)
{
    static const uint8_t _luts[2][256] = {
        // Log table.
        {0,   0,   1,   25,  2,   50,  26,  198, 3,   223, 51,  238, 27,  104,
         199, 75,  4,   100, 224, 14,  52,  141, 239, 129, 28,  193, 105, 248,
         200, 8,   76,  113, 5,   138, 101, 47,  225, 36,  15,  33,  53,  147,
         142, 218, 240, 18,  130, 69,  29,  181, 194, 125, 106, 39,  249, 185,
         201, 154, 9,   120, 77,  228, 114, 166, 6,   191, 139, 98,  102, 221,
         48,  253, 226, 152, 37,  179, 16,  145, 34,  136, 54,  208, 148, 206,
         143, 150, 219, 189, 241, 210, 19,  92,  131, 56,  70,  64,  30,  66,
         182, 163, 195, 72,  126, 110, 107, 58,  40,  84,  250, 133, 186, 61,
         202, 94,  155, 159, 10,  21,  121, 43,  78,  212, 229, 172, 115, 243,
         167, 87,  7,   112, 192, 247, 140, 128, 99,  13,  103, 74,  222, 237,
         49,  197, 254, 24,  227, 165, 153, 119, 38,  184, 180, 124, 17,  68,
         146, 217, 35,  32,  137, 46,  55,  63,  209, 91,  149, 188, 207, 205,
         144, 135, 151, 178, 220, 252, 190, 97,  242, 86,  211, 171, 20,  42,
         93,  158, 132, 60,  57,  83,  71,  109, 65,  162, 31,  45,  67,  216,
         183, 123, 164, 118, 196, 23,  73,  236, 127, 12,  111, 246, 108, 161,
         59,  82,  41,  157, 85,  170, 251, 96,  134, 177, 187, 204, 62,  90,
         203, 89,  95,  176, 156, 169, 160, 81,  11,  245, 22,  235, 122, 117,
         44,  215, 79,  174, 213, 233, 230, 231, 173, 232, 116, 214, 244, 234,
         168, 80,  88,  175},
        // Exponential table.
        {1,   2,   4,   8,   16,  32,  64,  128, 29,  58,  116, 232, 205, 135,
         19,  38,  76,  152, 45,  90,  180, 117, 234, 201, 143, 3,   6,   12,
         24,  48,  96,  192, 157, 39,  78,  156, 37,  74,  148, 53,  106, 212,
         181, 119, 238, 193, 159, 35,  70,  140, 5,   10,  20,  40,  80,  160,
         93,  186, 105, 210, 185, 111, 222, 161, 95,  190, 97,  194, 153, 47,
         94,  188, 101, 202, 137, 15,  30,  60,  120, 240, 253, 231, 211, 187,
         107, 214, 177, 127, 254, 225, 223, 163, 91,  182, 113, 226, 217, 175,
         67,  134, 17,  34,  68,  136, 13,  26,  52,  104, 208, 189, 103, 206,
         129, 31,  62,  124, 248, 237, 199, 147, 59,  118, 236, 197, 151, 51,
         102, 204, 133, 23,  46,  92,  184, 109, 218, 169, 79,  158, 33,  66,
         132, 21,  42,  84,  168, 77,  154, 41,  82,  164, 85,  170, 73,  146,
         57,  114, 228, 213, 183, 115, 230, 209, 191, 99,  198, 145, 63,  126,
         252, 229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75,  150, 49,
         98,  196, 149, 55,  110, 220, 165, 87,  174, 65,  130, 25,  50,  100,
         200, 141, 7,   14,  28,  56,  112, 224, 221, 167, 83,  166, 81,  162,
         89,  178, 121, 242, 249, 239, 195, 155, 43,  86,  172, 69,  138, 9,
         18,  36,  72,  144, 61,  122, 244, 245, 247, 243, 251, 235, 203, 139,
         11,  22,  44,  88,  176, 125, 250, 233, 207, 131, 27,  54,  108, 216,
         173, 71,  142, 1},
    };
    /*
    GF field multiplication flow: 
        x = 2^a
        y = 2^b
        x*y = 2^a * 2^b = 2^(a+b)%255 = 2^c
        After calculating exponent, get the integer mode by use antilog_table, which is _luts[1][c].
    */
    if (!x || !y)
        return 0;
    uint xp = _luts[0][x] + _luts[0][y]; // exponent addition: log(x) + log(y)
    if (xp > 255)
        xp -= 255;
    return _luts[1][xp];
}

#elif QR_OPT == 1 /* use iterative GF MUL */
static inline uint _rs_mul(uint x, uint y)
{
    uint z = 0;
    /* This is called (133, 340, 825) times in V(1, 2, 3) ECC calculation. */
    for (int i = 7; i >= 0; i--) {
        /* And this body is run 1064, 2720, 6600 times. */
        z = (z << 1) ^ ((z >> 7) * 0x11D); // 0x11d = 285
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}
#else /* QR_OPT == 2 */
static inline uint _rs_mul(uint x, uint y)
{   
    /* Reed-Solomon GF(2^8) multiplication using inline RISC-V assembly
     * Input: x (multiplicand), y (multiplier)
     * Output: result
     * Algorithm matches _rs_mul (c version) when QR_OPT == 1:
     *   z = 0
     *   for i = 7 down to 0:
     *     z = (z << 1) ^ ((z >> 7) * 0x11D)
     *     z ^= ((y >> i) & 1) * x
     */
    uint result;
    asm volatile(
        "li t0, 0\n"              /* z = 0 */
        "li t1, 7\n"              /* i = 7 (counter) */
        "mv t6, %1\n"             /* Save x in t6 */
        
        ".Lloop:\n"
        "  slli t0, t0, 1\n"      /* t0 = z << 1 */
        
        /* Calculate (z >> 7) * 0x11D without mul_loop
            (Optional, poor performance) 
            If doing shift-and-add multiplication (* 0x11D):
                0x11D = 0b100011101, 1 at position 0,2,3,4,8 
                So, calculate the a3 << 2,3,4,8 and sum them.
            Since (z >> 7) is either 0 or 1 for 8-bit values:
                - If (z >> 7) == 0: z = (z << 1)
                - If (z >> 7) == 1: z = (z << 1) ^ 0x11D
        */
        "  srli a3, t0, 8\n"      /* a3 = z >> 7, here shift right 8 due to t0 already shifts left 1 before */
        "  beqz a3, .Lnext\n"     /* if (z >> 7) == 0, skip multiplication, and then go to next equation */
        "  xori t0, t0, 0x11D\n"  /* if (z >> 7) == 1, z = (z << 1) ^ 0x11D*/
        /* Calculate ((y >> i) & 1) * x 
            (y >> i) & 1 must be 1 or 0, so it is simple to do this multiplication
            Just check if it is 1, if true, do the xor.
        */
        ".Lnext:\n"
        "  srl a4, %2, t1\n"      /* a4 = y >> i */
        "  andi a4, a4, 1\n"      /* a4 = (y >> i) & 1 */
        
        "  beqz a4, .Lskip_xor\n"  /* if (y >> i) & 1 == 0, skip XOR */
        "  xor t0, t0, t6\n"       /* z ^= x */
        ".Lskip_xor:\n"
        "  addi t1, t1, -1\n"      /* i-- */
        "  bgez t1, .Lloop\n"      /* if i >= 0, continue loop */
        
        "  mv %0, t0\n"            /* return z */
        : "=r"(result)             /* Output: result */
        : "r"(x), "r"(y)           /* Inputs: x, y */
        : "t0", "t1", "t6", "a3", "a4"  /* Clobbered: Remind those registers will be modified */
    );
    return result;
}
#endif
#if DEBUG_REED_SOLOMON
void write_mul_data(volatile uint8_t *r, uint deg)
{
    // Write ASCII QR code starting at mem[64] (0x100)
    volatile char *mem8 = (volatile char *)0x00000100;
    for(uint i = 0; i < deg; i++)
    {
        *mem8++ = r[i];
    }
}
#endif
/*
 * Calculate the ECC bytes.
 */
#if DEBUG_REED_SOLOMON && PARM_SOURCE_TYPE == 0
static void _reed_solomon(qr_ctx *ctx, uint8_t *buf)
{
    volatile qr_params *para = (volatile qr_params *) ctx->params;
    uint deg = para->eccdeg;
    volatile uint8_t *gen = para->gen;
    uint len = para->capa - para->eccdeg;
    volatile uint8_t *res = buf + len; // residual
    
    // Debug: Write deg, len, and gen to memory at 0x200
    volatile uint32_t *debug_ptr = (volatile uint32_t *)0x200;
    debug_ptr[0] = deg;  // 0x200: deg
    debug_ptr[1] = len;  // 0x204: len
    debug_ptr[2] = para->capa;  // 0x208: total capacity
    debug_ptr[3] = (uint32_t)gen;  // 0x20C: gen pointer address
    debug_ptr[4] = (uint32_t)res;  // 0x210: res pointer address
    debug_ptr[5] = (uint32_t)buf;  // 0x214: buf pointer address
    debug_ptr[6] = (uint32_t)para;  // 0x218: para pointer address
    debug_ptr[7] = (uint32_t)ctx->params;  // 0x21C: ctx->params address
    
    // Write gen array at 0x220 (via gen pointer)
    volatile uint8_t *gen_debug = (volatile uint8_t *)0x220;
    for (uint i = 0; i < deg; i++) {
        gen_debug[i] = gen[i];
    }
    
    // Write ENTIRE para structure at 0x230 (raw memory dump)
    volatile uint8_t *para_debug = (volatile uint8_t *)0x230;
    volatile uint8_t *para_bytes = (volatile uint8_t *)para;
    for (uint i = 0; i < (2 + deg); i++) {  // capa + eccdeg + gen array
        para_debug[i] = para_bytes[i];
    }
    
    // DIRECT read from gen pointer address (bypass the gen variable)
    volatile uint8_t *gen_direct = (volatile uint8_t *)debug_ptr[3];  // Use the address we just saved
    volatile uint8_t *gen_direct_debug = (volatile uint8_t *)0x250;
    for (uint i = 0; i < deg; i++) {
        gen_direct_debug[i] = gen_direct[i];
    }
    
    for (uint i = 0; i < len; i++) {
        uint factor = buf[i] ^ res[0];
        for (uint j = 1; j < deg; j++)
            res[j - 1] = res[j];
        res[deg - 1] = 0;
        for (uint j = 0; j < deg; j++)
            res[j] ^= _rs_mul(gen[j], factor);
    }
    write_mul_data(res, deg);
}
#elif DEBUG_REED_SOLOMON && PARM_SOURCE_TYPE == 1
static void _reed_solomon(qr_ctx *ctx, uint8_t *buf)
{
    // STACK-ALLOCATED array to completely avoid .rodata corruption
    uint8_t _params_hardcoded[] = {
        // total code words(data + EC), ECC code words, ECC generator polynomial...(The highest term's coefficient is always 1, so ignore here.)
        26,   7,    0x7f, 0x7a, 0x9a, 0xa4, 0x0b, 0x44, 0x75,  // V1
        44,   10,   0xd8, 0xc2, 0x9f, 0x6f, 0xc7, 0x5e, 0x5f,
        0x71, 0x9d, 0xc1,  // V2
        70,   15,   0x1d, 0xc4, 0x6f, 0xa3, 0x70, 0x4a, 0x0a,
        0x69, 0x69, 0x8b,  // V3
        0x84, 0x97, 0x20, 0x86, 0x1a};

    uint deg = _params_hardcoded[22];  // V3 eccdeg
    uint len = _params_hardcoded[21] - deg;  // V3: 70 - 15
    const uint8_t *gen = &_params_hardcoded[23];
    uint8_t *res = buf + len; // residual
    
    // Debug: Write parameters and gen array to verify correctness
    volatile uint32_t *debug_ptr = (volatile uint32_t *)0x200;
    debug_ptr[0] = deg;  // 0x200: deg (should be 15)
    debug_ptr[1] = len;  // 0x204: len (should be 55)
    debug_ptr[2] = deg + len;  // 0x208: capa (should be 70)
    debug_ptr[3] = (uint32_t)gen;  // 0x20C: gen pointer address
    debug_ptr[4] = (uint32_t)res;  // 0x210: res pointer address
    debug_ptr[5] = (uint32_t)buf;  // 0x214: buf pointer address
    debug_ptr[6] = 0xAAAAAAAA;  // 0x218: marker (no para in hardcoded version)
    debug_ptr[7] = 0xBBBBBBBB;  // 0x21C: marker (no ctx->params in hardcoded version)
    
    // Write gen array at 0x220 (via gen pointer)
    volatile uint8_t *gen_debug = (volatile uint8_t *)0x220;
    for (uint i = 0; i < deg; i++) {
        gen_debug[i] = gen[i];
    }
    
    // Write gen array again at 0x230 for verification
    volatile uint8_t *gen_verify = (volatile uint8_t *)0x230;
    for (uint i = 0; i < deg; i++) {
        gen_verify[i] = gen[i];
    }
    
    // Write gen array at 0x250 for compatibility with existing CPUTest.scala
    volatile uint8_t *gen_direct = (volatile uint8_t *)0x250;
    for (uint i = 0; i < deg; i++) {
        gen_direct[i] = _params_hardcoded[23+i];
    }
    
    for (uint i = 0; i < len; i++) {
        uint factor = buf[i] ^ res[0];
        for (uint j = 1; j < deg; j++)
            res[j - 1] = res[j];
        res[deg - 1] = 0;
        for (uint j = 0; j < deg; j++)
            res[j] ^= _rs_mul(gen[j], factor);
    }
    write_mul_data(res, deg);
}
#elif (!DEBUG_REED_SOLOMON) // not debug mode
static void _reed_solomon(qr_ctx *ctx, uint8_t *buf)
{
    uint8_t _params_hardcoded[] = {
        // total code words(data + EC), ECC code words, ECC generator polynomial...(The highest term's coefficient is always 1, so ignore here.)
        26,   7,    0x7f, 0x7a, 0x9a, 0xa4, 0x0b, 0x44, 0x75,  // V1
        44,   10,   0xd8, 0xc2, 0x9f, 0x6f, 0xc7, 0x5e, 0x5f,
        0x71, 0x9d, 0xc1,  // V2
        70,   15,   0x1d, 0xc4, 0x6f, 0xa3, 0x70, 0x4a, 0x0a,
        0x69, 0x69, 0x8b,  // V3
        0x84, 0x97, 0x20, 0x86, 0x1a};
    
    uint offset = 0;
    switch(QR_VERSION){
        case 1:
            offset = 0;
            break;
        case 2:
            offset = 9;
            break;
        case 3:
            offset = 21;
            break;
        default: // default to be version 1
            offset = 0;
            break;
    }
    uint deg = _params_hardcoded[offset + 1]; // V3 eccdeg: 15
    uint len = _params_hardcoded[offset] - deg; // V3: 70 - 15
    const uint8_t *gen = &_params_hardcoded[offset + 2];
    uint8_t *res = buf + len;
    
    /* Circular buffer optimization to avoid expensive shift operations */
    uint circ_offset = 0;
    for (uint i = 0; i < len; i++) {
        uint factor = buf[i] ^ res[circ_offset];
        res[circ_offset] = 0;
        
        /* Apply Reed-Solomon polynomial starting from next position */
        for (uint j = 0; j < deg; j++) {
            uint idx = circ_offset + 1 + j;
            if (idx >= deg) idx -= deg;
            res[idx] ^= _rs_mul(gen[j], factor);
        }
        
        circ_offset++;
        if (circ_offset >= deg) circ_offset = 0;
    }
    
    /* Rotate buffer back to start from index 0 */
    if (circ_offset != 0) {
        uint8_t temp[15];
        for (uint i = 0; i < deg; i++)
            temp[i] = res[(circ_offset + i) >= deg ? (circ_offset + i - deg) : (circ_offset + i)];
        for (uint i = 0; i < deg; i++)
            res[i] = temp[i];
    }
}
#endif
/*
 * Return if dot (x,y) is for data (i.e. not function patterns).
 */
static inline bool _is_data(uint x, uint y, uint size_m1)
{
    if (x == 6 || y == 6)
        return false;
    if (y <= 8)
        return x >= 9 && x <= size_m1 - 8;
    if (y >= size_m1 - 7 && x <= 8)
        return false;
    if (size_m1 > 20 && x >= size_m1 - 8 && x <= size_m1 - 4 &&
        y >= size_m1 - 8 && y <= size_m1 - 4)
        return false;
    return true;
}

typedef union poly64_t {
    uint64_t bits;
    uint32_t u[2];
    struct {
        uint32_t u0;
        uint32_t u1;
    };
} poly64_t;

/*
 * The QR zig-zag sequence generator.
 *
 * To iterate through all valid data positions: call with initial x and y =
 * size-1, feed back the result as input, and repeat the call-feed cycle.
 */
static uint64_t zigzag_step(uint x, uint y, uint size_m1)
{
    poly64_t r;
    while (true) {
        switch ((x - (x > 6)) & 3) {
        case 0:
            if (y < size_m1)
                x += 1, y += 1;
            else
                x -= 1;
            break;
        case 1:
            x -= 1;
            break;
        case 2:
            if (y > 0)
                x += 1, y -= 1;
            else {
                x -= 1;
                if (x == 6)
                    x = 5;
            }
            break;
        default:
            x -= 1;
        }
        if (_is_data(x, y, size_m1))
            break;
    }
    r.u0 = x, r.u1 = y;
    return r.bits;
}

/*
 * Put data bits to the QR bitmap.
 * Fixed masking (0) is applied on the fly.
 */
static void _place_data(qr_ctx *ctx, const uint8_t *buf)
{
    uint size_m1 = ctx->size - 1;
    poly64_t xy = {.u0 = size_m1, .u1 = size_m1}; // store the current coordinate where this buf bit data should put
    qr_params *para = (qr_params *) ctx->params;
    uint nbits = para->capa << 3;

    /* NB: count in the unused bits in V2 and V3. */
    if (size_m1 > 20)
        nbits += 7;

    for (int i = 0; i < nbits; i++) {
        bool mask0 = ((xy.u0 + xy.u1) & 1) == 0;
        bool dot = buf[i >> 3] & (0x80u >> (i & 0x7));
        if (dot ^ mask0) // switch the bit
            ctx->bmp[xy.u1] |= 0x80000000u >> xy.u0;
        xy.bits = zigzag_step(xy.u0, xy.u1, size_m1);
    }
}

/*
 * The actual encoding.
 */
static void qr_encode(qr_ctx *ctx)
{
    if (!ctx)
        return;

    uint8_t dbuf[72];  // V3 capacity 70 and extra 2.
    _serialize_data(ctx, dbuf);
    _reed_solomon(ctx, dbuf);
    _place_data(ctx, dbuf);
}
// String length helper function
static uint str_len(const char *s)
{
    uint len = 0;
    while (s[len])
        len++;
    return len;
}
// v3
int generate_qrcode_opt()
{
    if (!qr_eval(ctx, /* version */ QR_VERSION, (const uint8_t *) str, str_len(str))) {
        // printf("Evaluation failed. Version invalid or data too long?\n");
        return -2;
    }
    qr_encode(ctx);
    return 0;
}