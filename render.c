#include "render.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "array.h"
#include "spl.h"

// Game palette (16 RGB triplets) read from the unpacked SPIDEY.EXE DAC table
// at DS:0x97AE; see Spider-claude/game_palette.py.
static const uint8_t PAL[16][3] = {
    {0, 0, 0}, {0, 97, 194}, {227, 65, 0}, {227, 227, 227},
    {97, 97, 97}, {0, 65, 162}, {162, 32, 0}, {162, 162, 162},
    {97, 227, 227}, {32, 194, 65}, {194, 65, 227}, {227, 130, 0},
    {0, 162, 227}, {0, 97, 32}, {97, 32, 130}, {227, 194, 0},
};

// CGA mode 4, palette 1, high intensity: what the .CGA files are drawn with.
static const uint8_t CGA_PAL[4][3] = { {0, 0, 0}, {85, 255, 255}, {255, 85, 255}, {255, 255, 255} };

#define BW (WIDTH_TILES * 8)    // 256
#define BH (HEIGHT_TILES * 8)   // 176
#define SPIDS_W 24
#define SPIDS_H 21
#define SPIDS_STRIDE 256        // EGA: 4 planes x 63 bytes + 4 pad
#define CGA_SPIDS_STRIDE 128    // CGA: 21 rows x 6 bytes + 2 pad
#define EGA_TILE_BYTES 32
#define CGA_TILE_BYTES 16
#define SPRITE_KEY 15           // SPIDS transparency colour (EGA)
#define CGA_SPRITE_KEY 3        // ... and in the CGA files (white)
#define SPRITE_X_PAD 2          // figure sits ~2 tiles in from the box's left
#define SPRITE_X_NUDGE 3        // ...plus 3 px (measured on the player and Mary Jane in DOSBox)
#define PLAYER_ROOM 1
#define PLAYER_FRAME 24         // standing pose shown at game start (DOSBox capture, 100% match)

// EGA plane -> colour-bit order, universal for tiles and sprites.
static const int PLANE_ORDER[4] = {3, 2, 1, 0};

/* ------------------------------------------------------------ pixel access */

// Per-mode record sizes, transparency and palette.
static int tile_bytes(bool cga) { return cga ? CGA_TILE_BYTES : EGA_TILE_BYTES; }
static int spids_stride(bool cga) { return cga ? CGA_SPIDS_STRIDE : SPIDS_STRIDE; }
static int sprite_key(bool cga) { return cga ? CGA_SPRITE_KEY : SPRITE_KEY; }
static const uint8_t (*palette(bool cga))[3] { return cga ? CGA_PAL : PAL; }

// Byte holding plane p of pixel (x,y) in a w x h EGA image: plane-major (all
// rows of plane 0, then plane 1, ...: tiles and sprites) or row-planar (each
// row's four planes in turn: the screens).
static size_t ega_offset(int w, int h, int p, int x, int y, bool rowplanar) {
    size_t bpr = (size_t)(w / 8);
    return rowplanar ? ((size_t)y * 4 + p) * bpr + x / 8 : p * bpr * h + (size_t)y * bpr + x / 8;
}

// Colour of pixel (x,y) of the w x h image at `img` (`avail` bytes readable).
// EGA: four 1-bit planes (see ega_offset). CGA: packed 2 bpp rows, 4 pixels
// per byte, most significant pair leftmost, for every kind of image.
static int img_pixel(const uint8_t *img, size_t avail, int w, int h, int x, int y, bool cga, bool rowplanar) {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    if (cga) {
        size_t o = (size_t)y * (w / 4) + x / 4;
        return o < avail ? (img[o] >> (6 - 2 * (x % 4))) & 3 : 0;
    }
    int c = 0;
    for (int p = 0; p < 4; p++) {
        size_t o = ega_offset(w, h, p, x, y, rowplanar);
        if (o >= avail) return 0;
        c |= ((img[o] >> (7 - x % 8)) & 1) << PLANE_ORDER[p];
    }
    return c;
}

static void img_set_pixel(uint8_t *img, size_t avail, int w, int h, int x, int y, int colour, bool cga, bool rowplanar) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    if (cga) {
        size_t o = (size_t)y * (w / 4) + x / 4;
        int shift = 6 - 2 * (x % 4);
        if (o < avail) img[o] = (uint8_t)((img[o] & ~(3 << shift)) | ((colour & 3) << shift));
        return;
    }
    for (int p = 0; p < 4; p++) {
        size_t o = ega_offset(w, h, p, x, y, rowplanar);
        if (o >= avail) return;
        uint8_t mask = (uint8_t)(1 << (7 - x % 8));
        if ((colour >> PLANE_ORDER[p]) & 1) img[o] |= mask; else img[o] &= (uint8_t)~mask;
    }
}

/* ------------------------------------------------------------------ assets */

static uint8_t *read_asset(const char *dir, const char *name, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "render: required asset '%s' not found in '%s' (%s); "
                "use --data-dir <dir>\n", name, dir, strerror(errno));
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); fprintf(stderr, "render: asset '%s' is empty\n", name); return NULL; }
    uint8_t *buf = malloc((size_t)sz);
    if (buf == NULL) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp); free(buf);
        fprintf(stderr, "render: short read on '%s'\n", name);
        return NULL;
    }
    fclose(fp);
    if (len_out) *len_out = (size_t)sz;
    return buf;
}

