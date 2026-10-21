#include <assert.h>
#include <errno.h>

#include <ctype.h>

#include <stdint.h>

#include <string.h>

#include <unistd.h>

/* Only for ntohs, perhaps write our own? */
#include <arpa/inet.h>

#include <stdio.h>
long filesize(FILE *fp) {
    long tell = ftell(fp);
    if (fseek(fp, 0, SEEK_END) < 0) return -1;
    long length = ftell(fp);
    if (fseek(fp, tell, SEEK_SET) < 0) return -1;
    return length;
}

#define BOOL_S(b) ((b) ? "true" : "false")

#include "spl.h"
#include "room.h"

// header of ROOMS.SPL (and BACKS.SPL also)
typedef struct {
    uint16_t definitions[64];
    uint16_t filesize;
} Header;

void freeRoom(Room *room) {
    if (room == NULL) return;
    if (room->data.objects) {
        for (size_t i = 0; i < room->data.num_objects; i ++) {
            if (room->data.objects[i].tiles) free(room->data.objects[i].tiles);
        }
        free(room->data.objects);
    }
    if (room->data.switches) {
        for (size_t i = 0; i < room->data.num_switches; i ++) {
            ARRAY_FREE(room->data.switches[i].chunks);
        }
        free(room->data.switches);
    }
    ARRAY_FREE(room->rest);
    ARRAY_FREE(room->compressed);
    ARRAY_FREE(room->decompressed);
}

void freeRoomFile(RoomFile *file) {
    if (file == NULL) return;
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i ++) {
        freeRoom(&file->rooms[i]);
    }
}

void dumpHeader(Header *head) {
    printf("Header:\n");
    _Static_assert(sizeof(Header) == 130, "readHeader expected a different header size");
    printf("  Room offsets: [");
    for (int i = 0; i < 64; i ++) {
        if (i != 0) printf(", ");
        printf("0x%04X", head->definitions[i]);
    }
    printf("]\n");
    printf("  filesize = %u\n", head->filesize);
}

bool readHeader(Header *head, FILE *fp) {
    if (head == NULL) return false;
    if (fp == NULL) return false;
    _Static_assert(sizeof(Header) == 130, "readHeader expected a different header size");
    if (fread(head, sizeof(Header), 1, fp) != 1) return false;
    for (size_t i = 0; i < C_ARRAY_LEN(head->definitions); i ++) {
        head->definitions[i] = ntohs(head->definitions[i]);
    }
    head->filesize = ntohs(head->filesize);
    return true;
}

// MOVERTAB.DAT entry bytes 0 and 1 (previously "unknown, mostly zero" - long undocumented
// because the shipped table has almost every entry at 0/0). Hugh found live in DOSBox
// (2026-09-18) that they are position offsets in whole TILES, not pixels (confirmed once this
// editor's own render preview moved a sprite noticeably less far than the real game did for the
// same value), both mostly ordinary 8-bit two's complement, but each with its own glitchy patch
// near the top of the positive range - and byte 0 differs between sprites and blocks:
//   - byte 1 = X offset, ordinary 8-bit two's complement: 0xff/0xfe = -1/-2, and small positive
//     values work normally too (0x1a = +26, confirmed) - so unlike byte 2's bit 0x01, this isn't
//     "always glitchy", just bad in one spot. That spot is 0x7e AND 0x7f (which would be
//     +126/+127) - BOTH glitchy, on sprites and blocks alike ("for objects it seems the same").
//     0x00-0x7d and 0x80-0xfd beyond the tested points are inferred, not directly confirmed.
//   - byte 0 = Y offset. For a SPRITE it reads as a SIGNED 7-BIT value with bit 0x80 ignored
//     entirely: 0x7f and 0xff both = -1, 0x7e and 0xfe both = -2 (confirmed identical either
//     way), 0x00/0x01/0x02 = +0/+1/+2. For a BLOCK/object, 0x7f does NOT act as -1 the way it
//     does for a sprite - only 0xff does - so a block's byte 0 is ordinary two's complement like
//     byte 1, not the sprite's bit-0x80-agnostic 7-bit scheme. Hugh: "just limit those two bytes
//     to where it makes sense" rather than chase the exact glitch boundary further by hand.
// Not yet exposed by the editor (display or edit) - when it is, clamp away from the known-bad
// 0x7e/0x7f zone per byte/object-type above rather than exposing the full 0x00-0xff range.

// Describe a TOGGLE_OBJECT command byte. Verified in DOSBox-X 2026-09-15 with throwaway
// test rooms: 0x00 stop; 0x10/0x20/0x30 down
// slow/medium/fast; 0x50/0x60 up slow/medium (0x70 up fast is the same pattern but
// untested); 0x01/0x02 right slow/normal (0x03 right fast untested); 0x05/0x06 left
// slow/normal (0x07 left fast untested); 0x09-0x0f kills the object's sprite
// instantly (room.h's older static-analysis notes; 0x0b is the value this codebase
// actually uses, in the boss room). Anything else is shown as "?" by the callers,
// which all print the raw hex value alongside this.
const char *move_desc(uint8_t value) {
    if (value == 0x00) return "stop";
    if (value >= 0x09 && value <= 0x0f) return "kill";
    if ((value & 0x0f) && !(value & 0x70)) {          // horizontal only
        bool left = (value & 0x04) != 0;
        switch (value & 0x03) {
            case 0x01: return left ? "left (slow)" : "right (slow)";
            case 0x02: return left ? "left" : "right";
            case 0x03: return left ? "left (fast?)" : "right (fast?)";
        }
    }
    if ((value & 0x70) && !(value & 0x0f)) {          // vertical only
        bool up = (value & 0x40) != 0;
        switch (value & 0x30) {
            case 0x10: return up ? "up (slow)" : "down (slow)";
            case 0x20: return up ? "up" : "down";
            case 0x30: return up ? "up (fast?)" : "down (fast?)";
        }
    }
    return "?";
}

// Every value seen in the original game's MOVERTAB.DAT byte 2 (Spider-claude/COMMANDS.md
// "Session 11", static analysis of the shipped table). Byte 2's meaning depends on whether
// the object is a sprite or a block - confirmed by play/DOSBox testing (see
// [[project-room-index-bases]]):
//   - block, 0x00: static (doesn't move).
//   - sprite, 0x00: DEAD ON ROOM ENTRY, not merely motionless - this was the actual cause of
//     the "Mysterio's End? kills the player instantly" bug this editor's own test room hit
//     (2026-09-18): the shark's entry had been left at a stale 0x00 (correct for the *block*
//     objects elsewhere in that room, wrong for a sprite), and something about a dead sprite
//     record at room load corrupted state badly enough to hang/crash rather than just show no
//     shark. A relocated Lift car separately inheriting an R2D2's entry and falling by itself
//     is the same lesson from the other direction: this byte is genuinely load-bearing, not a
//     cosmetic default.
//   - sprite, 0xb8: patrolling (moves back and forth) - confirmed.
//   - block, 0x60: starts the room already moving up, matching the direction/speed bits
//     move_desc() decodes for TOGGLE_OBJECT's value byte - confirmed. 0x60 isn't in
//     MOVERTAB_KNOWN_BEHAVIORS below: it never appears in the original 768-byte table, so
//     this is one confirmed data point for a wider hypothesis (byte 2 reuses move_desc()'s
//     whole scheme for a block's initial movement), not a value anyone needs to pick from a
//     list of "seen" ones.
//   - sprite, 0x01-0x7f: GLITCHY, confirmed value-by-value across the entire range (2026-09-18)
//     - NOT the directions (or kill, for 0x09-0x0f) move_desc() would read there for a block.
//     Blocks and sprites are simply two different systems sharing one byte: the
//     move_desc()-as-initial-movement hypothesis is block-only and never transfers to a
//     sprite.
// NOTE on every "x + 1" / "x - 1" / "y - 1" below: Hugh checked the actual sprite graphics and
// believes these are NOT a real engine position shift - e.g. blue_man's frame 0x26 only draws
// on the right half of its cell, so drawing it plain ("facing right") looks like "x + 1" next
// to a centred sprite, and mirroring the same frame for "facing left" looks like "x - 1", with
// no coordinate actually changing. Kept as "x +/- 1" phrasing below because that's what you'll
// see if this editor's own renderer draws every frame un-mirrored at a fixed anchor (which it
// currently does) - the real fix is for the renderer to flip the frame per these facing/
// upside-down bits and stop shifting position at all, not to bake a fake offset into objects.
//   - sprite, 0x80: stationary, alive, FACING RIGHT (distinct from 0x00's dead-on-entry) -
//     confirmed. Its displayed/collision x is the object's stored x + 1 - byte 2 doesn't just
//     pick a behaviour, it can shift where the sprite actually sits.
//   - sprite, 0x82 (0x80 | 0x02): stationary, alive, FACING LEFT - confirmed, x is the
//     object's stored x - 1. So bit 0x02 is a facing flag (0 = right/+1, 1 = left/-1) for a
//     stationary sprite.
//   - sprite, 0x84 (0x80 | 0x04): stationary, alive, UPSIDE DOWN, facing right - confirmed, x
//     is the object's stored x + 1 (the same offset as plain 0x80), and maybe also y - 1. So
//     bit 0x04 is an orientation flag, and being upside down might shift y as well as facing
//     shifts x.
//   - sprite, 0x86 (0x84 | 0x02, i.e. 0x80 | 0x04 | 0x02): stationary, alive, UPSIDE DOWN,
//     FACING LEFT - confirmed, x is the object's stored x - 1, also y - 1 (confirmed more
//     solidly here and at 0x90/0x94/0x96 than at plain 0x84/0x94, where it's still "maybe").
//     Bits 0x02 (facing) and 0x04 (upside down) combine independently exactly as the
//     0x80/0x82/0x84 pattern predicted.
//   - sprite, 0x87 (0x86 | 0x01): GLITCHY - confirmed; bit 0x01 keeps breaking every base
//     value it's tried against (0x80/0x82/0x84/0x86 all glitch when | 0x01).
//   - sprite, 0x88 (0x80 | 0x08): MOVING LEFT, stopping at the first obstacle rather than
//     turning around - confirmed, starting from the object's stored x + 1. This is in
//     MOVERTAB_KNOWN_BEHAVIORS below (the original game really does use it) and reveals bit
//     0x08 as a distinct "moving" flag on top of the stationary 0x80 base. 0xb8 (the other
//     confirmed working value, patrol) is 0x88 | 0x30 - i.e. patrol = moving (0x08) plus two
//     more bits (0x10, 0x20) that turn "stop at obstacle" into "turn around and keep going".
//   - sprite, 0x89 (0x88 | 0x01): GLITCHY - confirmed, bit 0x01 strikes again.
//   - sprite, 0x8a (0x88 | 0x02): MOVING LEFT, stops at first obstacle, same as 0x88 - but
//     starting from the object's stored x - 1 instead of x + 1. So bit 0x02 here does NOT flip
//     direction the way it flipped facing for the stationary case (0x80 -> 0x82): both 0x88
//     and 0x8a move left, only the starting offset changes. Direction for this "moving, stop
//     at obstacle" family may be fixed at left, or a different bit (untested) controls it.
//   - sprite, 0x8b (0x8a | 0x01): GLITCHY - confirmed, bit 0x01 again.
//   - sprite, 0x8c (0x80 | 0x08 | 0x04): behaves STATIONARY despite the moving bit (0x08)
//     being set - upside down (0x04), facing right, x + 1, same as 0x84 - confirmed (Hugh:
//     "you can't move upside down", so the engine presumably can't animate an upside-down
//     sprite walking and it just sits there instead; the guess, untested, is that it would
//     move-and-stop-at-obstacle the way 0x88 does if upside-down movement existed at all).
//   - sprite, 0x8d (0x8c | 0x01): GLITCHY - confirmed, bit 0x01 again.
//   - sprite, 0x8e (0x8c | 0x02): same situation as 0x8c - stationary despite the moving bit,
//     upside down, facing left, x - 1 - confirmed. 0x8e is in MOVERTAB_KNOWN_BEHAVIORS below
//     (the original game uses it, presumably for a hanging enemy facing left).
//   - sprite, 0x8f (0x8e | 0x01): GLITCHY - confirmed, bit 0x01 again (every 0x88-0x8f | 0x01
//     combination glitches; every 0x88-0x8e value without it works).
//   - sprite, 0x90 (0x80 | 0x10): stationary, facing right, x + 1 - confirmed, and identical
//     to plain 0x80. So bit 0x10 alone (without the moving bit 0x08) does nothing observable.
//     0x90 is in MOVERTAB_KNOWN_BEHAVIORS below. Combined with 0xb8 = 0x88 | 0x10 | 0x20
//     (patrol), the working theory is 0x10/0x20 are movement-style modifiers that only matter
//     when bit 0x08 (moving) is set - patrol turns "stop at obstacle" into "turn around",
//     while on a stationary base they're inert.
//   - sprite, 0x81/0x83/0x85 (0x80, 0x82, 0x84 each | 0x01): all three GLITCHY - confirmed.
//     Bit 0x01 breaks every base value it's tried against so far, regardless of the other
//     flag bits, killing the simpler "bit 0x80 makes it real" hypothesis (having the high bit
//     set is not sufficient by itself). The 0x80+ range needs the same value-by-value
//     confirmation 0x00-0x7f got, not an extrapolated threshold - but bit 0x01 specifically
//     looks like a bad/unimplemented flag on every combination tried so far.
//   - sprite, 0x92/0x94/0x96: same as 0x82/0x84/0x86 respectively (bit 0x10 still inert without
//     the moving bit) - confirmed, including the upside-down y - 1 offset at 0x94/0x96.
//   - sprite, 0x98 (0x80 | 0x10 | 0x08): PATROLLING, starting facing right from x + 1 -
//     confirmed, same behaviour as 0xb8. So bit 0x20 (present in 0xb8 but not 0x98) isn't
//     needed for patrol once bit 0x10 is set alongside the moving bit - 0x10 alone is what
//     turns "stop at obstacle" (0x88) into "turn around" (patrol); 0x20's role is still open.
//
// room.h has an older, pre-editor comment block ("when sprite (midnight obj 1 test 2)")
// covering the *TOGGLE_OBJECT switch value byte* sent to a live sprite - a different
// mechanism (a runtime command) that happens to share this same byte's bit layout, tested by
// setting a switch's value on an already-patrolling (MOVERTAB 0xb8) sprite in After Midnight.
// Its "b8 no action" / "98 no action" entries reconciled: they're not inert values, they're
// exactly what you'd see commanding an already-patrolling sprite to patrol again - consistent
// with, not contradicting, 0xb8/0x98 both meaning "patrol" here. Its "88/8f freeze on next
// wall" and "80/90/97 freeze in place" independently corroborate this file's 0x88 (stop at
// obstacle) and 0x80/0x90 (stationary) findings. Its "09-0f kills" and "d8/e8 moves right"
// entries are for values this file hasn't reached yet in the 0x01-0x7f/0xa0+ ranges and are
// worth checking against once testing gets there.
// The rest of MOVERTAB_KNOWN_BEHAVIORS is listed because those are real, seen values in the
// shipped table - not because their exact meaning is confirmed.
const uint8_t MOVERTAB_KNOWN_BEHAVIORS[] = {
    0x00, 0xb8, 0xba, 0xb0, 0x9a, 0x88, 0xd0, 0xe0, 0x90, 0xf8, 0x98, 0xa8, 0x10, 0xf0, 0x0a, 0x8e, 0x40,
};
const size_t MOVERTAB_KNOWN_BEHAVIORS_LEN = C_ARRAY_LEN(MOVERTAB_KNOWN_BEHAVIORS);

