/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/*
 * fat-label - name a FAT32 partition (the console has no fatlabel).
 *
 *   fat-label <device>                print the label ("" when it has none)
 *   fat-label <device> NAME           set the label
 *   fat-label --if-empty <device> NAME  set it only when the card has no name
 *
 * Writes the label in the boot sector, its backup copy and the root folder's
 * volume-label entry (what Windows shows). FAT32 only. Run it while the
 * partition is not mounted.
 * Exit: 0 ok (or left alone), 1 error, 2 not FAT32.
 */
#define _FILE_OFFSET_BITS 64
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned u32(const unsigned char *p) { return u16(p) | (unsigned)u16(p + 2) << 16; }

static int blank(const unsigned char *l)          /* 11 spaces, zeros or "NO NAME" */
{
    char s[12];
    int i;
    memcpy(s, l, 11); s[11] = 0;
    for (i = 10; i >= 0 && (s[i] == ' ' || s[i] == 0); i--) s[i] = 0;
    return !s[0] || !strcmp(s, "NO NAME");
}

int main(int argc, char **argv)
{
    int ifempty = argc > 1 && !strcmp(argv[1], "--if-empty"), fd, i;
    const char *dev, *name;
    unsigned char bs[512], *dir, lab[11], cur[11];
    unsigned bps, spc, rsvd, nfat, fatsz, root, bk;
    off_t dir_off;
    size_t dir_len;
    unsigned char *ent = NULL;

    argv += ifempty; argc -= ifempty;
    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: fat-label [--if-empty] <device> [NAME]\n"); return 1; }
    dev = argv[1]; name = argc == 3 ? argv[2] : NULL;
    if ((fd = open(dev, name ? O_RDWR : O_RDONLY)) < 0) { perror(dev); return 1; }
    if (pread(fd, bs, 512, 0) != 512) { perror("read"); return 1; }
    bps = u16(bs + 11); spc = bs[13]; rsvd = u16(bs + 14); nfat = bs[16];
    fatsz = u32(bs + 36); root = u32(bs + 44); bk = u16(bs + 50);
    if (bs[510] != 0x55 || bs[511] != 0xAA || u16(bs + 22) != 0 || !fatsz || bs[66] != 0x29 || bps < 512 || !spc) {
        fprintf(stderr, "fat-label: %s is not FAT32\n", dev); return 2;
    }
    dir_len = (size_t)bps * spc;
    dir_off = ((off_t)rsvd + (off_t)nfat * fatsz + (off_t)(root - 2) * spc) * bps;
    if (!(dir = malloc(dir_len)) || pread(fd, dir, dir_len, dir_off) != (ssize_t)dir_len) { perror("read root"); return 1; }
    memcpy(cur, bs + 71, 11);
    for (i = 0; i + 32 <= (int)dir_len; i += 32) {         /* the root folder's label entry */
        unsigned char *e = dir + i;
        if (e[0] == 0) break;
        if (e[0] != 0xE5 && (e[11] & 0x08) && e[11] != 0x0F) { ent = e; memcpy(cur, e, 11); break; }
    }
    if (!name) {
        for (i = 10; i >= 0 && cur[i] == ' '; i--) ;
        printf("%.*s\n", blank(cur) ? 0 : i + 1, cur);
        return 0;
    }
    if (ifempty && !blank(cur)) return 0;
    memset(lab, ' ', 11);
    for (i = 0; i < 11 && name[i]; i++) lab[i] = (unsigned char)toupper((unsigned char)name[i]);
    if (!ent)                                             /* a free slot in the first cluster */
        for (i = 0; i + 32 <= (int)dir_len; i += 32)
            if (dir[i] == 0 || dir[i] == 0xE5) { ent = dir + i; memset(ent, 0, 32); break; }
    if (ent) {
        memcpy(ent, lab, 11); ent[11] = 0x08;
        if (pwrite(fd, dir, dir_len, dir_off) != (ssize_t)dir_len) { perror("write root"); return 1; }
    }
    memcpy(bs + 71, lab, 11);
    if (pwrite(fd, bs, 512, 0) != 512) { perror("write"); return 1; }
    if (bk && bk < rsvd) {
        unsigned char b2[512];
        if (pread(fd, b2, 512, (off_t)bk * bps) == 512 && b2[510] == 0x55) {
            memcpy(b2 + 71, lab, 11);
            if (pwrite(fd, b2, 512, (off_t)bk * bps) != 512) perror("write backup");
        }
    }
    fsync(fd); close(fd);
    printf("fat-label: %s named %.11s\n", dev, lab);
    return 0;
}
