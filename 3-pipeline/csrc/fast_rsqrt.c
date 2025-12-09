#include <stdint.h>
#include "fast_rsqrt.h"
static const uint32_t rsqrt_table[32] = {
    65536, 46341, 32768, 23170, 16384,  /* 2^0 to 2^4 */
    11585,  8192,  5793,  4096,  2896,  /* 2^5 to 2^9 */
     2048,  1448,  1024,   724,   512,  /* 2^10 to 2^14 */
      362,   256,   181,   128,    90,  /* 2^15 to 2^19 */
       64,    45,    32,    23,    16,  /* 2^20 to 2^24 */
       11,     8,     6,     4,     3,  /* 2^25 to 2^29 */
        2,     1                         /* 2^30, 2^31 */
};
static uint64_t mul32(uint32_t a, uint32_t b)
{
    uint64_t r = 0;
    for(int i = 0; i < 32; i++)
    {
        if(b & 1u << i) // current b's last bit is odd
            r += (uint64_t)a << i;
    }
    return r;
}
static int clz(uint32_t x)
{
    if(!x) return 32;
    int n = 0; //result
    // by binary search
    if(!(x & 0XFFFF0000)) { n += 16; x <<= 16;} // top 16 bits are all zero
    if(!(x & 0XFF000000)) { n += 8; x <<= 8;} // top 24 bits are all zero
    if(!(x & 0XF0000000)) { n += 4; x <<= 4;} // top 28 bits are all zero
    if(!(x & 0XC0000000)) { n += 2; x <<= 2;} // top 30 bits are all zero
    if(!(x & 0X80000000)) { n += 1;} // finish check all 31 bits;
    return n;
}
uint32_t fast_rsqrt(uint32_t x)
{
    /* y's format is Q16.16 */
    if(x == 0) return 0xFFFFFFFF; // represents inf
    if(x == 1) return 65536;
    /* Find MSB position */
    int exp = 31 - clz(x);
    /* get initial guess by LUT */
    uint32_t y = rsqrt_table[exp]; // lookup table by exp
    /* linear interpolation */
    if(x > (1u << exp))
    {
        uint32_t y_next = (exp < 31) ? rsqrt_table[exp + 1] : 0;
        uint32_t delta = y - y_next; 
        uint32_t frac = (uint32_t)((((uint64_t)x - (1UL << exp)) << 16) >> exp); // let frac be Q16.16

        // y -= (uint32_t)((delta * frac) >> 16); // scale down to  Q16.16
        y -= (uint32_t)(mul32(delta, frac) >> 16); // scale down to  Q16.16
    }
    /* Newton-Raphson Iteration */
    for(int i = 0; i < 2; i++)
    {
        uint32_t y2 = (uint32_t)mul32(y, y); 
        uint32_t xy2 = (uint32_t)(mul32(x, y2) >> 16);// scale down back to Q16.16
        y = (uint32_t)(mul32(y, (3u << 16) - xy2) >> 17); // scale down back to Q16.16 and divided by 2, so it is (>> 17 = 16 +1)
    }
    return y;
}
/* ============= Test Suite ============= */
/* Test fast_rsqrt */
static int test_fast_rsqrt(void)
{
    int passed = 1;
    int base_address = 8; // fast_rsqrt result stores at mem[8], mem[12], ...
    /* Test data */
    uint32_t in[] = {1, 4, 16, 20, 100, 258, 650};
    uint32_t expected_values[] = { 
        65536,
        32768,
        16384,
        14654,
        6553,
        4080,
        2570
    };
    uint32_t result; // result = rsqrt(x)
    for(int i = 0; i < sizeof(in)/sizeof(uint32_t); i++)
    {
        result = fast_rsqrt(in[i]); // call rsqrt
        *(int *)(base_address + 4 * i) = result; // store result in memory
        if(result != expected_values[i])
            passed = 0;
    }
    return passed;
}
int main(void)
{
    int passed = test_fast_rsqrt(); // pass : 1, fail : 0

    *(int *) (4) = passed;  // store at mem[4]
}