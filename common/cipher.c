/* cipher.c - Caesar theo byte (xem cipher.h). */
#include "cipher.h"

void cipher_encode(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(p[i] + NETCHAT_CAESAR_KEY);
}

void cipher_decode(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(p[i] - NETCHAT_CAESAR_KEY);
}