// Read every block of a BACKS.SPL: seek to each block's offset (64 big-endian
// offsets, then the file size), read its 4 markers and spl_decompress the
// rest. A block whose offset equals the next block's is absent (present[] =
// false, tiles all 0 = black). Missing file: no blocks.
static void load_backs_file(const char *path, uint8_t blocks[BACKS_BLOCKS][BACKS_TILES], bool present[BACKS_BLOCKS]) {
    memset(blocks, 0, (size_t)BACKS_BLOCKS * BACKS_TILES);
    memset(present, 0, BACKS_BLOCKS);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return;
    uint8_t hdr[0x82];
    if (fread(hdr, 1, sizeof hdr, fp) != sizeof hdr) { fclose(fp); return; }
    for (int bid = 0; bid < BACKS_BLOCKS; bid++) {
        int start = (hdr[bid * 2] << 8) | hdr[bid * 2 + 1];
        int end = (bid + 1 < BACKS_BLOCKS) ? ((hdr[(bid + 1) * 2] << 8) | hdr[(bid + 1) * 2 + 1])
                                           : ((hdr[0x80] << 8) | hdr[0x81]);
        if (end <= start + 4 || fseek(fp, start, SEEK_SET) != 0) continue;
        uint8_t markers[4];
        if (fread(markers, 1, 4, fp) != 4) continue;
        uint8_array dec = {0};
        if (spl_decompress(fp, (size_t)(end - start - 4), markers, &dec, NULL)) {
            size_t n = dec.length < BACKS_TILES ? dec.length : BACKS_TILES;
            memcpy(blocks[bid], dec.data, n);
            present[bid] = true;
        }
        ARRAY_FREE(dec);
    }
    fclose(fp);
}

/* ------------------------------------------------------------------- draw */

static inline void put_px(uint8_t *cv, int x, int y, const uint8_t rgb[3]) {
    if (x < 0 || x >= BW || y < 0 || y >= BH) return;
    uint8_t *p = cv + (y * BW + x) * 3;
    p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2];
}

// One 8x8 tile of a tile file, opaque (the game does no blending).
static void draw_tile(uint8_t *cv, const uint8_t *data, size_t len, int idx, int dx, int dy, bool cga) {
    size_t base = (size_t)idx * tile_bytes(cga);
    if (base + tile_bytes(cga) > len) return;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            put_px(cv, dx + x, dy + y, palette(cga)[img_pixel(data + base, len - base, 8, 8, x, y, cga, false)]);
}

// One 24x21 SPIDS frame, its key colour left undrawn.
static void draw_spids(uint8_t *cv, const uint8_t *data, size_t len, int frame, int dx, int dy, bool cga) {
    size_t base = (size_t)frame * spids_stride(cga);
    if (base >= len) return;
    for (int y = 0; y < SPIDS_H; y++)
        for (int x = 0; x < SPIDS_W; x++) {
            int c = img_pixel(data + base, len - base, SPIDS_W, SPIDS_H, x, y, cga, false);
            if (c != sprite_key(cga)) put_px(cv, dx + x, dy + y, palette(cga)[c]);
        }
}

/* --------------------------------------------------------------- creatures */

// Resolve a sprite (editor type + damage) to a SPIDS sheet + the frame the
// game shows on room entry (matched against DOSBox captures of every room).
// SHARK is overloaded by damage: 3 = Mysterio, 4 = Mary Jane, else shark.
static void creature_frame(SpriteType type, uint8_t dmg, int *use_spids2, int *frame) {
    switch (type) {
        case MUMMY:        *use_spids2 = 0; *frame = 35; return;
        case BLUE_MAN:     *use_spids2 = 0; *frame = 38; return;
        case WOLF:         *use_spids2 = 0; *frame = 46; return;
        case R2D2:         *use_spids2 = 0; *frame = 50; return;
        case DINOSAUR:     *use_spids2 = 0; *frame = 53; return;
        case RAT:          *use_spids2 = 0; *frame = 58; return;
        case SHOTGUN_LADY: *use_spids2 = 1; *frame = 5;  return;
        case SHARK:
            if (dmg == 3) { *use_spids2 = 1; *frame = 6;  return; }  // Mysterio (invisible until he materialises)
            if (dmg == 4) { *use_spids2 = 0; *frame = 62; return; }  // Mary Jane
            *use_spids2 = 0; *frame = 44; return;                    // Shark
        default:           *use_spids2 = 0; *frame = 0;  return;
    }
}

/* --------------------------------------------------------------- overlay */

