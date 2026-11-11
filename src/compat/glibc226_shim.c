/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/* glibc226_shim.c - lets RetroArch, compiled with glibc 2.39 headers, run on
 * the console's glibc 2.26. glibc >= 2.38 headers redirect strtol/sscanf to
 * C23 variants (__isoc23_*) that 2.26 lacks; map them back. Compiled without
 * _GNU_SOURCE so the calls below bind to the classic functions. */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
long __isoc23_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
long long __isoc23_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
int __isoc23_vsscanf(const char *s, const char *f, va_list a) { return vsscanf(s, f, a); }
int __isoc23_sscanf(const char *s, const char *f, ...)
{ va_list a; int r; va_start(a, f); r = vsscanf(s, f, a); va_end(a); return r; }

/* glibc 2.33 made stat64/fstat64/lstat64 real functions; 2.26 only has the
 * __xstat64 family (_STAT_VER 3 on ARM). glibc 2.28 added fcntl64; 2.26's
 * fcntl takes the same 64-bit lock commands when _FILE_OFFSET_BITS=64.
 * void * keeps this file free of the new headers' struct definitions. */
extern int __xstat64(int, const char *, void *);
extern int __fxstat64(int, int, void *);
extern int __lxstat64(int, const char *, void *);
extern int fcntl(int, int, ...);
int stat64(const char *p, void *b) { return __xstat64(3, p, b); }
int fstat64(int fd, void *b) { return __fxstat64(3, fd, b); }
int lstat64(const char *p, void *b) { return __lxstat64(3, p, b); }
int fcntl64(int fd, int cmd, ...)
{ va_list a; void *arg; va_start(a, cmd); arg = va_arg(a, void *); va_end(a); return fcntl(fd, cmd, arg); }
