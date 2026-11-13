/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/*
 * pce-normalize — clean up an unrecognised HuCard dump from the Retron GX
 * cartridge reader so Beetle PCE Fast can run it.
 *
 * The stock loader (tgdumper-loading) only fixes dumps that match its
 * built-in CRC database. Unknown carts (homebrew, flash carts, repros)
 * are passed through raw, which breaks in two cases:
 *
 *   1. TG16 / US-wired data: the data bus is bit-reversed (bit0<->bit7 ...).
 *      Beetle PCE Fast has no auto-decode, so the CPU executes garbage.
 *   2. Images of 2 MB or more: Beetle switches on the Street Fighter II
 *      mapper, which remaps banks 0x40-0x7F. A mirrored dump that large
 *      must be cut back to its real size.
 *
 *   3. Flash carts written with huconv.py (HuPROG): a 384 KB game is laid out
 *      as 256 KB + the same 256 KB again + the last 128 KB, padded with 0xFF.
 *      That is rebuilt into the standard 384 KB image.
 *
 * Usage: pce-normalize <in.pce> <out.pce>
 *        pce-normalize --vote a b c out       (majority of three reads)
 *        pce-normalize --unregion in out XX   (undo a region-free patch: every
 *            region check whose branch was patched to 0x80 gets opcode XX back)
 * Exit:  0 = output written, 1 = error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROM (4u * 1024u * 1024u)
#define ONE_MB  0x100000u
#define SF2_LEN 0x280000u

static unsigned char rev8(unsigned char b)
{
    b = (unsigned char)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (unsigned char)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (unsigned char)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

/* "LDA #$FF / TAM #$01 / LDA $1000 / AND #$40" — the region check found in
 * most HuCard boot code (the same pattern huconv.py and the stock loader use). */
static const unsigned char REGION_SIG[9] = {0xA9,0xFF,0x53,0x01,0xAD,0x00,0x10,0x29,0x40};

static int has_sig(const unsigned char *d, size_t n, int reversed)
{
    unsigned char s[9];
    size_t i;
    for (i = 0; i < 9; i++) s[i] = reversed ? rev8(REGION_SIG[i]) : REGION_SIG[i];
    for (i = 0; i + 9 <= n && i < ONE_MB; i++)
        if (d[i] == s[0] && memcmp(d + i, s, 9) == 0) return 1;
    return 0;
}

/* At power-on MPR7 = bank 0, so the interrupt/reset vectors sit at ROM
 * offsets 0x1FF6-0x1FFF and should point into $E000-$FFFF. */
static int vector_score(const unsigned char *d, size_t n, int reversed)
{
    static const size_t hi[] = {0x1FF7, 0x1FF9, 0x1FFB, 0x1FFD, 0x1FFF};
    int score = 0;
    size_t i;
    if (n < 0x2000) return 0;
    for (i = 0; i < 5; i++) {
        unsigned char b = reversed ? rev8(d[hi[i]]) : d[hi[i]];
        if (b >= 0xE0) score += (hi[i] == 0x1FFF) ? 3 : 1;  /* reset vector counts most */
    }
    return score;
}

/* pce-normalize --vote a b c out: byte-wise majority of three reads of the
 * same cart (the reader's reads can differ by a few bits on worn contacts).
 * Prints how many bytes needed the vote. */
static int vote(char **f)
{
    FILE *in[3], *o;
    long n[3], i, fixed = 0, bad = 0;
    int k;
    for (k = 0; k < 3; k++) {
        if (!(in[k] = fopen(f[k], "rb"))) { perror(f[k]); return 1; }
        fseek(in[k], 0, SEEK_END); n[k] = ftell(in[k]); rewind(in[k]);
    }
    if (n[0] != n[1] || n[0] != n[2]) { fprintf(stderr, "vote: sizes differ\n"); return 1; }
    if (!(o = fopen(f[3], "wb"))) { perror(f[3]); return 1; }
    for (i = 0; i < n[0]; i++) {
        int a = fgetc(in[0]), b = fgetc(in[1]), c = fgetc(in[2]), r;
        if (a == b || a == c) r = a; else if (b == c) r = b; else { r = a; bad++; }
        if (a != b || a != c) fixed++;
        fputc(r, o);
    }
    for (k = 0; k < 3; k++) fclose(in[k]);
    if (fclose(o) != 0) return 1;
    printf("vote: %ld bytes differed between reads, %ld with no majority\n", fixed, bad);
    return bad ? 2 : 0;
}

