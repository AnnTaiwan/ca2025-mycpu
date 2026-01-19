#ifndef QRCODE_DATA_H
#define QRCODE_DATA_H

#define QR_VERSION 3
#define QR_OPT 1

#define QR_LINES 29

typedef unsigned uint;

typedef struct qr_ctx {
    uint8_t size;            // 21, 25 or 29 (ver*4+17)
    uint8_t len;             // length of input data.
    const uint8_t *data;     // input data.
    void *params;            // data and ECC parameters.
    uint32_t bmp[QR_LINES];  // QR code bitmap, 1 word per line.
} qr_ctx;

extern qr_ctx ctx[1];

// encode string
extern const char str[];

// Hardcoded QR bitmap data (29 lines for version 3)
static const uint32_t hardcoded_qr_bitmap[29] = {
    0xFE4E93F8,  //  0
    0x823BDA08,  //  1
    0xBAEE3AE8,  //  2
    0xBA77BAE8,  //  3
    0xBA2312E8,  //  4
    0x82459A08,  //  5
    0xFEAAABF8,  //  6
    0x00EFD000,  //  7
    0xEFB3D620,  //  8
    0x6DB28A48,  //  9
    0x464446B8,  // 10
    0x3191E190,  // 11
    0x0D308C658, // 12
    0x05DCCE48,  // 13
    0x021AC2D8,  // 14
    0xDD6E6E50,  // 15
    0xFA927B58,  // 16
    0x40726668,  // 17
    0xBF246598,  // 18
    0x6D505FD0,  // 19
    0xAA887F80,  // 20
    0x00BC78B8,  // 21
    0xFEDABAD8,  // 22
    0x82EEE8D0,  // 23
    0xBAF26F98,  // 24
    0xBA1289B8,  // 25
    0xBAE4D1C8,  // 26
    0x82B07C90,  // 27
    0xFEE85D98   // 28
};

// Helper function to read from hardcoded bitmap
static inline bool qr_getdot_hardcoded(uint x, uint y)
{
    return hardcoded_qr_bitmap[y] << x >> 31;
}

#endif
