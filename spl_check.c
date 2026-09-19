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
    // every graphics file in both modes, plus the backgrounds
    static const char *GFX_FILES[] = { "BLOCKS", "SCENERY", "SPIDS1", "SPIDS2", "CHARSET", "LOADSCRN", "WINSCRN",
                                       "PAGE1", "PAGE2", "PAGE3", "PAGE4", "HISCORE", "SPIDFIGR", "SKELFIGR" };
    static const char *EXTS[] = { "EGA", "CGA" };
    char files[40][32];
    int nfiles = 0;
    snprintf(files[nfiles++], 32, "BACKS.SPL");
    snprintf(files[nfiles++], 32, "MASKS1.CGA");
    snprintf(files[nfiles++], 32, "MASKS2.CGA");
    for (size_t i = 0; i < sizeof GFX_FILES / sizeof GFX_FILES[0]; i++)
        for (int e = 0; e < 2; e++) snprintf(files[nfiles++], 32, "%s.%s", GFX_FILES[i], EXTS[e]);
    for (int i = 0; i < nfiles; i++) {
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
        CHECK(render_gfx_key(ctx, GFX_SPIDS1) == 4, "cga sprite key is the mask colour, got %d", render_gfx_key(ctx, GFX_SPIDS1));
        CHECK(render_gfx_available(ctx, GFX_MASKS1) && render_gfx_available(ctx, GFX_MASKS2), "MASKS1/2.CGA loaded");
        // frame 24 (Spider-Man standing): a corner is masked out, a body pixel is drawn black,
        // painting "transparent" and back moves the mask, and a record carries both frames
        CHECK(render_gfx_pixel(ctx, GFX_SPIDS1, 24, 0, 0) == 4, "cga sprite corner not masked: %d", render_gfx_pixel(ctx, GFX_SPIDS1, 24, 0, 0));
        CHECK(render_gfx_pixel(ctx, GFX_SPIDS1, 24, 16, 8) != 4, "cga sprite body masked out");
        uint8_t rec[GFX_MAX_BYTES], rec2[GFX_MAX_BYTES];
        CHECK(render_gfx_get(ctx, GFX_SPIDS1, 24, rec), "cga sprite record");
        int body = render_gfx_pixel(ctx, GFX_SPIDS1, 24, 16, 8);
        render_gfx_set_pixel(ctx, GFX_SPIDS1, 24, 16, 8, 4);
        CHECK(render_gfx_pixel(ctx, GFX_SPIDS1, 24, 16, 8) == 4, "cga sprite pixel not made transparent");
        render_gfx_set_pixel(ctx, GFX_SPIDS1, 24, 0, 0, 1);
        CHECK(render_gfx_pixel(ctx, GFX_SPIDS1, 24, 0, 0) == 1, "cga sprite corner not painted over the mask: %d", render_gfx_pixel(ctx, GFX_SPIDS1, 24, 0, 0));
        render_gfx_put(ctx, GFX_SPIDS1, 24, rec);
        CHECK(render_gfx_pixel(ctx, GFX_SPIDS1, 24, 16, 8) == body && render_gfx_pixel(ctx, GFX_SPIDS1, 24, 0, 0) == 4, "cga sprite record did not restore the mask");
        CHECK(render_gfx_get(ctx, GFX_SPIDS1, 24, rec2) && memcmp(rec, rec2, 256) == 0, "cga sprite record differs after put");
        render_gfx_set_pixel(ctx, GFX_SPIDS1, 24, 0, 0, 1);
        CHECK(render_gfx_save(ctx, GFX_SPIDS1), "cga sprite save");
        snprintf(p, sizeof p, "%s/MASKS1.CGA.orig", dir);
        CHECK(stat(p, &st) == 0, "MASKS1.CGA.orig not written with the sprite save");
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
    // the font and the full screens, in both modes: sizes, and a pixel round trip
    // through every colour on a row-planar picture and a tall narrow one
    for (int mode = 0; mode < 2; mode++) {
        bool cga = mode == 1;
        ctx = render_open(dir, cga);
        CHECK(ctx != NULL, "render_open(%s) for the screens", cga ? "cga" : "ega");
        if (ctx == NULL) continue;
        CHECK(render_gfx_count(GFX_CHARSET) == 128 && render_gfx_bytes(ctx, GFX_CHARSET) == (cga ? 16 : 32), "charset geometry");
        CHECK(render_gfx_available(ctx, GFX_CHARSET) && render_gfx_available(ctx, GFX_LOADSCRN) && render_gfx_available(ctx, GFX_SPIDFIGR), "screens present");
        int w, h;
        render_gfx_size(GFX_LOADSCRN, &w, &h);
        CHECK(w == 320 && h == 200 && render_gfx_is_screen(GFX_LOADSCRN) && !render_gfx_is_screen(GFX_CHARSET), "screen geometry");
        CHECK(render_gfx_bytes(ctx, GFX_LOADSCRN) == (cga ? 16000 : 32000), "screen bytes %d", render_gfx_bytes(ctx, GFX_LOADSCRN));
        GfxSource pics[] = { GFX_LOADSCRN, GFX_SPIDFIGR, GFX_HISCORE, GFX_CHARSET };
        for (size_t k = 0; k < sizeof pics / sizeof pics[0]; k++) {
            GfxSource s = pics[k];
            int idx = s == GFX_CHARSET ? 0x41 : 0, px = 13, py = 9;
            int left = render_gfx_pixel(ctx, s, idx, px - 1, py), right = render_gfx_pixel(ctx, s, idx, px + 1, py);
            int above = render_gfx_pixel(ctx, s, idx, px, py - 1), was = render_gfx_pixel(ctx, s, idx, px, py);
            if (s == GFX_CHARSET) { px = 3; py = 4; left = render_gfx_pixel(ctx, s, idx, 2, 4); right = render_gfx_pixel(ctx, s, idx, 4, 4); above = render_gfx_pixel(ctx, s, idx, 3, 3); was = render_gfx_pixel(ctx, s, idx, 3, 4); }
            for (int col = 0; col < render_colours(ctx); col++) {
                render_gfx_set_pixel(ctx, s, idx, px, py, col);
                CHECK(render_gfx_pixel(ctx, s, idx, px, py) == col, "%s %s: set %d read %d", cga ? "cga" : "ega", render_gfx_name(ctx, s), col, render_gfx_pixel(ctx, s, idx, px, py));
            }
            CHECK(render_gfx_pixel(ctx, s, idx, px - 1, py) == left && render_gfx_pixel(ctx, s, idx, px + 1, py) == right && render_gfx_pixel(ctx, s, idx, px, py - 1) == above,
                  "%s %s: a neighbour changed", cga ? "cga" : "ega", render_gfx_name(ctx, s));
            render_gfx_set_pixel(ctx, s, idx, px, py, was);
        }
        // a real picture decodes to more than one colour (a layout mistake gives noise or a flat field)
        int hist[16] = {0};
        for (int y = 0; y < 200; y++) for (int x = 0; x < 320; x++) hist[render_gfx_pixel(ctx, GFX_LOADSCRN, 0, x, y)]++;
        int used = 0;
        for (int i = 0; i < 16; i++) used += hist[i] > 0;
        CHECK(used >= 3, "%s LOADSCRN uses only %d colours", cga ? "cga" : "ega", used);
        render_close(ctx);
        printf("%s screens: geometry and pixel round trips OK\n", cga ? "cga" : "ega");
    }
    if (failures == 0) {
        for (int i = 0; i < nfiles; i++) {
            snprintf(p, sizeof p, "%s/%s", dir, files[i]); unlink(p);
            snprintf(p, sizeof p, "%s/%s.orig", dir, files[i]); unlink(p);
        }
        rmdir(dir);
    }
}

