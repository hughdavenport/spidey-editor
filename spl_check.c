// Round-trip checks for the .SPL compressor (spl_compress / spl_decompress)
// and the BACKS.SPL background store in render.c.
//   ./spl_check DATA_DIR SPL_FILE...
// DATA_DIR holds the game files (BACKS.SPL, *.EGA) and is copied to a scratch
// directory for the save test; every SPL_FILE's blocks are decompressed,
// recompressed and decompressed again. Exit status 0 = all checks passed.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "render.h"
#include "spl.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

// spl_decompress reads a FILE; feed it the compressed bytes through fmemopen.
static bool roundtrip(const uint8_t *data, size_t n, uint8_t marker0, const char *what) {
    static uint8_t comp[4 * 1024];
    size_t clen = spl_compress(data, n, marker0, comp, sizeof comp);
    CHECK(clen >= 4, "%s: compress failed", what);
    if (clen < 4) return false;
    FILE *fp = fmemopen(comp + 4, clen - 4, "rb");
    uint8_array out = {0};
    bool ok = spl_decompress(fp, clen - 4, comp, &out, NULL);
    fclose(fp);
    CHECK(ok, "%s: decompress of recompressed stream failed", what);
    CHECK(out.length == n, "%s: length %zu after round trip, expected %zu", what, out.length, n);
    bool same = ok && out.length == n && memcmp(out.data, data, n) == 0;
    CHECK(same, "%s: bytes differ after round trip", what);
    ARRAY_FREE(out);
    return same;
}

// Every block of an .SPL file (64 big-endian offsets, file size, blocks).
static void check_spl_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) { CHECK(false, "cannot open %s", path); return; }
    uint8_t hdr[0x82];
    if (fread(hdr, 1, sizeof hdr, fp) != sizeof hdr) { CHECK(false, "%s: short header", path); fclose(fp); return; }
    int blocks = 0;
    for (int b = 0; b < 64; b++) {
        int start = (hdr[b * 2] << 8) | hdr[b * 2 + 1];
        int end = b < 63 ? ((hdr[b * 2 + 2] << 8) | hdr[b * 2 + 3]) : ((hdr[0x80] << 8) | hdr[0x81]);
        if (end <= start + 4) continue;
        uint8_t markers[4];
        if (fseek(fp, start, SEEK_SET) != 0 || fread(markers, 1, 4, fp) != 4) { CHECK(false, "%s: block %d unreadable", path, b); continue; }
        uint8_array dec = {0};
        bool ok = spl_decompress(fp, (size_t)(end - start - 4), markers, &dec, NULL);
        CHECK(ok, "%s: block %d does not decompress", path, b);
        if (ok) {
            char what[256];
            snprintf(what, sizeof what, "%s block %d", path, b);
            roundtrip(dec.data, dec.length, markers[0], what);
            blocks++;
        }
        ARRAY_FREE(dec);
    }
    fclose(fp);
    printf("%s: %d blocks round-tripped\n", path, blocks);
}

// Random data with runs and repeats (what the format is built for), and
// data that contains every byte value (so the marker choice is exercised).
static void check_random(void) {
    srand(12345);
    static uint8_t buf[960];
    for (int t = 0; t < 300; t++) {
        size_t n = 1 + (size_t)rand() % sizeof buf;
        int alphabet = 1 + rand() % 256;
        for (size_t i = 0; i < n; i++) {
            int r = rand() % 10;
            if (r < 4 && i > 0) buf[i] = buf[i - 1];                       // run
            else if (r < 6 && i >= 32) buf[i] = buf[i - 32];                // the 0x20-back copy
            else if (r < 7 && i >= 300) buf[i] = buf[i - 300 + rand() % 40]; // far LZ
            else buf[i] = (uint8_t)(rand() % alphabet);
        }
        char what[64];
        snprintf(what, sizeof what, "random #%d (n=%zu)", t, n);
        roundtrip(buf, n, (uint8_t)(rand() % 256), what);
    }
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;                      // all values present
    roundtrip(buf, 256, 0x8f, "all byte values");
    roundtrip(buf, 256, 0x00, "all byte values, marker 0");
    printf("random buffers round-tripped\n");
}