const char *movertab_behavior_desc(uint8_t value, bool is_sprite) {
    if (value == 0x00) return is_sprite ? "DEAD ON ROOM ENTRY, not just idle (verified)" : "static block (verified)";
    if (is_sprite) {
        if (value == 0x80) return "stationary, alive, facing right (verified) - actual x is object's x + 1";
        if (value == 0x81) return "GLITCHY (verified) - bit 0x01 breaks it; 0x80 alone works";
        if (value == 0x82) return "stationary, alive, facing left (verified) - actual x is object's x - 1";
        if (value == 0x83) return "GLITCHY (verified) - bit 0x01 breaks it; 0x82 alone works";
        if (value == 0x84) return "stationary, alive, upside down, facing right (verified) - actual x is object's x + 1, maybe also y - 1";
        if (value == 0x85) return "GLITCHY (verified) - bit 0x01 breaks it; 0x84 alone works";
        if (value == 0x86) return "stationary, alive, upside down, facing left (verified) - actual x is object's x - 1, also y - 1";
        if (value == 0x87) return "GLITCHY (verified) - bit 0x01 breaks it; 0x86 alone works";
        if (value == 0x88) return "moving left, stops at first obstacle (verified) - starts from object's x + 1";
        if (value == 0x89) return "GLITCHY (verified) - bit 0x01 breaks it; 0x88 alone works";
        if (value == 0x8a) return "moving left, stops at first obstacle (verified) - starts from object's x - 1";
        if (value == 0x8b) return "GLITCHY (verified) - bit 0x01 breaks it; 0x8a alone works";
        if (value == 0x8c) return "stationary (can't move upside down), upside down, facing right (verified) - actual x is object's x + 1";
        if (value == 0x8d) return "GLITCHY (verified) - bit 0x01 breaks it; 0x8c alone works";
        if (value == 0x8e) return "stationary (can't move upside down), upside down, facing left (verified) - actual x is object's x - 1";
        if (value == 0x8f) return "GLITCHY (verified) - bit 0x01 breaks it; 0x8e alone works";
        if (value == 0x90) return "stationary, alive, facing right (verified) - actual x is object's x + 1; same as plain 0x80, bit 0x10 alone does nothing";
        if (value == 0x91) return "GLITCHY (verified) - bit 0x01 breaks it; 0x90 alone works";
        if (value == 0x92) return "stationary, alive, facing left (verified) - actual x is object's x - 1; same as plain 0x82";
        if (value == 0x94) return "stationary, alive, upside down, facing right (verified) - actual x is object's x + 1, maybe also y - 1; same as plain 0x84";
        if (value == 0x96) return "stationary, alive, upside down, facing left (verified) - actual x is object's x - 1, also y - 1; same as plain 0x86";
        if (value == 0x98) return "patrolling, starts facing right, x + 1 (verified) - same as 0xb8; bit 0x20 isn't needed once bit 0x10 is set";
        if (value == 0x99) return "GLITCHY (verified) - bit 0x01 breaks it; 0x98 alone works";
        if (value == 0x9a) return "patrolling, starts facing left, x - 1 (verified) - same as 0xba";
        if (value == 0x9b) return "GLITCHY (verified) - bit 0x01 breaks it; 0x9a alone works";
        if (value == 0x9c) return "stationary, alive, upside down, facing right (verified) - actual x is object's x + 1; same as 0x8c";
        if (value == 0x9d) return "GLITCHY (verified) - bit 0x01 breaks it; 0x9c alone works";
        if (value == 0x9e) return "stationary, alive, upside down, facing left (verified) - actual x is object's x - 1; same as 0x8e";
        if (value == 0x9f) return "GLITCHY (verified) - bit 0x01 breaks it; 0x9e alone works";
        if (value == 0xb0) return "stationary, alive, facing right (verified) - actual x is object's x + 1; same as 0x80/0x90";
        if (value == 0xb8) return "patrolling, starts facing right, x + 1 (verified) - same as 0x98";
        if (value == 0xba) return "patrolling, starts facing left, x - 1 (verified) - same as 0x9a";
        // The move_desc() fallback below is confirmed for BLOCKS only (0x60 = up). Applying
        // the same scheme to a sprite's byte 2 was a reasonable-looking guess that turned out
        // wrong: the whole 0x01-0x7f range (which move_desc() would read as directions/kill
        // for a block) is GLITCHY on a sprite instead (verified 2026-09-18, Hugh) - so for a
        // sprite, anything past the confirmed values (dead / stationary / patrol) is just
        // unknown, not a guessed direction.
        if (value >= 0x01 && value <= 0x7f) return "GLITCHY (verified) - not the move_desc() direction it would be for a block";
        for (size_t i = 0; i < MOVERTAB_KNOWN_BEHAVIORS_LEN; i++) {
            if (MOVERTAB_KNOWN_BEHAVIORS[i] == value) return "seen in the original game, exact meaning unconfirmed";
        }
        return "not seen in the original game - unverified";
    }
    if (value == 0x60) return "block starts moving up (verified)";
    const char *move = move_desc(value);
    if (strcmp(move, "?") != 0) {
        static char buf[64];
        snprintf(buf, sizeof buf, "block starts moving %s (untested, matches the 0x60 pattern)", move);
        return buf;
    }
    for (size_t i = 0; i < MOVERTAB_KNOWN_BEHAVIORS_LEN; i++) {
        if (MOVERTAB_KNOWN_BEHAVIORS[i] == value) return "seen in the original game, exact meaning unconfirmed";
    }
    return "not seen in the original game - unverified";
}

// Cycle byte 2 to the next/previous entry in MOVERTAB_KNOWN_BEHAVIORS (dir > 0 forward, < 0
// back), wrapping around; starts from the nearest known entry if the current value isn't one.
uint8_t movertab_behavior_cycle(uint8_t current, int dir) {
    int n = (int)MOVERTAB_KNOWN_BEHAVIORS_LEN, i = -1;
    for (int j = 0; j < n; j++) if (MOVERTAB_KNOWN_BEHAVIORS[j] == current) { i = j; break; }
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    return MOVERTAB_KNOWN_BEHAVIORS[i];
}