/* the region-free patch (huconv.py and others) turns the branch after the
 * region check into BRA (0x80); the original is BEQ (0xF0) or BNE (0xD0) */
static int unregion(char **a)
{
    FILE *f = fopen(a[0], "rb");
    unsigned char *d;
    long n, i, hits = 0;
    int v = (int)strtol(a[2], NULL, 16);
    if (!f) { perror(a[0]); return 1; }
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
    if (n <= 0 || n > (long)MAX_ROM || !(d = malloc((size_t)n))) { fclose(f); return 1; }
    if (fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return 1; }
    fclose(f);
    for (i = 0; i + 10 <= n; i++)
        if (d[i] == REGION_SIG[0] && !memcmp(d + i, REGION_SIG, 9) && d[i + 9] == 0x80) { d[i + 9] = (unsigned char)v; hits++; }
    if (!hits) { free(d); return 1; }
    if (!(f = fopen(a[1], "wb")) || fwrite(d, 1, (size_t)n, f) != (size_t)n) { if (f) fclose(f); free(d); return 1; }
    free(d);
    return fclose(f) != 0;
}

static int all_ff(const unsigned char *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (d[i] != 0xFF) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 6 && !strcmp(argv[1], "--vote")) return vote(argv + 2);
    if (argc == 5 && !strcmp(argv[1], "--unregion")) return unregion(argv + 2);
    FILE *f;
    unsigned char *buf, *d;
    size_t n, i;
    int sn, sr;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <in.pce> <out.pce>\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    buf = malloc(MAX_ROM + 1);
    if (!buf) { fclose(f); return 1; }
    n = fread(buf, 1, MAX_ROM + 1, f);
    fclose(f);
    if (n == 0 || n > MAX_ROM) {
        fprintf(stderr, "pce-normalize: bad size %lu\n", (unsigned long)n);
        return 1;
    }
    printf("pce-normalize: %s, %lu bytes\n", argv[1], (unsigned long)n);

    /* 1. 512-byte copier header */
    d = buf;
    if ((n & 0x200) && n > 0x200) {
        d += 0x200; n -= 0x200;
        printf("  stripped 512-byte header\n");
    }

    /* 2. TG16 bit order */
    sn = vector_score(d, n, 0) + (has_sig(d, n, 0) ? 10 : 0);
    sr = vector_score(d, n, 1) + (has_sig(d, n, 1) ? 10 : 0);
    printf("  bit-order score: normal=%d reversed=%d\n", sn, sr);
    if (sr > sn) {
        for (i = 0; i < n; i++) d[i] = rev8(d[i]);
        printf("  TG16 bit-swapped data detected -> decoded\n");
    }

    /* 3. huconv.py layout of a 384 KB game: 256K, the same 256K, 128K, 0xFF */
    if (n >= 0xC0000 && !memcmp(d, d + 0x40000, 0x40000) && all_ff(d + 0xA0000, 0x20000)) {
        memmove(d + 0x40000, d + 0x80000, 0x20000);
        n = 0x60000;
        printf("  huconv 384 KB layout -> rebuilt to %lu bytes\n", (unsigned long)n);
    }

    /* 4. Mirrored oversize dumps: halve while the two halves are identical */
    while (n > ONE_MB && (n % 2) == 0 && memcmp(d, d + n / 2, n / 2) == 0) {
        n /= 2;
        printf("  mirrored image -> trimmed to %lu bytes\n", (unsigned long)n);
    }
    if (n >= 2 * ONE_MB && n != SF2_LEN)
        printf("  warning: %lu bytes still triggers the SF2 mapper\n", (unsigned long)n);

    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    if (fwrite(d, 1, n, f) != n) { perror("write"); fclose(f); return 1; }
    fclose(f);
    printf("  wrote %s (%lu bytes)\n", argv[2], (unsigned long)n);
    free(buf);
    return 0;
}
