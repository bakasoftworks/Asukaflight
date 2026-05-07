#include "aes.h"
#include "access_denied.h"

static const uint8_t mask[] = {
    0x72, 0x42, 0x60, 0x4b, 0x38, 0x4e, 0x11, 0x06,
    0x5d, 0x10, 0x2b, 0x08, 0x27, 0x3a, 0x37, 0x29
};

static const char key[] = "403 AccessDenied";

#define AD_KEYLEN (sizeof(key)-1)

uint8_t access_denied(uint8_t mode, const uint8_t * input, uint8_t * output)
{
    static struct AES_ctx ctx;
    static uint8_t ctx_valid = 0;
    int i;

    if (mode) return 0;
    
    if (!ctx_valid) {
        uint8_t buffer[AD_KEYLEN];

        for (i=0; i<AD_KEYLEN; i++)
            buffer[i] = key[i] ^ mask[i];

        AES_init_ctx(&ctx, buffer);
        ctx_valid = 1;
    }

    for (i=0; i<AD_KEYLEN; i++)
        output[i] = input[i];
    
    AES_ECB_decrypt(&ctx, output);
    return 1;
}