void dumpRoom(Room *room, RoomFile *file, bool color) {
    printf("Room %u: \"%s\"\n", room->index, room->data.name);

    /* printf("  Compressed room (length=%zu): [", room->compressed.length); */
    /* for (size_t i = 0; i < room->compressed.length; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", room->compressed.data[i]); */
    /* } */
    /* printf("]\n"); */

    /* printf("  Decompressed room (length=%zu): [", room->decompressed.length); */
    /* for (size_t i = 0; i < room->decompressed.length; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", room->decompressed.data[i]); */
    /* } */
    /* printf("]\n"); */

    printf("Tiles:");
    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        uint8_t x = i % WIDTH_TILES;
        uint8_t y = i / WIDTH_TILES;
        if (x == 0) printf("\n");
        bool colored = false;
        bool obj = false;
        uint8_t tile = room->data.tiles[i];
        /* if (x != 0) printf(" "); */
        for (size_t o = 0; o < room->data.num_objects; o ++) {
            struct RoomObject *object = room->data.objects + o;
            switch (object->type) {
                case BLOCK:
                    if (x >= object->x && x < object->x + object->block.width &&
                        y >= object->y && y < object->y + object->block.height) {

                        if (color) printf("\033[3%ldm", (o % 7) + 1);
                        colored = true;
                        assert(object->tiles != NULL);
                        int o_x = x - object->x;
                        int o_y = y - object->y;
                        tile = object->tiles[o_y * object->block.width + o_x];
                        obj = true;
                    }
                    break;

                case SPRITE:
                    if (x == object->x && y == object->y) {
                        if (color) printf("\033[4%ld;30m", (o % 7) + 1);
                        colored = true;
                        tile = object->sprite.type << 4 | object->sprite.damage;
                        obj = true;
                    }
            }
            if (colored) break;
        }
        for (size_t s = 0; s < room->data.num_switches; s ++) {
            struct SwitchObject *sw = room->data.switches + s;
            assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
            if (x == sw->chunks.data[0].x && y == sw->chunks.data[0].y) {
                if (color) printf("\033[4%ld;30m", (s % 3) + 4);
                colored = true;
                break;
            }
            for (size_t c = 1; !colored && c < sw->chunks.length; c ++) {
                struct SwitchChunk *chunk = sw->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK) {
                    // Idle (no switch state to preview here) - the room shows the off tile.
                    if (chunk->dir == HORIZONTAL) {
                        if (y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size) {
                            if (color) printf("\033[m\033[3%ld;40m", (s % 3) + 4);
                            colored = true;
                            tile = chunk->off;
                        }
                    } else if (x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) {
                        if (color) printf("\033[m\033[3%ld;40m", (s % 3) + 4);
                        colored = true;
                        tile = chunk->off;
                    }
                }
            }
        }

        if (tile != BLANK_TILE || (colored && !obj)) printf("%02X", tile);
        else printf("  ");
        if (colored && color) printf("\033[m");
    }
    printf("\n");
    printf("\n");

    printf("bkgrnd: %u, tiles: %u, dmg: %d, gravity (|): %u, gravity (-): %u\n",
            room->data.background, room->data.tile_offset, room->data.room_damage,
            room->data.gravity_vertical, room->data.gravity_horizontal);

#define READ_NEIGHBOUR(id) do { \
neighbour_room = file->rooms[(id)].data; \
assert(C_ARRAY_LEN(neighbour_name) >= C_ARRAY_LEN(neighbour_room.name)); \
strncpy(neighbour_name, neighbour_room.name, C_ARRAY_LEN(neighbour_room.name)); \
for (int i = C_ARRAY_LEN(neighbour_name) - 1; i >= 0; i --) { \
    if (neighbour_name[i] == '\0' || isspace(neighbour_name[i])) { \
        neighbour_name[i] = '\0'; \
    } else { \
        break; \
    } \
} \
} while (0)
    struct DecompressedRoom neighbour_room = {0};
    char neighbour_name[25] = {0};
    printf("left: %u", room->data.room_west);
    READ_NEIGHBOUR(room->data.room_west);
    printf(" - \"%s\"\n", neighbour_name);
    printf("down: %u", room->data.room_south);
    READ_NEIGHBOUR(room->data.room_south);
    printf(" - \"%s\"\n", neighbour_name);
    printf("up: %u", room->data.room_north);
    READ_NEIGHBOUR(room->data.room_north);
    printf(" - \"%s\"\n", neighbour_name);
    printf("right: %u", room->data.room_east);
    READ_NEIGHBOUR(room->data.room_east);
    printf(" - \"%s\"\n", neighbour_name);

    printf("UNKNOWN_b: %d, UNKNOWN_c: %d, UNKNOWN_e: %d, UNKNOWN_f: %d\n",
            room->data.UNKNOWN_b, room->data.UNKNOWN_c,
            room->data._num_switches & 0x3, room->data.UNKNOWN_f);

    printf("  objects (%u):\n", room->data.num_objects);
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        switch (room->data.objects[i].type) {
            case BLOCK:
                printf("    ");
                if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                printf("block %ld: (x,y)=%d,%d (w,h)=%d,%d", i,
                        room->data.objects[i].x, room->data.objects[i].y,
                        room->data.objects[i].block.width, room->data.objects[i].block.height);
                for (size_t y = 0; y < room->data.objects[i].block.height; y ++) {
                    if (color) printf("\033[m");
                    printf("\n        ");
                    if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                    for (size_t x = 0; x < room->data.objects[i].block.width; x ++) {
                        uint8_t tile = room->data.objects[i].tiles[y * room->data.objects[i].block.width + x];
                        if (tile == BLANK_TILE) {
                            if (color) printf("\033[m");
                            printf("  ");
                            if (color) printf("\033[3%ld;1m", (i % 3) + 1);
                        } else {
                            printf("%02x", tile);
                        }
                    }
                }
                break;

            case SPRITE:
                printf("    ");
                if (color) printf("\033[4%ld;30;1m", (i % 3) + 1);
                printf("sprite %ld: (x,y)=%d,%d ", i,
                        room->data.objects[i].x, room->data.objects[i].y);
                _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
                switch (room->data.objects[i].sprite.type) {
                    case SHARK: printf("SHARK"); break;
                    case MUMMY: printf("MUMMY"); break;
                    case BLUE_MAN: printf("BLUE_MAN"); break;
                    case WOLF: printf("WOLF"); break;
                    case R2D2: printf("R2D2"); break;
                    case DINOSAUR: printf("DINOSAUR"); break;
                    case RAT: printf("RAT"); break;
                    case SHOTGUN_LADY: printf("SHOTGUN_LADY"); break;

                    default:
                        fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected sprite type %d\n", __FILE__, __LINE__, room->data.objects[i].sprite.type);
                        exit(1);
                        break;
                }
                printf(" dmg:%d", room->data.objects[i].sprite.damage);
                break;
        }
        if (color) printf("\033[m");
        printf("\n");
    }

    printf("  switches (%u):\n", room->data.num_switches);
    for (size_t i = 0; i < room->data.num_switches; i ++) {
        printf("    ");
        if (color) printf("\033[4%ld;30;1m", (i % 3) + 4);
        assert(room->data.switches[i].chunks.length > 0 && room->data.switches[i].chunks.data[0].type == PREAMBLE);
        struct SwitchChunk *preamble = room->data.switches[i].chunks.data;
        // entry: the effective value, not the raw stored bit - a switch with no backing
        // tile has it forced true on every write (see switch_entry_effective), so showing
        // the raw bit here would make this dump go stale the moment the room is saved.
        printf("switch %ld: (x,y)=%d,%d (entry)=%s (once)=%s (side)=%s",
                i, preamble->x, preamble->y,
                BOOL_S(switch_entry_effective(&room->data, preamble)), BOOL_S(preamble->one_time_use),
                SWITCH_SIDE(preamble->side));
        if (color) printf("\033[m");
        printf("\n");
        if (room->data.switches[i].chunks.length > 1) {
            printf("      chunks:\n");
            for (size_t c = 1; c < room->data.switches[i].chunks.length; c ++) {
                struct SwitchChunk *chunk = room->data.switches[i].chunks.data + c;
                printf("        (%ld) type ", c);
                _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
                switch (chunk->type) {
                    case PREAMBLE:
                        fprintf(stderr, "%s:%d: UNREACHABLE\n", __FILE__, __LINE__);
                        exit(1);
                        break;

                    case TOGGLE_BLOCK:
                        printf("block: (x,y)=%d,%d (%s)=%d (on/off)=%02x/%02x",
                                chunk->x, chunk->y,
                                chunk->dir == VERTICAL ? "height" : "width",
                                chunk->size, chunk->on, chunk->off);
                        break;

                    case TOGGLE_BIT: {
                        char target_name[25] = {0};
                        assert(chunk->room_idx < C_ARRAY_LEN(file->rooms));
                        strncpy(target_name, file->rooms[chunk->room_idx].data.name, C_ARRAY_LEN(target_name) - 1);
                        for (int j = (int)C_ARRAY_LEN(target_name) - 1; j >= 0; j--)
                            if (target_name[j] == '\0' || isspace((unsigned char)target_name[j])) target_name[j] = '\0';
                            else break;
                        printf("toggle: (room) %s (switch) %u (on/off)=%d/%d",
                                target_name, chunk->switch_idx, chunk->on, chunk->off);
                        break;
                    }

                    case TOGGLE_OBJECT:
                        printf("object: idx=%d value=0x%02x test=%d (%s)",
                                chunk->index, chunk->value, chunk->test >> 4,
                                move_desc(chunk->value));
                        break;

                    default:
                        fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type %d\n", __FILE__, __LINE__, chunk->type);
                        exit(1);
                        break;
                }
                printf("\n");
            }
        }
    }

    printf("  Left over data (length=%zu): [", room->rest.length);
    for (size_t i = 0; i < room->rest.length; i ++) {
        if (i != 0) printf(", ");
        printf("0x%02x", room->rest.data[i]);
    }
    printf("]\n");
}

// Export one room's full editable state (tiles, objects, switches, metadata) as dumpRoom's
// `display`-format text - the same format `./a.out display N` prints and room_import_text (or
// `./a.out importRoom`) reads back, so a room can be lifted out and dropped into another file.
bool room_export_text(Room *room, RoomFile *file, const char *path) {
    FILE *fp = fopen(path, "w");
    if (fp == NULL) return false;
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    if (saved_fd < 0) { fclose(fp); return false; }
    dup2(fileno(fp), STDOUT_FILENO);
    dumpRoom(room, file, false);
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);
    fclose(fp);
    return true;
}

// Import a `display`-format text file (room_export_text's own output, or a hand-edited one)
// into this one room, leaving every other room's own data untouched. Recomputes the whole
// file's object/switch index bases and TOGGLE_BIT targets afterward, since this room's object
// or switch count may have changed and later rooms' bases are cumulative.
bool room_import_text(Room *room, RoomFile *file, const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return false;
    bool ok = importRooms(fp, file, (int)room->index);
    fclose(fp);
    if (!ok) return false;
    recomputeBases(file);
    return recomputeToggleBits(file);
}