static uint8_t *read_whole(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    *len = buf && n > 0 ? fread(buf, 1, (size_t)n, f) : 0;
    fclose(f);
    return buf;
}

static bool copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
    bool ok = in && out;
    if (ok) {
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) ok &= fwrite(buf, 1, n, out) == n;
    }
    if (in) fclose(in);
    if (out) fclose(out);
    return ok;
}

// Edit one background tile through the render API, save, reopen: the edit is
// there, every other block is untouched, and BACKS.SPL.orig is the original.
static void check_backs_store(const char *data_dir) {
    const char *tmpdir = getenv("TMPDIR");
    char dir[1024];
    snprintf(dir, sizeof dir, "%s/spl_check.XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(dir) == NULL) { CHECK(false, "mkdtemp failed"); return; }
    const char *files[] = { "BACKS.SPL", "BLOCKS.EGA", "SCENERY.EGA", "SPIDS1.EGA", "SPIDS2.EGA",
                            "BLOCKS.CGA", "SCENERY.CGA", "SPIDS1.CGA", "SPIDS2.CGA" };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        char from[1200], to[1200];
        snprintf(from, sizeof from, "%s/%s", data_dir, files[i]);
        snprintf(to, sizeof to, "%s/%s", dir, files[i]);
        if (!copy_file(from, to)) { CHECK(false, "cannot copy %s", from); return; }
    }
    RenderCtx *ctx = render_open(dir, false);
    CHECK(ctx != NULL, "render_open(%s) failed", dir);
    if (ctx == NULL) return;

    static uint8_t before[BACKS_BLOCKS][BACKS_TILES];
    for (int b = 0; b < BACKS_BLOCKS; b++) render_backs_get(ctx, b, before[b]);
    int bid = 8, x = 5, y = 7;                     // the basement background, some cell
    int was = render_backs_tile(ctx, bid, x, y), want = was == 0x21 ? 0x22 : 0x21;
    render_backs_set_tile(ctx, bid, x, y, want);
    CHECK(render_backs_tile(ctx, bid, x, y) == want, "set_tile did not take");
    CHECK(render_backs_save(ctx), "render_backs_save failed");
    render_close(ctx);

    ctx = render_open(dir, false);
    CHECK(ctx != NULL, "reopen failed");
    if (ctx == NULL) return;
    CHECK(render_backs_tile(ctx, bid, x, y) == want, "edited tile not read back: %d", render_backs_tile(ctx, bid, x, y));
    for (int b = 0; b < BACKS_BLOCKS; b++) {
        uint8_t after[BACKS_TILES];
        render_backs_get(ctx, b, after);
        if (b == bid) { after[y * WIDTH_TILES + x] = (uint8_t)was; }
        CHECK(memcmp(after, before[b], BACKS_TILES) == 0, "block %d changed by save/reload", b);
    }
    uint8_t orig[BACKS_TILES];
    CHECK(render_backs_original(ctx, bid, orig), "render_backs_original failed");
    CHECK(orig[y * WIDTH_TILES + x] == was, "original tile lost: %d", orig[y * WIDTH_TILES + x]);
    // scenery tiles are a pixel-editable source
    CHECK(render_gfx_count(GFX_SCENERY) == 256, "scenery count");
    int c = render_gfx_pixel(ctx, GFX_SCENERY, 1, 0, 0);
    render_gfx_set_pixel(ctx, GFX_SCENERY, 1, 0, 0, (c + 1) % 16);
    CHECK(render_gfx_pixel(ctx, GFX_SCENERY, 1, 0, 0) == (c + 1) % 16, "scenery set_pixel");
    CHECK(render_gfx_save(ctx, GFX_SCENERY), "scenery save");
    render_close(ctx);

    char p[1200];
    struct stat st;
    snprintf(p, sizeof p, "%s/BACKS.SPL.orig", dir);
    CHECK(stat(p, &st) == 0, "BACKS.SPL.orig not written");
    snprintf(p, sizeof p, "%s/SCENERY.EGA.orig", dir);
    CHECK(stat(p, &st) == 0, "SCENERY.EGA.orig not written");
    printf("background store: edit, save, reload OK (%s)\n", dir);

    // Over the limit: every block full of incompressible tiles must be
    // refused, and the file on disk left exactly as it was.
    ctx = render_open(dir, false);
    CHECK(ctx != NULL, "reopen for the limit check failed");
    if (ctx != NULL) {
        snprintf(p, sizeof p, "%s/BACKS.SPL", dir);
        size_t before_len = 0;
        uint8_t *before_bytes = read_whole(p, &before_len);
        srand(777);
        for (int b = 0; b < BACKS_BLOCKS; b++) {
            uint8_t block[BACKS_TILES];
            for (int i = 0; i < BACKS_TILES; i++) block[i] = (uint8_t)(rand() % 256);
            render_backs_put(ctx, b, block);
        }
        CHECK(!render_backs_save(ctx), "an oversized BACKS.SPL was written");
        size_t after_len = 0;
        uint8_t *after_bytes = read_whole(p, &after_len);
        CHECK(before_bytes && after_bytes && before_len == after_len && memcmp(before_bytes, after_bytes, before_len) == 0,
              "BACKS.SPL changed although the save was refused");
        free(before_bytes); free(after_bytes);
        render_close(ctx);
        printf("background store: oversized save refused, file untouched\n");
    }
    // scratch directory is left for inspection when something failed
    // CGA mode: the packed 2-bpp files decode, edit and save like the EGA ones
    ctx = render_open(dir, true);
    CHECK(ctx != NULL, "render_open(cga) failed");
    if (ctx != NULL) {
        CHECK(render_is_cga(ctx) && render_colours(ctx) == 4, "cga mode flags");
        CHECK(render_gfx_bytes(ctx, GFX_TILES) == 16 && render_gfx_bytes(ctx, GFX_SPIDS1) == 128, "cga record sizes");
        CHECK(render_gfx_key(ctx, GFX_SPIDS1) == 3, "cga sprite key");
        CHECK(strcmp(render_gfx_name(ctx, GFX_SCENERY), "SCENERY.CGA") == 0, "cga file name: %s", render_gfx_name(ctx, GFX_SCENERY));
        // a pixel of tile 0x12: set to each colour and read back, the neighbours untouched
        int t = 0x12;
        int before_l = render_gfx_pixel(ctx, GFX_TILES, t, 2, 3), before_r = render_gfx_pixel(ctx, GFX_TILES, t, 4, 3);
        for (int col = 0; col < 4; col++) {
            render_gfx_set_pixel(ctx, GFX_TILES, t, 3, 3, col);
            CHECK(render_gfx_pixel(ctx, GFX_TILES, t, 3, 3) == col, "cga set_pixel %d read back %d", col, render_gfx_pixel(ctx, GFX_TILES, t, 3, 3));
        }
        CHECK(render_gfx_pixel(ctx, GFX_TILES, t, 2, 3) == before_l && render_gfx_pixel(ctx, GFX_TILES, t, 4, 3) == before_r, "cga set_pixel disturbed a neighbour");
        render_gfx_set_pixel(ctx, GFX_TILES, t, 3, 3, 5);                  // out of range: ignored
        CHECK(render_gfx_pixel(ctx, GFX_TILES, t, 3, 3) == 3, "cga colour range not enforced");
        // a CGA sprite frame: the key colour is white (3) and frame 0 has some drawn pixels
        int nonkey = 0;
        for (int y = 0; y < 21; y++) for (int x = 0; x < 24; x++) nonkey += render_gfx_pixel(ctx, GFX_SPIDS1, 0, x, y) != 3;
        CHECK(nonkey > 50, "cga sprite frame 0 decodes to only %d drawn pixels", nonkey);
        CHECK(render_gfx_save(ctx, GFX_TILES), "cga save");
        render_close(ctx);
        snprintf(p, sizeof p, "%s/BLOCKS.CGA.orig", dir);
        CHECK(stat(p, &st) == 0, "BLOCKS.CGA.orig not written");
        printf("cga mode: decode, edit, save OK\n");
    }
    if (failures == 0) {
        const char *all[] = { "BACKS.SPL", "BACKS.SPL.orig", "BLOCKS.EGA", "SCENERY.EGA", "SCENERY.EGA.orig", "SPIDS1.EGA", "SPIDS2.EGA",
                              "BLOCKS.CGA", "BLOCKS.CGA.orig", "SCENERY.CGA", "SPIDS1.CGA", "SPIDS2.CGA" };
        for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) { snprintf(p, sizeof p, "%s/%s", dir, all[i]); unlink(p); }
        rmdir(dir);
    }
}

