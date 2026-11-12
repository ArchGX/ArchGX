/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/*
 * pce-launcher - full-screen game picker for the Retron GX SD-card library.
 *
 * Usage: pce-launcher <library-dir> <out-file>
 * Exit:  0 = game chosen (path written to <out-file>, plus a second line
 *        "resume" when the player picked Resume; "__RESTART__" for a console
 *        restart, "__RETROARCH__" for the full RetroArch menu),
 *        2 = cartridge detected, 3 = quit, 1 = error
 *
 * Controls (buttons read from RetroArch autoconfig, matched by USB id/name):
 *   D-pad        move / page          I or RUN   play
 *   SELECT       options: Rename, Delete
 *   II           back
 * Rename keyboard: D-pad move, I type, II erase, RUN done, SELECT cancel.
 *
 * Test hooks: PCE_LAUNCHER_KEYS="down,select,ok,..." queues input at start;
 * PCE_LAUNCHER_SHOT=<file.bmp> saves the screen once that input is handled.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>

#define MAX_GAMES 4096
#define NAME_MAX_LEN 64
#ifndef AUTOCONF_DIR
#define AUTOCONF_DIR "/userdata/retroarch/autoconfig"
#endif
#ifndef FONT_PATH
#define FONT_PATH "/etc/Inter-Medium.otf"
#endif
#ifndef FONT_LIGHT
#define FONT_LIGHT "/etc/Inter-Light.otf"
#endif
#ifndef BG_PATH
#define BG_PATH "/usr/share/archgx/background.png"
#endif
#ifndef LAST_FILE
#define LAST_FILE "/tmp/library/last.txt"
#endif

/* ------------------------------------------------------------------ data */

enum { K_HUCARD, K_SGX, K_CD };
/* check: from read-check.txt. C_NONE (CD games, not checked yet) and
 * C_UNKNOWN show grey, C_OK (matches the game database) green, C_MOD (known
 * title, other checksum: hack, revision or bad read) amber */
enum { C_NONE, C_UNKNOWN, C_OK, C_MOD };
typedef struct { char *path; char *label; SDL_Texture *tex; int tw, th; int recent; int kind; int check; char crc[9]; } Game;
#define ARCHGX_VERSION "GP2-beta8"
#define LOGO_PATH "/usr/share/archgx/logo.png"
#define EMBLEM_PATH "/usr/share/archgx/emblem.png"
#define MAX_RECENT 5

static Game games[MAX_GAMES];
static int ngames;          /* list entries, including the restart entry */
static int nreal;           /* actual games */
static char lib_dir[512], data_dir[512], titles_path[512], localdb_path[512], recent_path[512], bios_dir[512], readcheck_path[512];

/* ------------------------------------------------------------- titles */
/* titles.txt: "file<TAB>title" lines; the last entry for a file wins.
 * File names never change once created, so saves stay matched to them;
 * Rename only writes a new title here. */
#define MAX_TITLES 4096
static char *t_file[MAX_TITLES], *t_title[MAX_TITLES];
static int ntitles;

static void load_titles(void)
{
    FILE *f;
    char line[1024];
    while (ntitles) { ntitles--; free(t_file[ntitles]); free(t_title[ntitles]); }
    if (!(f = fopen(titles_path, "r"))) return;
    while (fgets(line, sizeof line, f) && ntitles < MAX_TITLES) {
        char *tab = strchr(line, '\t'), *nl;
        if (!tab) continue;
        *tab = '\0';
        if ((nl = strpbrk(tab + 1, "\r\n"))) *nl = '\0';
        t_file[ntitles] = strdup(line); t_title[ntitles] = strdup(tab + 1);
        ntitles++;
    }
    fclose(f);
}

static const char *title_for(const char *file)
{
    int i;
    for (i = ntitles - 1; i >= 0; i--)
        if (!strcmp(t_file[i], file)) return t_title[i][0] ? t_title[i] : NULL;
    return NULL;
}

static char saves_dir[512];

/* does this game have a save state? (RetroArch names them <file>.state, .state1 ...) */
static int has_state(const char *path)
{
    const char *file = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    char p[1024];
    const char *dot = strrchr(file, '.');
    int n = dot ? (int)(dot - file) : (int)strlen(file), i;
    if (n <= 0) return 0;
    for (i = -1; i <= 9; i++) {
        if (i < 0) snprintf(p, sizeof p, "%s/%.*s.state", saves_dir, n, file);
        else snprintf(p, sizeof p, "%s/%.*s.state%d", saves_dir, n, file, i);
        if (access(p, F_OK) == 0) return 1;
    }
    return 0;
}

/* "cart-1234abcd.pce" -> crc "1234abcd" (an unidentified HuCard dump) */
static int cart_crc(const char *file, char crc[9])
{
    int i;
    if (strncmp(file, "cart-", 5) || strlen(file) != 17 || strcasecmp(file + 13, ".pce")) return 0;
    for (i = 0; i < 8; i++) if (!isxdigit((unsigned char)file[5 + i])) return 0;
    memcpy(crc, file + 5, 8); crc[8] = '\0';
    return 1;
}
static volatile sig_atomic_t quit_flag;
static volatile sig_atomic_t last_event;

static int has_ext(const char *n, const char *ext)
{
    size_t l = strlen(n), e = strlen(ext);
    return l > e && strcasecmp(n + l - e, ext) == 0;
}

/* library files: HuCards, SuperGrafx and CD images (-1: not a game) */
static int game_kind(const char *n)
{
    if (has_ext(n, ".pce")) return K_HUCARD;
    if (has_ext(n, ".sgx")) return K_SGX;
    if (has_ext(n, ".cue") || has_ext(n, ".chd") || has_ext(n, ".ccd") || has_ext(n, ".m3u")) return K_CD;
    return -1;
}

/* file names a .cue (FILE "x" ...) or .m3u (one per line) refers to; calls
 * fn(dir, name, arg) for each. Returns the number found. */
static int each_ref(const char *dir, const char *file, void (*fn)(const char *, const char *, void *), void *arg)
{
    char p[1024], line[1024];
    int n = 0, m3u = has_ext(file, ".m3u");
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, file);
    if (!(f = fopen(p, "r"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char *a = line, *b;
        line[strcspn(line, "\r\n")] = '\0';
        while (*a == ' ' || *a == '\t') a++;
        if (m3u) {
            if (!*a || *a == '#') continue;
        } else {
            if (strncasecmp(a, "FILE", 4) || (a[4] != ' ' && a[4] != '\t')) continue;
            a += 5; while (*a == ' ' || *a == '\t') a++;
            if (*a == '"') { a++; if (!(b = strchr(a, '"'))) continue; *b = '\0'; }
            else if ((b = strpbrk(a, " \t"))) *b = '\0';
        }
        if (strchr(a, '/') || strchr(a, '\\') || !*a) continue;   /* same folder only */
        fn(dir, a, arg); n++;
    }
    fclose(f);
    return n;
}

/* read-check.txt (written by gx-start): "file<TAB>state<TAB>crc32", last line
 * wins. state: ok / mismatch / unknown (beta7 also wrote "unstable") */
static int read_check(const char *file, char *crc)
{
    char line[600], *st, *c;
    int r = C_NONE;
    size_t n = strlen(file);
    FILE *f = fopen(readcheck_path, "r");
    crc[0] = 0;
    if (!f) return C_NONE;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, file, n) || line[n] != '\t') continue;
        st = line + n + 1;
        c = strchr(st, '\t');
        r = !strncmp(st, "ok", 2) ? C_OK : !strncmp(st, "unknown", 7) ? C_UNKNOWN : C_MOD;
        crc[0] = 0;
        if (c) { snprintf(crc, 9, "%.8s", c + 1); if (strlen(crc) < 8) crc[0] = 0; }
        else if (r == C_OK) r = C_NONE;          /* beta7 "ok" line: checked again by gx-start */
    }
    fclose(f);
    return r;
}

/* debug.cfg (Debug screen, hidden: hold SELECT on About) */
enum { D_FPS, D_CRC, D_TWICE, D_USBPAUSE, D_N };
static const char *dbg_key[D_N] = { "fps", "crc", "readtwice", "usbpause" };
static const char *dbg_name[D_N] = { "FPS counter in games", "Checksums in the library", "Read every HuCard twice", "Pause the USB log during games" };
static int dbg_val[D_N] = { 0, 0, 0, 1 };
static char debug_path[512];
static void debug_load(void)
{
    char line[200];
    int i;
    FILE *f = fopen(debug_path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        for (i = 0; i < D_N; i++) {
            size_t k = strlen(dbg_key[i]);
            if (!strncmp(line, dbg_key[i], k) && line[k] == '=') dbg_val[i] = atoi(line + k + 1) ? 1 : 0;
        }
    fclose(f);
}
static void debug_save(void)
{
    char tmp[600];
    int i;
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.new", debug_path);
    if (!(f = fopen(tmp, "w"))) return;
    fprintf(f, "# ArchGX debug options (Debug screen)\n");
    for (i = 0; i < D_N; i++) fprintf(f, "%s=%d\n", dbg_key[i], dbg_val[i]);
    if (fclose(f) == 0) rename(tmp, debug_path);
}

static int recent_rank(const char *file);
static void add_game(const char *dir, const char *name, int kind)
{
    size_t nl = strlen(name);
    const char *t = title_for(name);
    char crc[9], *p, *lab = NULL;
    if (ngames >= MAX_GAMES - 8) return;
    if (!(p = malloc(strlen(dir) + nl + 2))) return;
    sprintf(p, "%s/%s", dir, name);
    if (t) lab = strdup(t);
    else if (cart_crc(name, crc)) {
        if ((lab = malloc(32))) snprintf(lab, 32, "Unknown HuCard (%s)", crc);
    } else if ((lab = strdup(name))) {
        char *dot = strrchr(lab, '.');
        if (dot && dot != lab) *dot = '\0';
    }
    if (!lab) { free(p); return; }
    games[ngames].path = p; games[ngames].label = lab; games[ngames].tex = NULL; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
    games[ngames].recent = recent_rank(name); games[ngames].kind = kind;
    games[ngames].check = kind == K_CD ? C_NONE : read_check(name, games[ngames].crc);
    ngames++;
}

/* names referred to by a .m3u/.cue in the same folder are not listed */
#define MAX_HIDE 256
static char *hide[MAX_HIDE];
static int nhide;
static void hide_ref(const char *dir, const char *name, void *arg)
{
    (void)dir; (void)arg;
    if (nhide < MAX_HIDE) hide[nhide++] = strdup(name);
}
static int hidden(const char *name)
{
    int i;
    for (i = 0; i < nhide; i++) if (hide[i] && !strcasecmp(hide[i], name)) return 1;
    return 0;
}

/* one folder: its games, minus discs that a playlist (.m3u) already lists */
static void scan_dir(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while (nhide) free(hide[--nhide]);
    while ((e = readdir(d)))
        if (e->d_name[0] != '.' && has_ext(e->d_name, ".m3u")) each_ref(dir, e->d_name, hide_ref, NULL);
    rewinddir(d);
    while ((e = readdir(d)) && ngames < MAX_GAMES - 8) {
        int k;
        struct stat st;
        char p[1024];
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (depth == 0 && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) continue;   /* folders: second pass */
        if ((k = game_kind(e->d_name)) < 0 || hidden(e->d_name)) continue;
        add_game(dir, e->d_name, k);
    }
    if (depth == 0) {                       /* one level of game folders (CD games) */
        rewinddir(d);
        while ((e = readdir(d)) && ngames < MAX_GAMES - 8) {
            struct stat st;
            char p[1024];
            if (e->d_name[0] == '.') continue;
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
                int before = ngames;
                const char *f;
                scan_dir(p, 1);
                /* a folder holding one game: show the folder's name (unless retitled) */
                if (ngames == before + 1 && (f = strrchr(games[before].path, '/')) && !title_for(f + 1)) {
                    char *lab = strdup(e->d_name);
                    if (lab) { free(games[before].label); games[before].label = lab; }
                }
            }
        }
    }
    closedir(d);
}

/* recently played games first (newest first), then the rest A-Z */
static int cmp_games(const void *a, const void *b)
{
    const Game *x = (const Game *)a, *y = (const Game *)b;
    if (x->recent || y->recent) {
        if (!x->recent) return 1;
        if (!y->recent) return -1;
        return x->recent - y->recent;
    }
    return strcasecmp(x->label, y->label);
}

static int recent_rank(const char *file)
{
    char line[512];
    int n = 0;
    FILE *f = fopen(recent_path, "r");
    if (!f) return 0;
    while (n < MAX_RECENT && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!*line) continue;
        n++;
        if (!strcmp(line, file)) { fclose(f); return n; }
    }
    fclose(f);
    return 0;
}

static void free_games(void)
{
    int i;
    for (i = 0; i < ngames; i++) {
        free(games[i].path); free(games[i].label);
        if (games[i].tex) SDL_DestroyTexture(games[i].tex);
    }
    ngames = nreal = 0;
}

/* a disc in a USB CD drive (/dev/sr0)? */
#include <limits.h>
#include <linux/cdrom.h>
static int cd_present;
static int cd_check(void)
{
    const char *dev = getenv("PCE_LAUNCHER_CD") ? getenv("PCE_LAUNCHER_CD") : "/dev/sr0";   /* test hook */
    int fd = open(dev, O_RDONLY | O_NONBLOCK), now;
    if (fd < 0) now = 0;
    else {
        now = getenv("PCE_LAUNCHER_CD") ? 1 : ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT) == CDS_DISC_OK;
        close(fd);
    }
    if (now == cd_present) return 0;
    cd_present = now;
    SDL_Log("cd drive: %s", now ? "disc inserted" : "no disc");
    return 1;
}

static void scan_library(void)
{
    free_games();
    load_titles();
    scan_dir(lib_dir, 0);
    qsort(games, ngames, sizeof(Game), cmp_games);
    nreal = ngames;
    if (cd_present) {                        /* a disc in a USB CD drive */
        games[ngames].path = strdup("__RIPCD__");
        games[ngames].label = strdup("Play the CD in the drive");
        games[ngames].tex = NULL; games[ngames].recent = 0; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
        ngames++;
    }
    if (getenv("PCE_LAUNCHER_CART")) {      /* a HuCard is sitting in the slot */
        games[ngames].path = strdup("__CART__");
        games[ngames].label = strdup("Play the HuCard in the slot");
        games[ngames].tex = NULL; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
        ngames++;
    }
    games[ngames].path = strdup("__PADTEST__");
    games[ngames].label = strdup("Controller test");
    games[ngames].tex = NULL; games[ngames].recent = 0; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
    ngames++;
    games[ngames].path = strdup("__ABOUT__");
    games[ngames].label = strdup("About");
    games[ngames].tex = NULL; games[ngames].recent = 0; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
    ngames++;
    games[ngames].path = strdup("__RETROARCH__");
    games[ngames].label = strdup("RetroArch (advanced)");
    games[ngames].tex = NULL; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
    ngames++;
    games[ngames].path = strdup("__RESTART__");
    games[ngames].label = strdup("Restart console");
    games[ngames].tex = NULL; games[ngames].kind = K_HUCARD; games[ngames].check = C_NONE; games[ngames].crc[0] = 0;
    ngames++;
}