bool readRoom(Room *room, Header *head, size_t idx, FILE *fp) {
    if (head == NULL) return false;
    if (idx >= C_ARRAY_LEN(head->definitions)) return false;
    if (fp == NULL) return false;

    long seek = head->definitions[idx];
    if (fseek(fp, seek, SEEK_SET) < 0) {
        perror("Could not seek to start of room");
        return false;
    }
    long size = head->definitions[idx + 1] - head->definitions[idx];
    if (size <= 0) return false;

    Room tmp = {
        .index = idx,
        .address = seek,
        .decompressed = {0},
        .compressed = {0},
    };

/* #define log(...) printf(__VA_ARGS__) */
#define log(...) do {} while (false)
#define read_next(dst, fp) { \
        int next_val = fgetc((fp)); \
        if (next_val == EOF) { \
            log("%s:%d: Unexpected end of file @ 0x%lx4x\n", \
                    __FILE__, __LINE__, ftell(fp)); \
            freeRoom(&tmp); \
            return false; \
        } \
        size --; \
        if (isprint(next_val)) log("%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        else log("%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left, decompressed %zu so far\n", __FILE__, __LINE__, next_val, next_val, ftell(fp), size, tmp.decompressed.length); \
        ARRAY_ADD(tmp.compressed, next_val); \
        dst = next_val; \
}

// The source reads the first two words into 0x1d1d-0x1d20 (including marker and these tile markers)
    uint8_t markers[4];
    log("%s:%d: Starting read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
    read_next(markers[0], fp);
    if (markers[0] != 0x8F) {
        log("Expected room to start with 0x8F, found %02X\n", markers[0]);
        freeRoom(&tmp);
        return false;
    }
    for (size_t i = 1; i < C_ARRAY_LEN(markers); i ++) {
        read_next(markers[i], fp);
    }

    if (!spl_decompress(fp, (size_t)size, markers, &tmp.decompressed, &tmp.compressed)) {
        log("%s:%d: Decompression failed\n", __FILE__, __LINE__);
        freeRoom(&tmp);
        return false;
    }
    log("%s:%d: Finishing read at 0x%lx\n", __FILE__, __LINE__, ftell(fp));
#undef read_next

    size_t data_idx = 0;
#define read_decompressed(dst, a) { \
        if (data_idx == (a).length) { \
            log("%s:%d: Not enough compressed data at %lu bytes long\n", \
                    __FILE__, __LINE__, (a).length); \
            freeRoom(&tmp); \
            return false; \
        } \
        int next_val = (a).data[data_idx++]; \
        if (isprint(next_val)) log("%s:%d: READ 0x%02x (%hhd, '%c') @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        else log("%s:%d: READ 0x%02x (%hhd) @ 0x%lx, %zu left\n", __FILE__, __LINE__, next_val, next_val, data_idx - 1, (a).length - data_idx); \
        dst = next_val; \
}

    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.tiles); i ++) {
        read_decompressed(tmp.data.tiles[i], tmp.decompressed);
    }
    read_decompressed(tmp.data.tile_offset, tmp.decompressed);
    read_decompressed(tmp.data.background, tmp.decompressed);
    read_decompressed(tmp.data.room_north, tmp.decompressed);
    read_decompressed(tmp.data.room_east, tmp.decompressed);
    read_decompressed(tmp.data.room_south, tmp.decompressed);
    read_decompressed(tmp.data.room_west, tmp.decompressed);

    read_decompressed(tmp.data.room_damage, tmp.decompressed);

    read_decompressed(tmp.data.gravity_vertical, tmp.decompressed);
    read_decompressed(tmp.data.gravity_horizontal, tmp.decompressed);

    read_decompressed(tmp.data.UNKNOWN_b, tmp.decompressed);
    read_decompressed(tmp.data.UNKNOWN_c, tmp.decompressed);
    read_decompressed(tmp.data.num_objects, tmp.decompressed);
    read_decompressed(tmp.data._num_switches, tmp.decompressed);
    tmp.data.num_switches = tmp.data._num_switches >> 2;
    read_decompressed(tmp.data.UNKNOWN_f, tmp.decompressed);

    log("%s:%d: Reading name at %04lu\n", __FILE__, __LINE__, data_idx);
    for (size_t i = 0; i < C_ARRAY_LEN(tmp.data.name); i ++) {
        read_decompressed(tmp.data.name[i], tmp.decompressed);
    }

    log("%s:%d: Reading %u moving objects at %04lu\n", __FILE__, __LINE__, tmp.data.UNKNOWN_d, data_idx);
    tmp.data.objects = calloc(tmp.data.num_objects, sizeof(struct RoomObject));
    assert(tmp.data.objects != NULL);
    struct RoomObject *objects = tmp.data.objects;
    for (size_t i = 0; i < tmp.data.num_objects; i ++) {
        uint8_t msb, lsb;
        read_decompressed(msb, tmp.decompressed);
        read_decompressed(lsb, tmp.decompressed);
        if ((msb & 0x80) == 0) {
            objects[i].type = BLOCK;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;
            objects[i].block.width = ((lsb & 0xe0) >> 5) + 1;
            objects[i].block.height = ((msb & 0x60) >> 5) + 1;
            assert(objects[i].x < WIDTH_TILES);
            assert(objects[i].block.width < WIDTH_TILES);
            assert(objects[i].x + objects[i].block.width <= WIDTH_TILES);
            if (!(objects[i].y < HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y, HEIGHT_TILES);
                continue;
            }
            assert(objects[i].y < HEIGHT_TILES);
            assert(objects[i].block.height < HEIGHT_TILES);
            if (!(objects[i].y + objects[i].block.height <= HEIGHT_TILES)) {
                fprintf(stderr, "WARNING: Room %ld (%s) object %zu y+height is out of bounds (%u >= %u)\n",
                        idx, tmp.data.name, i, objects[i].y + objects[i].block.height, HEIGHT_TILES);
                continue;
            }
            objects[i].tiles = malloc(objects[i].block.width * objects[i].block.height);
            assert(objects[i].tiles != NULL);
            for (size_t y = objects[i].y; y < objects[i].y + objects[i].block.height; y ++) {
                memcpy(
                        objects[i].tiles + (y - objects[i].y) * objects[i].block.width,
                        tmp.data.tiles + TILE_IDX(objects[i].x, y),
                        objects[i].block.width
                      );
                memset(
                        tmp.data.tiles + TILE_IDX(objects[i].x, y),
                        '\0',
                        objects[i].block.width
                      );

            }
        } else {
            objects[i].type = SPRITE;
            objects[i].x = lsb & 0x1f;
            objects[i].y = msb & 0x1f;

            objects[i].sprite.type = (((lsb & 0xe0) >> 5) + 1) % NUM_SPRITE_TYPES;
            objects[i].sprite.damage = ((msb & 0x60) >> 5) + 1;
        }
    }

    log("%s:%d: Reading %u switches at %04lu\n", __FILE__, __LINE__, tmp.data.UNKNOWN_d, data_idx);
    tmp.data.switches = calloc(tmp.data.num_switches, sizeof(struct SwitchObject));
    assert(tmp.data.switches != NULL);
    struct SwitchObject *switches = tmp.data.switches;
    for (size_t i = 0; i < tmp.data.num_switches; i ++) {
        uint8_t msb, lsb;
        read_decompressed(msb, tmp.decompressed);
        read_decompressed(lsb, tmp.decompressed);
        uint8_t y = msb & 0x1f;
        uint8_t x = lsb & 0x1f;
        _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
        ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = PREAMBLE, .x = x, .y = y, .room_entry = (lsb & 0x80) == 0x00, .one_time_use = (msb & 0x20) != 0x00, .side = (lsb & 0x60) >> 5, .msb = msb, .lsb = lsb }));

        while (data_idx < tmp.decompressed.length && (tmp.decompressed.data[data_idx] & 0xc0) != 0x00) {
            read_decompressed(msb, tmp.decompressed);
            read_decompressed(lsb, tmp.decompressed);
            switch (msb & 0xc0) {
                case 0x80: {
                    x = lsb & 0x1f;
                    y = msb & 0x1f;
                    uint8_t size = ((lsb >> 5) & 0x7) + 1;
                    uint8_t off, on;
                    enum SwitchChunkDirection dir = ((msb & 0x20) == 0) ? HORIZONTAL : VERTICAL;
                    // .on/.off were swapped from what their names say (Hugh, verified: "the
                    // first number is the off value... the second number shows the on value") -
                    // read them into the other local var so chunk->on/chunk->off end up meaning
                    // what they say, without changing the on-disk byte order itself.
                    read_decompressed(on, tmp.decompressed);
                    read_decompressed(off, tmp.decompressed);
                    // All bits accounted for
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK, .x = x, .y = y, .size = size, .dir = dir, .off = off, .on = on }));
                }; break;

                case 0x40: {
                    // Only does this if switch_structs[unknown(f) + i / 3] has {0x2, 0x8, 0x20, 0x80}[i] set
                    uint8_t on = (msb >> 4) & 0x3;
                    uint8_t off = (msb >> 2) & 0x3;
                    uint8_t bitmask = (uint8_t[]){0x01, 0x04, 0x10, 0x40}[msb & 0x3];
                    // removes bit, sets bit << 1
                    // Note that this static array index is based purely on byte, not on switch index
                    uint8_t index = lsb;
                    // This seems to be the index that is set, not the index that is checked
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_BIT, .on = on, .off = off, .bitmask = bitmask, .index = index }));
                }; break;

                case 0xc0: {
                    // Only does this if switch_structs[unknown(f) + i / 3] has {0x2, 0x8, 0x20, 0x80}[i] set
                    // 0b00110000 is tested against *0x1d13
                    // msb & 0xf is index into objects
                    // if object is block, set movertab[2] = lsb
                    // if sprite, set movertab[2] = lsb & 0xf8, maybe | 0x80 if movertab[2] & 0x2 != 0
                    uint8_t test = msb & 0x30;
                    uint8_t index = msb & 0xf;
                    ARRAY_ADD(switches[i].chunks, ((struct SwitchChunk){ .type = TOGGLE_OBJECT, .index = index, .test = test, .value = lsb }));

                }; break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type 0x%02x\n", __FILE__, __LINE__, msb);
                    exit(1);
            }
        }
    }

    // then stuff that controls enemy placement, switch actions, etc
    while (data_idx != tmp.decompressed.length) {
        uint8_t val;
        read_decompressed(val, tmp.decompressed);
        ARRAY_ADD(tmp.rest, val);
    }

    /* fprintf(stderr, "%s:%d: UNIMPLEMENTED\n", __FILE__, __LINE__); return false; */
#undef read_decompressed
#undef log
    *room = tmp;
    return true;
}

