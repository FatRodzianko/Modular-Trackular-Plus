/* Modular-Trackular Plus: Synth FX plugins (.ptsfx). SPDX-License-Identifier: MIT
 *
 * Built into every plugin by ptsfx_builder.py: the few library functions the compiler can
 * call on its own (clearing or copying a struct), since a plugin links against nothing in
 * the firmware. Plain loops: a plugin's state is small, and init is the only big clear. */

#include <stddef.h>
#include <stdint.h>

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s)
        while (n--)
            *d++ = *s++;
    else
        while (n--)
            d[n] = s[n];
    return dst;
}