// 3x5 bitmap digit font (low 3 bits per row), for debug labels.
static const uint8_t FONT3x5[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
static const uint8_t BLACK[3] = {0, 0, 0};

static void draw_digit(uint8_t *cv, int d, int x, int y, const uint8_t rgb[3]) {
    for (int ry = 0; ry < 5; ry++)
        for (int rx = 0; rx < 3; rx++)
            if (FONT3x5[d][ry] & (4 >> rx)) put_px(cv, x + rx, y + ry, rgb);
}

// Draw a non-negative integer with a 1px black backing for legibility.
static void draw_number(uint8_t *cv, int n, int x, int y, const uint8_t rgb[3]) {
    char buf[8];
    int len = snprintf(buf, sizeof buf, "%d", n);
    for (int dy = -1; dy <= 5; dy++)
        for (int dx = -1; dx <= len * 4; dx++)
            put_px(cv, x + dx, y + dy, BLACK);
    for (int i = 0; i < len; i++)
        draw_digit(cv, buf[i] - '0', x + i * 4, y, rgb);
}

static void draw_box(uint8_t *cv, int x0, int y0, int x1, int y1, const uint8_t rgb[3]) {
    for (int x = x0; x <= x1; x++) { put_px(cv, x, y0, rgb); put_px(cv, x, y1, rgb); }
    for (int y = y0; y <= y1; y++) { put_px(cv, x0, y, rgb); put_px(cv, x1, y, rgb); }
}

static void draw_line(uint8_t *cv, int x0, int y0, int x1, int y1, const uint8_t rgb[3]) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(cv, x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static const uint8_t COL_BLOCK[3]  = {0, 255, 255};    // cyan
static const uint8_t COL_SPRITE[3] = {255, 255, 0};    // yellow
static const uint8_t COL_SW[3]     = {255, 0, 255};    // magenta: switch origin
static const uint8_t COL_TBLOCK[3] = {255, 96, 0};     // orange: toggles a block
static const uint8_t COL_TBIT[3]   = {0, 255, 96};     // green:  toggles a bit
static const uint8_t COL_TOBJ[3]   = {96, 160, 255};   // blue:   toggles an object
static const uint8_t COL_GRID[3]   = {40, 40, 40};

static void draw_overlay(uint8_t *cv, struct DecompressedRoom *room) {
    // faint tile grid
    for (int gx = 0; gx <= WIDTH_TILES; gx++)
        for (int y = 0; y < BH; y++) put_px(cv, gx * 8, y, COL_GRID);
    for (int gy = 0; gy <= HEIGHT_TILES; gy++)
        for (int x = 0; x < BW; x++) put_px(cv, x, gy * 8, COL_GRID);

    // objects / sprites, labelled with their index
    for (int i = 0; i < room->num_objects; i++) {
        struct RoomObject *o = room->objects + i;
        const uint8_t *col = (o->type == BLOCK) ? COL_BLOCK : COL_SPRITE;
        if (o->type == BLOCK)
            draw_box(cv, o->x * 8, o->y * 8,
                     (o->x + o->block.width) * 8 - 1, (o->y + o->block.height) * 8 - 1, col);
        else
            draw_box(cv, o->x * 8, o->y * 8, o->x * 8 + 7, o->y * 8 + 7, col);
        draw_number(cv, i, o->x * 8 + 1, o->y * 8 + 1, col);
    }

    // switches: marker at origin, then per-chunk effect, by type.
    for (int i = 0; i < room->num_switches; i++) {
        struct SwitchObject *sw = room->switches + i;
        if (sw->chunks.length == 0) continue;
        int ox = sw->chunks.data[0].x * 8 + 4, oy = sw->chunks.data[0].y * 8 + 4;
        draw_box(cv, ox - 3, oy - 3, ox + 3, oy + 3, COL_SW);
        draw_number(cv, i, ox + 5, oy - 2, COL_SW);
        for (size_t c = 1; c < sw->chunks.length; c++) {   // chunk 0 = PREAMBLE
            struct SwitchChunk *ch = sw->chunks.data + c;
            if (ch->type == TOGGLE_BLOCK) {
                // outline the affected tile span and link the switch to it
                int x1 = ch->x + (ch->dir == HORIZONTAL ? ch->size - 1 : 0);
                int y1 = ch->y + (ch->dir == VERTICAL ? ch->size - 1 : 0);
                draw_box(cv, ch->x * 8, ch->y * 8, (x1 + 1) * 8 - 1, (y1 + 1) * 8 - 1, COL_TBLOCK);
                draw_line(cv, ox, oy, ch->x * 8 + 4, ch->y * 8 + 4, COL_TBLOCK);
            } else if (ch->type == TOGGLE_OBJECT) {
                // link to the object it targets (by index), not an x,y tile
                if (ch->index < room->num_objects) {
                    struct RoomObject *t = room->objects + ch->index;
                    int tcx = t->x * 8 + (t->type == BLOCK ? t->block.width * 4 : 4);
                    int tcy = t->y * 8 + (t->type == BLOCK ? t->block.height * 4 : 4);
                    draw_line(cv, ox, oy, tcx, tcy, COL_TOBJ);
                    draw_box(cv, tcx - 2, tcy - 2, tcx + 2, tcy + 2, COL_TOBJ);
                }
            } else if (ch->type == TOGGLE_BIT) {
                // cross-room: target is a switch in room_idx, not a local tile.
                // Point an arrow toward the matching neighbour edge.
                int rid = ch->room_idx, ex = ox, ey = oy;
                if (rid == room->room_north) ey = 1;
                else if (rid == room->room_south) ey = BH - 2;
                else if (rid == room->room_west) ex = 1;
                else if (rid == room->room_east) ex = BW - 2;
                if (ex != ox || ey != oy) draw_line(cv, ox, oy, ex, ey, COL_TBIT);
                draw_box(cv, ox - 1, oy - 1, ox + 1, oy + 1, COL_TBIT);
            }
        }
    }
}

/* ------------------------------------------------------------------- PNG */

static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
    return c;
}

static void put_be32(FILE *f, uint32_t v) {
    fputc(v >> 24, f); fputc(v >> 16, f); fputc(v >> 8, f); fputc(v, f);
}
static void put_chunk(FILE *f, const char *type, const uint8_t *data, size_t len) {
    put_be32(f, (uint32_t)len);
    uint32_t c = 0xffffffffu;
    c = crc_upd(c, (const uint8_t *)type, 4);
    if (len) c = crc_upd(c, data, len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    put_be32(f, c ^ 0xffffffffu);
}

static bool write_png(const char *path, const uint8_t *rgb, int w, int h) {
    crc_init();
    FILE *f = fopen(path, "wb");
    if (f == NULL) { fprintf(stderr, "render: cannot write '%s': %s\n", path, strerror(errno)); return false; }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    put_chunk(f, "IHDR", ihdr, sizeof ihdr);

    // raw filtered scanlines: 0x00 filter byte + RGB row
    size_t row = (size_t)w * 3;
    size_t raw_len = (size_t)h * (row + 1);
    uint8_t *raw = malloc(raw_len);
    if (raw == NULL) { fclose(f); return false; }
    for (int y = 0; y < h; y++) {
        raw[y * (row + 1)] = 0;
        memcpy(raw + y * (row + 1) + 1, rgb + (size_t)y * row, row);
    }
    // zlib stream of stored DEFLATE blocks
    size_t zcap = raw_len + (raw_len / 65535 + 1) * 5 + 16;
    uint8_t *z = malloc(zcap);
    if (z == NULL) { free(raw); fclose(f); return false; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    size_t pos = 0;
    while (pos < raw_len) {
        size_t blk = raw_len - pos;
        if (blk > 65535) blk = 65535;
        z[zp++] = (pos + blk == raw_len) ? 1 : 0;
        z[zp++] = blk & 0xff; z[zp++] = (blk >> 8) & 0xff;
        z[zp++] = ~blk & 0xff; z[zp++] = (~blk >> 8) & 0xff;
        memcpy(z + zp, raw + pos, blk); zp += blk; pos += blk;
    }
    // adler32 of raw
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    uint32_t adler = (b << 16) | a;
    z[zp++] = adler >> 24; z[zp++] = adler >> 16; z[zp++] = adler >> 8; z[zp++] = adler;

    put_chunk(f, "IDAT", z, zp);
    put_chunk(f, "IEND", NULL, 0);
    free(z); free(raw); fclose(f);
    return true;
}

/* --------------------------------------------------------------- render */

typedef struct {
    bool cga;                                   // .CGA files (packed 2 bpp) rather than .EGA
    uint8_t *data[GFX_SOURCES];                 // each graphics file, NULL if absent
    size_t len[GFX_SOURCES];
    uint8_t backs[BACKS_BLOCKS][BACKS_TILES];   // BACKS.SPL decoded (absent blocks: zeros)
    bool backs_present[BACKS_BLOCKS];
} Assets;

// The graphics files, one per source. `count` images of w x h; sheets are
// tiles (plane-major 8x8) or sprite frames, screens a single row-planar picture.
enum { KIND_TILES, KIND_SPRITE, KIND_SCREEN };
static const struct { const char *file; int count, w, h, kind; bool required; } GFX[GFX_SOURCES] = {
    [GFX_TILES]    = { "BLOCKS",   512, 8, 8, KIND_TILES, true },
    [GFX_SPIDS1]   = { "SPIDS1",   64, SPIDS_W, SPIDS_H, KIND_SPRITE, true },
    [GFX_SPIDS2]   = { "SPIDS2",   64, SPIDS_W, SPIDS_H, KIND_SPRITE, true },
    [GFX_SCENERY]  = { "SCENERY",  256, 8, 8, KIND_TILES, true },
    [GFX_CHARSET]  = { "CHARSET",  128, 8, 8, KIND_TILES, false },
    [GFX_LOADSCRN] = { "LOADSCRN", 1, 320, 200, KIND_SCREEN, false },
    [GFX_WINSCRN]  = { "WINSCRN",  1, 320, 200, KIND_SCREEN, false },
    [GFX_PAGE1]    = { "PAGE1",    1, 320, 200, KIND_SCREEN, false },
    [GFX_PAGE2]    = { "PAGE2",    1, 320, 200, KIND_SCREEN, false },
    [GFX_PAGE3]    = { "PAGE3",    1, 320, 200, KIND_SCREEN, false },
    [GFX_PAGE4]    = { "PAGE4",    1, 320, 200, KIND_SCREEN, false },
    [GFX_HISCORE]  = { "HISCORE",  1, 320, 38, KIND_SCREEN, false },
    [GFX_SPIDFIGR] = { "SPIDFIGR", 1, 64, 192, KIND_SCREEN, false },
    [GFX_SKELFIGR] = { "SKELFIGR", 1, 64, 192, KIND_SCREEN, false },
};

static bool load_assets(const char *dir, Assets *a, bool cga) {
    const char *ext = cga ? "CGA" : "EGA";
    bool ok = true;
    a->cga = cga;
    for (int s = 0; s < GFX_SOURCES; s++) {
        char name[32];
        snprintf(name, sizeof name, "%s.%s", GFX[s].file, ext);
        a->data[s] = read_asset(dir, name, &a->len[s]);
        if (a->data[s] == NULL && GFX[s].required) ok = false;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, "BACKS.SPL");
    load_backs_file(path, a->backs, a->backs_present);     // missing file: black backgrounds
    return ok;
}
static void free_assets(Assets *a) {
    for (int s = 0; s < GFX_SOURCES; s++) free(a->data[s]);
}

// Effective foreground tile at (x,y), mirroring dumpRoom's resolution: a BLOCK
// object's own tile data overrides the base tilemap, and a TOGGLE_BLOCK switch
// chunk (shown in its "on" state) overrides that. Sprites are NOT applied here
// (they're drawn as real sprite graphics on top).
static uint8_t effective_tile(struct DecompressedRoom *room, int x, int y) {
    uint8_t tile = room->tiles[y * WIDTH_TILES + x];
    for (int o = 0; o < room->num_objects; o++) {
        struct RoomObject *ob = room->objects + o;
        if (ob->type == BLOCK && ob->tiles &&
            x >= ob->x && x < ob->x + ob->block.width &&
            y >= ob->y && y < ob->y + ob->block.height) {
            return ob->tiles[(y - ob->y) * ob->block.width + (x - ob->x)];
        }
    }
    for (int s = 0; s < room->num_switches; s++) {
        struct SwitchObject *sw = room->switches + s;
        for (size_t c = 1; c < sw->chunks.length; c++) {
            struct SwitchChunk *ch = sw->chunks.data + c;
            if (ch->type != TOGGLE_BLOCK) continue;
            bool hit = (ch->dir == HORIZONTAL)
                ? (y == ch->y && x >= ch->x && x < ch->x + ch->size)
                : (x == ch->x && y >= ch->y && y < ch->y + ch->size);
            if (hit) return ch->on;
        }
    }
    return tile;
}

// Fill a BW*BH*3 RGB buffer with one room (background + foreground + sprites,
// plus the overlay if requested). Assumes the room is valid.
static void render_room_base(RoomFile *file, int room_id,
                             const Assets *a, bool debug, bool player, uint8_t *cv) {
    struct DecompressedRoom *room = &file->rooms[room_id].data;
    memset(cv, 0, (size_t)BW * BH * 3);

    const uint8_t *bg = a->backs[room->background % BACKS_BLOCKS];
    for (int ty = 0; ty < HEIGHT_TILES; ty++)
        for (int tx = 0; tx < WIDTH_TILES; tx++) {
            int v = bg[ty * WIDTH_TILES + tx];
            if (v) draw_tile(cv, a->data[GFX_SCENERY], a->len[GFX_SCENERY], v, tx * 8, ty * 8, a->cga);
        }
    for (int ty = 0; ty < HEIGHT_TILES; ty++)
        for (int tx = 0; tx < WIDTH_TILES; tx++) {
            int v = effective_tile(room, tx, ty) & 0x3f;
            if (v) draw_tile(cv, a->data[GFX_TILES], a->len[GFX_TILES], v + room->tile_offset * 16, tx * 8, ty * 8, a->cga);
        }
    for (int i = 0; i < room->num_objects; i++) {
        struct RoomObject *o = room->objects + i;
        if (o->type != SPRITE) continue;
        int s2, frame;
        creature_frame(o->sprite.type, o->sprite.damage, &s2, &frame);
        int dx = (o->x + 1 - SPRITE_X_PAD) * 8 + SPRITE_X_NUDGE;
        int dy = (o->y + 1 + 1) * 8 - SPIDS_H;
        if (s2) draw_spids(cv, a->data[GFX_SPIDS2], a->len[GFX_SPIDS2], frame, dx, dy, a->cga);
        else    draw_spids(cv, a->data[GFX_SPIDS1], a->len[GFX_SPIDS1], frame, dx, dy, a->cga);
    }
    if (player && room_id == PLAYER_ROOM)
        draw_spids(cv, a->data[GFX_SPIDS1], a->len[GFX_SPIDS1], PLAYER_FRAME,
                   (3 - SPRITE_X_PAD) * 8 + SPRITE_X_NUDGE, (20 + 1) * 8 - SPIDS_H, a->cga);
    if (debug) draw_overlay(cv, room);
}

// Nearest-neighbour upscale `cv` (BW*BH) into a fresh buffer and write a PNG.
static bool write_scaled(const char *out_path, const uint8_t *cv, int scale) {
    int w = BW * scale, h = BH * scale;
    uint8_t *big = malloc((size_t)w * h * 3);
    if (big == NULL) return false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *src = cv + ((y / scale) * BW + (x / scale)) * 3;
            uint8_t *dst = big + ((size_t)y * w + x) * 3;
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        }
    bool ok = write_png(out_path, big, w, h);
    free(big);
    return ok;
}

bool render_room_png(RoomFile *file, int room_id, const char *out_path, RenderOpts opts) {
    if (room_id < 0 || room_id >= 64 || !file->rooms[room_id].valid) {
        fprintf(stderr, "render: room %d is not present\n", room_id);
        return false;
    }
    const char *dir = opts.data_dir ? opts.data_dir : ".";
    Assets a = {0};
    if (!load_assets(dir, &a, false)) { free_assets(&a); return false; }
    uint8_t *cv = malloc((size_t)BW * BH * 3);
    bool ok = cv != NULL;
    if (ok) {
        render_room_base(file, room_id, &a, opts.debug, true, cv);
        ok = write_scaled(out_path, cv, opts.scale > 0 ? opts.scale : 1);
    }
    free(cv); free_assets(&a);
    return ok;
}

bool render_all_png(RoomFile *file, const char *out_dir, RenderOpts opts) {
    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "render: cannot create '%s': %s\n", out_dir, strerror(errno));
        return false;
    }
    const char *dir = opts.data_dir ? opts.data_dir : ".";
    Assets a = {0};
    if (!load_assets(dir, &a, false)) { free_assets(&a); return false; }
    uint8_t *cv = malloc((size_t)BW * BH * 3);
    bool ok = cv != NULL;
    int n = 0;
    for (int rid = 0; ok && rid < 64; rid++) {
        if (!file->rooms[rid].valid) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/room%02d.png", out_dir, rid);
        render_room_base(file, rid, &a, opts.debug, true, cv);
        ok = write_scaled(path, cv, opts.scale > 0 ? opts.scale : 1);
        n++;
    }
    free(cv); free_assets(&a);
    if (!ok) return false;
    fprintf(stderr, "Wrote %d rooms to %s\n", n, out_dir);
    return true;
}