// writeRooms() must refuse a file over the game's limit and leave ROOMS.SPL
// as it was: a scratch copy of `rooms_path` gets 64 rooms of random tiles.
static void check_rooms_limit(const char *rooms_path) {
    const char *tmpdir = getenv("TMPDIR");
    char dir[1024], cwd[1024];
    snprintf(dir, sizeof dir, "%s/spl_check_rooms.XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(dir) == NULL || getcwd(cwd, sizeof cwd) == NULL) { CHECK(false, "scratch dir for the rooms check"); return; }
    char copy[1200];
    snprintf(copy, sizeof copy, "%s/%s", dir, ROOMS_FILE);
    if (!copy_file(rooms_path, copy)) { CHECK(false, "cannot copy %s", rooms_path); return; }
    size_t before_len = 0;
    uint8_t *before = read_whole(copy, &before_len);
    if (chdir(dir) != 0) { CHECK(false, "chdir %s", dir); free(before); return; }
    RoomFile file = {0};
    CHECK(readRooms(&file), "readRooms on the copy failed");
    srand(4242);
    for (size_t r = 0; r < 64; r++) {
        Room *room = &file.rooms[r];
        if (!room->valid) { memset(&room->data, 0, sizeof room->data); memset(room->data.name, ' ', sizeof room->data.name); room->index = (uint8_t)r; room->valid = true; }
        for (size_t i = 0; i < sizeof room->data.tiles; i++) room->data.tiles[i] = (uint8_t)(rand() % 256);
        ARRAY_FREE(room->compressed);
    }
    CHECK(!writeRooms(&file), "an oversized ROOMS.SPL was written");
    size_t after_len = 0;
    uint8_t *after = read_whole(copy, &after_len);
    CHECK(before && after && before_len == after_len && memcmp(before, after, before_len) == 0,
          "ROOMS.SPL changed although the write was refused");
    // and a file that fits still writes
    for (size_t r = 0; r < 64; r++) { memset(file.rooms[r].data.tiles, 0, sizeof file.rooms[r].data.tiles); ARRAY_FREE(file.rooms[r].compressed); }
    CHECK(writeRooms(&file), "a small ROOMS.SPL was refused");
    free(before); free(after);
    freeRoomFile(&file);
    if (chdir(cwd) != 0) CHECK(false, "chdir back");
    if (failures == 0) { unlink(copy); snprintf(copy, sizeof copy, "%s/%s.tmp", dir, ROOMS_FILE); unlink(copy); rmdir(dir); }
    printf("rooms file: oversized write refused, file untouched\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s DATA_DIR [SPL_FILE...]\n", argv[0]); return 2; }
    check_random();
    for (int i = 2; i < argc; i++) check_spl_file(argv[i]);
    check_backs_store(argv[1]);
    if (argc > 2) check_rooms_limit(argv[2]);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}