static int find_game(const char *path)
{
    int i;
    for (i = 0; i < nreal; i++) if (strcmp(games[i].path, path) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------ file ops */

static int files_equal(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int eq = 0;
    if (fa && fb) {
        char ba[4096], bb[4096];
        size_t na, nb;
        eq = 1;
        do {
            na = fread(ba, 1, sizeof ba, fa);
            nb = fread(bb, 1, sizeof bb, fb);
            if (na != nb || memcmp(ba, bb, na) != 0) { eq = 0; break; }
        } while (na > 0);
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return eq;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out;
    char buf[8192];
    size_t n;
    if (!in) return -1;
    out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    return fclose(out);
}

static int exists(const char *p) { return access(p, F_OK) == 0; }

/* Delete a library game. Copies of it that the library made in data/ (to
 * run it from there) go too; raw cartridge dumps and save files stay. */
static void unlink_ref(const char *dir, const char *name, void *arg)
{
    char p[1024];
    (void)arg;
    if (has_ext(name, ".cue")) each_ref(dir, name, unlink_ref, NULL);   /* m3u -> cue -> tracks */
    snprintf(p, sizeof p, "%s/%s", dir, name);
    unlink(p);
}

static int delete_game(int idx)
{
    char base[NAME_MAX_LEN * 2], p[1024], dir[1024];
    static const char *suffix[] = { ".pce", "-fixed.pce" };
    const char *path = games[idx].path, *slash = strrchr(path, '/'), *file = slash ? slash + 1 : path;
    int i;
    snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
    if (games[idx].kind == K_CD) {
        /* the disc's own files: tracks named in a .cue, discs in a .m3u, a .ccd's .img/.sub */
        if (has_ext(file, ".cue") || has_ext(file, ".m3u")) each_ref(dir, file, unlink_ref, NULL);
        else if (has_ext(file, ".ccd")) {
            static const char *cx[] = { ".img", ".sub", ".IMG", ".SUB" };
            int n = (int)strlen(file) - 4;
            for (i = 0; i < 4; i++) { snprintf(p, sizeof p, "%s/%.*s%s", dir, n, file, cx[i]); unlink(p); }
        }
    } else {
        snprintf(base, sizeof base, "%s", games[idx].label);
        for (i = 0; i < 2; i++) {
            snprintf(p, sizeof p, "%s/%s%s", data_dir, base, suffix[i]);
            if (exists(p) && files_equal(p, path)) unlink(p);
        }
    }
    if (unlink(path) != 0) return -1;
    if (strcmp(dir, lib_dir)) rmdir(dir);      /* a game folder: removed once empty */
    sync();
    SDL_Log("deleted %s", games[idx].path);
    return 0;
}

/* Rename a library game: record a new title for its file (the file and its
 * saves keep their names). Naming an unidentified HuCard dump (cart-<crc32>)
 * also adds it to the local game database (gamedb-local.csv), ready to share. */
static int rename_game(int idx, const char *newname, char *newpath, size_t np)
{
    const char *file = strrchr(games[idx].path, '/') ? strrchr(games[idx].path, '/') + 1 : games[idx].path;
    char crc[9];
    FILE *f = fopen(titles_path, "a");
    if (!f) return -1;
    fprintf(f, "%s\t%s\n", file, newname);
    fclose(f);
    if (cart_crc(file, crc)) {
        int fresh = !exists(localdb_path);
        if ((f = fopen(localdb_path, "a"))) {
            const char *c;
            if (fresh) fprintf(f, "crc32,header,title,region,notes\r\n");
            fprintf(f, "0x%s,,\"", crc);                 /* 0x: spreadsheets keep it as text */
            for (c = newname; *c; c++) { if (*c == '"') fputc('"', f); fputc(*c, f); }
            fprintf(f, "\",,named on console\r\n");
            fclose(f);
        }
    }
    sync();
    snprintf(newpath, np, "%s", games[idx].path);
    SDL_Log("titled %s -> %s", file, newname);
    return 0;
}

/* SELECT menu entries for a game, by kind */
enum { OP_RENAME, OP_SGX, OP_HUCARD, OP_BIOS, OP_DELETE, OP_CANCEL };
static int game_ops(int kind, int ops[6], const char *names[6])
{
    int n = 0;
    ops[n] = OP_RENAME; names[n++] = "Rename";
    if (kind == K_HUCARD) {
        ops[n] = OP_SGX;  names[n++] = "Mark as SuperGrafx";
        ops[n] = OP_BIOS; names[n++] = "Use as CD System Card";
    } else if (kind == K_SGX) {
        ops[n] = OP_HUCARD; names[n++] = "Mark as HuCard";
    }
    ops[n] = OP_DELETE; names[n++] = "Delete";
    ops[n] = OP_CANCEL; names[n++] = "Cancel";
    return n;
}

/* rewrite recent.txt with one file name replaced (or removed: to == NULL) */
static void recent_rename(const char *from, const char *to)
{
    char tmp[600], line[512];
    FILE *in = fopen(recent_path, "r"), *out;
    if (!in) return;
    snprintf(tmp, sizeof tmp, "%s.new", recent_path);
    if (!(out = fopen(tmp, "w"))) { fclose(in); return; }
    while (fgets(line, sizeof line, in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, from)) { if (to) fprintf(out, "%s\n", to); }
        else if (*line) fprintf(out, "%s\n", line);
    }
    fclose(in); fclose(out);
    rename(tmp, recent_path);
}

/* Mark a HuCard as SuperGrafx (.pce -> .sgx) or back. Only the extension
 * changes, so saves (named without it) stay matched; title and recent follow. */
static int set_kind(int idx, int to_sgx, char *newpath, size_t np)
{
    const char *path = games[idx].path, *file = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    const char *dot = strrchr(file, '.');
    char newfile[512];
    FILE *f;
    if (!dot) return -1;
    snprintf(newfile, sizeof newfile, "%.*s%s", (int)(dot - file), file, to_sgx ? ".sgx" : ".pce");
    snprintf(newpath, np, "%.*s%s", (int)(file - path), path, newfile);
    if (exists(newpath)) return -1;
    if (rename(path, newpath) != 0) return -1;
    if ((f = fopen(titles_path, "a"))) {         /* keep the name shown */
        fprintf(f, "%s\t%s\n", newfile, games[idx].label);
        fclose(f);
    }
    recent_rename(file, newfile);
    sync();
    SDL_Log("kind: %s -> %s", file, newfile);
    return 0;
}

static unsigned long crc32_buf(const unsigned char *p, size_t n)
{
    unsigned long c = 0xffffffffUL;
    int k;
    while (n--) { c ^= *p++; for (k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xedb88320UL : c >> 1; }
    return c ^ 0xffffffffUL;
}

/* Use a dump as the CD BIOS: its first 256 KB (a System Card's whole ROM;
 * the reader may dump more) go to bios/, named after the card when its
 * checksum is known, else syscard3.pce (the name the cores try first).
 * The dump then leaves the library. */
static int use_as_bios(int idx, char *out, size_t on)
{
    static const struct { unsigned long crc; const char *name; } known[] = {
        { 0x6d9a73efUL, "syscard3.pce" }, { 0x2b5b75feUL, "syscard3u.pce" },
        { 0x52520bc6UL, "syscard2.pce" }, { 0x283b74e0UL, "syscard2.pce" },
        { 0xff2a5ec3UL, "syscard2u.pce" }, { 0x3f9f95a4UL, "syscard1.pce" },
        { 0x51a12d90UL, "gexpress.pce" } };
    const char *name = "syscard3.pce";
    unsigned char *buf = malloc(262144);
    char dst[600], part[640];
    size_t n;
    unsigned long c;
    unsigned i;
    FILE *in, *o;
    if (!buf) return -1;
    if (!(in = fopen(games[idx].path, "rb"))) { free(buf); return -1; }
    n = fread(buf, 1, 262144, in);
    fclose(in);
    if (n < 8192) { free(buf); return -1; }
    c = crc32_buf(buf, n);
    for (i = 0; i < sizeof known / sizeof known[0]; i++) if (known[i].crc == c) name = known[i].name;
    mkdir(bios_dir, 0755);
    snprintf(dst, sizeof dst, "%s/%s", bios_dir, name);
    snprintf(part, sizeof part, "%s.part", dst);
    if (!(o = fopen(part, "wb"))) { free(buf); return -1; }
    if (fwrite(buf, 1, n, o) != n) { fclose(o); unlink(part); free(buf); return -1; }
    free(buf);
    if (fclose(o) != 0 || rename(part, dst) != 0) { unlink(part); return -1; }
    {   /* out of the library */
        const char *file = strrchr(games[idx].path, '/') ? strrchr(games[idx].path, '/') + 1 : games[idx].path;
        recent_rename(file, NULL);
        unlink(games[idx].path);
    }
    sync();
    snprintf(out, on, "%s", name);
    SDL_Log("bios: %s -> %s (crc32 %08lx, %lu bytes)", games[idx].path, dst, c, (unsigned long)n);
    return 0;
}

/* --------------------------------------------------------- controllers */

typedef struct { int a, b, start, select; } PadMap;

static int cfg_val(const char *line, const char *key, char *out, size_t on)
{
    size_t kl = strlen(key), n = 0;
    const char *q;
    if (strncmp(line, key, kl) != 0) return 0;
    q = line + kl;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != '=') return 0;
    q++;
    while (*q == ' ' || *q == '"') q++;
    while (*q && *q != '"' && *q != '\n' && n + 1 < on) out[n++] = *q++;
    out[n] = '\0';
    return 1;
}

static int cfg_btn(const char *v) { return isdigit((unsigned char)v[0]) ? atoi(v) : -1; }

/* RetroArch autoconfig: match vendor/product id first, then device name. */
static PadMap lookup_pad(const char *name, int vid, int pid)
{
    PadMap best = { -1, -1, -1, -1 };
    int best_score = 0;
    DIR *d = opendir(AUTOCONF_DIR);
    struct dirent *e;
    char path[512], line[256], v[128];
    if (!d) return best;
    while ((e = readdir(d))) {
        FILE *f;
        PadMap t = { -1, -1, -1, -1 };
        int fv = -1, fp = -1, name_ok = 0, score;
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", AUTOCONF_DIR, e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        while (fgets(line, sizeof line, f)) {
            if (cfg_val(line, "input_device_display_name", v, sizeof v)) { if (name && !strcmp(v, name)) name_ok = 1; continue; }
            if (cfg_val(line, "input_device", v, sizeof v)) { if (name && !strcmp(v, name)) name_ok = 1; continue; }
            if (cfg_val(line, "input_vendor_id", v, sizeof v)) fv = atoi(v);
            else if (cfg_val(line, "input_product_id", v, sizeof v)) fp = atoi(v);
            else if (cfg_val(line, "input_a_btn", v, sizeof v)) t.a = cfg_btn(v);
            else if (cfg_val(line, "input_b_btn", v, sizeof v)) t.b = cfg_btn(v);
            else if (cfg_val(line, "input_start_btn", v, sizeof v)) t.start = cfg_btn(v);
            else if (cfg_val(line, "input_select_btn", v, sizeof v)) t.select = cfg_btn(v);
        }
        fclose(f);
        score = (vid > 0 && fv == vid && fp == pid) ? 2 : name_ok ? 1 : 0;
        if (score > best_score) { best = t; best_score = score; }
    }
    closedir(d);
    return best;
}

#define MAX_PADS 8
static struct { SDL_JoystickID id; PadMap map; } pads[MAX_PADS];
static int npads;

static void add_pad(int index)
{
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
    SDL_Joystick *j;
    int i;
    for (i = 0; i < npads; i++) if (pads[i].id == id) return;
    if (npads >= MAX_PADS || !(j = SDL_JoystickOpen(index))) return;
    pads[npads].id = SDL_JoystickInstanceID(j);
    pads[npads].map = lookup_pad(SDL_JoystickName(j), SDL_JoystickGetVendor(j), SDL_JoystickGetProduct(j));
    SDL_Log("pad %d: \"%s\" %04x:%04x I=%d II=%d run=%d select=%d", npads, SDL_JoystickName(j),
            SDL_JoystickGetVendor(j), SDL_JoystickGetProduct(j),
            pads[npads].map.a, pads[npads].map.b, pads[npads].map.start, pads[npads].map.select);
    npads++;
}

static PadMap *pad_map(SDL_JoystickID id)
{
    int i;
    for (i = 0; i < npads; i++) if (pads[i].id == id) return &pads[i].map;
    return NULL;
}

/* ---------------------------------------------------------------- misc */

static unsigned long cart_state(void)
{
    static const char *paths[] = { "/media/usb0/Test_pcengine.pce", "/media/usb1/Test_pcengine.pce" };
    unsigned long h = 0;
    int i, j;
    for (i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) {
            long sz;
            unsigned char b[64];
            size_t n;
            fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
            n = fread(b, 1, sizeof b, f);
            fclose(f);
            if (sz > 0) {
                h = h * 31 + (unsigned long)sz + 1;
                for (j = 0; j < (int)n; j++) h = h * 131 + b[j];
            }
        }
    }
    return h;
}

static void on_signal(int s) { (void)s; quit_flag = 1; }

static void on_crash(int s)
{
    char msg[96];
    int n = snprintf(msg, sizeof msg, "pce-launcher: crashed (signal %d), last SDL event 0x%x\n",
                     s, (unsigned)last_event);
    if (n > 0) { ssize_t w = write(2, msg, (size_t)n); (void)w; }
    _exit(128 + s);
}

static int load_last(void)
{
    char buf[1024];
    int i;
    FILE *f = fopen(LAST_FILE, "r");
    if (!f) return 0;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return 0; }
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
    for (i = 0; i < ngames; i++) if (strcmp(games[i].path, buf) == 0) return i;
    return 0;
}

static void save_last(const char *p)
{
    FILE *f = fopen(LAST_FILE, "w");
    if (f) { fprintf(f, "%s\n", p); fclose(f); }
}

/* ----------------------------------------------------------- rendering */