/* --- reusable decode API for the GUI ------------------------------------ */

struct RenderCtx {
    Assets a;
    char dir[1024];
    bool hide_player;
};

RenderCtx *render_open(const char *data_dir, bool cga) {
    RenderCtx *ctx = calloc(1, sizeof *ctx);
    if (ctx == NULL) return NULL;
    snprintf(ctx->dir, sizeof ctx->dir, "%s", data_dir ? data_dir : ".");
    if (!load_assets(ctx->dir, &ctx->a, cga)) {
        free_assets(&ctx->a);
        free(ctx);
        return NULL;
    }
    return ctx;
}

bool render_is_cga(RenderCtx *ctx) { return ctx != NULL && ctx->a.cga; }

void render_close(RenderCtx *ctx) {
    if (ctx == NULL) return;
    free_assets(&ctx->a);
    free(ctx);
}

bool render_room_rgb(RenderCtx *ctx, RoomFile *file, int room_id, uint8_t *out) {
    if (ctx == NULL || room_id < 0 || room_id >= 64 || !file->rooms[room_id].valid)
        return false;
    render_room_base(file, room_id, &ctx->a, false, !ctx->hide_player, out);
    return true;
}

void render_show_player(RenderCtx *ctx, bool show) {
    if (ctx) ctx->hide_player = !show;
}

