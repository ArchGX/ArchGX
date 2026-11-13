/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/*
 * zipit - write a plain .zip (stored, not compressed) that Windows, macOS
 * and Linux open without extra software. Used for bug reports.
 *
 *   zipit <out.zip> <dir>     every file directly in <dir> (no subfolders)
 *
 * Files over 64 MB are skipped (logs are small; no zip64).
 * Exit: 0 ok, 1 error.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAXF 256
#define MAXSZ (64L << 20)

static unsigned long crc_tab[256];
static void crc_init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        for (c = n, k = 0; k < 8; k++) c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static void w16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void w32(FILE *f, unsigned long v) { w16(f, v & 0xffff); w16(f, v >> 16 & 0xffff); }

struct ent { char name[256]; unsigned long crc, size, off; };
static struct ent e[MAXF];

static int cmp(const void *a, const void *b) { return strcmp(((const struct ent *)a)->name, ((const struct ent *)b)->name); }

int main(int argc, char **argv)
{
    DIR *d;
    struct dirent *de;
    FILE *out;
    int n = 0, i;
    unsigned long cd_off, cd_size;
    static unsigned char buf[65536];
    if (argc != 3) { fprintf(stderr, "usage: zipit <out.zip> <dir>\n"); return 1; }
    crc_init();
    if (!(d = opendir(argv[2]))) { perror(argv[2]); return 1; }
    while ((de = readdir(d)) && n < MAXF) {
        char p[1024];
        struct stat st;
        if (de->d_name[0] == '.' || strlen(de->d_name) > 200) continue;
        snprintf(p, sizeof p, "%s/%s", argv[2], de->d_name);
        if (stat(p, &st) || !S_ISREG(st.st_mode) || st.st_size > MAXSZ) continue;
        snprintf(e[n++].name, sizeof e[0].name, "%s", de->d_name);
    }
    closedir(d);
    qsort(e, n, sizeof e[0], cmp);
    if (!(out = fopen(argv[1], "wb"))) { perror(argv[1]); return 1; }
    for (i = 0; i < n; i++) {
        char p[1024];
        FILE *in;
        size_t r;
        unsigned long c = 0xffffffffUL, sz = 0;
        long hdr = ftell(out);
        snprintf(p, sizeof p, "%s/%s", argv[2], e[i].name);
        if (!(in = fopen(p, "rb"))) { e[i].name[0] = 0; continue; }
        /* local header, sizes patched after the data */
        w32(out, 0x04034b50); w16(out, 10); w16(out, 0); w16(out, 0);
        w16(out, 0); w16(out, 0x21);                       /* 1980-01-01 00:00 */
        w32(out, 0); w32(out, 0); w32(out, 0);
        w16(out, strlen(e[i].name)); w16(out, 0);
        fwrite(e[i].name, 1, strlen(e[i].name), out);
        while ((r = fread(buf, 1, sizeof buf, in)) > 0) {
            size_t k;
            for (k = 0; k < r; k++) c = crc_tab[(c ^ buf[k]) & 255] ^ (c >> 8);
            if (fwrite(buf, 1, r, out) != r) { fclose(in); fclose(out); return 1; }
            sz += r;
            if (sz > (unsigned long)MAXSZ) break;            /* a log still growing */
        }
        fclose(in);
        e[i].crc = c ^ 0xffffffffUL; e[i].size = sz; e[i].off = hdr;
        fseek(out, hdr + 14, SEEK_SET);
        w32(out, e[i].crc); w32(out, sz); w32(out, sz);
        fseek(out, 0, SEEK_END);
    }
    cd_off = ftell(out);
    for (i = 0; i < n; i++) {
        if (!e[i].name[0]) continue;
        w32(out, 0x02014b50); w16(out, 0x031e); w16(out, 10); w16(out, 0); w16(out, 0);
        w16(out, 0); w16(out, 0x21);
        w32(out, e[i].crc); w32(out, e[i].size); w32(out, e[i].size);
        w16(out, strlen(e[i].name)); w16(out, 0); w16(out, 0); w16(out, 0); w16(out, 0);
        w32(out, 0100644UL << 16); w32(out, e[i].off);
        fwrite(e[i].name, 1, strlen(e[i].name), out);
    }
    cd_size = ftell(out) - cd_off;
    {
        int cnt = 0;
        for (i = 0; i < n; i++) if (e[i].name[0]) cnt++;
        w32(out, 0x06054b50); w16(out, 0); w16(out, 0); w16(out, cnt); w16(out, cnt);
        w32(out, cd_size); w32(out, cd_off); w16(out, 0);
    }
    if (fclose(out)) { perror(argv[1]); return 1; }
    return 0;
}
