/* Host test of the update step-one core against a simulated installer serving
 * the real package (build/fm1bridge.fwsc) and a simulated 1 MB flash. */
#include "../app/update/fm1_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- simulated host ---- */
static uint8_t *image; static size_t image_len;        /* logical image */
static uint8_t pending[UPD_WIRE_MAX]; static uint32_t pending_len;
static char identity[32];
static int refuse_success, requests;
static uint32_t clock_ms;

static void host_queue(const uint8_t *p, uint32_t n) { memcpy(pending, p, n); pending_len = n; }

static void host_response(uint32_t addr, const uint8_t *data, uint32_t n, uint8_t fl)
{
    uint8_t body[15 + 512], wire[UPD_WIRE_MAX];
    uint32_t i, s = 0, w;
    body[0] = 0; body[1] = 0x59; body[2] = 0x30;
    body[3] = (uint8_t)(n + 8); body[4] = (uint8_t)((n + 8) >> 8); body[5] = 0; body[6] = fl;
    body[7] = (uint8_t)addr; body[8] = (uint8_t)(addr >> 8); body[9] = (uint8_t)(addr >> 16); body[10] = (uint8_t)(addr >> 24);
    body[11] = (uint8_t)n; body[12] = (uint8_t)(n >> 8); body[13] = 0;
    memcpy(body + 14, data, n);
    for (i = 6; i < 14 + n; i++) s += body[i];
    body[14 + n] = (uint8_t)~s;
    wire[0] = 0xF0;
    w = 1 + upd_pack7(body, 15 + n, wire + 1);
    wire[w++] = 0xF7;
    host_queue(wire, w);
}

int upd_port_send(const uint8_t *wire, uint32_t n)       /* device -> host */
{
    uint8_t d[64];
    uint32_t len = upd_unpack7(wire + 1, n - 2, d, sizeof d);
    if (len == 34 && d[2] == 0x11) { memcpy(identity, d + 6, 27); identity[27] = 0; return 0; }
    if (len == 15 && d[2] == 0x30) {
        uint32_t addr = d[7] | d[8] << 8 | d[9] << 16 | (uint32_t)d[10] << 24;
        uint32_t rl = d[11] | d[12] << 8 | d[13] << 16;
        requests++;
        if (addr == 0xE0000000u || addr == 0xF0000000u) {
            host_response(addr, (const uint8_t *)(refuse_success ? "failure" : "success"), 8, 0);
            return 0;
        }
        if (rl > 512 || addr + rl > image_len) { printf("bad request %x+%u\n", addr, rl); fails++; return 0; }
        host_response(addr, image + addr, rl, d[6]);
        return 0;
    }
    printf("unexpected device message len %u\n", len); fails++;
    return 0;
}
int upd_port_frame_get(const uint8_t **p, uint32_t *n) { if (!pending_len) return 0; *p = pending; *n = pending_len; return 1; }
void upd_port_frame_done(void) { pending_len = 0; }
uint32_t upd_port_now_ms(void) { return clock_ms; }
void upd_port_idle(void) { clock_ms += 10; }

/* ---- simulated flash ---- */
static uint8_t flash[0x100000];
static int erases;
int upd_port_flash_erase(uint32_t off) { if (off & 0xFFF) return -1; memset(flash + off, 0xFF, 0x1000); erases++; return 0; }
int upd_port_flash_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (flash[off + i] != 0xFF) { printf("prog over non-erased %x\n", off + i); return -1; }
        flash[off + i] = ((const uint8_t *)p)[i];
    }
    return 0;
}
int upd_port_flash_read(uint32_t off, void *p, uint32_t n) { memcpy(p, flash + off, n); return 0; }
void upd_port_show(uint32_t step, int32_t code) { printf("  step %u code %d\n", step, code); }
static int committed; static uint8_t committed_parm[UPD_PARM_LEN];
void upd_port_commit(const uint8_t *parm) { committed = 1; memcpy(committed_parm, parm, UPD_PARM_LEN); }
static int ubooted;
void upd_port_uboot(void) { ubooted = 1; }

/* ---- package ---- */
static int load_package(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw; long n; size_t i, o = 0;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    raw = malloc((size_t)n); if (fread(raw, 1, (size_t)n, f) != (size_t)n) return -1;
    fclose(f);
    image = malloc((size_t)n);
    for (i = 0; i < 20; i++) { memcpy(image + o, raw + i * 48, 47); o += 47; }   /* drop the 20 marker bytes */
    memcpy(image + o, raw + 960, (size_t)n - 960); o += (size_t)n - 960;
    image_len = o;
    free(raw);
    return 0;
}