int render_player_frame(void) { return PLAYER_FRAME; }

const uint8_t (*render_palette(RenderCtx *ctx))[3] { return palette(render_is_cga(ctx)); }
int render_colours(RenderCtx *ctx) { return render_is_cga(ctx) ? 4 : 16; }

/* --- graphic sources -------------------------------------------------- */

const char *render_gfx_name(RenderCtx *ctx, GfxSource src) {
    static char name[GFX_SOURCES][32];
    snprintf(name[src], sizeof name[src], "%s.%s", GFX[src].file, render_is_cga(ctx) ? "CGA" : "EGA");
    return name[src];
}
int render_gfx_count(GfxSource src) { return GFX[src].count; }
void render_gfx_size(GfxSource src, int *w, int *h) { *w = GFX[src].w; *h = GFX[src].h; }
bool render_gfx_is_screen(GfxSource src) { return GFX[src].kind == KIND_SCREEN; }
bool render_gfx_available(RenderCtx *ctx, GfxSource src) { return ctx != NULL && ctx->a.data[src] != NULL; }
int render_gfx_bytes(RenderCtx *ctx, GfxSource src) {
    bool cga = render_is_cga(ctx);
    switch (GFX[src].kind) {
        case KIND_SPRITE: return spids_stride(cga);
        case KIND_SCREEN: return GFX[src].w * GFX[src].h / (cga ? 4 : 2);
        default:          return tile_bytes(cga);
    }
}
int render_gfx_key(RenderCtx *ctx, GfxSource src) { return GFX[src].kind == KIND_SPRITE ? sprite_key(render_is_cga(ctx)) : -1; }

