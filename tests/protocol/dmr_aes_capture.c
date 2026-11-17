/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The firmware's DMR AES-256 keystream against a real recording: dmrgo's public capture (see test_dmr_aes.py). Each line
 * of the file is one AMBE+2 frame (49 bits MSB first in 7 octets) as received, then as dsd-fme printed it with the key.
 * usage: dmr_aes_capture FILE KEYHEX. Exit 0 when every frame the key undoes is the one dsd-fme printed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "voice_aes.h"
enum { MAX_FRAMES = 2000 };
static int hexn(const char *s, uint8_t *o, unsigned n)
{
    for (unsigned i = 0; i < n; i++) { unsigned x; if (sscanf(s + 2 * i, "%2x", &x) != 1) return 0; o[i] = (uint8_t)x; }
    return 1;
}
static void bits49(const uint8_t f[7], uint8_t b[49]) { for (unsigned i = 0; i < 49; i++) b[i] = (f[i / 8] >> (7 - i % 8)) & 1; }
int main(int argc, char **argv)
{
    static uint8_t ct[MAX_FRAMES][7], pt[MAX_FRAMES][7];
    uint8_t key[32], pi[12] = {0};
    char line[256];
    unsigned n = 0;
    if (argc != 3 || !hexn(argv[2], key, 32)) return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    while (fgets(line, sizeof line, f)) {
        char a[64], b[64];
        if (line[0] == '#' || !strncmp(line, "lc ", 3)) continue;
        if (!strncmp(line, "pi ", 3)) { if (!hexn(line + 3, pi, 12)) return 2; continue; }
        if (sscanf(line, "%63s %63s", a, b) == 2 && strlen(a) == 14 && n < MAX_FRAMES) {
            if (!hexn(a, ct[n], 7) || !hexn(b, pt[n], 7)) return 2;
            n++;
        }
    }
    fclose(f);
    uint32_t mi = ((uint32_t)pi[3] << 24) | ((uint32_t)pi[4] << 16) | ((uint32_t)pi[5] << 8) | pi[6];
    voice_aes s;
    voice_aes_dmr_start(&s, key, mi);
    unsigned bad = 0;
    for (unsigned burst = 0; burst * 3 + 2 < n; burst++) {
        uint8_t bits[3][49];
        for (unsigned k = 0; k < 3; k++) bits49(ct[burst * 3 + k], bits[k]);
        if (!voice_aes_dmr_burst(&s, burst % 6, bits)) { printf("burst %u refused\n", burst); return 1; }
        for (unsigned k = 0; k < 3; k++) {
            uint8_t want[49];
            bits49(pt[burst * 3 + k], want);
            if (memcmp(bits[k], want, 49)) bad++;
        }
    }
    printf("%u frames (%u superframes) of the capture: %u differ\n", n, n / 18, bad);
    return bad != 0;
}