bool readRoomFromFile(Room *room, FILE *fp, const char *filename) {

    assert(fseek(fp, 0L, SEEK_END) == 0);
    long ftold = ftell(fp);
    assert(ftold != -1);
    size_t filesize = ftold;
    assert(fseek(fp, 0L, SEEK_SET) == 0);

    uint8_t *data = malloc(filesize);
    assert(data != NULL);

    if (fread(data, sizeof(uint8_t), filesize, fp) != (size_t)filesize) {
        free(data);
        fprintf(stderr, "Could read file fully: %s: %s", filename, strerror(errno));
        return false;
    }

    size_t tile_idx = 0;
    size_t data_idx = 0;
    uint16_t fullbyte = 0;
    while (tile_idx < sizeof(room->data.tiles)) {
        if (data_idx >= (size_t)filesize) {
            free(data);
            fprintf(stderr, "Could read full tileset from file: %s: Read %ld tiles\n", filename, tile_idx);
            return false;
        }

        char c = data[data_idx++];
        if (c == '\033') {
            if (data_idx >= filesize) {
                free(data);
                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", filename);
                return false;
            }

            if (data[data_idx] != '[') {
                free(data);
                fprintf(stderr, "Invalid escape sequence at %ld: %s\n", data_idx, filename);
                return false;
            }
            while (data_idx < filesize && !isalpha(data[data_idx])) data_idx ++;
            if (data_idx >= filesize) {
                free(data);
                fprintf(stderr, "Incomplete escape sequence at EOF: %s\n", filename);
                return false;
            }
            data_idx++;
        } else if (c == '\n') {
            if ((fullbyte & 0xFF00) != 0) {
                free(data);
                fprintf(stderr, "Unexpected newline at %ld: %s\n", data_idx - 1, filename);
                return false;
            }
            if (tile_idx == 0 || (data_idx >= 2 && data[data_idx - 2] == '\n')) {
                room->data.tiles[tile_idx++] = 0;
            }
            while (tile_idx < WIDTH_TILES * HEIGHT_TILES && (tile_idx % WIDTH_TILES) != 0) {
                room->data.tiles[tile_idx++] = 0;
            }
            assert(tile_idx % WIDTH_TILES == 0);
        } else {
            if (c != ' ' && !isxdigit(c)) {
                free(data);
                fprintf(stderr, "Invalid hex digit at %ld: %s\n", data_idx - 1, filename);
                return false;
            }

            uint8_t b = 0;
            if (c == ' ') b = 0;
            else if (isdigit(c)) b = c - '0';
            else b = 10 + tolower(c) - 'a';

            if ((fullbyte & 0xFF00) == 0) {
                fullbyte = 0xFF00 | (b << 4);
            } else {
                fullbyte = (fullbyte & 0xF0) | b;
                room->data.tiles[tile_idx++] = fullbyte;
            }
        }
    }

    if (data_idx < filesize) {
        fprintf(stderr, "WARNING: Still more file to read at %ld: %s\n", data_idx, filename);
    }

    free(data);
    return true;
}

bool readFile(RoomFile *file, FILE *fp) {
    if (file == NULL) return false;
    if (fp == NULL) return false;
    if (fseek(fp, 0L, SEEK_SET) < 0) {
        perror("Could not seek file to header");
        return false;
    }
    Header head = {0};
    if (!readHeader(&head, fp)) {
        perror("Could not read header");
        return false;
    }
    /* dumpHeader(&head); */
    long length = filesize(fp);
    if (length < 0) {
        perror("Could not get filesize");
        return false;
    }
    if (head.filesize != length) {
        fprintf(stderr, "Unexpected filesize %u, actual = %zu\n", head.filesize, length);
        return false;
    }

    for (size_t idx = 0; idx < C_ARRAY_LEN(head.definitions); idx ++) {
        if (!readRoom(&file->rooms[idx], &head, idx, fp)) {
            file->rooms[idx].valid = false;
            /* fprintf(stderr, "Could not read room %lu\n", idx); */
            continue;
        }
        file->rooms[idx].valid = true;
        /* dumpRoom(&file->rooms[idx]); */
    }

    uint16_t index = 0;
    uint8_t mask = 0;
    uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};

    for (size_t idx = 0; idx < C_ARRAY_LEN(head.definitions); idx ++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw ++) {
            for (size_t _idx = 0; _idx < C_ARRAY_LEN(head.definitions); _idx ++) {
                Room *_r = file->rooms + _idx;
                if (!_r->valid) continue;
                for (size_t _sw = 0; _sw < _r->data.num_switches; _sw ++) {
                    struct SwitchObject *switcch = _r->data.switches + _sw;
                    for (size_t c = 1; c < switcch->chunks.length; c ++) {
                        struct SwitchChunk *chunk = switcch->chunks.data + c;
                        if (chunk->type != TOGGLE_BIT) continue;
                        if (chunk->index == index && chunk->bitmask == bitmasks[mask]) {
                            chunk->room_idx = idx;
                            chunk->switch_idx = sw;
                        }
                    }
                }
            }
            mask ++;
            if (mask == C_ARRAY_LEN(bitmasks)) {
                mask = 0;
                index ++;
            }
        }
    }

    return true;
}

// "side" has no meaning for a switch that isn't actually approached from a wall - an
// out-of-bounds one (SB-SE style off-map targets) is never touched at all, and one sitting on
// a blank (invisible) tile just needs walking over, from any direction, to fire (Hugh: "when
// tiles are blank 0x00... side is not important, any direction works and the tile just has to
// be gone over"). Both cases hide/disable "side".
bool switch_side_hidden(struct DecompressedRoom *room, struct SwitchChunk *preamble) {
    if (preamble->x >= WIDTH_TILES || preamble->y >= HEIGHT_TILES) return true;
    return room->tiles[TILE_IDX(preamble->x, preamble->y)] == BLANK_TILE;
}

// Whether "entry" is forced true regardless of its stored value: confirmed by testing, an
// in-bounds switch sitting on a blank tile reuses that bit for "the switch is live" rather than
// "fires on room entry" - but an out-of-bounds switch is NOT the same case even though it's
// "blank" too (there's no tile array slot for it at all): entry keeps its ordinary meaning
// there (Hugh, 2026-09-19: "entry _does_ matter for out of bounds switches, even if they are
// 'blank' (they all are)"), so this deliberately excludes switch_side_hidden's OOB case.
bool switch_entry_forced(struct DecompressedRoom *room, struct SwitchChunk *preamble) {
    if (preamble->x >= WIDTH_TILES || preamble->y >= HEIGHT_TILES) return false;
    return room->tiles[TILE_IDX(preamble->x, preamble->y)] == BLANK_TILE;
}

bool switch_entry_effective(struct DecompressedRoom *room, struct SwitchChunk *preamble) {
    return preamble->room_entry || switch_entry_forced(room, preamble);
}

#define MAX_ROOM_FILE_SIZE 0x3100
// there is also a MAX_ROOM_SIZE, unknown yet, add a few switches to midnight and it will corrupt

bool writeRoom(Room *room, FILE *fp) {
    if (fp == NULL || room == NULL || !room->valid) return false;

    room->compressed.length = 0;

    /* fprintf(stderr, "Compressing and writing Room %d \"%s\" at %ld.\n", room->index, room->data.name, ftell(fp)); */

/* #define log(...) printf(__VA_ARGS__) */
#define log(...) do {} while (false)
    uint8_t decompressed[960] = {0};
    uint8_t compressed[960] = {0};
    size_t d_len = 0;
    size_t c_len = 0;

    /* Build a serialisation tile array without modifying room->data.tiles.
     * Overlay objects in reverse order so lower-indexed objects take priority
     * on any overlapping regions — matching readRoom's forward-read-then-zero
     * behaviour, which prevents tile corruption when two block objects overlap. */
    uint8_t ser_tiles[WIDTH_TILES * HEIGHT_TILES];
    memcpy(ser_tiles, room->data.tiles, sizeof(ser_tiles));
    if (room->data.num_objects > 0) {
        for (size_t ii = room->data.num_objects; ii-- > 0; ) {
            struct RoomObject *obj = room->data.objects + ii;
            if (obj->type == SPRITE || obj->tiles == NULL) continue;
            assert(obj->y + obj->block.height <= HEIGHT_TILES);
            for (size_t y = obj->y; y < obj->y + obj->block.height; y++) {
                memcpy(
                        ser_tiles + TILE_IDX(obj->x, y),
                        obj->tiles + (y - obj->y) * obj->block.width,
                        obj->block.width
                      );
            }
        }
    }

    room->data._num_switches = (room->data.num_switches << 2) | (room->data._num_switches & 0x3);

    for (size_t i = 0; i < C_ARRAY_LEN(room->data.tiles); i ++) {
        decompressed[d_len++] = ser_tiles[i];
    }
    decompressed[d_len++] = room->data.tile_offset;
    decompressed[d_len++] = room->data.background;
    decompressed[d_len++] = room->data.room_north;
    decompressed[d_len++] = room->data.room_east;
    decompressed[d_len++] = room->data.room_south;
    decompressed[d_len++] = room->data.room_west;

    decompressed[d_len++] = room->data.room_damage;

    decompressed[d_len++] = room->data.gravity_vertical;
    decompressed[d_len++] = room->data.gravity_horizontal;

    decompressed[d_len++] = room->data.UNKNOWN_b;
    decompressed[d_len++] = room->data.UNKNOWN_c;
    decompressed[d_len++] = room->data.num_objects;
    decompressed[d_len++] = room->data._num_switches;
    decompressed[d_len++] = room->data.UNKNOWN_f;

    for (size_t i = 0; i < C_ARRAY_LEN(room->data.name); i ++) {
        decompressed[d_len++] = room->data.name[i];
    }

    if (d_len + 2 * room->data.num_objects >= C_ARRAY_LEN(decompressed)) {
        fprintf(stderr, "Room %d is too big to write (objects)\n", room->index);
        return false;
    }
    for (size_t i = 0; i < room->data.num_objects; i ++) {
        // FIXME depending on type, first byte should have 0x80
        switch (room->data.objects[i].type) {
            case BLOCK:
                decompressed[d_len++] = room->data.objects[i].y | ((room->data.objects[i].block.height - 1) << 5);
                decompressed[d_len++] = room->data.objects[i].x | ((room->data.objects[i].block.width - 1) << 5);
                break;

            case SPRITE:
                decompressed[d_len++] = 0x80 | room->data.objects[i].y | ((room->data.objects[i].sprite.damage - 1) << 5);
                decompressed[d_len++] = room->data.objects[i].x | ((room->data.objects[i].sprite.type - 1) << 5);
                break;

        }
    }
    for (size_t i = 0; i < room->data.num_switches; i ++) {
        struct SwitchObject *sw = room->data.switches + i;
        assert(sw->chunks.length > 0 && sw->chunks.data[0].type == PREAMBLE);
        for (size_t c = 0; c < sw->chunks.length; c ++) {
            struct SwitchChunk *chunk = sw->chunks.data + c;
            _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
            switch (chunk->type) {
                case PREAMBLE:
                    assert(c == 0);
                    decompressed[d_len++] = (chunk->y & 0x1f) | (chunk->one_time_use ? 0x20 : 0x00);
                    decompressed[d_len++] = (chunk->x & 0x1f) | (switch_entry_effective(&room->data, chunk) ? 0x00 : 0x80) | (chunk->side << 5);
                    break;

                case TOGGLE_BLOCK:
                    decompressed[d_len++] = 0x80 | (chunk->y & 0x1f) | (chunk->dir == VERTICAL ? 0x20 : 0);
                    decompressed[d_len++] = (chunk->x & 0x1f) | ((chunk->size - 1) << 5);
                    decompressed[d_len++] = chunk->on;
                    decompressed[d_len++] = chunk->off;
                    break;

                case TOGGLE_BIT: {
                    uint8_t mask_idx = 4;
                    switch(chunk->bitmask) {
                        case 0x01: mask_idx = 0; break;
                        case 0x04: mask_idx = 1; break;
                        case 0x10: mask_idx = 2; break;
                        case 0x40: mask_idx = 3; break;
                    }
                    assert(mask_idx < 4);
                    decompressed[d_len++] = 0x40 | ((chunk->on & 0x3) << 4) | ((chunk->off & 0x3) << 2) | (mask_idx & 0x3);
                    decompressed[d_len++] = chunk->index;
                }; break;

                case TOGGLE_OBJECT:
                    decompressed[d_len++] = 0xc0 | (chunk->test & 0x30) | (chunk->index & 0xf);
                    decompressed[d_len++] = chunk->value;
                    break;

                default:
                    fprintf(stderr, "%s:%d: UNREACHABLE: Unexpected chunk type %d\n", __FILE__, __LINE__, chunk->type);
                    exit(1);
                    break;
            }
        }
    }
    // then stuff that controls enemy placement, switch actions, etc
    if (d_len + room->rest.length >= C_ARRAY_LEN(decompressed)) {
        fprintf(stderr, "Room %d is too big to write (%zu bytes)\n", room->index, d_len + room->rest.length);
        return false;
    }
    for (size_t i = 0; i < room->rest.length; i ++) {
        decompressed[d_len++] = room->rest.data[i];
    }

    if (d_len >= C_ARRAY_LEN(decompressed)) {
        fprintf(stderr, "Room %d is too big to write (%zu bytes)\n", room->index, d_len);
        return false;
    }
    log("Compressing %ld bytes of data\n", d_len);

    c_len = spl_compress(decompressed, d_len, 0x8f, compressed, C_ARRAY_LEN(compressed));
    log("Compressed %ld bytes of data into %ld bytes\n", d_len, c_len);
    if (c_len == 0) {
        fprintf(stderr, "Room %d does not compress into %zu bytes\n", room->index, C_ARRAY_LEN(compressed));
        return false;
    }

    /* printf("  Data to compress (length=%zu): [", d_len); */
    /* for (size_t i = 0; i < d_len; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", decompressed[i]); */
    /* } */
    /* printf("]\n"); */

    /* printf("  Newly compressed room (length=%zu): [", c_len); */
    /* for (size_t i = 0; i < c_len; i ++) { */
    /*     if (i % 32 == 0) printf("\n    "); */
    /*     if ((i % 32) != 0) printf(" "); */
    /*     printf("%02x", compressed[i]); */
    /* } */
    /* printf("]\n"); */

    size_t written = 0;
    do {
        size_t write_ret = fwrite(compressed + written, sizeof(uint8_t), c_len - written, fp);
        if (write_ret == 0) return false;
        written += write_ret;
    } while (written < c_len);

#undef log
    return true;
}