static uint8_t *gfx_data(RenderCtx *ctx, GfxSource src, size_t *len) {
    if ((unsigned)src >= GFX_SOURCES) { *len = 0; return NULL; }
    *len = ctx->a.len[src];
    return ctx->a.data[src];
}

// Start of image idx in its file, with the bytes available from there; NULL if out of range.
static uint8_t *gfx_image(RenderCtx *ctx, GfxSource src, int idx, size_t *avail) {
    size_t len;
    uint8_t *d = ctx ? gfx_data(ctx, src, &len) : NULL;
    size_t base = (size_t)idx * render_gfx_bytes(ctx, src);
    if (d == NULL || idx < 0 || idx >= GFX[src].count || base >= len) return NULL;
    *avail = len - base;
    return d + base;
}

int render_gfx_pixel(RenderCtx *ctx, GfxSource src, int idx, int x, int y) {
    size_t avail;
    uint8_t *img = gfx_image(ctx, src, idx, &avail);
    return img ? img_pixel(img, avail, GFX[src].w, GFX[src].h, x, y, ctx->a.cga, GFX[src].kind == KIND_SCREEN) : 0;
}

void render_gfx_set_pixel(RenderCtx *ctx, GfxSource src, int idx, int x, int y, int colour) {
    size_t avail;
    uint8_t *img = gfx_image(ctx, src, idx, &avail);
    if (img && colour >= 0 && colour < render_colours(ctx))
        img_set_pixel(img, avail, GFX[src].w, GFX[src].h, x, y, colour, ctx->a.cga, GFX[src].kind == KIND_SCREEN);
}