static void jl_enc(uint8_t *p, uint32_t n)
{
    uint32_t k = 0xFFFF;
    while (n--) { *p++ ^= (uint8_t)k; k = ((k << 1) ^ ((k & 0x8000) ? 0x1021 : 0)) & 0xFFFF; }
}

/* where ota.bin's body is in the image, independently of the core */
static int find_loader(uint32_t *off, uint32_t *len)
{
    uint8_t hdr[0x400]; uint32_t i, nent;
    memcpy(hdr, image, sizeof hdr);
    jl_enc(hdr, 0x40);
    nent = hdr[8] | hdr[9] << 8;
    for (i = 0; i < nent; i++) {
        uint8_t *e = hdr + 0x40 + i * 0x50;
        jl_enc(e, 0x50);
        if ((e[0] | e[1] << 8) == 100) {
            uint32_t o = e[8] | e[9] << 8 | e[10] << 16 | (uint32_t)e[11] << 24;
            *off = o + 0x20;
            *len = image[o + 8] | image[o + 9] << 8 | image[o + 10] << 16 | (uint32_t)image[o + 11] << 24;
            return 0;
        }
    }
    return -1;
}

static const uint8_t HS_QUERY[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
static const uint8_t UPGRADE[] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
static const uint8_t SOFTKEY[] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};

int main(int argc, char **argv)
{
    uint32_t lo, ll, i;
    const char *pkg = argc > 1 ? argv[1] : "../build/fm1bridge.fwsc";
    if (load_package(pkg)) { printf("no package %s: skipping (build and package first)\n", pkg); return 0; }
    CHECK(find_loader(&lo, &ll) == 0);
    printf("package %s: image %zu bytes, loader body at %#x, %u bytes\n", pkg, image_len, lo, ll);
    memset(flash, 0xFF, sizeof flash);

    /* handshake */
    host_queue(HS_QUERY, sizeof HS_QUERY);
    CHECK(fm1_update_poll() == 1);
    CHECK(strcmp(identity, UPD_IDENTITY) == 0);
    CHECK(fm1_update_poll() == 0);

    /* soft key */
    host_queue(SOFTKEY, sizeof SOFTKEY);
    CHECK(fm1_update_poll() == 1 && ubooted == 1);

    /* refused session: nothing committed, staging erased */
    refuse_success = 1;
    host_queue(UPGRADE, sizeof UPGRADE);
    CHECK(fm1_update_poll() == 1);
    CHECK(!committed);
    for (i = 0; i < UPD_OTA_AREA_LEN; i++) if (flash[UPD_OTA_AREA + i] != 0xFF) { CHECK(!"staging not erased"); break; }

    /* real session */
    refuse_success = 0; requests = 0;
    host_queue(UPGRADE, sizeof UPGRADE);
    CHECK(fm1_update_poll() == 1);
    CHECK(committed == 1);
    CHECK(memcmp(flash + UPD_OTA_AREA + 32, image + lo, ll) == 0);
    CHECK(memcmp(flash + UPD_OTA_AREA + 16, "LOADER.BIN\0\0\0\0\0\0", 16) == 0);
    CHECK((uint32_t)(flash[UPD_OTA_AREA] | flash[UPD_OTA_AREA + 1] << 8) == upd_crc16(flash + UPD_OTA_AREA + 2, 30, 0));
    CHECK((uint32_t)(flash[UPD_OTA_AREA + 8] | flash[UPD_OTA_AREA + 9] << 8 | flash[UPD_OTA_AREA + 10] << 16) == ll + 32);
    CHECK(memcmp(flash + UPD_OTA_RES, committed_parm, UPD_PARM_LEN) == 0);
    CHECK((uint32_t)(committed_parm[0] | committed_parm[1] << 8) == upd_crc16(committed_parm + 2, 78, 0));
    CHECK(committed_parm[2] == 0x0D && committed_parm[3] == 0x5A && committed_parm[6] == 0x41 && committed_parm[7] == 0x54);
    CHECK(memcmp(committed_parm + 8, "ota-FM-1_015", 12) == 0);
    CHECK((committed_parm[72] | committed_parm[73] << 8 | committed_parm[74] << 16 | (uint32_t)committed_parm[75] << 24) == UPD_OTA_AREA);
    printf("  %d read requests served\n", requests);

    /* boot cleanup erases the leftovers */
    fm1_update_boot_cleanup();
    for (i = 0; i < UPD_OTA_AREA_LEN; i++) if (flash[UPD_OTA_AREA + i] != 0xFF) { CHECK(!"cleanup did not erase"); break; }

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("update tests passed\n");
    return 0;
}