bool writeFile(RoomFile *file, FILE *fp, int *failed_room) {
    // structure:
    //   128 byte header, 2 byte indices to where that room is, ascending
    //   room marker 0x8f
    //   3 bytes which control compression
    //   compressed room
    // must fit within 0x3000 bytes

    Header head = {0};
    long offset = sizeof(Header);
    if (fseek(fp, offset, SEEK_SET) == -1) return false;

    // Any room that will not fit fails the whole write: the caller must not
    // end up with a file missing rooms. failed_room (if given) says why: the
    // index of the one room whose own compressed size overflowed, or -1 when
    // it was the whole file's cumulative size that went over instead.
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i ++) {
        head.definitions[i] = htons(offset);
        if (!file->rooms[i].valid) continue;
        if (!writeRoom(&file->rooms[i], fp)) {
            if (failed_room) *failed_room = (int)i;
            return false;
        }
        if (ftell(fp) > MAX_ROOM_FILE_SIZE) {
            if (failed_room) *failed_room = -1;
            fprintf(stderr, "%s:%d: Can't write room %ld @ 0x%2lx within the 0x%x byte limit. size now %lx\n",
                    __FILE__, __LINE__, i, offset, MAX_ROOM_FILE_SIZE, ftell(fp));
            return false;
        }
        /* printf("%s:%d: Wrote room %ld \"%s\" @ 0x%2lx length %ld\n", */
        /*        __FILE__, __LINE__, i, file->rooms[i].data.name, offset, ftell(fp) - offset); */

        offset = ftell(fp);
    }
    head.filesize = htons(ftell(fp));

    if (fseek(fp, 0, SEEK_SET) == -1) return false;
    if (fwrite(&head, sizeof(Header), 1, fp) != 1) return false;
    return true;
}

static void strip_ansi(char *line) {
    char *r = line, *w = line;
    while (*r) {
        if (*r == '\033' && *(r+1) == '[') {
            r += 2;
            while (*r && !isalpha((unsigned char)*r)) r++;
            if (*r) r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static uint8_t parse_hex_pair(char a, char b) {
    uint8_t hi = isdigit((unsigned char)a) ? (uint8_t)(a - '0') : (uint8_t)(10 + tolower((unsigned char)a) - 'a');
    uint8_t lo = isdigit((unsigned char)b) ? (uint8_t)(b - '0') : (uint8_t)(10 + tolower((unsigned char)b) - 'a');
    return (hi << 4) | lo;
}

// Recompute chunk->index and chunk->bitmask for all TOGGLE_BIT chunks from
// chunk->room_idx / chunk->switch_idx, using the current global switch ordering.
// Called after importRooms (since import may not have the original file for lookup).
bool recomputeToggleBits(RoomFile *file) {
    uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};
    for (size_t idx = 0; idx < C_ARRAY_LEN(file->rooms); idx++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw++) {
            struct SwitchObject *switcch = r->data.switches + sw;
            for (size_t c = 1; c < switcch->chunks.length; c++) {
                struct SwitchChunk *chunk = switcch->chunks.data + c;
                if (chunk->type != TOGGLE_BIT) continue;
                uint16_t index = 0; uint8_t mask = 0;
                for (size_t _idx = 0; _idx < C_ARRAY_LEN(file->rooms); _idx++) {
                    Room *_r = file->rooms + _idx;
                    if (!_r->valid) continue;
                    for (size_t _sw = 0; _sw < _r->data.num_switches; _sw++) {
                        if (chunk->room_idx == _idx && chunk->switch_idx == _sw) {
                            chunk->index   = index;
                            chunk->bitmask = bitmasks[mask];
                            goto tb_found;
                        }
                        if (++mask == C_ARRAY_LEN(bitmasks)) { mask = 0; index++; }
                    }
                }
                fprintf(stderr, "recomputeToggleBits: room %u switch %u not found\n",
                        chunk->room_idx, chunk->switch_idx);
                return false;
tb_found:       ;
            }
        }
    }
    return true;
}

// MOVERTAB.DAT holds 3 bytes per object, in global object order (see recomputeBases). After
// swapRooms(a, b) the order changes; permute a copy of the table (`table` in, `out` out - may
// not alias) so every object keeps its own entry. `before[64]` = objects per room id (0 for an
// invalid room) as it was BEFORE the swap - the caller must capture this first, since swapRooms
// itself only leaves the AFTER counts to read back out.
void permuteMovertabBuffer(const unsigned before[64], int a, int b, const uint8_t table[768], uint8_t out[768]) {
    memset(out, 0, 768);
    unsigned old_base[64], new_base[64], after[64];
    memcpy(after, before, sizeof(unsigned) * 64);
    after[a] = before[b]; after[b] = before[a];
    for (unsigned i = 0, o = 0, w = 0; i < 64; i++) {
        old_base[i] = o; new_base[i] = w;
        o += before[i]; w += after[i];
    }
    for (unsigned i = 0; i < 64; i++) {
        unsigned from = (i == (unsigned)a) ? (unsigned)b : (i == (unsigned)b) ? (unsigned)a : i;
        for (unsigned k = 0; k < after[i]; k++) {
            unsigned oi = old_base[from] + k, ni = new_base[i] + k;
            if (3 * oi + 3 <= 768 && 3 * ni + 3 <= 768) memcpy(out + 3 * ni, table + 3 * oi, 3);
        }
    }
}

// The game addresses MOVERTAB.DAT and the switch-state table by cumulative counts over
// the lower-numbered rooms: a room's UNKNOWN_c is the number of objects in every valid room
// before it, and its switch base (UNKNOWN_f * 4 + the low two bits of _num_switches) the
// number of switches before it. Holds for every original room; a wrong base makes remote
// toggles miss and gives sprites another object's mover entry (verified in play 2026-09-15).
void recomputeBases(RoomFile *file) {
    unsigned objects = 0, switches = 0;
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i++) {
        Room *r = file->rooms + i;
        if (!r->valid) continue;
        r->data.UNKNOWN_c = (uint8_t)objects;
        r->data._num_switches = (uint8_t)((r->data.num_switches << 2) | (switches & 0x3));
        r->data.UNKNOWN_f = (uint8_t)(switches >> 2);
        objects += r->data.num_objects;
        switches += r->data.num_switches;
    }
}

// A TOGGLE_BLOCK chunk whose (x,y) falls in the 4-byte window just past the tile grid
// doesn't paint a tile at all: the game overwrites one of the room's own exit bytes with
// the chunk's `on`/`off` value instead (verified 2026-09-15; the GUI draws these as orange
// arrows to the on/off rooms' thumbnails). Returns which exit - 2 north, 3 east, 4 south,
// 5 west, matching room_north/east/south/west's own field order (point - 2) - or -1 for an
// ordinary tile block.
int exit_memory_offset(const struct SwitchChunk *chunk) {
    if (chunk->type != TOGGLE_BLOCK) return -1;
    int point = chunk->y * WIDTH_TILES + chunk->x - WIDTH_TILES * HEIGHT_TILES;
    return point >= 2 && point <= 5 ? point : -1;
}

// Swap rooms a and b (so that b can become the start room, say) keeping every link: exits
// into either room follow it, TOGGLE_BIT chunks aimed at either room follow it, exit-memory
// chunks (see exit_memory_offset) that rewrite to either room follow it too, and the index
// bases and toggle bits are recomputed for the new order. Room 0 stands for "no exit" in the
// exit bytes, so it cannot take part.
bool swapRooms(RoomFile *file, int a, int b) {
    if (a <= 0 || b <= 0 || a >= (int)C_ARRAY_LEN(file->rooms) || b >= (int)C_ARRAY_LEN(file->rooms)) {
        fprintf(stderr, "swapRooms: rooms must be 1..63\n");
        return false;
    }
    if (a == b) return true;
    Room tmp = file->rooms[a];
    file->rooms[a] = file->rooms[b];
    file->rooms[b] = tmp;
    file->rooms[a].index = (uint8_t)a;
    file->rooms[b].index = (uint8_t)b;
    for (size_t i = 0; i < C_ARRAY_LEN(file->rooms); i++) {
        Room *r = file->rooms + i;
        if (!r->valid) continue;
        uint8_t *exits[] = { &r->data.room_north, &r->data.room_east, &r->data.room_south, &r->data.room_west };
        for (size_t e = 0; e < C_ARRAY_LEN(exits); e++) {
            if (*exits[e] == a) *exits[e] = (uint8_t)b;
            else if (*exits[e] == b) *exits[e] = (uint8_t)a;
        }
        for (size_t sw = 0; sw < r->data.num_switches; sw++) {
            struct SwitchObject *s = r->data.switches + sw;
            for (size_t c = 1; c < s->chunks.length; c++) {
                struct SwitchChunk *chunk = s->chunks.data + c;
                if (chunk->type == TOGGLE_BIT) {
                    if (chunk->room_idx == a) chunk->room_idx = (uint16_t)b;
                    else if (chunk->room_idx == b) chunk->room_idx = (uint16_t)a;
                } else if (exit_memory_offset(chunk) >= 0) {
                    if (chunk->on == a) chunk->on = (uint8_t)b;
                    else if (chunk->on == b) chunk->on = (uint8_t)a;
                    if (chunk->off == a) chunk->off = (uint8_t)b;
                    else if (chunk->off == b) chunk->off = (uint8_t)a;
                }
            }
        }
        ARRAY_FREE(r->compressed);
    }
    recomputeBases(file);
    return recomputeToggleBits(file);
}