bool render_gfx_get(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out) {
    if ((unsigned)src >= GFX_SOURCES) return false;
    size_t len;
    uint8_t *d = ctx ? gfx_data(ctx, src, &len) : NULL;
    size_t n = (size_t)render_gfx_bytes(ctx, src);
    if (d == NULL || idx < 0 || (size_t)(idx + 1) * n > len) return false;
    memcpy(out, d + (size_t)idx * n, n);
    return true;
}

void render_gfx_put(RenderCtx *ctx, GfxSource src, int idx, const uint8_t *in) {
    if ((unsigned)src >= GFX_SOURCES) return;
    size_t len;
    uint8_t *d = ctx ? gfx_data(ctx, src, &len) : NULL;
    size_t n = (size_t)render_gfx_bytes(ctx, src);
    if (d == NULL || idx < 0 || (size_t)(idx + 1) * n > len) return;
    memcpy(d + (size_t)idx * n, in, n);
}

bool render_gfx_original(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out) {
    if (ctx == NULL || idx < 0) return false;
    char path[1200];
    snprintf(path, sizeof path, "%s/%s.orig", ctx->dir, render_gfx_name(ctx, src));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {                                // nothing saved yet: the file on disk is original
        snprintf(path, sizeof path, "%s/%s", ctx->dir, render_gfx_name(ctx, src));
        f = fopen(path, "rb");
    }
    if (f == NULL) return false;
    size_t n = (size_t)render_gfx_bytes(ctx, src);
    bool ok = fseek(f, (long)idx * (long)n, SEEK_SET) == 0 && fread(out, 1, n, f) == n;
    fclose(f);
    return ok;
}

// Before the first write of a data file, copy it to <file>.orig (kept forever
// as "the original" for reset / render_*_original).
static void keep_original(const char *path, const char *orig) {
    struct stat st;
    if (stat(orig, &st) == 0) return;
    FILE *in = fopen(path, "rb"), *out = in ? fopen(orig, "wb") : NULL;
    if (in && out) {
        uint8_t buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    }
    if (in) fclose(in);
    if (out) fclose(out);
}

bool render_gfx_save(RenderCtx *ctx, GfxSource src) {
    if (ctx == NULL) return false;
    size_t len;
    uint8_t *d = gfx_data(ctx, src, &len);
    if (d == NULL) return false;
    char path[1200], orig[1200];
    snprintf(path, sizeof path, "%s/%s", ctx->dir, render_gfx_name(ctx, src));
    snprintf(orig, sizeof orig, "%s/%s.orig", ctx->dir, render_gfx_name(ctx, src));
    keep_original(path, orig);
    FILE *f = fopen(path, "wb");
    if (f == NULL) { fprintf(stderr, "render: cannot write %s: %s\n", path, strerror(errno)); return false; }
    bool ok = fwrite(d, 1, len, f) == len;
    fclose(f);
    return ok;
}

void render_gfx_rgba(RenderCtx *ctx, GfxSource src, int idx, uint8_t *out) {
    int w = GFX[src].w, h = GFX[src].h;
    memset(out, 0, (size_t)w * h * 4);
    if (ctx == NULL) return;
    const uint8_t (*pal)[3] = render_palette(ctx);
    int key = render_gfx_key(ctx, src);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = render_gfx_pixel(ctx, src, idx, x, y);
            if (c == key) continue;
            uint8_t *px = out + (y * w + x) * 4;
            px[0] = pal[c][0]; px[1] = pal[c][1]; px[2] = pal[c][2]; px[3] = 255;
        }
}

/* --- backgrounds (BACKS.SPL) ------------------------------------------- */

static bool backs_ok(RenderCtx *ctx, int bid, int x, int y) {
    return ctx != NULL && bid >= 0 && bid < BACKS_BLOCKS && x >= 0 && x < WIDTH_TILES && y >= 0 && y < HEIGHT_TILES;
}

int render_backs_tile(RenderCtx *ctx, int bid, int x, int y) {
    return backs_ok(ctx, bid, x, y) ? ctx->a.backs[bid][y * WIDTH_TILES + x] : 0;
}

void render_backs_set_tile(RenderCtx *ctx, int bid, int x, int y, int tile) {
    if (!backs_ok(ctx, bid, x, y) || tile < 0 || tile >= GFX[GFX_SCENERY].count) return;
    ctx->a.backs[bid][y * WIDTH_TILES + x] = (uint8_t)tile;
    ctx->a.backs_present[bid] = true;             // painting into an absent block creates it
}

bool render_backs_get(RenderCtx *ctx, int bid, uint8_t *out) {
    if (!backs_ok(ctx, bid, 0, 0)) return false;
    memcpy(out, ctx->a.backs[bid], BACKS_TILES);
    return true;
}

void render_backs_put(RenderCtx *ctx, int bid, const uint8_t *in) {
    if (!backs_ok(ctx, bid, 0, 0)) return;
    memcpy(ctx->a.backs[bid], in, BACKS_TILES);
    ctx->a.backs_present[bid] = true;
}