static SDL_Renderer *ren;
static TTF_Font *f_title, *f_item, *f_hint;
static int W, H;
static const SDL_Color white = {255, 255, 255, 255}, dim = {170, 178, 196, 255},
                       accent = {250, 210, 0, 255}, red = {255, 90, 80, 255},
                       ink = {18, 18, 22, 255},    /* text on the yellow accent */
                       c_ok = {130, 225, 140, 255}, c_mod = {255, 170, 70, 255}, c_unk = {150, 156, 168, 255};

static void text(TTF_Font *font, const char *s, SDL_Color c, int x, int y, int align)
{
    SDL_Surface *sf;
    SDL_Texture *t;
    SDL_Rect dst;
    if (!s || !*s) return;
    if (!(sf = TTF_RenderUTF8_Blended(font, s, c))) return;
    t = SDL_CreateTextureFromSurface(ren, sf);
    dst.w = sf->w; dst.h = sf->h; dst.y = y;
    dst.x = align == 0 ? x : align == 1 ? x - sf->w / 2 : x - sf->w;
    SDL_FreeSurface(sf);
    if (t) { SDL_RenderCopy(ren, t, NULL, &dst); SDL_DestroyTexture(t); }
}

static void fill(int x, int y, int w, int h, int r, int g, int b, int a)
{
    SDL_Rect rc = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderFillRect(ren, &rc);
}

/* ArchGX logo (wide) and emblem ("A"), with transparency; drawn scaled to a height */
static SDL_Texture *tex_logo, *tex_emblem;
static void load_brand(void)
{
    tex_logo = IMG_LoadTexture(ren, LOGO_PATH);
    tex_emblem = IMG_LoadTexture(ren, EMBLEM_PATH);
}
static void free_brand(void)
{
    if (tex_logo) SDL_DestroyTexture(tex_logo);
    if (tex_emblem) SDL_DestroyTexture(tex_emblem);
    tex_logo = tex_emblem = NULL;
}
static int draw_img(SDL_Texture *t, int x, int y, int h, int align)   /* returns drawn width */
{
    int tw, th;
    SDL_Rect r;
    if (!t || SDL_QueryTexture(t, NULL, NULL, &tw, &th) != 0 || th <= 0) return 0;
    r.h = h; r.w = tw * h / th; r.y = y;
    r.x = align == 0 ? x : align == 1 ? x - r.w / 2 : x - r.w;
    SDL_RenderCopy(ren, t, NULL, &r);
    return r.w;
}

static void panel(int w, int h, int *px, int *py)
{
    *px = (W - w) / 2; *py = (H - h) / 2;
    fill(0, 0, W, H, 0, 0, 0, 150);
    fill(*px, *py, w, h, 28, 30, 40, 245);
    fill(*px, *py, w, H / 160 + 2, accent.r, accent.g, accent.b, 255);
}

/* -------------------------------------------------------- status screen */
/* pce-launcher --status <file>: a progress screen driven by gx-start.sh.
 * The file has up to four lines: message, subtitle (game title), progress
 * ("0".."100" = real percentage, "-" = activity with no measurable progress)
 * and state ("" = running, "error" = show message in red, "done" = exit).
 * The screen also exits if the file disappears. Nothing here invents a
 * percentage: the bar is only drawn when gx-start supplies one. */
static int run_status(const char *path, SDL_Texture *bg, const char *shot)
{
    char line[4][256];
    Uint32 start = SDL_GetTicks(), last_read = 0, missing_since = 0;
    int pct = -1, err = 0, frame = 0, i;
    memset(line, 0, sizeof line);
    for (;;) {
        SDL_Event ev;
        Uint32 now = SDL_GetTicks();
        while (SDL_PollEvent(&ev)) if (ev.type == SDL_QUIT) _exit(0);
        if (quit_flag) _exit(0);
        if (now - last_read >= 100 || !last_read) {
            FILE *f = fopen(path, "r");
            last_read = now;
            if (!f) {
                if (!missing_since) missing_since = now;
                if (now - missing_since > 1500) _exit(0);
            } else {
                missing_since = 0;
                memset(line, 0, sizeof line);
                for (i = 0; i < 4 && fgets(line[i], sizeof line[i], f); i++)
                    line[i][strcspn(line[i], "\r\n")] = '\0';
                fclose(f);
                if (!strcmp(line[3], "done")) _exit(0);
                err = !strcmp(line[3], "error");
                pct = (line[2][0] >= '0' && line[2][0] <= '9') ? atoi(line[2]) : -1;
                if (pct > 100) pct = 100;
            }
        }
        SDL_SetRenderDrawColor(ren, 20, 22, 30, 255);
        SDL_RenderClear(ren);
        if (bg) { SDL_RenderCopy(ren, bg, NULL, NULL); fill(0, 0, W, H, 0, 0, 0, 90); }
        {
            int pw = W * 60 / 100, ph = H * 30 / 100, px, py, bx, by, bw, bh;
            panel(pw, ph, &px, &py);
            draw_img(tex_emblem, px + pw / 40, py + ph / 14, H / 18, 0);
            text(f_item, line[0], err ? red : white, W / 2, py + ph * 18 / 100, 1);
            text(f_hint, line[1], accent, W / 2, py + ph * 40 / 100, 1);
            bw = pw * 70 / 100; bh = H / 60 + 2; bx = (W - bw) / 2; by = py + ph * 64 / 100;
            if (!err && pct >= 0) {                       /* real, measured progress */
                char t[16];
                fill(bx, by, bw, bh, 60, 64, 80, 255);
                fill(bx, by, bw * pct / 100, bh, accent.r, accent.g, accent.b, 255);
                snprintf(t, sizeof t, "%d%%", pct);
                text(f_hint, t, dim, W / 2, by + bh * 2, 1);
            } else if (!err) {                            /* activity only: no percentage */
                int n = 8, r = H / 90 + 2, cx = W / 2, cy = by + bh / 2, k;
                for (k = 0; k < n; k++) {
                    static const int ox[8] = { 0, 7, 10, 7, 0, -7, -10, -7 }, oy[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
                    int on = (k == (frame / 4) % n), x = cx + ox[k] * H / 300, y = cy + oy[k] * H / 300;
                    fill(x - r / 2, y - r / 2, r, r, on ? accent.r : 90, on ? accent.g : 94, on ? accent.b : 110, 255);
                }
            }
        }
        if (shot && now - start > 300) {
            SDL_Surface *ss = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            if (ss && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch) == 0)
                SDL_SaveBMP(ss, shot);
            _exit(3);
        }
        SDL_RenderPresent(ren);
        frame++;
        SDL_Delay(16);
    }
}

/* ------------------------------------------------------- input actions */

enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_RUN, A_SELECT, A_DEBUG };   /* A_DEBUG: test hook */

static int key_action(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return A_UP;
    case SDLK_DOWN: return A_DOWN;
    case SDLK_LEFT: return A_LEFT;
    case SDLK_RIGHT: return A_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_KP_ENTER: return A_OK;
    case SDLK_BACKSPACE: case SDLK_ESCAPE: return A_BACK;
    case SDLK_TAB: return A_SELECT;
    case SDLK_F5: return A_RUN;
    default: return A_NONE;
    }
}

static int test_action(const char *w)
{
    static const char *names[] = { "", "up", "down", "left", "right", "ok", "back", "run", "select", "debug" };
    int i;
    for (i = 1; i < 10; i++) if (!strcmp(w, names[i])) return i;
    return A_NONE;
}

#define QMAX 256
static int queue[QMAX], qhead, qtail;
static void push(int a) { if (a && (qtail + 1) % QMAX != qhead) { queue[qtail] = a; qtail = (qtail + 1) % QMAX; } }
static int pop(void) { int a; if (qhead == qtail) return A_NONE; a = queue[qhead]; qhead = (qhead + 1) % QMAX; return a; }

/* ------------------------------------------------------------ keyboard */

#define KB_COLS 10
static const char *kb_rows[] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789-'.!", "()&,+_=@[]" };
enum { KB_SHIFT, KB_SPACE, KB_DEL, KB_CLEAR, KB_DONE, KB_CANCEL, KB_NSPECIAL };
static const char *kb_special[] = { "abc", "Space", "Del", "Clear", "Done", "Cancel" };

/* ---------------------------------------------------------------- main */

enum { M_LIST, M_OPTIONS, M_DELETE, M_KEYBOARD, M_MESSAGE, M_PLAY, M_ABOUT, M_ASK, M_DEBUG };
static const char *opnames[6];

/* ------------------------------------------------------ in-game menu */
/* pce-launcher --ingame <command-fifo> <card-save-base> [title]
 * Runs alongside RetroArch for the whole game. Holding RUN + SELECT pauses
 * the game (stdin command interface) and opens a menu in the library style:
 * Resume, Save State, Load State, Rewind, Options, Restart, RetroArch Menu, Quit.
 *
 * Save states: RetroArch always saves/loads its slot 0, kept in RAM
 * (/tmp/gx/states/<base>.state + .png). Saving copies that file to the
 * card as <card-base>.state1..4 (+ .png); loading copies a slot back first.
 * Rewind: every 10 s a snapshot is taken the same way and kept in RAM
 * (/tmp/gx/rewind, last 2 minutes), shown as thumbnails.
 * Options: written to <rom>/archgx-settings.cfg and, for the running game,
 * to /tmp/gx/ra-set (read by our RetroArch patch).
 * RetroArch ignores save/load/menu commands while paused, so actions
 * unpause first and then send the command. */

#define NSLOTS 4
#define NREWIND 12
#define REWIND_EVERY_MS 10000
#define GX_TMP "/tmp/gx"
#define RA_SET GX_TMP "/ra-set"
#define SWITCH_PATH "/sys/devices/platform/adc-vol/ratio"
#define FILTER_DIR "/usr/lib/retroarch/filters/video"

static const char *ig_fifo, *ig_base;          /* ig_base: card save base */
static char ig_tmpbase[512], ig_settings[512], ig_shots[512];
static SDL_Window *ig_win;
static SDL_DisplayMode ig_dm;

static void ig_log(const char *fmt, const char *a)
{
    fprintf(stderr, "[ingame] "); fprintf(stderr, fmt, a); fputc('\n', stderr);
    fsync(2);                                  /* survive a power-off */
}

static void ig_send(const char *cmds)
{
    int fd = open(ig_fifo, O_WRONLY | O_NONBLOCK);
    if (fd < 0) { ig_log("cannot open command fifo %s", ig_fifo); return; }
    if (write(fd, cmds, strlen(cmds)) < 0) ig_log("write failed: %s", cmds);
    close(fd);
    ig_log("sent %s", cmds);
}

static long ig_mtime(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_mtime : -1;
}

/* wait until a file exists and its size is stable over 150 ms */
static int ig_wait_file(const char *p, Uint32 timeout_ms)
{
    Uint32 t0 = SDL_GetTicks();
    long last = -2;
    struct stat st;
    while (SDL_GetTicks() - t0 < timeout_ms) {
        if (stat(p, &st) == 0 && st.st_size > 0) {
            if ((long)st.st_size == last) return 1;
            last = (long)st.st_size;
        }
        SDL_Delay(150);
    }
    return 0;
}

/* ---- settings ---- */
#define NFILTERS 3                             /* Off, Scanlines, Composite TV */
enum { OPT_ASPECT, OPT_FILTER, OPT_SMOOTH, OPT_PALETTE, OPT_VOLUME, OPT_FLICKER, OPT_TURBO1, OPT_TURBO2, OPT_N };
static const char *opt_label[OPT_N] = { "Aspect ratio", "Screen filter", "Smoothing", "Composite colours", "Volume",
                                         "Reduce flicker", "Turbo I", "Turbo II" };
static const char *opt_key[OPT_N] = { "aspect", "filter", "smooth", "palette", "volume", "flicker", "turbo1", "turbo2" };
static const int opt_max[OPT_N] = { 1, NFILTERS - 1, 1, 1, 10, 1, 1, 1 };
static int opt_val[OPT_N] = { 0, 0, 0, 0, 10, 0, 0, 0 };
static int opt_switch = -1;                    /* switch position when last seen */
static int opt_dirty;                          /* changed in Options, not yet applied */
/* CRT phosphor was removed in S2g-2: far too slow on this CPU. An old
 * "filter=phosphor" setting simply reads as Off. */
static const char *filter_names[NFILTERS] = { "Off", "Scanlines", "Composite TV" };
static const char *filter_keys[NFILTERS] = { "off", "scanlines", "composite" };
static const char *filter_files[NFILTERS] = { "", FILTER_DIR "/ArchGX_Scanlines.filt",
                                              FILTER_DIR "/Blargg_NTSC_SNES_Composite.filt" };

static int read_switch(void)
{
    char c = 0;
    FILE *f = fopen(SWITCH_PATH, "r");
    if (!f) return -1;
    if (fread(&c, 1, 1, f) != 1) c = 0;
    fclose(f);
    return c == '0' ? 0 : c == '1' ? 1 : -1;
}

static void settings_load(void)
{
    char line[256], v[128];
    int i;
    FILE *f = fopen(ig_settings, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        for (i = 0; i < OPT_N; i++) {
            size_t kl = strlen(opt_key[i]);
            if (strncmp(line, opt_key[i], kl) || line[kl] != '=') continue;
            snprintf(v, sizeof v, "%s", line + kl + 1);
            if (i == OPT_FILTER) {
                int k;
                opt_val[i] = 0;
                for (k = 0; k < NFILTERS; k++) if (!strcmp(v, filter_keys[k])) opt_val[i] = k;
            } else {
                int n = atoi(v);
                opt_val[i] = n < 0 ? 0 : n > opt_max[i] ? opt_max[i] : n;
            }
        }
        if (!strncmp(line, "switch=", 7)) opt_switch = atoi(line + 7);
    }
    fclose(f);
}

static void settings_save(void)
{
    char tmp[600];
    FILE *f;
    int i;
    snprintf(tmp, sizeof tmp, "%s.new", ig_settings);
    if ((f = fopen(tmp, "w"))) {
        fprintf(f, "# ArchGX settings (changed from the in-game Options menu)\n");
        for (i = 0; i < OPT_N; i++) {
            if (i == OPT_FILTER) fprintf(f, "filter=%s\n", filter_keys[opt_val[i]]);
            else fprintf(f, "%s=%d\n", opt_key[i], opt_val[i]);
        }
        fprintf(f, "switch=%d\n", opt_switch);
        fclose(f);
        rename(tmp, ig_settings);
    }
    snprintf(tmp, sizeof tmp, "%s.new", RA_SET);
    if ((f = fopen(tmp, "w"))) {
        int vol = opt_val[OPT_VOLUME];
        fprintf(f, "aspect=%d\nfilter=%s\nsmooth=%d\nvolume=%d\nsprites=%d\nturbo1=%d\nturbo2=%d\npalette=%d\n",
                opt_val[OPT_ASPECT], filter_files[opt_val[OPT_FILTER]], opt_val[OPT_SMOOTH],
                vol == 0 ? -80 : (vol - 10) * 3, opt_val[OPT_FLICKER], opt_val[OPT_TURBO1], opt_val[OPT_TURBO2],
                opt_val[OPT_PALETTE]);
        fclose(f);
        rename(tmp, RA_SET);
    }
    sync();
}

