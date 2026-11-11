/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 ArchGX contributors - https://github.com/ArchGX/ArchGX */
/* GPGX: bind libm calls to the symbol versions present in the console's glibc 2.26 */
__asm__(".symver pow,pow@GLIBC_2.4");
__asm__(".symver exp,exp@GLIBC_2.4");
__asm__(".symver powf,powf@GLIBC_2.4");