bool render_backs_original(RenderCtx *ctx, int bid, uint8_t *out) {
    if (!backs_ok(ctx, bid, 0, 0)) return false;
    char path[1200];
    snprintf(path, sizeof path, "%s/BACKS.SPL.orig", ctx->dir);
    struct stat st;
    if (stat(path, &st) != 0) snprintf(path, sizeof path, "%s/BACKS.SPL", ctx->dir);   // nothing saved yet
    static uint8_t blocks[BACKS_BLOCKS][BACKS_TILES];
    bool present[BACKS_BLOCKS];
    load_backs_file(path, blocks, present);
    memcpy(out, blocks[bid], BACKS_TILES);
    return true;
}

// Recompress every present block and write BACKS.SPL: 64 big-endian block
// offsets, the file size, then the blocks (an absent block gets the next
// block's offset, as the original file does). The game's size limit for the
// file is not known, so the shipped file's size (BACKS_FILE_LIMIT) is the
// bound: a result larger than that is refused and nothing is written.
bool render_backs_save(RenderCtx *ctx) {
    if (ctx == NULL) return false;
    char path[1200], orig[1200];
    snprintf(path, sizeof path, "%s/BACKS.SPL", ctx->dir);
    snprintf(orig, sizeof orig, "%s/BACKS.SPL.orig", ctx->dir);
    keep_original(path, orig);

    static uint8_t body[BACKS_BLOCKS * 2 * BACKS_TILES];   // worst case: every byte escaped
    uint8_t hdr[0x82];
    size_t len = 0;
    for (int bid = 0; bid < BACKS_BLOCKS; bid++) {
        size_t off = sizeof hdr + len;
        hdr[bid * 2] = (uint8_t)(off >> 8); hdr[bid * 2 + 1] = (uint8_t)off;
        if (!ctx->a.backs_present[bid]) continue;
        size_t n = spl_compress(ctx->a.backs[bid], BACKS_TILES, 0x00, body + len, sizeof body - len);
        if (n == 0) { fprintf(stderr, "render: BACKS.SPL block %d would not compress\n", bid); return false; }
        len += n;
    }
    size_t total = sizeof hdr + len;
    if (total > 0xffff) { fprintf(stderr, "render: BACKS.SPL too large (%zu bytes)\n", total); return false; }
    hdr[0x80] = (uint8_t)(total >> 8); hdr[0x81] = (uint8_t)total;
    if (total > BACKS_FILE_LIMIT) {
        fprintf(stderr, "render: not writing BACKS.SPL: %zu bytes would exceed the shipped file's %d\n", total, BACKS_FILE_LIMIT);
        return false;
    }

    char tmp[1300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) { fprintf(stderr, "render: cannot write %s: %s\n", tmp, strerror(errno)); return false; }
    bool ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr && fwrite(body, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (ok && rename(tmp, path) != 0) { fprintf(stderr, "render: cannot replace %s: %s\n", path, strerror(errno)); ok = false; }
    if (!ok) remove(tmp);
    return ok;
}

void render_spids_rgba(RenderCtx *ctx, int frame, uint8_t *out) {
    render_gfx_rgba(ctx, GFX_SPIDS1, frame, out);
}

void render_tileset_rgb(RenderCtx *ctx, int tile_offset, uint8_t *out) {
    memset(out, 0, (size_t)TILESET_PX * TILESET_PX * 3);
    if (ctx == NULL) return;
    const uint8_t (*pal)[3] = render_palette(ctx);
    for (int t = 0; t < 64; t++) {
        int idx = t + tile_offset * 16;
        int cx = (t % 8) * 8, cy = (t / 8) * 8;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int c = render_gfx_pixel(ctx, GFX_TILES, idx, x, y);
                uint8_t *px = out + ((size_t)(cy + y) * TILESET_PX + (cx + x)) * 3;
                px[0] = pal[c][0]; px[1] = pal[c][1]; px[2] = pal[c][2];
            }
    }
}

bool render_montage_png(RoomFile *file, const char *out_path, RenderOpts opts) {
    const char *dir = opts.data_dir ? opts.data_dir : ".";
    Assets a = {0};
    if (!load_assets(dir, &a, false)) { free_assets(&a); return false; }
    const int cw = BW / 2, ch = BH / 2, pad = 4;       // half-size cells, 8x8 grid
    const int mw = 8 * (cw + pad) + pad, mh = 8 * (ch + pad) + pad;
    uint8_t *cv = malloc((size_t)BW * BH * 3);
    uint8_t *m = malloc((size_t)mw * mh * 3);
    bool ok = cv != NULL && m != NULL;
    if (ok) {
        for (int i = 0; i < mw * mh; i++) { m[i*3] = 40; m[i*3+1] = 40; m[i*3+2] = 48; }
        for (int rid = 0; rid < 64; rid++) {
            if (!file->rooms[rid].valid) continue;
            render_room_base(file, rid, &a, opts.debug, true, cv);
            int ox = pad + (rid % 8) * (cw + pad), oy = pad + (rid / 8) * (ch + pad);
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++) {
                    const uint8_t *src = cv + ((y * 2) * BW + (x * 2)) * 3;
                    uint8_t *dst = m + ((size_t)(oy + y) * mw + (ox + x)) * 3;
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                }
        }
        ok = write_png(out_path, m, mw, mh);
    }
    free(cv); free(m); free_assets(&a);
    return ok;
}