/* the physical switch wins when it has moved since the settings were written */
static void settings_follow_switch(void)
{
    int s = read_switch();
    if (s >= 0 && s != opt_switch) { opt_switch = s; opt_val[OPT_ASPECT] = s; }
}

/* ---- picture tiles (save slots and rewind snapshots) ---- */
typedef struct { char state[600], png[600], label[32]; long when; SDL_Texture *tex; } Tile;
#define MAXTILES NREWIND
static Tile tiles[MAXTILES];
static int ntiles;

static void tiles_free(void)
{
    int i;
    for (i = 0; i < MAXTILES; i++) if (tiles[i].tex) { SDL_DestroyTexture(tiles[i].tex); tiles[i].tex = NULL; }
    ntiles = 0;
}

static void tiles_slots(void)
{
    int i;
    tiles_free();
    for (i = 0; i < NSLOTS; i++) {
        Tile *t = &tiles[i];
        snprintf(t->state, sizeof t->state, "%s.state%d", ig_base, i + 1);
        snprintf(t->png, sizeof t->png, "%s.state%d.png", ig_base, i + 1);
        snprintf(t->label, sizeof t->label, "Slot %d", i + 1);
        t->when = ig_mtime(t->state);
        if (t->when >= 0 && access(t->png, R_OK) == 0) t->tex = IMG_LoadTexture(ren, t->png);
    }
    ntiles = NSLOTS;
}

static int cmp_tile(const void *a, const void *b)
{
    long x = ((const Tile *)a)->when, y = ((const Tile *)b)->when;
    return x < y ? 1 : x > y ? -1 : 0;          /* newest first */
}

static void tiles_rewind(void)
{
    int i, n = 0;
    long now = (long)time(NULL);
    tiles_free();
    for (i = 0; i < NREWIND; i++) {
        Tile *t = &tiles[n];
        snprintf(t->state, sizeof t->state, GX_TMP "/rewind/r%02d.state", i);
        snprintf(t->png, sizeof t->png, GX_TMP "/rewind/r%02d.png", i);
        if ((t->when = ig_mtime(t->state)) < 0) continue;
        n++;
    }
    qsort(tiles, n, sizeof(Tile), cmp_tile);
    for (i = 0; i < n; i++) {
        long ago = now - tiles[i].when;
        if (ago < 60) snprintf(tiles[i].label, sizeof tiles[i].label, "%ld s ago", ago < 1 ? 1 : ago);
        else snprintf(tiles[i].label, sizeof tiles[i].label, "%ld:%02ld ago", ago / 60, ago % 60);
        if (access(tiles[i].png, R_OK) == 0) tiles[i].tex = IMG_LoadTexture(ren, tiles[i].png);
    }
    ntiles = n;
}

/* ---- rewind snapshots (taken while the game runs) ---- */
static int rw_next;                 /* ring index for the next snapshot */
static int rw_pending;              /* SAVE_STATE sent, waiting for the file */
static Uint32 rw_due, rw_deadline;

static void rw_reset(void)
{
    char p[256];
    int i;
    for (i = 0; i < NREWIND; i++) {
        snprintf(p, sizeof p, GX_TMP "/rewind/r%02d.state", i); unlink(p);
        snprintf(p, sizeof p, GX_TMP "/rewind/r%02d.png", i); unlink(p);
    }
    mkdir(GX_TMP "/rewind", 0755);
}

static void rw_tick(void)
{
    char s0[600], p0[600], d[256];
    Uint32 now = SDL_GetTicks();
    snprintf(s0, sizeof s0, "%s.state", ig_tmpbase);
    snprintf(p0, sizeof p0, "%s.state.png", ig_tmpbase);
    if (!rw_pending && now >= rw_due) {
        unlink(s0); unlink(p0);
        ig_send("SAVE_STATE\n");
        rw_pending = 1; rw_deadline = now + 3000;
    } else if (rw_pending) {
        struct stat a, b;
        if (stat(s0, &a) == 0 && a.st_size > 0) {
            SDL_Delay(150);
            if (stat(s0, &b) == 0 && b.st_size == a.st_size) {
                SDL_Delay(150);                          /* thumbnail follows the state */
                snprintf(d, sizeof d, GX_TMP "/rewind/r%02d.state", rw_next); rename(s0, d);
                snprintf(d, sizeof d, GX_TMP "/rewind/r%02d.png", rw_next);
                if (rename(p0, d) != 0) unlink(d);
                rw_next = (rw_next + 1) % NREWIND;
                rw_pending = 0; rw_due = now + REWIND_EVERY_MS;
            }
        } else if (now > rw_deadline) {                  /* RetroArch busy (its own menu?) */
            rw_pending = 0; rw_due = now + REWIND_EVERY_MS;
        }
    }
}