// dumpRoom() prints to stdout; capture it into a string by redirecting stdout
// through a tmpfile for the duration of the call.
static char *capture_dump(Room *room, RoomFile *file) {
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    FILE *tmp = tmpfile();
    if (saved_fd < 0 || tmp == NULL) { CHECK(false, "dup/tmpfile for dump capture"); if (saved_fd >= 0) close(saved_fd); return NULL; }
    dup2(fileno(tmp), STDOUT_FILENO);
    dumpRoom(room, file, false);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    long len = ftell(tmp);
    rewind(tmp);
    char *buf = malloc((size_t)len + 1);
    size_t n = buf ? fread(buf, 1, (size_t)len, tmp) : 0;
    if (buf) buf[n] = 0;
    fclose(tmp);
    return buf;
}

// A save is a full decompress -> recompress round trip through the higher-level
// room/switch/chunk parsing (not just the raw LZ byte stream, which
// check_spl_file already covers) - a decode/encode formula that doesn't agree
// with itself (e.g. the on/off and block-size mixups from 2026-09-18) silently
// changes a room's values on every save/reload. Catch that by comparing each
// room's full dumpRoom() text before and after one round trip.
static void check_room_values(const char *rooms_path) {
    const char *tmpdir = getenv("TMPDIR");
    char dir[1024], cwd[1024];
    snprintf(dir, sizeof dir, "%s/spl_check_values.XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(dir) == NULL || getcwd(cwd, sizeof cwd) == NULL) { CHECK(false, "scratch dir for %s values check", rooms_path); return; }
    char copy[1200];
    snprintf(copy, sizeof copy, "%s/%s", dir, ROOMS_FILE);
    if (!copy_file(rooms_path, copy)) { CHECK(false, "cannot copy %s", rooms_path); return; }
    if (chdir(dir) != 0) { CHECK(false, "chdir %s", dir); return; }

    RoomFile before = {0};
    CHECK(readRooms(&before, "."), "%s: readRooms (before) failed", rooms_path);

    char *dumps[C_ARRAY_LEN(before.rooms)] = {0};
    for (size_t r = 0; r < C_ARRAY_LEN(before.rooms); r++) {
        if (before.rooms[r].valid) dumps[r] = capture_dump(&before.rooms[r], &before);
    }
    for (size_t r = 0; r < C_ARRAY_LEN(before.rooms); r++) {
        if (before.rooms[r].valid) ARRAY_FREE(before.rooms[r].compressed);   // force recompression from .data
    }
    CHECK(writeRooms(&before, ".", NULL, NULL, 0), "%s: writeRooms failed", rooms_path);

    RoomFile after = {0};
    CHECK(readRooms(&after, "."), "%s: readRooms (after) failed", rooms_path);
    int rooms = 0;
    for (size_t r = 0; r < C_ARRAY_LEN(before.rooms); r++) {
        if (!before.rooms[r].valid) continue;
        char *dump2 = capture_dump(&after.rooms[r], &after);
        CHECK(dumps[r] && dump2 && strcmp(dumps[r], dump2) == 0,
              "%s room %u: values changed after a decompress/recompress round trip", rooms_path, before.rooms[r].index);
        free(dump2);
        free(dumps[r]);
        rooms++;
    }

    freeRoomFile(&before);
    freeRoomFile(&after);
    if (chdir(cwd) != 0) CHECK(false, "chdir back");
    if (failures == 0) { unlink(copy); rmdir(dir); }
    printf("%s: %d rooms' values stable across a recompress round trip\n", rooms_path, rooms);
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
    CHECK(readRooms(&file, "."), "readRooms on the copy failed");
    srand(4242);
    for (size_t r = 0; r < 64; r++) {
        Room *room = &file.rooms[r];
        if (!room->valid) { memset(&room->data, 0, sizeof room->data); memset(room->data.name, ' ', sizeof room->data.name); room->index = (uint8_t)r; room->valid = true; }
        for (size_t i = 0; i < sizeof room->data.tiles; i++) room->data.tiles[i] = (uint8_t)(rand() % 256);
        ARRAY_FREE(room->compressed);
    }
    CHECK(!writeRooms(&file, ".", NULL, NULL, 0), "an oversized ROOMS.SPL was written");
    size_t after_len = 0;
    uint8_t *after = read_whole(copy, &after_len);
    CHECK(before && after && before_len == after_len && memcmp(before, after, before_len) == 0,
          "ROOMS.SPL changed although the write was refused");
    // and a file that fits still writes
    for (size_t r = 0; r < 64; r++) { memset(file.rooms[r].data.tiles, 0, sizeof file.rooms[r].data.tiles); ARRAY_FREE(file.rooms[r].compressed); }
    CHECK(writeRooms(&file, ".", NULL, NULL, 0), "a small ROOMS.SPL was refused");
    free(before); free(after);
    freeRoomFile(&file);
    if (chdir(cwd) != 0) CHECK(false, "chdir back");
    if (failures == 0) { unlink(copy); snprintf(copy, sizeof copy, "%s/%s.tmp", dir, ROOMS_FILE); unlink(copy); rmdir(dir); }
    printf("rooms file: oversized write refused, file untouched\n");
}

// readRooms() / writeRooms() take the data directory explicitly; nothing about them may
// depend on the working directory (`./a.out gui --data-dir original` used to read the
// cwd's ROOMS.SPL and only take its graphics from original/). Two scratch directories: A
// becomes the cwd and holds a sentinel ROOMS.SPL, B holds a real copy of `rooms_path`.
// Reading and writing through B must change B's file and leave A's sentinel alone, and
// data_path() must join a directory and a file name whether or not the directory ends in
// a slash.
static void check_data_dir(const char *rooms_path) {
    const char *tmpdir = getenv("TMPDIR");
    char a[1024], b[1024], cwd[1024];
    snprintf(a, sizeof a, "%s/spl_check_cwd.XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    snprintf(b, sizeof b, "%s/spl_check_data.XXXXXX", tmpdir && *tmpdir ? tmpdir : "/tmp");
    if (mkdtemp(a) == NULL || mkdtemp(b) == NULL || getcwd(cwd, sizeof cwd) == NULL) { CHECK(false, "scratch dirs for the data-dir check"); return; }
    CHECK(strcmp(data_path(".", ROOMS_FILE), "./ROOMS.SPL") == 0, "data_path(\".\") gave %s", data_path(".", ROOMS_FILE));
    CHECK(strcmp(data_path("original/", ROOMS_FILE), "original/ROOMS.SPL") == 0, "data_path with a trailing slash gave %s", data_path("original/", ROOMS_FILE));
    CHECK(strcmp(data_path("original", ROOMS_FILE), "original/ROOMS.SPL") == 0, "data_path gave %s", data_path("original", ROOMS_FILE));

    static const char sentinel[] = "this is the working directory's ROOMS.SPL and must not be touched";
    char a_rooms[1200], b_rooms[1200];
    snprintf(a_rooms, sizeof a_rooms, "%s/%s", a, ROOMS_FILE);
    snprintf(b_rooms, sizeof b_rooms, "%s/%s", b, ROOMS_FILE);
    FILE *f = fopen(a_rooms, "wb");
    if (f == NULL || fwrite(sentinel, 1, sizeof sentinel, f) != sizeof sentinel || fclose(f) != 0) { CHECK(false, "cannot write the sentinel"); return; }
    if (!copy_file(rooms_path, b_rooms)) { CHECK(false, "cannot copy %s", rooms_path); return; }
    size_t orig_len = 0;
    uint8_t *orig = read_whole(b_rooms, &orig_len);
    if (chdir(a) != 0) { CHECK(false, "chdir %s", a); free(orig); return; }

    RoomFile file = {0};
    CHECK(readRooms(&file, b), "readRooms through the data dir failed");
    int valid = 0;
    for (size_t r = 0; r < 64; r++) {
        if (!file.rooms[r].valid) continue;
        valid++;
        memset(file.rooms[r].data.tiles, 0, sizeof file.rooms[r].data.tiles);
        ARRAY_FREE(file.rooms[r].compressed);
    }
    CHECK(valid > 0, "the copy has no rooms");
    CHECK(writeRooms(&file, b, NULL, NULL, 0), "writeRooms through the data dir failed");

    size_t a_len = 0, b_len = 0;
    uint8_t *a_after = read_whole(a_rooms, &a_len);
    uint8_t *b_after = read_whole(b_rooms, &b_len);
    CHECK(a_after && a_len == sizeof sentinel && memcmp(a_after, sentinel, a_len) == 0,
          "the working directory's ROOMS.SPL was touched by a data-dir write");
    CHECK(b_after && orig && !(b_len == orig_len && memcmp(b_after, orig, b_len) == 0),
          "the data dir's ROOMS.SPL did not change after a write");
    RoomFile again = {0};
    CHECK(readRooms(&again, b), "readRooms after the data-dir write failed");
    int valid_again = 0;
    for (size_t r = 0; r < 64; r++) if (again.rooms[r].valid) valid_again++;
    CHECK(valid_again == valid, "room count changed across the data-dir round trip (%d -> %d)", valid, valid_again);
    RoomFile wrong = {0};
    CHECK(!readRooms(&wrong, a), "readRooms accepted the sentinel as a rooms file");
    freeRoomFile(&wrong);

    free(orig); free(a_after); free(b_after);
    freeRoomFile(&file);
    freeRoomFile(&again);
    if (chdir(cwd) != 0) CHECK(false, "chdir back");
    if (failures == 0) {
        unlink(a_rooms); rmdir(a);
        unlink(b_rooms); snprintf(b_rooms, sizeof b_rooms, "%s/%s.tmp", b, ROOMS_FILE); unlink(b_rooms); rmdir(b);
    }
    printf("data dir: ROOMS.SPL read and written where --data-dir points, cwd untouched\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s DATA_DIR [SPL_FILE...]\n", argv[0]); return 2; }
    check_random();
    for (int i = 2; i < argc; i++) check_spl_file(argv[i]);
    check_backs_store(argv[1]);
    if (argc > 2) check_room_values(argv[2]);
    if (argc > 2) check_rooms_limit(argv[2]);
    if (argc > 2) check_data_dir(argv[2]);
    if (failures) { fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    printf("all checks passed\n");
    return 0;
}
