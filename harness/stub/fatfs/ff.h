// harness: FatFs over stdio, for common/hiscore.cpp
#pragma once
#include <stdio.h>
typedef unsigned UINT;
typedef struct { FILE *fp; } FIL;
enum { FR_OK = 0, FR_ERR = 1 };
#define FA_READ			1
#define FA_WRITE		2
#define FA_CREATE_ALWAYS	8
static inline int f_open (FIL *f, const char *p, int m) { f->fp = fopen (p, (m & FA_WRITE) ? "wb" : "rb"); return f->fp ? FR_OK : FR_ERR; }
static inline int f_read (FIL *f, void *b, UINT n, UINT *r) { *r = (UINT) fread (b, 1, n, f->fp); return FR_OK; }
static inline int f_write (FIL *f, const void *b, UINT n, UINT *w) { *w = (UINT) fwrite (b, 1, n, f->fp); return FR_OK; }
static inline int f_close (FIL *f) { fclose (f->fp); return FR_OK; }
static inline int f_unlink (const char *p) { return remove (p) == 0 ? FR_OK : FR_ERR; }