// force_room_id >= 0: import the first room found into that slot (ignore file's Room N id).
// force_room_id < 0: use the room id from the "Room N @..." header.
bool importRooms(FILE *fp, RoomFile *file, int force_room_id) {
    char line[1024];
    int room_id = -1;
    Room *room = NULL;
    int tile_row = -1;
    int obj_idx = -1;
    int obj_tile_row = -1;
    int sw_idx = -1;
    bool in_chunks = false;

    // Deferred TOGGLE_BIT entries: chunk pointers with the room name and switch_idx to resolve
    // after all rooms have been imported (handles forward references and blank-first imports).
    typedef struct { struct SwitchChunk *chunk; char room_name[64]; unsigned switch_idx; } DeferredTB;
    DeferredTB deferred[1024];
    int n_deferred = 0;

    while (fgets(line, sizeof(line), fp)) {
        strip_ansi(line);
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';

        // Room header: "Room N @0xADDR: "name""
        unsigned uid;
        if (sscanf(line, "Room %u:", &uid) == 1 && uid < 64) {
            // If force_room_id is set and we already imported one room, stop.
            if (force_room_id >= 0 && room != NULL) break;
            room_id = (force_room_id >= 0) ? force_room_id : (int)uid;
            room = &file->rooms[room_id];
            room->valid = true;
            room->index = (uint8_t)room_id;
            // Parse name from header: Room N @ADDR: "NAME"
            memset(room->data.name, ' ', sizeof(room->data.name));
            char *q = strchr(line, '"');
            if (q) {
                q++; // skip opening quote
                char *eq = strchr(q, '"');
                size_t nlen = eq ? (size_t)(eq - q) : strlen(q);
                if (nlen > sizeof(room->data.name)) nlen = sizeof(room->data.name);
                memcpy(room->data.name, q, nlen);
            }
            // Free dynamic data; fixed fields (tiles, metadata, name) will be overwritten below
            if (room->data.objects) {
                for (size_t i = 0; i < room->data.num_objects; i++)
                    if (room->data.objects[i].tiles) free(room->data.objects[i].tiles);
                free(room->data.objects);
                room->data.objects = NULL;
            }
            room->data.num_objects = 0;
            if (room->data.switches) {
                for (size_t i = 0; i < room->data.num_switches; i++)
                    ARRAY_FREE(room->data.switches[i].chunks);
                free(room->data.switches);
                room->data.switches = NULL;
            }
            room->data.num_switches = 0;
            room->data._num_switches = 0;
            ARRAY_FREE(room->rest);
            ARRAY_FREE(room->compressed);
            tile_row = -1; obj_idx = -1; obj_tile_row = -1; sw_idx = -1; in_chunks = false;
            continue;
        }

        if (room == NULL) continue;

        // Tiles label
        if (strcmp(line, "Tiles:") == 0) {
            tile_row = 0;
            memset(room->data.tiles, 0, sizeof(room->data.tiles));
            continue;
        }

        // Reading tile rows (HEIGHT_TILES rows, 2 chars per tile)
        if (tile_row >= 0 && tile_row < HEIGHT_TILES) {
            for (int x = 0; x < WIDTH_TILES; x++) {
                char a = (size_t)(x*2)   < len ? line[x*2]   : ' ';
                char b = (size_t)(x*2+1) < len ? line[x*2+1] : ' ';
                room->data.tiles[tile_row * WIDTH_TILES + x] =
                    (a == ' ' && b == ' ') ? 0 : parse_hex_pair(a, b);
            }
            tile_row++;
            if (tile_row == HEIGHT_TILES) tile_row = -1;
            continue;
        }

        // Empty line
        if (len == 0) continue;

        // Metadata: "bkgrnd: B, tiles: T, dmg: D, gravity (|): V, gravity (-): H"
        unsigned bkgrnd, tiles_v, grav_v, grav_h; int dmg;
        if (sscanf(line, "bkgrnd: %u, tiles: %u, dmg: %d, gravity (|): %u, gravity (-): %u",
                   &bkgrnd, &tiles_v, &dmg, &grav_v, &grav_h) == 5) {
            room->data.background = bkgrnd;
            room->data.tile_offset = tiles_v;
            room->data.room_damage = dmg;
            room->data.gravity_vertical = grav_v;
            room->data.gravity_horizontal = grav_h;
            continue;
        }

        // Neighbor links
        unsigned nb;
        if (sscanf(line, "left: %u",  &nb) == 1) { room->data.room_west  = nb; continue; }
        if (sscanf(line, "down: %u",  &nb) == 1) { room->data.room_south = nb; continue; }
        if (sscanf(line, "up: %u",    &nb) == 1) { room->data.room_north = nb; continue; }
        if (sscanf(line, "right: %u", &nb) == 1) { room->data.room_east  = nb; continue; }

        // UNKNOWN fields
        int ub, uc, ue, uf;
        if (sscanf(line, "UNKNOWN_b: %d, UNKNOWN_c: %d, UNKNOWN_e: %d, UNKNOWN_f: %d",
                   &ub, &uc, &ue, &uf) == 4) {
            room->data.UNKNOWN_b = ub;
            room->data.UNKNOWN_c = uc;
            room->data._num_switches = (room->data._num_switches & ~0x3) | (ue & 0x3);
            room->data.UNKNOWN_f = uf;
            continue;
        }

        // Objects header: "  objects (N):"
        unsigned num_obj;
        if (sscanf(line, "  objects (%u):", &num_obj) == 1) {
            room->data.num_objects = num_obj;
            room->data.objects = num_obj ? calloc(num_obj, sizeof(struct RoomObject)) : NULL;
            assert(room->data.objects != NULL || num_obj == 0);
            obj_idx = -1;
            continue;
        }

        // Block object tile rows (8-space indent, only while obj_tile_row >= 0)
        if (obj_tile_row >= 0 && len >= 8 && memcmp(line, "        ", 8) == 0) {
            struct RoomObject *obj = &room->data.objects[obj_idx];
            char *p = line + 8;
            size_t plen = len - 8;
            for (int x = 0; x < obj->block.width; x++) {
                char a = (size_t)(x*2)   < plen ? p[x*2]   : ' ';
                char b = (size_t)(x*2+1) < plen ? p[x*2+1] : ' ';
                obj->tiles[obj_tile_row * obj->block.width + x] =
                    (a == ' ' && b == ' ') ? 0 : parse_hex_pair(a, b);
            }
            obj_tile_row++;
            if (obj_tile_row >= obj->block.height) obj_tile_row = -1;
            continue;
        }
        obj_tile_row = -1;

        // Block object header: "    block I: (x,y)=X,Y (w,h)=W,H"
        long bi; int bx, by, bw, bh;
        if (sscanf(line, "    block %ld: (x,y)=%d,%d (w,h)=%d,%d", &bi, &bx, &by, &bw, &bh) == 5) {
            obj_idx = (int)bi;
            if (obj_idx < 0 || obj_idx >= (int)room->data.num_objects) {
                fprintf(stderr, "import: block %d out of range (num_objects=%u)\n", obj_idx, room->data.num_objects);
                return false;
            }
            struct RoomObject *obj = &room->data.objects[obj_idx];
            obj->type = BLOCK;
            obj->x = bx; obj->y = by;
            obj->block.width = bw; obj->block.height = bh;
            obj->tiles = calloc((size_t)(bw * bh), 1);
            assert(obj->tiles != NULL);
            obj_tile_row = 0;
            continue;
        }

        // Sprite object: "    sprite I: (x,y)=X,Y TYPENAME dmg:D"
        long si; int sx, sy, sdmg; char stype[32];
        if (sscanf(line, "    sprite %ld: (x,y)=%d,%d %31s dmg:%d", &si, &sx, &sy, stype, &sdmg) == 5) {
            obj_idx = (int)si;
            if (obj_idx < 0 || obj_idx >= (int)room->data.num_objects) {
                fprintf(stderr, "import: sprite %d out of range (num_objects=%u)\n", obj_idx, room->data.num_objects);
                return false;
            }
            struct RoomObject *obj = &room->data.objects[obj_idx];
            obj->type = SPRITE;
            obj->x = sx; obj->y = sy;
            obj->sprite.damage = sdmg;
            // A sprite doesn't need a tile under it - one there just blocks the sprite (Hugh: "the
            // import/export keeps tiles underneath the sprites... strip [them]").
            if (sx >= 0 && sx < WIDTH_TILES && sy >= 0 && sy < HEIGHT_TILES) room->data.tiles[TILE_IDX(sx, sy)] = 0;
            _Static_assert(NUM_SPRITE_TYPES == 8, "Unexpected number of sprite types");
            if      (strcmp(stype, "SHARK")        == 0) obj->sprite.type = SHARK;
            else if (strcmp(stype, "MUMMY")        == 0) obj->sprite.type = MUMMY;
            else if (strcmp(stype, "BLUE_MAN")     == 0) obj->sprite.type = BLUE_MAN;
            else if (strcmp(stype, "WOLF")         == 0) obj->sprite.type = WOLF;
            else if (strcmp(stype, "R2D2")         == 0) obj->sprite.type = R2D2;
            else if (strcmp(stype, "DINOSAUR")     == 0) obj->sprite.type = DINOSAUR;
            else if (strcmp(stype, "RAT")          == 0) obj->sprite.type = RAT;
            else if (strcmp(stype, "SHOTGUN_LADY") == 0) obj->sprite.type = SHOTGUN_LADY;
            else if (strcmp(stype, "MYSTERIO") == 0) {
                obj->sprite.type = SHARK;
                obj->sprite.damage = 3;
            } else if (strcmp(stype, "MARY_JANE") == 0) {
                obj->sprite.type = SHARK;
                obj->sprite.damage = 4;
            } else {
                fprintf(stderr, "import: unknown sprite type '%s'\n", stype);
                return false;
            }
            continue;
        }

        // Switches header: "  switches (N):"
        unsigned num_sw;
        if (sscanf(line, "  switches (%u):", &num_sw) == 1) {
            room->data.num_switches = num_sw;
            room->data._num_switches = (num_sw << 2) | (room->data._num_switches & 0x3);
            room->data.switches = num_sw ? calloc(num_sw, sizeof(struct SwitchObject)) : NULL;
            assert(room->data.switches != NULL || num_sw == 0);
            sw_idx = -1; in_chunks = false;
            continue;
        }

        // Switch header: "    switch I: (x,y)=X,Y (entry)=E (once)=O (side)=S"
        long swi; int swx, swy; char entry_s[8], once_s[8], side_s[8];
        if (sscanf(line, "    switch %ld: (x,y)=%d,%d (entry)=%7s (once)=%7s (side)=%7s",
                   &swi, &swx, &swy, entry_s, once_s, side_s) == 6) {
            sw_idx = (int)swi;
            if (sw_idx < 0 || sw_idx >= (int)room->data.num_switches) {
                fprintf(stderr, "import: switch %d out of range (num_switches=%u)\n", sw_idx, room->data.num_switches);
                return false;
            }
            bool room_entry    = strncmp(entry_s, "true", 4) == 0;
            bool one_time_use  = strncmp(once_s,  "true", 4) == 0;
            _Static_assert(NUM_SIDES == 4, "Unexpected number of switch sides");
            enum SwitchSide side = TOP;
            if      (strncmp(side_s, "RIGHT",  5) == 0) side = RIGHT;
            else if (strncmp(side_s, "BOTTOM", 6) == 0) side = BOTTOM;
            else if (strncmp(side_s, "LEFT",   4) == 0) side = LEFT;
            struct SwitchObject *sw = &room->data.switches[sw_idx];
            ARRAY_FREE(sw->chunks);
            ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                .type = PREAMBLE, .x = swx, .y = swy,
                .room_entry = room_entry, .one_time_use = one_time_use, .side = side
            }));
            in_chunks = false;
            continue;
        }

        // Chunks label
        if (strcmp(line, "      chunks:") == 0) { in_chunks = true; continue; }

        // Chunk line: "        (N) type ..."
        if (in_chunks && sw_idx >= 0 && len > 9 && memcmp(line, "        (", 9) == 0) {
            struct SwitchObject *sw = &room->data.switches[sw_idx];
            char *type_p = strstr(line, ") type ");
            if (!type_p) continue;
            type_p += 7; // skip ") type "

            _Static_assert(NUM_CHUNK_TYPES == 4, "Unexpected number of chunk types");
            if (strncmp(type_p, "block: ", 7) == 0) {
                // TOGGLE_BLOCK: "(x,y)=X,Y (width|height)=S (on/off)=HH/HH"
                int cx, cy, csize; unsigned con, coff; char dir_s[8];
                if (sscanf(type_p, "block: (x,y)=%d,%d (%7[^)])=%d (on/off)=%x/%x",
                           &cx, &cy, dir_s, &csize, &con, &coff) == 6) {
                    enum SwitchChunkDirection dir = strncmp(dir_s, "height", 6) == 0 ? VERTICAL : HORIZONTAL;
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                        .type = TOGGLE_BLOCK, .x = cx, .y = cy, .size = csize,
                        .dir = dir, .on = (uint8_t)con, .off = (uint8_t)coff
                    }));
                }
            } else if (strncmp(type_p, "toggle: ", 8) == 0) {
                // TOGGLE_BIT: "(room) NAME (switch) IDX (on/off)=D/D"
                // Parse room name (may contain spaces) by finding " (switch) "
                const char *p = type_p + 8;
                if (strncmp(p, "(room) ", 7) != 0) continue;
                p += 7;
                const char *sw_marker = strstr(p, " (switch) ");
                if (!sw_marker) continue;
                size_t name_len = (size_t)(sw_marker - p);
                char room_name[64] = {0};
                if (name_len >= sizeof(room_name)) name_len = sizeof(room_name) - 1;
                memcpy(room_name, p, name_len);
                unsigned switch_idx; int ton, toff;
                if (sscanf(sw_marker + 10, "%u (on/off)=%d/%d", &switch_idx, &ton, &toff) != 3) continue;
                // Defer name→room_id resolution until all rooms are parsed
                ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                    .type = TOGGLE_BIT, .on = ton, .off = toff,
                }));
                if (n_deferred < (int)(sizeof(deferred)/sizeof(deferred[0]))) {
                    deferred[n_deferred].chunk = sw->chunks.data + sw->chunks.length - 1;
                    deferred[n_deferred].switch_idx = switch_idx;
                    if (name_len >= sizeof(deferred[n_deferred].room_name))
                        name_len = sizeof(deferred[n_deferred].room_name) - 1;
                    memcpy(deferred[n_deferred].room_name, room_name, name_len);
                    deferred[n_deferred].room_name[name_len] = '\0';
                    n_deferred++;
                }
            } else if (strncmp(type_p, "object: ", 8) == 0) {
                // TOGGLE_OBJECT: "idx=I value=0xHH test=N"
                int oidx, otest; unsigned oval;
                if (sscanf(type_p, "object: idx=%d value=0x%x test=%d", &oidx, &oval, &otest) == 3) {
                    ARRAY_ADD(sw->chunks, ((struct SwitchChunk){
                        .type = TOGGLE_OBJECT, .index = (uint8_t)oidx,
                        .value = (uint8_t)oval, .test = (uint8_t)(otest << 4)
                    }));
                }
            }
            continue;
        }

        // Rest data: "  Left over data (length=N): [0xHH, ...]"
        if (strncmp(line, "  Left over data", 16) == 0) {
            ARRAY_FREE(room->rest);
            char *p = strchr(line, '[');
            if (p) {
                p++;
                while (*p && *p != ']') {
                    unsigned val;
                    if (sscanf(p, "0x%x", &val) == 1)
                        ARRAY_ADD(room->rest, (uint8_t)val);
                    p = strchr(p, ',');
                    if (p) p++; else break;
                }
            }
            continue;
        }
    }

    // Resolve deferred TOGGLE_BIT entries now that all rooms have been imported
    for (int d = 0; d < n_deferred; d++) {
        DeferredTB *dt = &deferred[d];
        uint16_t target_room = 0xFFFF;
        for (size_t r = 0; r < C_ARRAY_LEN(file->rooms); r++) {
            if (!file->rooms[r].valid) continue;
            char trimmed[25] = {0};
            strncpy(trimmed, file->rooms[r].data.name, 24);
            for (int j = 23; j >= 0; j--)
                if (trimmed[j] == ' ' || trimmed[j] == '\0') trimmed[j] = '\0'; else break;
            if (strcmp(trimmed, dt->room_name) == 0) { target_room = (uint16_t)r; break; }
        }
        if (target_room == 0xFFFF) {
            fprintf(stderr, "import: TOGGLE_BIT target room '%s' not found\n", dt->room_name);
            return false;
        }
        dt->chunk->room_idx = target_room;
        dt->chunk->switch_idx = dt->switch_idx;
        // chunk->index and chunk->bitmask are set by recomputeToggleBits() before writing
    }

    return true;
}