/* ---- window ---- */
static int ig_open_window(void)
{
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);      /* translucent: the paused game shows through */
    ig_win = SDL_CreateWindow("ArchGX Menu", 0, 0, ig_dm.w, ig_dm.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!ig_win) { ig_log("SDL_CreateWindow: %s", SDL_GetError()); return 0; }
    ren = SDL_CreateRenderer(ig_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(ig_win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { ig_log("SDL_CreateRenderer: %s", SDL_GetError()); SDL_DestroyWindow(ig_win); ig_win = NULL; return 0; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_GetRendererOutputSize(ren, &W, &H);
    if (!f_title) {
        f_title = TTF_OpenFont(FONT_PATH, H / 16);
        f_item  = TTF_OpenFont(FONT_PATH, H / 26);
        f_hint  = TTF_OpenFont(FONT_LIGHT, H / 34);
        if (!f_hint) f_hint = TTF_OpenFont(FONT_PATH, H / 34);
    }
    load_brand();
    SDL_RaiseWindow(ig_win);
    return f_title && f_item && f_hint;
}

static void ig_close_window(void)
{
    tiles_free();
    free_brand();
    if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
    if (ig_win) { SDL_DestroyWindow(ig_win); ig_win = NULL; }
}

/* any pad button still down? (so the button that closed the menu
 * doesn't reach the game when it resumes) */
static void ig_wait_release(void)
{
    Uint32 t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 1000) {
        int i, b, down = 0, k;
        SDL_JoystickUpdate();
        for (i = 0; i < npads; i++) {
            SDL_Joystick *j = SDL_JoystickFromInstanceID(pads[i].id);
            if (!j) continue;
            k = SDL_JoystickNumButtons(j);
            for (b = 0; b < k; b++) down |= SDL_JoystickGetButton(j, b);
        }
        if (!down) return;
        SDL_Delay(20);
    }
}

/* ---- drawing ---- */
enum { IG_RESUME, IG_SAVE, IG_LOAD, IG_REWIND, IG_SHOT, IG_OPTIONS, IG_RESTART, IG_RA, IG_QUIT, IG_N };
static const char *ig_items[IG_N] = { "Resume", "Save State", "Load State", "Rewind", "Screenshot", "Options",
                                      "Restart Game", "RetroArch Menu", "Quit Game" };
enum { IGM_MAIN, IGM_TILES, IGM_OPTIONS, IGM_CONFIRM };

static void box(int pw, int ph, int *px, int *py)
{
    *px = (W - pw) / 2; *py = (H - ph) / 2;
    fill(*px, *py, pw, ph, 28, 30, 40, 235);
    fill(*px, *py, pw, H / 160 + 2, accent.r, accent.g, accent.b, 255);
}

static void draw_switch(int x, int y, int on)       /* pill toggle, x = right edge */
{
    int w = H / 12, h = H / 28, k = h - 6;
    x -= w;
    fill(x, y, w, h, on ? accent.r : 70, on ? accent.g : 74, on ? accent.b : 90, 255);
    fill(on ? x + w - k - 3 : x + 3, y + 3, k, k, 245, 245, 245, 255);
}

static void ig_draw(int mode, int sel, int tmode, int tsel, int cwhat, int csel,
                    const char *title, const char *toast)
{
    int pw, ph, px, py, i;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    fill(0, 0, W, H, 0, 0, 0, 150);
    if (mode == IGM_MAIN) {
        int rh = H / 15;
        pw = W * 44 / 100; ph = H * 12 / 100 + rh * IG_N;
        box(pw, ph, &px, &py);
        draw_img(tex_emblem, px + pw / 30, py + H / 40, H / 16, 0);
        text(f_item, title && *title ? title : "Paused", accent, W / 2, py + H / 34, 1);
        for (i = 0; i < IG_N; i++) {
            int y = py + H * 10 / 100 + i * rh;
            if (i == sel) fill(px + pw / 12, y - rh / 12, pw - pw / 6, rh * 9 / 10, accent.r, accent.g, accent.b, 255);
            text(f_item, ig_items[i], i == sel ? ink : dim, W / 2, y, 1);
        }
        text(f_hint, "I  Select      II  Back", dim, W / 2, py + ph + H / 40, 1);
    } else if (mode == IGM_TILES) {
        int gap = W / 40, tw = (W * 86 / 100 - gap * (NSLOTS + 1)) / NSLOTS, th = tw * 3 / 4, first;
        const char *head = tmode == IG_SAVE ? "Save State" : tmode == IG_LOAD ? "Load State" : "Rewind";
        pw = tw * NSLOTS + gap * (NSLOTS + 1); ph = th + H * 26 / 100;
        box(pw, ph, &px, &py);
        text(f_item, head, accent, W / 2, py + H / 30, 1);
        first = tsel < NSLOTS ? 0 : tsel - NSLOTS + 1;
        if (ntiles == 0)
            text(f_hint, "No snapshots yet: one is taken every 10 seconds while you play.", dim, W / 2, py + ph / 2, 1);
        for (i = 0; i < NSLOTS && first + i < ntiles; i++) {
            Tile *t = &tiles[first + i];
            int x = px + gap + i * (tw + gap), y = py + H * 12 / 100, b = H / 120 + 2;
            if (first + i == tsel) fill(x - b, y - b, tw + 2 * b, th + 2 * b, accent.r, accent.g, accent.b, 255);
            fill(x, y, tw, th, 12, 13, 18, 255);
            if (t->tex) { SDL_Rect r = { x, y, tw, th }; SDL_RenderCopy(ren, t->tex, NULL, &r); }
            else text(f_hint, t->when >= 0 ? "Saved" : "Empty", dim, x + tw / 2, y + th / 2 - H / 60, 1);
            text(f_hint, t->label, first + i == tsel ? white : dim, x + tw / 2, y + th + H / 50, 1);
        }
        if (first > 0) text(f_item, "<", accent, px + gap / 2, py + H * 12 / 100 + th / 2 - H / 40, 1);
        if (first + NSLOTS < ntiles) text(f_item, ">", accent, px + pw - gap / 2, py + H * 12 / 100 + th / 2 - H / 40, 1);
        if (ntiles) {
            const char *h = tmode == IG_SAVE ? (tiles[tsel].when >= 0 ? "I  Overwrite      II  Back" : "I  Save      II  Back")
                          : tmode == IG_LOAD ? (tiles[tsel].when >= 0 ? "I  Load      II  Back" : "Empty slot      II  Back")
                          : "I  Go back to this point      II  Back";
            text(f_hint, h, dim, W / 2, py + ph - H / 14, 1);
        }
    } else if (mode == IGM_OPTIONS) {
        int rh = H / 13, lx, rx;
        pw = W * 56 / 100; ph = H * 13 / 100 + rh * OPT_N;
        box(pw, ph, &px, &py);
        lx = px + pw / 14; rx = px + pw - pw / 14;
        text(f_item, "Options", accent, W / 2, py + H / 34, 1);
        for (i = 0; i < OPT_N; i++) {
            int y = py + H * 10 / 100 + i * rh, v = opt_val[i], ty = y + rh / 2 - H / 56;
            char val[32];
            if (i == sel) fill(px + pw / 30, y - rh / 12, pw - pw / 15, rh * 9 / 10, 48, 52, 68, 255);
            text(f_item, opt_label[i], i == sel ? white : dim, lx, y, 0);
            if (opt_max[i] == 1 && i != OPT_ASPECT) { draw_switch(rx, ty, v); continue; }
            if (i == OPT_VOLUME) {
                int bw = pw * 30 / 100, bh = H / 70 + 2, bx = rx - bw, by = y + rh / 2 - bh;
                fill(bx, by, bw, bh, 70, 74, 90, 255);
                fill(bx, by, bw * v / 10, bh, accent.r, accent.g, accent.b, 255);
                continue;
            }
            snprintf(val, sizeof val, "<  %s  >", i == OPT_ASPECT ? (v ? "16:9" : "4:3") : filter_names[v]);
            text(f_item, val, i == sel ? accent : white, rx, y, 2);
        }
        text(f_hint, "Left / Right  Change      II  Back", dim, W / 2, py + ph + H / 40, 1);
    } else {
        static const char *opts[2] = { "Yes", "No" };
        pw = W * 50 / 100; ph = H * 30 / 100;
        box(pw, ph, &px, &py);
        text(f_item, cwhat == IG_QUIT ? "Quit to the library?" : "Restart the game?", white, W / 2, py + ph * 15 / 100, 1);
        text(f_hint, "Unsaved progress will be lost.", dim, W / 2, py + ph * 36 / 100, 1);
        for (i = 0; i < 2; i++) {
            int bw = pw / 4, bx = W / 2 + (i == 0 ? -bw - pw / 20 : pw / 20), by = py + ph * 62 / 100, bh = H / 13;
            fill(bx, by, bw, bh, i == csel ? accent.r : 50, i == csel ? accent.g : 54, i == csel ? accent.b : 70, 255);
            text(f_item, opts[i], i == csel ? ink : white, bx + bw / 2, by + bh / 10, 1);
        }
    }
    if (toast) {
        int tw = W * 40 / 100, th = H / 9;
        fill((W - tw) / 2, H - th * 2, tw, th, 28, 30, 40, 245);
        text(f_item, toast, white, W / 2, H - th * 2 + th / 5, 1);
    }
    SDL_RenderPresent(ren);
}

static void ig_toast(const char *t)
{
    ntiles = 0;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    fill(0, 0, W, H, 0, 0, 0, 150);
    {
        int tw = W * 40 / 100, th = H / 9;
        fill((W - tw) / 2, (H - th) / 2, tw, th, 28, 30, 40, 245);
        fill((W - tw) / 2, (H - th) / 2, tw, H / 160 + 2, accent.r, accent.g, accent.b, 255);
        text(f_item, t, white, W / 2, (H - th) / 2 + th / 5, 1);
    }
    SDL_RenderPresent(ren);
}

static void opt_change(int i, int d)
{
    int v = opt_val[i] + d;
    if (opt_max[i] == 1) v = !opt_val[i];                 /* toggles flip either way */
    else if (i == OPT_VOLUME) v = v < 0 ? 0 : v > 10 ? 10 : v;
    else v = (v + opt_max[i] + 1) % (opt_max[i] + 1);     /* lists wrap */
    if (v == opt_val[i]) return;
    opt_val[i] = v;
    if (i == OPT_ASPECT) opt_switch = read_switch();      /* the menu choice wins until the switch moves */
    opt_dirty = 1;      /* applied once, on leaving Options: rapid video re-inits crashed RetroArch (S2g) */
    { char m[64]; snprintf(m, sizeof m, "option %s=%d", opt_key[i], v); ig_log("%s", m); }
}

/* returns 1 = keep running, 0 = game is quitting */
static int ig_menu(const char *title)
{
    int mode = IGM_MAIN, sel = 0, osel = 0, tmode = IG_SAVE, tsel = 0, cwhat = 0, csel = 1, held = 0, result = -1, a;
    Uint32 repeat_at = 0;
    char s0[600], p0[600], msg[96];
    const char *pick = NULL;

    ig_send("PAUSE_TOGGLE\n");
    if (!ig_open_window()) { ig_send("PAUSE_TOGGLE\n"); return 1; }
    if (!getenv("PCE_LAUNCHER_KEYS")) qhead = qtail = 0;
    ig_wait_release();                     /* RUN + SELECT must not count as menu input */

    while (result < 0 && !quit_flag) {
        SDL_Event ev;
        Uint32 now;
        ig_draw(mode, mode == IGM_OPTIONS ? osel : sel, tmode, tsel, cwhat, csel, title, NULL);
        if (SDL_WaitEventTimeout(&ev, 50)) {
            do {
                switch (ev.type) {
                case SDL_QUIT: quit_flag = 1; break;
                case SDL_KEYDOWN: push(key_action(ev.key.keysym.sym)); break;
                case SDL_JOYDEVICEADDED: add_pad(ev.jdevice.which); break;
                case SDL_JOYHATMOTION: {
                    int v = ev.jhat.value, d = A_NONE;
                    if (v & SDL_HAT_UP) d = A_UP; else if (v & SDL_HAT_DOWN) d = A_DOWN;
                    else if (v & SDL_HAT_LEFT) d = A_LEFT; else if (v & SDL_HAT_RIGHT) d = A_RIGHT;
                    held = d; push(d); repeat_at = SDL_GetTicks() + 400;
                    break; }
                case SDL_JOYBUTTONDOWN: {
                    PadMap *m = pad_map(ev.jbutton.which);
                    int b = ev.jbutton.button;
                    if (m && m->a >= 0) {
                        if (b == m->a || b == m->start) push(A_OK);
                        else if (b == m->b || b == m->select) push(A_BACK);
                    } else push(A_OK);
                    break; }
                default: break;
                }
            } while (SDL_PollEvent(&ev));
        }
        now = SDL_GetTicks();
        if (held && now >= repeat_at) { push(held); repeat_at = now + 120; }
        if (getenv("PCE_LAUNCHER_SHOT") && qhead == qtail) {   /* test hook: screenshot, exit */
            SDL_Surface *ss;
            ig_draw(mode, mode == IGM_OPTIONS ? osel : sel, tmode, tsel, cwhat, csel, title, NULL);
            ss = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            if (ss && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch) == 0)
                SDL_SaveBMP(ss, getenv("PCE_LAUNCHER_SHOT"));
            _exit(3);
        }
        while ((a = pop()) != A_NONE && result < 0) {
            if (mode == IGM_MAIN) {
                if (a == A_UP) sel = (sel + IG_N - 1) % IG_N;
                else if (a == A_DOWN) sel = (sel + 1) % IG_N;
                else if (a == A_BACK) result = IG_RESUME;
                else if (a == A_OK) {
                    if (sel == IG_SAVE || sel == IG_LOAD || sel == IG_REWIND) {
                        mode = IGM_TILES; tmode = sel; tsel = 0;
                        if (sel == IG_REWIND) tiles_rewind(); else tiles_slots();
                        if (sel == IG_LOAD) {                /* start on the newest save */
                            int k; long best = -1;
                            for (k = 0; k < ntiles; k++) if (tiles[k].when > best) { best = tiles[k].when; tsel = k; }
                        }
                    } else if (sel == IG_OPTIONS) { mode = IGM_OPTIONS; settings_follow_switch(); }
                    else if (sel == IG_RESTART || sel == IG_QUIT) { mode = IGM_CONFIRM; cwhat = sel; csel = 1; }
                    else result = sel;
                }
            } else if (mode == IGM_TILES) {
                if (a == A_LEFT && ntiles) tsel = (tsel + ntiles - 1) % ntiles;
                else if (a == A_RIGHT && ntiles) tsel = (tsel + 1) % ntiles;
                else if (a == A_BACK) { mode = IGM_MAIN; tiles_free(); }
                else if (a == A_OK && ntiles && (tmode == IG_SAVE || tiles[tsel].when >= 0)) {
                    result = tmode; pick = tiles[tsel].state;
                }
            } else if (mode == IGM_OPTIONS) {
                if (a == A_UP) osel = (osel + OPT_N - 1) % OPT_N;
                else if (a == A_DOWN) osel = (osel + 1) % OPT_N;
                else if (a == A_LEFT) opt_change(osel, -1);
                else if (a == A_RIGHT || a == A_OK) opt_change(osel, +1);
                else if (a == A_BACK) { mode = IGM_MAIN; if (opt_dirty) { settings_save(); opt_dirty = 0; ig_log("%s", "options applied"); } }
            } else {
                if (a == A_LEFT || a == A_RIGHT) csel ^= 1;
                else if (a == A_BACK) mode = IGM_MAIN;
                else if (a == A_OK) { if (csel == 0) result = cwhat; else mode = IGM_MAIN; }
            }
        }
    }
    if (quit_flag) result = IG_RESUME;
    if (opt_dirty) { settings_save(); opt_dirty = 0; }

    snprintf(s0, sizeof s0, "%s.state", ig_tmpbase);
    snprintf(p0, sizeof p0, "%s.state.png", ig_tmpbase);

    if (result == IG_SAVE) {
        char png[640];
        snprintf(msg, sizeof msg, "Saving to slot %d...", tsel + 1);
        ig_toast(msg);
        ig_wait_release();
        rw_pending = 0;
        unlink(s0); unlink(p0);
        ig_send("PAUSE_TOGGLE\n");
        SDL_Delay(100);
        ig_send("SAVE_STATE\n");
        snprintf(png, sizeof png, "%s.png", pick);
        if (ig_wait_file(s0, 4000) && copy_file(s0, pick) == 0) {
            if (ig_wait_file(p0, 1000)) copy_file(p0, png); else unlink(png);
            unlink(s0); unlink(p0);
            sync();
            snprintf(msg, sizeof msg, "Saved to slot %d", tsel + 1);
            ig_log("saved %s", pick);
        } else {
            snprintf(msg, sizeof msg, "Save failed");
            ig_log("save failed: %s", pick);
        }
        ig_toast(msg);
        SDL_Delay(700);
    } else if (result == IG_SHOT) {
        SDL_Surface *src;
        ig_toast("Taking screenshot...");
        ig_wait_release();
        rw_pending = 0;
        unlink(s0); unlink(p0);
        ig_send("PAUSE_TOGGLE\n");
        SDL_Delay(100);
        ig_send("SAVE_STATE\n");                 /* its thumbnail is the game picture */
        snprintf(msg, sizeof msg, "Screenshot failed");
        if (ig_wait_file(s0, 4000) && ig_wait_file(p0, 1500) && (src = IMG_Load(p0))) {
            SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 24, SDL_PIXELFORMAT_RGB24);
            const char *b = strrchr(ig_base, '/') ? strrchr(ig_base, '/') + 1 : ig_base;
            char out[800];
            int n;
            mkdir(ig_shots, 0755);
            for (n = 1; n < 1000; n++) {
                snprintf(out, sizeof out, "%s/%s-%03d.png", ig_shots, b, n);
                if (access(out, F_OK) != 0) break;
            }
            if (dst && SDL_BlitScaled(src, NULL, dst, NULL) == 0 && IMG_SavePNG(dst, out) == 0) {
                sync();
                snprintf(msg, sizeof msg, "Screenshot saved");
                ig_log("screenshot %s", out);
            }
            if (dst) SDL_FreeSurface(dst);
            SDL_FreeSurface(src);
        }
        unlink(s0); unlink(p0);
        ig_toast(msg);
        SDL_Delay(900);
    } else if (result == IG_LOAD || result == IG_REWIND) {
        ig_toast(result == IG_LOAD ? "Loading..." : "Going back...");
        ig_wait_release();
        rw_pending = 0;
        unlink(p0);
        if (copy_file(pick, s0) == 0) {
            ig_send("PAUSE_TOGGLE\n");
            SDL_Delay(100);
            ig_send("LOAD_STATE\n");
            SDL_Delay(400);
            ig_log("loaded %s", pick);
        } else {
            ig_log("cannot copy %s", pick);
            ig_send("PAUSE_TOGGLE\n");
        }
    } else if (result == IG_RESTART) {
        ig_wait_release();
        ig_send("PAUSE_TOGGLE\n");
        SDL_Delay(100);
        ig_send("RESET\n");
    } else if (result == IG_RA) {
        ig_wait_release();
        ig_send("PAUSE_TOGGLE\n");
        SDL_Delay(100);
        ig_send("MENU_TOGGLE\n");
    } else if (result == IG_QUIT) {
        ig_toast("Returning to the library...");
        ig_send("QUIT\n");
        SDL_Delay(300);
        ig_close_window();
        return 0;
    } else {
        ig_wait_release();
        ig_send("PAUSE_TOGGLE\n");
    }
    ig_close_window();
    rw_due = SDL_GetTicks() + REWIND_EVERY_MS;      /* next snapshot 10 s after the menu closes */
    return 1;
}

static int run_ingame(const char *title)
{
    int run_down[MAX_PADS] = { 0 }, sel_down[MAX_PADS] = { 0 };
    const char *b = strrchr(ig_base, '/') ? strrchr(ig_base, '/') + 1 : ig_base;
    char *slash;
    snprintf(ig_tmpbase, sizeof ig_tmpbase, GX_TMP "/states/%s", b);
    snprintf(ig_settings, sizeof ig_settings, "%s", ig_base);      /* <rom>/saves/<base> -> <rom>/archgx-settings.cfg */
    if ((slash = strrchr(ig_settings, '/'))) *slash = '\0';
    if ((slash = strrchr(ig_settings, '/'))) strcpy(slash + 1, "archgx-settings.cfg");
    else strcpy(ig_settings, "archgx-settings.cfg");
    snprintf(ig_shots, sizeof ig_shots, "%s", ig_settings);       /* <rom>/screenshots */
    if ((slash = strrchr(ig_shots, '/'))) strcpy(slash + 1, "screenshots"); else strcpy(ig_shots, "screenshots");
    settings_load();
    if (SDL_GetCurrentDisplayMode(0, &ig_dm) != 0 || ig_dm.w <= 0) { ig_dm.w = 1280; ig_dm.h = 720; }
    rw_reset();
    rw_due = SDL_GetTicks() + REWIND_EVERY_MS;
    ig_log("watching for RUN + SELECT, saves %s", ig_base);
    while (!quit_flag) {
        SDL_Event ev;
        /* test hook: scripted menu keys, optionally after PCE_LAUNCHER_KEYS_DELAY s of play */
        int trigger = getenv("PCE_LAUNCHER_KEYS") != NULL &&
            SDL_GetTicks() >= (Uint32)(1000 * atoi(getenv("PCE_LAUNCHER_KEYS_DELAY") ? getenv("PCE_LAUNCHER_KEYS_DELAY") : "0"));
        if (SDL_WaitEventTimeout(&ev, 100)) {
            do {
                if (ev.type == SDL_QUIT) quit_flag = 1;
                else if (ev.type == SDL_JOYDEVICEADDED) add_pad(ev.jdevice.which);
                else if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
                    int k, d = ev.type == SDL_JOYBUTTONDOWN;
                    for (k = 0; k < npads; k++) if (pads[k].id == ev.jbutton.which) {
                        if (ev.jbutton.button == pads[k].map.start) run_down[k] = d;
                        if (ev.jbutton.button == pads[k].map.select) sel_down[k] = d;
                        if (d && run_down[k] && sel_down[k]) trigger = 1;
                    }
                }
            } while (SDL_PollEvent(&ev));
        }
        if (trigger && getenv("PCE_LAUNCHER_KEYS")) {   /* test hook: scripted menu keys */
            char buf[1024], *t;
            snprintf(buf, sizeof buf, "%s", getenv("PCE_LAUNCHER_KEYS"));
            for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) push(test_action(t));
        }
        if (trigger && !quit_flag) {
            memset(run_down, 0, sizeof run_down); memset(sel_down, 0, sizeof sel_down);
            if (!ig_menu(title)) break;
            if (getenv("PCE_LAUNCHER_KEYS")) break;
        } else rw_tick();
    }
    return 0;
}

/* ------------------------------------------------- library: About, pad test */
#include <sys/statvfs.h>

/* ---------------------------------------------------- hardware check */
/* The cart reader (BTE TG Dumper, 2e25:988c: HuCard slot + both pad ports)
 * and the front USB ports (hub 1a40:0101) are USB devices. If one is missing
 * some time after start-up, say so on screen instead of failing silently. */
#define HW_READER   1
#define HW_READER_X 2      /* a 2e25 device with another ID (update mode?) */
#define HW_HUB      4
#define HW_GRACE_S  20     /* seconds after power-on before checking */
#define HW_WARN_S   30     /* the reader re-connects at every cart change: warn only after 30 s missing */
static int hw_flags = HW_READER | HW_HUB, hw_checked;
static double hw_missing_since, hw_up;

static int read_hex(const char *path)
{
    char b[16];
    FILE *f = fopen(path, "r");
    int v = -1;
    if (f) { if (fgets(b, sizeof b, f)) v = (int)strtol(b, NULL, 16); fclose(f); }
    return v;
}

