/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/*
 * cd-rip - copy a PC Engine CD from a USB CD drive into the game library.
 *
 *   cd-rip <device> <games-dir> <status-file>          rip /dev/sr0
 *   cd-rip --id <device>                               print the disc id
 *   cd-rip --test <toc-file> <image> <games-dir> <status-file>
 *       simulate a drive (toc-file: "track type startLBA" lines plus
 *       "leadout LBA"; image: 2352-byte sectors from LBA 0)
 *
 * Result: <games-dir>/cd-<id>/cd-<id>.cue plus one "cd-<id> (Track NN).bin"
 * per track, all 2352-byte sectors (data MODE1/2352, audio AUDIO). Each file
 * runs from its track's start to the next track's start, so the disc's LBAs
 * are kept. Work happens in a hidden ".rip-<id>" folder, renamed at the end.
 *
 * Reading: SCSI READ CD (0xBE) through SG_IO, 16 sectors at a time. A failed
 * chunk is retried, then read sector by sector (3 tries each); a sector that
 * still fails is written as zeros and counted. If the drive refuses raw reads
 * of a data track, that track falls back to normal 2048-byte reads (MODE1/2048).
 *
 * Progress goes to <status-file> in pce-launcher --status format:
 * message, subtitle, percent, state.
 *
 * Exit: 0 ok (prints the cue path), 1 error, 2 no disc, 3 not enough space.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <linux/cdrom.h>
#include <scsi/sg.h>

#define SECTOR 2352
#define CHUNK  16
#define MAXT   99

typedef struct { int num, data, start, end, cooked; } Track;
static Track trk[MAXT + 1];
static int ntrk;
static int fd = -1;
static FILE *test_img;
static const char *status_path;

static void status(const char *msg, const char *sub, int pct, const char *state)
{
    char tmp[600];
    FILE *f;
    if (!status_path) return;
    snprintf(tmp, sizeof tmp, "%s.new", status_path);
    if (!(f = fopen(tmp, "w"))) return;
    if (pct < 0) fprintf(f, "%s\n%s\n-\n%s\n", msg, sub, state);
    else fprintf(f, "%s\n%s\n%d\n%s\n", msg, sub, pct, state);
    fclose(f);
    rename(tmp, status_path);
}

/* ------------------------------------------------------------ reading */

static int read_cd(int lba, int n, int data, unsigned char *buf)
{
    unsigned char cdb[12] = { 0xBE }, sense[32];
    sg_io_hdr_t io;
    if (test_img) {
        if (fseek(test_img, (long)lba * SECTOR, SEEK_SET) != 0) return -1;
        if ((int)fread(buf, SECTOR, n, test_img) != n) return -1;
        if (getenv("CDRIP_TEST_BAD")) {        /* simulate one unreadable sector */
            int bad = atoi(getenv("CDRIP_TEST_BAD"));
            if (bad >= lba && bad < lba + n) return -1;
        }
        return 0;
    }
    cdb[2] = lba >> 24; cdb[3] = lba >> 16; cdb[4] = lba >> 8; cdb[5] = lba;
    cdb[6] = n >> 16; cdb[7] = n >> 8; cdb[8] = n;
    cdb[9] = data ? 0xF8 : 0x10;   /* data: sync+headers+user data+EDC/ECC; audio: user data */
    memset(&io, 0, sizeof io);
    io.interface_id = 'S';
    io.cmd_len = sizeof cdb; io.cmdp = cdb;
    io.dxfer_direction = SG_DXFER_FROM_DEV;
    io.dxferp = buf; io.dxfer_len = n * SECTOR;
    io.sbp = sense; io.mx_sb_len = sizeof sense;
    io.timeout = 30000;
    if (ioctl(fd, SG_IO, &io) < 0) return -1;
    if (io.status || io.host_status || io.driver_status) return -1;
    return 0;
}

/* normal 2048-byte reads, for drives that refuse raw data sectors */
static int read_cooked(int lba, int n, unsigned char *buf)
{
    if (test_img) {
        int i;
        for (i = 0; i < n; i++) {
            unsigned char s[SECTOR];
            if (fseek(test_img, (long)(lba + i) * SECTOR, SEEK_SET) || fread(s, SECTOR, 1, test_img) != 1) return -1;
            memcpy(buf + i * 2048, s + 16, 2048);
        }
        return 0;
    }
    return pread(fd, buf, (size_t)n * 2048, (off_t)lba * 2048) == (ssize_t)n * 2048 ? 0 : -1;
}