bool tile_hurts(uint8_t tile) {
    if ((tile & 0xC0) == 0xC0) return false;   // slippery: "won't hurt"
    uint8_t idx = tile & 0x3F;
    return idx == 0x01 || (idx >= 0x1C && idx <= 0x21) || idx >= 0x3C;
}

const char *data_path(const char *dir, const char *name) {
    static char bufs[4][4096];
    static unsigned next = 0;
    char *buf = bufs[next++ % 4];
    if (dir == NULL || *dir == '\0') dir = ".";
    size_t n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/') n--;
    snprintf(buf, sizeof bufs[0], "%.*s/%s", (int)n, dir, name);
    return buf;
}

bool readRooms(RoomFile *file, const char *dir) {
    const char *path = data_path(dir, ROOMS_FILE);
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for reading.\n", path);
        return false;
    }
    bool ret = readFile(file, fp);
    fclose(fp);
    return ret;
}

bool writeRooms(RoomFile *file, const char *dir, int *failed_room, char *dangling_msg, size_t dangling_msg_size) {
    if (failed_room) *failed_room = -1;
    // Every room's object/switch index base is a cumulative count over lower-numbered
    // rooms (recomputeBases' own comment), so it only ever depends on the file's current
    // layout, never on history: recompute it on every save rather than trusting whatever
    // a room happened to arrive with (e.g. from being built by cloning another room, or
    // from switches/objects added one at a time in the interactive editor, which - unlike
    // swap/import - never touched these fields on its own).
    recomputeBases(file);
    for (size_t idx = 0; idx < C_ARRAY_LEN(file->rooms); idx ++) {
        Room *r = file->rooms + idx;
        if (!r->valid) continue;
        for (size_t sw = 0; sw < r->data.num_switches; sw ++) {
            struct SwitchObject *switcch = r->data.switches + sw;
            for (size_t c = 1; c < switcch->chunks.length; c ++) {
                struct SwitchChunk *chunk = switcch->chunks.data + c;
                if (chunk->type != TOGGLE_BIT) continue;
                uint16_t index = 0;
                uint8_t mask = 0;
                uint8_t bitmasks[] = {0x01, 0x04, 0x10, 0x40};

                for (size_t _idx = 0; _idx < C_ARRAY_LEN(file->rooms); _idx ++) {
                    Room *_r = file->rooms + _idx;
                    if (!_r->valid) continue;
                    for (size_t _sw = 0; _sw < _r->data.num_switches; _sw ++) {
                        if (chunk->room_idx == _idx && chunk->switch_idx == _sw) {
                            chunk->index = index;
                            chunk->bitmask = bitmasks[mask];
                            goto next;
                        }
                        mask ++;
                        if (mask == C_ARRAY_LEN(bitmasks)) {
                            mask = 0;
                            index ++;
                        }
                    }
                }
                if (chunk->room_idx == 0 && chunk->switch_idx == 0) {
                    chunk->index = 0;
                    chunk->bitmask = 0x1;
                    goto next;
                }
                // A caller that takes dangling_msg reports this itself (the editor's status line);
                // printing it here as well lands on top of the TUI's map.
                if (!dangling_msg)
                    fprintf(stderr, "Could not find switch for room %lu switch %lu chunk %lu pointing to room %u switch %u\n", idx, sw, c, chunk->room_idx, chunk->switch_idx);
                if (failed_room) *failed_room = -2;   // distinct from -1 (whole file over its total limit): not a size problem at all
                if (dangling_msg) {
                    snprintf(dangling_msg, dangling_msg_size,
                             "room %02zx switch %zu toggles a switch that no longer exists (room %02x switch %u)",
                             idx, sw, chunk->room_idx, chunk->switch_idx);
                }
                return false;
next:
                {}
            }
        }
    }

    // Write beside the file and rename over it only when everything fitted,
    // so a refused write leaves the last good ROOMS.SPL in place.
    const char *path = data_path(dir, ROOMS_FILE);
    const char *tmp = data_path(dir, ROOMS_FILE ".tmp");
    FILE *fp = fopen(tmp, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open %s for writing.\n", tmp);
        return false;
    }
    bool ret = writeFile(file, fp, failed_room);
    if (fclose(fp) != 0) ret = false;
    if (!ret) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "Could not replace %s: %s\n", path, strerror(errno));
        remove(tmp);
        return false;
    }
    return true;
}