static int hw_scan(void)
{
    const char *root = getenv("PCE_LAUNCHER_USB") ? getenv("PCE_LAUNCHER_USB") : "/sys/bus/usb/devices";   /* test hook */
    DIR *d = opendir(root);
    struct dirent *e;
    char p[300];
    int fl = 0;
    if (!d) return HW_READER | HW_HUB;     /* not on the console: no warning */
    while ((e = readdir(d))) {
        int vid, pid;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s/idVendor", root, e->d_name);
        if ((vid = read_hex(p)) < 0) continue;
        snprintf(p, sizeof p, "%s/%s/idProduct", root, e->d_name);
        pid = read_hex(p);
        if (vid == 0x2e25) fl |= pid == 0x988c ? HW_READER : HW_READER_X;
        if (vid == 0x1a40 && pid == 0x0101) fl |= HW_HUB;
    }
    closedir(d);
    return fl;
}

/* returns 1 when the result changed (the screen needs a redraw) */
static int hw_check(void)
{
    double up = 0;
    FILE *f = fopen("/proc/uptime", "r");
    int old = hw_flags;
    if (f) { if (fscanf(f, "%lf", &up) != 1) up = 0; fclose(f); }
    hw_up = up;
    if (up < HW_GRACE_S) return 0;
    hw_flags = hw_scan();
    if (hw_flags & HW_READER) hw_missing_since = 0;
    else if (!hw_missing_since) hw_missing_since = up;
    if (!hw_checked || hw_flags != old) {
        hw_checked = 1;
        if (!(hw_flags & HW_READER)) SDL_Log("hardware: cart reader not found%s", hw_flags & HW_READER_X ? " (a BTE device with another ID is present)" : "");
        if (!(hw_flags & HW_HUB)) SDL_Log("hardware: front USB ports (hub) not found");
        return hw_flags != old;
    }
    return !(hw_flags & HW_READER);     /* keep redrawing while the 30 s run */
}

static const char *hw_warning(void)
{
    if (!(hw_flags & HW_READER) && hw_missing_since && hw_up - hw_missing_since >= HW_WARN_S)
        return hw_flags & HW_READER_X ? "Cart reader is in update mode: restart the console"
                                      : "Cart reader not found: HuCards and the controller ports won't work";
    return NULL;        /* the front USB ports are shown in About only */
}

static void draw_about(void)
{
    int pw = W * 70 / 100, ph = H * 66 / 100, px, py, nst = 0, lh = H / 17, y;
    char line[160];
    struct statvfs vf;
    DIR *d = opendir(saves_dir);
    struct dirent *e;
    if (d) {
        while ((e = readdir(d))) {
            const char *s = strstr(e->d_name, ".state");
            if (s && !strstr(s, ".png") && !strstr(s, ".auto")) nst++;
        }
        closedir(d);
    }
    panel(pw, ph, &px, &py);
    y = py + H / 30;
    if (draw_img(tex_logo, W / 2, y, H / 9, 1)) y += H / 9 + H / 40;
    else { text(f_title, "ArchGX", accent, W / 2, y, 1); y += H / 10; }
    text(f_hint, "Open-source firmware for the RetroN GX  -  build " ARCHGX_VERSION, dim, W / 2, y, 1); y += lh * 3 / 2;
    snprintf(line, sizeof line, "Games: %d        Save states: %d", nreal, nst);
    text(f_item, line, white, W / 2, y, 1); y += lh;
    if (statvfs(lib_dir, &vf) == 0) {
        double fr = (double)vf.f_bavail * vf.f_frsize / 1e9, tot = (double)vf.f_blocks * vf.f_frsize / 1e9;
        snprintf(line, sizeof line, "SD card: %.1f GB free of %.1f GB", fr, tot);
        text(f_item, line, white, W / 2, y, 1);
    }
    y += lh;
    snprintf(line, sizeof line, "Cart reader: %s", hw_flags & HW_READER ? "OK" : "not found");
    text(f_item, line, hw_flags & HW_READER ? white : red, W / 2 - W / 40, y, 2);
    snprintf(line, sizeof line, "Front USB ports: %s", hw_flags & HW_HUB ? "OK" : "not found");
    text(f_item, line, hw_flags & HW_HUB ? white : red, W / 2 + W / 40, y, 0);
    y += lh * 3 / 2;
    text(f_hint, "RetroArch 1.7.4 (with ArchGX patch)  -  Beetle PCE Fast", dim, W / 2, y, 1); y += lh;
    text(f_hint, "github.com/ArchGX/ArchGX  -  not affiliated with Hyperkin", dim, W / 2, y, 1);
    text(f_hint, "I / II  Close", dim, W / 2, py + ph - H / 16, 1);
}

/* Debug screen (hidden: hold SELECT for 3 s on About) */
#define DBG_ITEMS (D_N + 1)            /* the options, then Close */
static void draw_debug(int opt)
{
    int pw = W * 72 / 100, ph = H * 80 / 100, px, py, lh = H / 17, y, i, drive;
    char line[200], ls[3][160], lp[600];
    FILE *f;
    const char *dev = getenv("PCE_LAUNCHER_CD") ? getenv("PCE_LAUNCHER_CD") : "/dev/sr0";
    panel(pw, ph, &px, &py);
    y = py + H / 40;
    text(f_title, "Debug", accent, W / 2, y, 1); y += H / 10;
    for (i = 0; i < DBG_ITEMS; i++, y += lh) {
        if (i == opt) fill(px + W / 60, y - lh / 10, pw - W / 30, lh, 255, 255, 255, 40);
        if (i < D_N) {
            text(f_item, dbg_name[i], white, px + W / 30, y, 0);
            text(f_item, dbg_val[i] ? "On" : "Off", dbg_val[i] ? c_ok : dim, px + pw - W / 30, y, 2);
        } else text(f_item, "Close", white, W / 2, y, 1);
    }
    y += lh / 2;
    fill(px + W / 30, y, pw - W / 15, 1, 255, 255, 255, 60); y += lh / 2;
    drive = access(dev, F_OK) == 0;
    snprintf(line, sizeof line, "Cart reader: %s", hw_flags & HW_READER ? "OK" : "not found");
    text(f_hint, line, hw_flags & HW_READER ? white : red, px + W / 30, y, 0);
    snprintf(line, sizeof line, "Front USB ports: %s", hw_flags & HW_HUB ? "OK" : "not found");
    text(f_hint, line, hw_flags & HW_HUB ? white : red, W / 2, y, 1);
    snprintf(line, sizeof line, "CD drive: %s", drive ? (cd_present ? "disc in" : "connected") : "none");
    text(f_hint, line, white, px + pw - W / 30, y, 2); y += lh;
    /* logs/last-session.txt (gx-start): title, dropped frames, how it closed */
    snprintf(lp, sizeof lp, "%s", readcheck_path);
    if (strrchr(lp, '/')) strcpy(strrchr(lp, '/') + 1, "logs/last-session.txt"); else strcpy(lp, "logs/last-session.txt");
    ls[0][0] = ls[1][0] = ls[2][0] = 0;
    if ((f = fopen(lp, "r"))) {
        for (i = 0; i < 3 && fgets(ls[i], sizeof ls[i], f); i++) ls[i][strcspn(ls[i], "\r\n")] = 0;
        fclose(f);
    }
    if (ls[0][0]) {
        snprintf(line, sizeof line, "Last game: %s", ls[0]);
        text(f_hint, line, white, px + W / 30, y, 0); y += lh * 3 / 4;
        snprintf(line, sizeof line, "%s  -  %s", ls[1], ls[2]);
        text(f_hint, line, dim, px + W / 30, y, 0);
    } else text(f_hint, "Last game: none yet", dim, px + W / 30, y, 0);
    text(f_hint, "I / Left / Right  Change      II  Close", dim, W / 2, py + ph - H / 16, 1);
}

static int pad_dir(SDL_Joystick *j, int which)      /* 0 up, 1 down, 2 left, 3 right */
{
    if (SDL_JoystickNumHats(j) > 0) {
        Uint8 h = SDL_JoystickGetHat(j, 0);
        static const Uint8 m[4] = { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT };
        if (h & m[which]) return 1;
    }
    if (SDL_JoystickNumAxes(j) >= 2) {
        int v = SDL_JoystickGetAxis(j, which < 2 ? 1 : 0);
        if ((which == 0 || which == 2) && v < -16000) return 1;
        if ((which == 1 || which == 3) && v > 16000) return 1;
    }
    return 0;
}

static int pad_btn(SDL_Joystick *j, int b) { return b >= 0 && SDL_JoystickGetButton(j, b); }

/* Controller test: every connected pad with its d-pad and four buttons.
 * Hold SELECT + RUN for a second (or Escape) to leave. */
