/* SPDX-License-Identifier: MIT */
/* Stand-alone driver for the fuzz harnesses when libFuzzer is not available:
 * mutates the harness's seeds and random inputs under ASan/UBSan for a number of
 * iterations. Same entry point as libFuzzer, so the harnesses work with both.
 *   ./fuzz_x [iterations] [corpus_dir]   (default 200000; corpus files are extra seeds) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n);
const uint8_t *fuzz_seed(int i, size_t *n);            /* harness: NULL when i is past the last seed */

#define MAX_IN 2048
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 11); }

static uint8_t *seeds[256]; static size_t seed_len[256]; static int nseeds;

static void add_seed(const uint8_t *p, size_t n)
{
    if (nseeds >= 256 || n > MAX_IN) return;
    seeds[nseeds] = malloc(n ? n : 1); memcpy(seeds[nseeds], p, n); seed_len[nseeds] = n; nseeds++;
}

static void load_dir(const char *dir)
{
    DIR *d = opendir(dir); struct dirent *e; char path[1024]; static uint8_t buf[MAX_IN];
    if (!d) return;
    while ((e = readdir(d))) {
        FILE *f; size_t n;
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        f = fopen(path, "rb"); if (!f) continue;
        n = fread(buf, 1, sizeof buf, f); fclose(f);
        add_seed(buf, n);
    }
    closedir(d);
}

static size_t mutate(uint8_t *b, size_t n)
{
    int k = 1 + (int)(rnd() % 4);
    while (k--) {
        uint32_t op = rnd() % 8;
        if (n == 0 && (op < 5 || op == 7)) op = 5;
        switch (op) {
        case 0: b[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8)); break;                       /* bit flip */
        case 1: b[rnd() % n] = (uint8_t)rnd(); break;                                      /* byte set */
        case 2: { static const uint8_t sp[] = {0, 1, 0x7F, 0x80, 0xF0, 0xF7, 0xFF, 0x59, 0x7D, 0x30, 0x11}; b[rnd() % n] = sp[rnd() % sizeof sp]; break; }
        case 3: { size_t i = rnd() % n; memmove(b + i, b + i + 1, n - i - 1); n--; break; } /* delete */
        case 4: { size_t i = rnd() % n, j = rnd() % n; uint8_t t = b[i]; b[i] = b[j]; b[j] = t; break; }
        case 5: if (n < MAX_IN) { size_t i = n ? rnd() % n : 0; memmove(b + i + 1, b + i, n - i); b[i] = (uint8_t)rnd(); n++; } break;
        case 6: if (nseeds && n < MAX_IN) {                                                /* splice a seed piece in */
                    int s = (int)(rnd() % (uint32_t)nseeds); size_t sl = seed_len[s];
                    if (sl) { size_t from = rnd() % sl, len = rnd() % (sl - from) + 1, at = n ? rnd() % n : 0;
                              if (len > MAX_IN - n) len = MAX_IN - n;
                              memmove(b + at + len, b + at, n - at); memcpy(b + at, seeds[s] + from, len); n += len; }
                }
                break;
        default: { size_t i = rnd() % n; uint32_t r = rnd(); b[i] = (uint8_t)(b[i] + (r & 1 ? 1 : -1)); break; }
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 200000, it;
    static uint8_t buf[MAX_IN + 64];
    const uint8_t *s; size_t n; int i;
    for (i = 0; (s = fuzz_seed(i, &n)) != NULL; i++) add_seed(s, n);
    if (argc > 2) load_dir(argv[2]);
    for (i = 0; i < nseeds; i++) LLVMFuzzerTestOneInput(seeds[i], seed_len[i]);
    for (it = 0; it < iters; it++) {
        if (nseeds && (rnd() % 4)) { int k = (int)(rnd() % (uint32_t)nseeds); n = seed_len[k]; memcpy(buf, seeds[k], n); }
        else { n = rnd() % 64; for (size_t j = 0; j < n; j++) buf[j] = (uint8_t)rnd(); }
        n = mutate(buf, n);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("%s: %ld iterations, %d seeds, no crash\n", argv[0], iters, nseeds);
    return 0;
}