/* --------------------------------------------------------------- TOC */

static int read_toc(void)
{
    struct cdrom_tochdr h;
    struct cdrom_tocentry e;
    int t;
    if (ioctl(fd, CDROMREADTOCHDR, &h) < 0) return -1;
    ntrk = 0;
    for (t = h.cdth_trk0; t <= h.cdth_trk1 && ntrk < MAXT; t++) {
        memset(&e, 0, sizeof e);
        e.cdte_track = t; e.cdte_format = CDROM_LBA;
        if (ioctl(fd, CDROMREADTOCENTRY, &e) < 0) return -1;
        trk[ntrk].num = t; trk[ntrk].data = (e.cdte_ctrl & CDROM_DATA_TRACK) != 0;
        trk[ntrk].start = e.cdte_addr.lba; ntrk++;
    }
    memset(&e, 0, sizeof e);
    e.cdte_track = CDROM_LEADOUT; e.cdte_format = CDROM_LBA;
    if (ioctl(fd, CDROMREADTOCENTRY, &e) < 0) return -1;
    trk[ntrk].start = e.cdte_addr.lba;          /* lead-out */
    return ntrk ? 0 : -1;
}

static int read_test_toc(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[128], type[16];
    int num, lba;
    if (!f) return -1;
    ntrk = 0;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "leadout %d", &lba) == 1) trk[ntrk].start = lba;
        else if (sscanf(line, "%d %15s %d", &num, type, &lba) == 3 && ntrk < MAXT) {
            trk[ntrk].num = num; trk[ntrk].data = !strcmp(type, "data"); trk[ntrk].start = lba; ntrk++;
        }
    }
    fclose(f);
    return ntrk ? 0 : -1;
}

/* disc id: FNV-1a over the TOC (track starts, types, lead-out) */
static unsigned disc_id(void)
{
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i <= ntrk; i++) {
        unsigned v = (unsigned)trk[i].start ^ (i < ntrk && trk[i].data ? 0x80000000u : 0);
        int b;
        for (b = 0; b < 4; b++) { h ^= (v >> (8 * b)) & 0xff; h *= 16777619u; }
    }
    return h;
}

/* ---------------------------------------------------------------- rip */

static int rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char p[1024];
    if (d) {
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            unlink(p);
        }
        closedir(d);
    }
    return rmdir(dir);
}