static void run_padtest(void)
{
    Uint32 combo_since = 0;
    const char *shot = getenv("PCE_LAUNCHER_SHOT");
    Uint32 t0 = SDL_GetTicks();
    for (;;) {
        SDL_Event ev;
        int i, k, combo = 0;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return;
            if (ev.type == SDL_KEYDOWN && (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE)) return;
            if (ev.type == SDL_JOYDEVICEADDED) add_pad(ev.jdevice.which);
            if (ev.type == SDL_JOYDEVICEREMOVED)
                for (k = 0; k < npads; k++) if (pads[k].id == ev.jdevice.which) { pads[k] = pads[--npads]; break; }
        }
        if (quit_flag) return;
        SDL_SetRenderDrawColor(ren, 20, 22, 30, 255);
        SDL_RenderClear(ren);
        text(f_title, "Controller test", white, W / 10, H * 6 / 100, 0);
        fill(W / 10, H * 17 / 100, W * 8 / 10, H / 240 + 1, accent.r, accent.g, accent.b, 255);
        if (!npads) text(f_item, "No controllers found. Plug one in.", dim, W / 2, H * 45 / 100, 1);
        for (i = 0; i < npads && i < 4; i++) {
            SDL_Joystick *j = SDL_JoystickFromInstanceID(pads[i].id);
            PadMap *m = &pads[i].map;
            int cw = W * 40 / 100, ch = H * 30 / 100, cx = W / 10 + (i % 2) * (cw + W / 20), cy = H * 22 / 100 + (i / 2) * (ch + H / 30);
            int u = H / 22, dx = cx + cw / 6, dy = cy + ch / 2 + u / 2, d;
            char lbl[32];
            fill(cx, cy, cw, ch, 28, 30, 40, 255);
            snprintf(lbl, sizeof lbl, "Pad %d", i + 1);
            text(f_hint, lbl, accent, cx + cw / 30, cy + ch / 30, 0);
            if (!j) continue;
            /* d-pad cross */
            for (d = 0; d < 4; d++) {
                static const int ox[4] = { 0, 0, -1, 1 }, oy[4] = { -1, 1, 0, 0 };
                int on = pad_dir(j, d);
                fill(dx + ox[d] * u - u / 2, dy + oy[d] * u - u / 2, u, u, on ? accent.r : 70, on ? accent.g : 74, on ? accent.b : 90, 255);
            }
            fill(dx - u / 2, dy - u / 2, u, u, 50, 54, 70, 255);
            /* SELECT, RUN, II, I */
            {
                struct { const char *n; int b; int x; } bt[4] = {
                    { "SELECT", m->select, cx + cw * 38 / 100 }, { "RUN", m->start, cx + cw * 55 / 100 },
                    { "II", m->b, cx + cw * 72 / 100 }, { "I", m->a, cx + cw * 87 / 100 } };
                for (k = 0; k < 4; k++) {
                    int on = pad_btn(j, bt[k].b), bw = k < 2 ? cw / 7 : u * 3 / 2, bh = k < 2 ? u * 2 / 3 : u * 3 / 2;
                    int by = dy - bh / 2;
                    fill(bt[k].x - bw / 2, by, bw, bh, on ? accent.r : 70, on ? accent.g : 74, on ? accent.b : 90, 255);
                    text(f_hint, bt[k].n, on ? white : dim, bt[k].x, by + bh + H / 80, 1);
                }
            }
            if (pad_btn(j, m->select) && pad_btn(j, m->start)) combo = 1;
        }
        text(f_hint, "Hold SELECT + RUN to go back", dim, W / 2, H * 91 / 100, 1);
        if (combo) { if (!combo_since) combo_since = SDL_GetTicks(); else if (SDL_GetTicks() - combo_since > 1000) return; }
        else combo_since = 0;
        if (shot && SDL_GetTicks() - t0 > 300) {
            SDL_Surface *ss = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            if (ss && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch) == 0)
                SDL_SaveBMP(ss, shot);
            _exit(3);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
}

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Texture *bg = NULL;
    SDL_DisplayMode dm;
    int sel = 0, top = 0, rows, row_h, list_y, list_x, list_w, dirty = 1, i;
    int mode = M_LIST, opt = 0, held = 0, kb_r = 0, kb_c = 0, kb_lower = 0;
    Uint32 about_sel = 0;
    char kb_text[NAME_MAX_LEN + 1] = "", msg[160] = "";
    Uint32 last_cart = 0, repeat_at = 0, msg_until = 0;
    unsigned long cart0;
    const char *shot = getenv("PCE_LAUNCHER_SHOT");
    char *slash;

    int status_mode = argc == 3 && !strcmp(argv[1], "--status");
    int ingame_mode = (argc == 4 || argc == 5) && !strcmp(argv[1], "--ingame");
    if (argc != 3 && !ingame_mode) {
        fprintf(stderr, "usage: %s <library-dir> <out-file> | --status <file> | --ingame <fifo> <state-base> [title]\n", argv[0]);
        return 1;
    }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    signal(SIGSEGV, on_crash); signal(SIGBUS, on_crash); signal(SIGABRT, on_crash);

    snprintf(lib_dir, sizeof lib_dir, "%s", argv[1]);
    while (strlen(lib_dir) > 1 && lib_dir[strlen(lib_dir) - 1] == '/') lib_dir[strlen(lib_dir) - 1] = '\0';
    snprintf(data_dir, sizeof data_dir, "%s", lib_dir);
    slash = strrchr(data_dir, '/');
    if (slash) strcpy(slash + 1, "data"); else strcpy(data_dir, "data");
    snprintf(titles_path, sizeof titles_path, "%.*stitles.txt", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    snprintf(localdb_path, sizeof localdb_path, "%.*sgamedb-local.csv", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    snprintf(recent_path, sizeof recent_path, "%.*srecent.txt", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    snprintf(bios_dir, sizeof bios_dir, "%.*sbios", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    snprintf(readcheck_path, sizeof readcheck_path, "%.*sread-check.txt", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    snprintf(debug_path, sizeof debug_path, "%.*sdebug.cfg", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    debug_load();
    snprintf(saves_dir, sizeof saves_dir, "%.*ssaves", slash ? (int)(slash - data_dir + 1) : 0, data_dir);
    cart0 = (status_mode || ingame_mode) ? 0 : cart_state();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 1; }
    IMG_Init(IMG_INIT_PNG);
    SDL_ShowCursor(SDL_DISABLE);
    if (ingame_mode) {
        ig_fifo = argv[2]; ig_base = argv[3];
        for (i = 0; i < SDL_NumJoysticks(); i++) add_pad(i);
        return run_ingame(argc == 5 ? argv[4] : "");
    }
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0 || dm.w <= 0) { dm.w = 1280; dm.h = 720; }
    win = SDL_CreateWindow("Game Library", 0, 0, dm.w, dm.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_GetRendererOutputSize(ren, &W, &H);

    f_title = TTF_OpenFont(FONT_PATH, H / 16);
    f_item  = TTF_OpenFont(FONT_PATH, H / 26);
    f_hint  = TTF_OpenFont(FONT_LIGHT, H / 34);
    if (!f_hint) f_hint = TTF_OpenFont(FONT_PATH, H / 34);
    if (!f_title || !f_item || !f_hint) { fprintf(stderr, "TTF_OpenFont: %s\n", TTF_GetError()); return 1; }
    bg = IMG_LoadTexture(ren, BG_PATH);
    load_brand();
    if (status_mode) return run_status(argv[2], bg, shot);

    cd_check();
    scan_library();
    SDL_Log("screen %dx%d, %d games, data dir %s", W, H, nreal, data_dir);
    for (i = 0; i < SDL_NumJoysticks(); i++) add_pad(i);

    row_h = H / 15; list_y = H * 22 / 100; list_x = W / 10; list_w = W - 2 * list_x;
    rows = (H * 66 / 100) / row_h;
    sel = load_last();
    if (getenv("PCE_LAUNCHER_ASK") && (i = find_game(getenv("PCE_LAUNCHER_ASK"))) >= 0) {
        sel = i; mode = M_ASK; opt = 0;          /* a new unknown HuCard: how to play it? */
    }

    if (getenv("PCE_LAUNCHER_KEYS")) {
        char buf[1024], *t;
        snprintf(buf, sizeof buf, "%s", getenv("PCE_LAUNCHER_KEYS"));
        for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) push(test_action(t));
    }

    while (!quit_flag) {
        SDL_Event ev;
        Uint32 now;
        int a;

        /* ---- collect input into the action queue ---- */
        if (qhead == qtail ? SDL_WaitEventTimeout(&ev, 50) : SDL_PollEvent(&ev)) {
            do {
                last_event = (sig_atomic_t)ev.type;
                switch (ev.type) {
                case SDL_QUIT: quit_flag = 1; break;
                case SDL_KEYDOWN: push(key_action(ev.key.keysym.sym)); break;
                case SDL_JOYDEVICEADDED: add_pad(ev.jdevice.which); break;
                case SDL_JOYDEVICEREMOVED: {
                    int k;
                    for (k = 0; k < npads; k++)
                        if (pads[k].id == ev.jdevice.which) { pads[k] = pads[--npads]; break; }
                    break; }
                case SDL_JOYHATMOTION: {
                    int v = ev.jhat.value, d = A_NONE;
                    if (v & SDL_HAT_UP) d = A_UP; else if (v & SDL_HAT_DOWN) d = A_DOWN;
                    else if (v & SDL_HAT_LEFT) d = A_LEFT; else if (v & SDL_HAT_RIGHT) d = A_RIGHT;
                    held = d; push(d); repeat_at = SDL_GetTicks() + 400;
                    break; }
                case SDL_JOYAXISMOTION: {
                    int d = A_NONE, v = ev.jaxis.value;
                    if (ev.jaxis.axis == 1) d = v < -16000 ? A_UP : v > 16000 ? A_DOWN : A_NONE;
                    else if (ev.jaxis.axis == 0) d = v < -16000 ? A_LEFT : v > 16000 ? A_RIGHT : A_NONE;
                    else break;
                    if (d != held) { held = d; push(d); repeat_at = SDL_GetTicks() + 400; }
                    break; }
                case SDL_JOYBUTTONDOWN: {
                    PadMap *m = pad_map(ev.jbutton.which);
                    int b = ev.jbutton.button;
                    if (m && m->a >= 0) {
                        if (b == m->a) push(A_OK);
                        else if (b == m->start) push(A_RUN);
                        else if (b == m->b) push(A_BACK);
                        else if (b == m->select) push(A_SELECT);
                    } else {
                        push(A_OK);   /* unknown pad: any button plays */
                    }
                    break; }
                default: break;
                }
            } while (SDL_PollEvent(&ev));
        }
        now = SDL_GetTicks();
        if (held && now >= repeat_at) { push(held); repeat_at = now + 90; }
        if (about_sel && mode == M_ABOUT) {        /* SELECT on About: released = close, held 3 s = Debug */
            int k, down = 0;
            for (k = 0; k < npads; k++) {
                SDL_Joystick *j = SDL_JoystickFromInstanceID(pads[k].id);
                if (j && pad_btn(j, pads[k].map.select)) down = 1;
            }
            if (!down) { mode = M_LIST; about_sel = 0; dirty = 1; }
            else if (now - about_sel >= 3000) { mode = M_DEBUG; opt = 0; about_sel = 0; dirty = 1; SDL_Log("debug screen opened"); }
        } else about_sel = 0;

        /* ---- handle actions ---- */
        while ((a = pop()) != A_NONE) {
            dirty = 1;
            if (mode == M_MESSAGE) { mode = M_LIST; continue; }
            if (a == A_DEBUG) { mode = M_DEBUG; opt = 0; continue; }
            if (mode == M_ABOUT) {
                if (a == A_SELECT) about_sel = now ? now : 1;      /* held 3 s: Debug (checked below) */
                else if (a == A_OK || a == A_BACK || a == A_RUN) mode = M_LIST;
                continue;
            }
            if (mode == M_DEBUG) {
                if (a == A_UP) opt = (opt + DBG_ITEMS - 1) % DBG_ITEMS;
                else if (a == A_DOWN) opt = (opt + 1) % DBG_ITEMS;
                else if (a == A_BACK || a == A_SELECT || ((a == A_OK || a == A_RUN) && opt == D_N)) mode = M_LIST;
                else if (opt < D_N && (a == A_OK || a == A_RUN || a == A_LEFT || a == A_RIGHT)) {
                    dbg_val[opt] = !dbg_val[opt]; debug_save();
                    SDL_Log("debug: %s=%d", dbg_key[opt], dbg_val[opt]);
                    if (opt == D_CRC) { int k; for (k = 0; k < ngames; k++) if (games[k].tex) { SDL_DestroyTexture(games[k].tex); games[k].tex = NULL; } }
                }
                continue;
            }

            if (mode == M_LIST) {
                if (a == A_UP || a == A_DOWN) {
                    sel += a == A_UP ? -1 : 1;
                    if (sel < 0) sel = ngames - 1;
                    if (sel >= ngames) sel = 0;
                } else if (a == A_LEFT || a == A_RIGHT) {
                    sel += a == A_LEFT ? -rows : rows;
                    if (sel < 0) sel = 0;
                    if (sel >= ngames) sel = ngames - 1;
                } else if ((a == A_OK || a == A_RUN) && sel < nreal && has_state(games[sel].path)) {
                    mode = M_PLAY; opt = 0;                      /* Resume / Start new game */
                } else if ((a == A_OK || a == A_RUN) && !strcmp(games[sel].path, "__PADTEST__")) {
                    run_padtest(); qhead = qtail = 0; held = 0;
                } else if ((a == A_OK || a == A_RUN) && !strcmp(games[sel].path, "__ABOUT__")) {
                    mode = M_ABOUT;
                } else if (a == A_OK || a == A_RUN) {
                    FILE *f = fopen(argv[2], "w");
                    if (f) { fprintf(f, "%s\n", games[sel].path); fclose(f); }
                    save_last(games[sel].path);
                    SDL_Log("exit: chose %s", games[sel].path);
                    _exit(0);   /* skip SDL/Mali teardown */
                } else if (a == A_SELECT && sel < nreal) {
                    mode = M_OPTIONS; opt = 0;
                }
            } else if (mode == M_PLAY) {
                if (a == A_UP && opt > 0) opt--;
                else if (a == A_DOWN && opt < 2) opt++;
                else if (a == A_BACK || a == A_SELECT || ((a == A_OK || a == A_RUN) && opt == 2)) mode = M_LIST;
                else if (a == A_OK || a == A_RUN) {
                    FILE *f = fopen(argv[2], "w");
                    if (f) { fprintf(f, "%s\n%s\n", games[sel].path, opt == 0 ? "resume" : "new"); fclose(f); }
                    save_last(games[sel].path);
                    SDL_Log("exit: chose %s (%s)", games[sel].path, opt == 0 ? "resume" : "new game");
                    _exit(0);
                }
            } else if (mode == M_ASK) {
                if (a == A_UP && opt > 0) opt--;
                else if (a == A_DOWN && opt < 3) opt++;
                else if (a == A_BACK || a == A_SELECT || ((a == A_OK || a == A_RUN) && opt == 3)) mode = M_LIST;
                else if (a == A_OK || a == A_RUN) {
                    char np[1024], bn[64];
                    const char *play = NULL;
                    if (opt == 0) play = games[sel].path;
                    else if (opt == 1) {
                        if (set_kind(sel, 1, np, sizeof np) == 0) play = np;
                        else { snprintf(msg, sizeof msg, "Could not change the game"); mode = M_MESSAGE; msg_until = now + 2500; }
                    } else {
                        if (use_as_bios(sel, bn, sizeof bn) == 0) snprintf(msg, sizeof msg, "Saved as CD System Card (bios/%s)", bn);
                        else snprintf(msg, sizeof msg, "Could not save it as a System Card");
                        scan_library();
                        if (sel >= ngames) sel = ngames - 1;
                        mode = M_MESSAGE; msg_until = now + 3000;
                    }
                    if (play) {
                        FILE *f = fopen(argv[2], "w");
                        if (f) { fprintf(f, "%s\n", play); fclose(f); }
                        save_last(play);
                        SDL_Log("exit: chose %s (new HuCard)", play);
                        _exit(0);
                    }
                }
            } else if (mode == M_OPTIONS) {
                int ops[6], nops = game_ops(games[sel].kind, ops, (const char **)opnames);
                if (a == A_UP && opt > 0) opt--;
                else if (a == A_DOWN && opt < nops - 1) opt++;
                else if (a == A_BACK || a == A_SELECT || ((a == A_OK || a == A_RUN) && ops[opt] == OP_CANCEL)) mode = M_LIST;
                else if (a == A_OK || a == A_RUN) {
                    char lab[NAME_MAX_LEN * 2], np[1024], bn[64];
                    snprintf(lab, sizeof lab, "%s", games[sel].label);
                    if (ops[opt] == OP_RENAME) {
                        mode = M_KEYBOARD; kb_r = 0; kb_c = 0; kb_lower = 0;
                        snprintf(kb_text, sizeof kb_text, "%s",
                                 strncmp(games[sel].label, "Unknown ", 8) ? games[sel].label : "");
                    } else if (ops[opt] == OP_DELETE) {
                        mode = M_DELETE; opt = 1;   /* default: No */
                    } else if (ops[opt] == OP_SGX || ops[opt] == OP_HUCARD) {
                        int to = ops[opt] == OP_SGX;
                        if (set_kind(sel, to, np, sizeof np) == 0) {
                            snprintf(msg, sizeof msg, to ? "\"%s\" will run as SuperGrafx" : "\"%s\" will run as a HuCard", lab);
                            scan_library();
                            if ((i = find_game(np)) >= 0) sel = i;
                        } else snprintf(msg, sizeof msg, "Could not change \"%s\"", lab);
                        mode = M_MESSAGE; msg_until = now + 2500;
                    } else if (ops[opt] == OP_BIOS) {
                        if (use_as_bios(sel, bn, sizeof bn) == 0) snprintf(msg, sizeof msg, "Saved as CD System Card (bios/%s)", bn);
                        else snprintf(msg, sizeof msg, "Could not save \"%s\" as a System Card", lab);
                        scan_library();
                        if (sel >= ngames) sel = ngames - 1;
                        mode = M_MESSAGE; msg_until = now + 3000;
                    }
                }
            } else if (mode == M_DELETE) {
                if (a == A_LEFT || a == A_RIGHT || a == A_UP || a == A_DOWN) opt = !opt;
                else if (a == A_BACK || a == A_SELECT || ((a == A_OK || a == A_RUN) && opt == 1)) mode = M_LIST;
                else if (a == A_OK || a == A_RUN) {
                    char lab[NAME_MAX_LEN * 2];
                    snprintf(lab, sizeof lab, "%s", games[sel].label);
                    if (delete_game(sel) == 0) snprintf(msg, sizeof msg, "Deleted \"%s\"", lab);
                    else snprintf(msg, sizeof msg, "Could not delete \"%s\"", lab);
                    scan_library();
                    if (sel >= ngames) sel = ngames - 1;
                    mode = M_MESSAGE; msg_until = now + 2000;
                }
            } else if (mode == M_KEYBOARD) {
                int nrows = (int)(sizeof kb_rows / sizeof kb_rows[0]);
                size_t len = strlen(kb_text);
                int key = -1;   /* special index when on the last row */
                if (a == A_UP) { kb_r = kb_r > 0 ? kb_r - 1 : nrows; }
                else if (a == A_DOWN) { kb_r = kb_r < nrows ? kb_r + 1 : 0; }
                else if (a == A_LEFT) { int n = kb_r == nrows ? KB_NSPECIAL : KB_COLS; kb_c = (kb_c + n - 1) % n; }
                else if (a == A_RIGHT) { int n = kb_r == nrows ? KB_NSPECIAL : KB_COLS; kb_c = (kb_c + 1) % n; }
                else if (a == A_BACK) { if (len) kb_text[len - 1] = '\0'; }
                else if (a == A_SELECT) key = KB_CANCEL;
                else if (a == A_RUN) key = KB_DONE;
                else if (a == A_OK) {
                    if (kb_r == nrows) key = kb_c;
                    else if (len < NAME_MAX_LEN) {
                        char c = kb_rows[kb_r][kb_c];
                        if (kb_lower) c = (char)tolower((unsigned char)c);
                        kb_text[len] = c; kb_text[len + 1] = '\0';
                    }
                }
                if (kb_r == nrows && kb_c >= KB_NSPECIAL) kb_c = KB_NSPECIAL - 1;
                if (key == KB_SHIFT) kb_lower = !kb_lower;
                else if (key == KB_SPACE) { if (len && len < NAME_MAX_LEN) { kb_text[len] = ' '; kb_text[len + 1] = '\0'; } }
                else if (key == KB_DEL) { if (len) kb_text[len - 1] = '\0'; }
                else if (key == KB_CLEAR) kb_text[0] = '\0';
                else if (key == KB_CANCEL) mode = M_LIST;
                else if (key == KB_DONE) {
                    char newpath[1024];
                    int r;
                    /* FAT does not allow trailing spaces or dots */
                    while ((len = strlen(kb_text)) && (kb_text[len - 1] == ' ' || kb_text[len - 1] == '.'))
                        kb_text[len - 1] = '\0';
                    if (!kb_text[0]) { snprintf(msg, sizeof msg, "The name cannot be empty"); }
                    else if (!strcmp(kb_text, games[sel].label)) { mode = M_LIST; continue; }
                    else if ((r = rename_game(sel, kb_text, newpath, sizeof newpath)) == 0) {
                        snprintf(msg, sizeof msg, "Renamed to \"%s\"", kb_text);
                        scan_library();
                        if ((i = find_game(newpath)) >= 0) sel = i;
                        save_last(newpath);
                    } else if (r == -2) snprintf(msg, sizeof msg, "\"%s\" already exists", kb_text);
                    else snprintf(msg, sizeof msg, "Rename failed (%s)", strerror(errno));
                    mode = M_MESSAGE; msg_until = now + 2000;
                }
            }
        }
        if (mode == M_MESSAGE && now >= msg_until && !shot) { mode = M_LIST; dirty = 1; }
        {   /* hardware check every 2 s */
            static Uint32 hw_next;
            if (now >= hw_next) {
                hw_next = now + 2000;
                if (hw_check()) dirty = 1;
                if (cd_check()) {                /* disc in or out: list it */
                    char keep[1024];
                    snprintf(keep, sizeof keep, "%s", sel < ngames ? games[sel].path : "");
                    scan_library();
                    for (i = 0; i < ngames; i++) if (!strcmp(games[i].path, keep)) { sel = i; break; }
                    if (sel >= ngames) sel = ngames - 1;
                    dirty = 1;
                }
            }
        }

        if (now - last_cart > 500) {
            unsigned long cs = cart_state();
            last_cart = now;
            if (cs != cart0 && cs != 0) { SDL_Log("exit: cartridge detected"); _exit(2); }
            cart0 = cs;
        }
        if (!dirty) continue;
        dirty = 0;

        /* ---- draw ---- */
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        SDL_SetRenderDrawColor(ren, 20, 22, 30, 255);
        SDL_RenderClear(ren);
        if (bg) { SDL_RenderCopy(ren, bg, NULL, NULL); fill(0, 0, W, H, 0, 0, 0, 90); }

        {
            int eh = TTF_FontHeight(f_title), ew = draw_img(tex_emblem, list_x, H * 6 / 100, eh, 0);
            text(f_title, "Game Library", white, list_x + (ew ? ew + W / 80 : 0), H * 6 / 100, 0);
        }
        {
            char cnt[64];
            if (sel >= nreal) snprintf(cnt, sizeof cnt, "%d game%s", nreal, nreal == 1 ? "" : "s");
            else snprintf(cnt, sizeof cnt, "%d of %d", sel + 1, nreal);
            text(f_hint, cnt, dim, list_x + list_w, H * 9 / 100, 2);
        }
        fill(list_x, H * 17 / 100, list_w, H / 240 + 1, accent.r, accent.g, accent.b, 255);

        if (!nreal) {
            text(f_item, "No games in the library yet.", white, W / 2, H * 45 / 100, 1);
            text(f_hint, "Insert a HuCard to dump it, or copy games (.pce .sgx .cue .chd)", dim, W / 2, H * 55 / 100, 1);
            text(f_hint, "into the \"games\" folder on the SD card.", dim, W / 2, H * 60 / 100, 1);
        }
        for (i = 0; i < rows && top + i < ngames; i++) {
            Game *g = &games[top + i];
            int y = list_y + i * row_h;
            if (!g->tex) {
                SDL_Surface *sf = TTF_RenderUTF8_Blended(f_item, g->label, top + i >= nreal ? accent : white);
                if (sf) { g->tex = SDL_CreateTextureFromSurface(ren, sf); g->tw = sf->w; g->th = sf->h; SDL_FreeSurface(sf); }
            }
            if (top + i == sel) {
                fill(list_x - W / 80, y, list_w + W / 40, row_h - 4, 255, 255, 255, 40);
                fill(list_x - W / 80, y, W / 200 + 2, row_h - 4, accent.r, accent.g, accent.b, 255);
            }
            if (top + i < nreal && (g->recent || g->kind != K_HUCARD || g->check == C_MOD || (dbg_val[D_CRC] && g->crc[0]))) {
                static const char *kn[] = { "", "SGX", "CD" };
                char tag[64];
                const char *first = dbg_val[D_CRC] && g->crc[0] ? g->crc : g->check == C_MOD ? "Modified?" : "";
                snprintf(tag, sizeof tag, "%s%s%s%s%s", first, *first && (g->recent || g->kind) ? "  " : "",
                         g->recent ? "Recent" : "", g->recent && g->kind ? "  " : "", kn[g->kind]);
                text(f_hint, tag, g->check == C_MOD ? c_mod : dim, list_x + list_w, y + (row_h - 4 - TTF_FontHeight(f_hint)) / 2, 2);
            }
            if (top + i > 0 && top + i < nreal && !g->recent && games[top + i - 1].recent)
                fill(list_x, y - 2, list_w, 1, 255, 255, 255, 60);      /* end of the recent block */
            if (g->tex) {
                SDL_Rect dst = { list_x + W / 60, y + (row_h - 4 - g->th) / 2, g->tw, g->th };
                SDL_Rect src;
                int tagw = (g->recent ? W / 10 : 0) + (g->kind ? W / 20 : 0) + (g->check == C_MOD || (dbg_val[D_CRC] && g->crc[0]) ? W / 8 : 0);
                if (dst.w > list_w - W / 30 - tagw) dst.w = list_w - W / 30 - tagw;
                src.x = 0; src.y = 0; src.w = dst.w; src.h = g->th;
                SDL_SetTextureAlphaMod(g->tex, top + i == sel ? 255 : 190);
                if (top + i < nreal) {          /* green: matches the database, amber: modified?, grey: unknown */
                    SDL_Color c = g->check == C_OK ? c_ok : g->check == C_MOD ? c_mod : c_unk;
                    SDL_SetTextureColorMod(g->tex, c.r, c.g, c.b);
                }
                SDL_RenderCopy(ren, g->tex, &src, &dst);
            }
        }
        if (ngames > rows) {
            int track_h = rows * row_h, bar_h = track_h * rows / ngames, bar_y;
            if (bar_h < row_h / 2) bar_h = row_h / 2;
            bar_y = list_y + (track_h - bar_h) * top / (ngames - rows);
            fill(list_x + list_w + W / 40, list_y, W / 320 + 1, track_h, 255, 255, 255, 50);
            fill(list_x + list_w + W / 40, bar_y, W / 320 + 1, bar_h, accent.r, accent.g, accent.b, 255);
        }
        text(f_hint, sel >= nreal ? (!strcmp(games[sel].path, "__CART__") ? "I / RUN  Play the HuCard that is in the slot"
                                   : !strcmp(games[sel].path, "__RIPCD__") ? "I / RUN  Copy the CD to the SD card (first time only) and play it"
                                   : !strcmp(games[sel].path, "__PADTEST__") ? "I / RUN  Check that every button works"
                                   : !strcmp(games[sel].path, "__ABOUT__") ? "I / RUN  Version and card space"
                                   : !strcmp(games[sel].path, "__RETROARCH__") ? "I / RUN  The full RetroArch menu (advanced)"
                                   : "I / RUN  Restart the console")
                                  : games[sel].check == C_MOD ? "Doesn't match the known copy: a hack, a revision or a bad read. Re-insert the HuCard to check"
                                  : "I / RUN  Play       SELECT  Game options       Left/Right  Page",
             dim, W / 2, H * 91 / 100, 1);

        if (hw_warning()) text(f_hint, hw_warning(), red, W / 2, H * 86 / 100, 1);
        if (mode == M_ABOUT) draw_about();
        if (mode == M_DEBUG) draw_debug(opt);
        if (mode == M_PLAY) {
            static const char *items[] = { "Resume last save", "Start new game", "Cancel" };
            int pw = W * 40 / 100, ph = row_h * 5, px, py;
            panel(pw, ph, &px, &py);
            text(f_hint, games[sel].label, dim, px + pw / 2, py + row_h / 3, 1);
            for (i = 0; i < 3; i++) {
                int y = py + row_h * (i + 1) + row_h / 4;
                if (i == opt) fill(px + W / 60, y, pw - W / 30, row_h - 6, 255, 255, 255, 40);
                text(f_item, items[i], white, px + pw / 2, y + (row_h - 6 - TTF_FontHeight(f_item)) / 2, 1);
            }
        } else if (mode == M_OPTIONS) {
            int ops[6], nops = game_ops(games[sel].kind, ops, (const char **)opnames);
            int pw = W * 46 / 100, ph = row_h * (nops + 2), px, py;
            panel(pw, ph, &px, &py);
            text(f_hint, games[sel].label, dim, px + pw / 2, py + row_h / 3, 1);
            for (i = 0; i < nops; i++) {
                int y = py + row_h * (i + 1) + row_h / 4;
                if (i == opt) fill(px + W / 60, y, pw - W / 30, row_h - 6, 255, 255, 255, 40);
                text(f_item, opnames[i], ops[i] == OP_DELETE ? red : white, px + pw / 2, y + (row_h - 6 - TTF_FontHeight(f_item)) / 2, 1);
            }
        } else if (mode == M_DELETE) {
            int pw = W * 60 / 100, ph = row_h * 9 / 2, px, py, bw = pw / 4;
            char q[200];
            panel(pw, ph, &px, &py);
            snprintf(q, sizeof q, "Delete \"%s\"?", games[sel].label);
            text(f_item, q, white, px + pw / 2, py + row_h * 3 / 4, 1);
            text(f_hint, "The game is removed from the library. Its saves are kept.", dim, px + pw / 2, py + row_h * 7 / 4, 1);
            for (i = 0; i < 2; i++) {
                int bx = px + pw / 2 + (i == 0 ? -bw - W / 80 : W / 80), by = py + row_h * 3;
                fill(bx, by, bw, row_h - 8, i == opt ? 255 : 80, i == opt ? 255 : 80, i == opt ? 255 : 90, i == opt ? 60 : 40);
                text(f_item, i == 0 ? "Delete" : "Cancel", i == 0 ? red : white, bx + bw / 2, by + (row_h - 8 - TTF_FontHeight(f_item)) / 2, 1);
            }
        } else if (mode == M_KEYBOARD) {
            int nrows = (int)(sizeof kb_rows / sizeof kb_rows[0]);
            int kw = W * 6 / 100, kh = H * 8 / 100, gap = W / 200;
            int gw = KB_COLS * kw + (KB_COLS - 1) * gap;
            int pw = gw + W / 20, ph = kh * (nrows + 1) + gap * nrows + row_h * 2 + H / 20, px, py, gx, gy, r, c;
            char shown[NAME_MAX_LEN + 2];
            panel(pw, ph, &px, &py);
            gx = px + (pw - gw) / 2;
            fill(gx, py + H / 40, gw, row_h, 0, 0, 0, 120);
            snprintf(shown, sizeof shown, "%s_", kb_text);
            text(f_item, shown, white, gx + W / 80, py + H / 40 + (row_h - TTF_FontHeight(f_item)) / 2, 0);
            gy = py + H / 40 + row_h + H / 40;
            for (r = 0; r <= nrows; r++) {
                int n = r == nrows ? KB_NSPECIAL : KB_COLS;
                int w = r == nrows ? (gw - (n - 1) * gap) / n : kw;
                for (c = 0; c < n; c++) {
                    int x = gx + c * (w + gap), y = gy + r * (kh + gap), on = (r == kb_r && c == kb_c);
                    char k[8];
                    if (r == nrows) snprintf(k, sizeof k, "%s", c == KB_SHIFT ? (kb_lower ? "ABC" : "abc") : kb_special[c]);
                    else { k[0] = kb_lower ? (char)tolower((unsigned char)kb_rows[r][c]) : kb_rows[r][c]; k[1] = '\0'; }
                    fill(x, y, w, kh, on ? accent.r : 60, on ? accent.g : 64, on ? accent.b : 80, on ? 255 : 200);
                    text(r == nrows ? f_hint : f_item, k, on ? ink : white, x + w / 2, y + (kh - TTF_FontHeight(r == nrows ? f_hint : f_item)) / 2, 1);
                }
            }
            text(f_hint, "I  Type      II  Erase      RUN  Done      SELECT  Cancel", dim, px + pw / 2, py + ph - row_h * 3 / 4, 1);
        } else if (mode == M_ASK) {
            static const char *items[] = { "Play as PC Engine", "Play as SuperGrafx", "Use as CD System Card", "Not now" };
            int pw = W * 56 / 100, ph = row_h * 7, px, py;
            panel(pw, ph, &px, &py);
            text(f_item, "New HuCard, not in the game database", white, px + pw / 2, py + row_h / 4, 1);
            text(f_hint, "How should it run? (SELECT changes it later)", dim, px + pw / 2, py + row_h, 1);
            for (i = 0; i < 4; i++) {
                int y = py + row_h * (i + 2) + row_h / 4;
                if (i == opt) fill(px + W / 60, y, pw - W / 30, row_h - 6, 255, 255, 255, 40);
                text(f_item, items[i], white, px + pw / 2, y + (row_h - 6 - TTF_FontHeight(f_item)) / 2, 1);
            }
        } else if (mode == M_MESSAGE) {
            int pw = W * 60 / 100, ph = row_h * 2, px, py;
            panel(pw, ph, &px, &py);
            text(f_item, msg, white, px + pw / 2, py + (ph - TTF_FontHeight(f_item)) / 2, 1);
        }

        if (shot && qhead == qtail) {
            SDL_Surface *ss = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
            if (ss && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, ss->pixels, ss->pitch) == 0)
                SDL_SaveBMP(ss, shot);
            if (ss) SDL_FreeSurface(ss);
            _exit(3);
        }
        SDL_RenderPresent(ren);
    }
    SDL_Log("exit: %s", quit_flag ? "signal" : "quit");
    _exit(3);
}