static void fmt_time(char *out, size_t n, double s)
{
    if (s < 60) snprintf(out, n, "%d s", (int)s + 1);
    else snprintf(out, n, "%d min", (int)(s / 60) + 1);
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int rip(const char *games)
{
    char id[16], tmpdir[600], final[600], path[800], cue[800], sub[160], tl[32];
    unsigned char *buf = malloc(CHUNK * SECTOR);
    long long total = 0, done = 0;
    int i, bad = 0, lastpct = -1;
    double t0 = now_s(), lastst = 0;
    struct statvfs vf;
    FILE *cf;

    if (!buf) return 1;
    for (i = 0; i < ntrk; i++) { trk[i].end = trk[i + 1].start; total += trk[i].end - trk[i].start; }
    snprintf(id, sizeof id, "%08x", disc_id());
    snprintf(final, sizeof final, "%s/cd-%s", games, id);
    snprintf(cue, sizeof cue, "%s/cd-%s.cue", final, id);
    if (access(cue, F_OK) == 0) { printf("%s\n", cue); fprintf(stderr, "cd-rip: already in the library\n"); return 0; }
    if (statvfs(games, &vf) == 0 && (long long)vf.f_bavail * vf.f_frsize < total * SECTOR + (16 << 20)) {
        fprintf(stderr, "cd-rip: not enough space (%lld MB needed)\n", total * SECTOR >> 20);
        status("Not enough space on the SD card", "", -1, "error");
        return 3;
    }
    snprintf(tmpdir, sizeof tmpdir, "%s/.rip-%s", games, id);
    rm_tree(tmpdir);
    if (mkdir(tmpdir, 0755) != 0) { perror(tmpdir); return 1; }
    fprintf(stderr, "cd-rip: disc %s, %d tracks, %lld sectors (%lld MB)\n", id, ntrk, total, total * SECTOR >> 20);

    for (i = 0; i < ntrk; i++) {
        Track *t = &trk[i];
        FILE *o;
        int lba = t->start, sz = SECTOR;
        snprintf(path, sizeof path, "%s/cd-%s (Track %02d).bin", tmpdir, id, t->num);
        if (!(o = fopen(path, "wb"))) { perror(path); goto fail; }
        while (lba < t->end) {
            int n = t->end - lba < CHUNK ? t->end - lba : CHUNK, ok, k, r;
            if (t->cooked) ok = read_cooked(lba, n, buf) == 0;
            else {
                ok = read_cd(lba, n, t->data, buf) == 0 || read_cd(lba, n, t->data, buf) == 0;
                if (!ok && t->data && lba == t->start && read_cooked(lba, n, buf) == 0) {
                    /* the drive won't give raw data sectors: this track goes 2048-byte */
                    fprintf(stderr, "cd-rip: track %d: raw reads refused, using 2048-byte sectors\n", t->num);
                    t->cooked = 1; sz = 2048; ok = 1;
                }
            }
            if (!ok) {                       /* sector by sector */
                for (k = 0; k < n; k++) {
                    unsigned char *s = buf + k * sz;
                    for (r = 0; r < 3; r++)
                        if ((t->cooked ? read_cooked(lba + k, 1, s) : read_cd(lba + k, 1, t->data, s)) == 0) break;
                    if (r == 3) { memset(s, 0, sz); bad++; fprintf(stderr, "cd-rip: unreadable sector %d\n", lba + k); }
                }
            }
            if (fwrite(buf, sz, n, o) != (size_t)n) { fclose(o); fprintf(stderr, "cd-rip: write failed: %s\n", strerror(errno)); goto fail; }
            lba += n; done += n;
            {
                double el = now_s() - t0, rate = el > 0 ? done * SECTOR / el / 1e6 : 0;
                int pct = (int)(done * 100 / total);
                if (pct != lastpct || now_s() - lastst > 1) {
                    fmt_time(tl, sizeof tl, rate > 0 ? (total - done) * SECTOR / 1e6 / rate : 0);
                    snprintf(sub, sizeof sub, "Track %d of %d  -  %.1f MB/s  -  about %s left", i + 1, ntrk, rate, tl);
                    status("Copying CD...", sub, pct, "");
                    lastpct = pct; lastst = now_s();
                }
            }
        }
        if (fclose(o) != 0) goto fail;
    }

    snprintf(path, sizeof path, "%s/cd-%s.cue", tmpdir, id);
    if (!(cf = fopen(path, "w"))) goto fail;
    for (i = 0; i < ntrk; i++) {
        fprintf(cf, "FILE \"cd-%s (Track %02d).bin\" BINARY\r\n", id, trk[i].num);
        fprintf(cf, "  TRACK %02d %s\r\n", trk[i].num,
                !trk[i].data ? "AUDIO" : trk[i].cooked ? "MODE1/2048" : "MODE1/2352");
        fprintf(cf, "    INDEX 01 00:00:00\r\n");
    }
    if (fclose(cf) != 0) goto fail;
    sync();
    if (rename(tmpdir, final) != 0) { perror(final); goto fail; }
    sync();
    fprintf(stderr, "cd-rip: done in %.0f s, %d unreadable sectors\n", now_s() - t0, bad);
    printf("%s\n", cue);
    free(buf);
    return bad ? 4 : 0;

fail:
    rm_tree(tmpdir);
    free(buf);
    status("Could not copy the CD", "See logs/gx-start.log", -1, "error");
    return 1;
}

int main(int argc, char **argv)
{
    int r;
    if (argc == 3 && !strcmp(argv[1], "--id")) {
        if ((fd = open(argv[2], O_RDONLY | O_NONBLOCK)) < 0 || read_toc() != 0) return 2;
        printf("%08x %d\n", disc_id(), ntrk);
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "--test")) {
        if (read_test_toc(argv[2]) != 0 || !(test_img = fopen(argv[3], "rb"))) return 1;
        status_path = argv[5];
        return rip(argv[4]);
    }
    if (argc != 4) {
        fprintf(stderr, "usage: cd-rip <device> <games-dir> <status-file>\n");
        return 1;
    }
    status_path = argv[3];
    if ((fd = open(argv[1], O_RDONLY | O_NONBLOCK)) < 0) { perror(argv[1]); return 2; }
    if (ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) != CDS_DISC_OK) { fprintf(stderr, "cd-rip: no disc\n"); return 2; }
    if (read_toc() != 0) { fprintf(stderr, "cd-rip: cannot read the table of contents\n"); return 2; }
    r = rip(argv[2]);
    close(fd);
    return r;
}
