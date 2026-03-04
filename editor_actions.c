#include "editor_types.h"
#include <string.h>
#include <stdlib.h>

/* ── Undo/redo helpers ─────────────────────────────────────────────────── */

static void room_snapshot_copy(struct DecompressedRoom *dst,
                               const struct DecompressedRoom *src) {
    *dst = *src;  /* copies all inline fields (tiles[], name, metadata, etc.) */

    /* Deep-copy objects */
    if (src->num_objects && src->objects) {
        dst->objects = malloc(src->num_objects * sizeof(*dst->objects));
        assert(dst->objects != NULL);
        memcpy(dst->objects, src->objects, src->num_objects * sizeof(*dst->objects));
        for (uint8_t i = 0; i < src->num_objects; i++) {
            if (src->objects[i].type == BLOCK && src->objects[i].tiles) {
                size_t n = (size_t)src->objects[i].block.width
                         * src->objects[i].block.height;
                dst->objects[i].tiles = malloc(n);
                assert(dst->objects[i].tiles != NULL);
                memcpy(dst->objects[i].tiles, src->objects[i].tiles, n);
            } else {
                dst->objects[i].tiles = NULL;
            }
        }
    } else {
        dst->objects = NULL;
    }

    /* Deep-copy switches */
    if (src->num_switches && src->switches) {
        dst->switches = malloc(src->num_switches * sizeof(*dst->switches));
        assert(dst->switches != NULL);
        for (uint8_t i = 0; i < src->num_switches; i++) {
            dst->switches[i] = src->switches[i];
            size_t n = src->switches[i].chunks.length;
            if (n) {
                dst->switches[i].chunks.data = malloc(n * sizeof(struct SwitchChunk));
                assert(dst->switches[i].chunks.data != NULL);
                memcpy(dst->switches[i].chunks.data, src->switches[i].chunks.data,
                       n * sizeof(struct SwitchChunk));
                dst->switches[i].chunks.capacity = n;
            } else {
                dst->switches[i].chunks.data = NULL;
                dst->switches[i].chunks.capacity = 0;
            }
        }
    } else {
        dst->switches = NULL;
    }
}

static void room_snapshot_free(struct DecompressedRoom *snap) {
    if (snap->objects) {
        for (uint8_t i = 0; i < snap->num_objects; i++)
            free(snap->objects[i].tiles);
        free(snap->objects);
        snap->objects = NULL;
    }
    if (snap->switches) {
        for (uint8_t i = 0; i < snap->num_switches; i++)
            free(snap->switches[i].chunks.data);
        free(snap->switches);
        snap->switches = NULL;
    }
}

void push_undo(uint8_t room_idx, bool grouped) {
    /* Clear redo stack */
    for (size_t i = 0; i < state->redo_stack.length; i++)
        room_snapshot_free(&state->redo_stack.data[i].snapshot);
    state->redo_stack.length = 0;

    UndoEntry entry = { .room_idx = room_idx, .grouped = grouped };
    room_snapshot_copy(&entry.snapshot, &state->rooms.rooms[room_idx].data);
    ARRAY_ADD(state->undo_stack, entry);
}

/* Pop undo_stack, restore snapshot, push current state to redo_stack.
 * Groups (grouped=true on top entry) pop multiple entries atomically.
 * Returns room_idx of last restored room, or 255 if nothing to undo. */
uint8_t do_undo(void) {
    if (state->undo_stack.length == 0) return 255;

    size_t redo_start = state->redo_stack.length;
    uint8_t last_room = 255;

    do {
        UndoEntry *e = &state->undo_stack.data[state->undo_stack.length - 1];
        bool keep_going = e->grouped;

        /* Save current room state to redo stack */
        UndoEntry redo = { .room_idx = e->room_idx, .grouped = false };
        room_snapshot_copy(&redo.snapshot, &state->rooms.rooms[e->room_idx].data);
        ARRAY_ADD(state->redo_stack, redo);

        /* Restore snapshot (transfers ownership of pointers) */
        room_snapshot_free(&state->rooms.rooms[e->room_idx].data);
        state->rooms.rooms[e->room_idx].data = e->snapshot;
        ARRAY_FREE(state->rooms.rooms[e->room_idx].compressed);
        e->snapshot.objects = NULL;
        e->snapshot.switches = NULL;

        last_room = e->room_idx;
        state->undo_stack.length--;

        if (!keep_going) break;
    } while (state->undo_stack.length > 0);

    /* Fix grouped flag on redo entries so do_redo pops the whole group */
    size_t redo_count = state->redo_stack.length - redo_start;
    if (redo_count > 1)
        state->redo_stack.data[state->redo_stack.length - 1].grouped = true;

    assert(writeRooms(&state->rooms));
    return last_room;
}

uint8_t do_redo(void) {
    if (state->redo_stack.length == 0) return 255;

    size_t undo_start = state->undo_stack.length;
    uint8_t last_room = 255;

    do {
        UndoEntry *e = &state->redo_stack.data[state->redo_stack.length - 1];
        bool keep_going = e->grouped;

        UndoEntry undo = { .room_idx = e->room_idx, .grouped = false };
        room_snapshot_copy(&undo.snapshot, &state->rooms.rooms[e->room_idx].data);
        ARRAY_ADD(state->undo_stack, undo);

        room_snapshot_free(&state->rooms.rooms[e->room_idx].data);
        state->rooms.rooms[e->room_idx].data = e->snapshot;
        ARRAY_FREE(state->rooms.rooms[e->room_idx].compressed);
        e->snapshot.objects = NULL;
        e->snapshot.switches = NULL;

        last_room = e->room_idx;
        state->redo_stack.length--;

        if (!keep_going) break;
    } while (state->redo_stack.length > 0);

    size_t undo_count = state->undo_stack.length - undo_start;
    if (undo_count > 1)
        state->undo_stack.data[state->undo_stack.length - 1].grouped = true;

    assert(writeRooms(&state->rooms));
    return last_room;
}


void free_undo_stacks(void) {
    for (size_t i = 0; i < state->undo_stack.length; i++)
        room_snapshot_free(&state->undo_stack.data[i].snapshot);
    ARRAY_FREE(state->undo_stack);
    for (size_t i = 0; i < state->redo_stack.length; i++)
        room_snapshot_free(&state->redo_stack.data[i].snapshot);
    ARRAY_FREE(state->redo_stack);
}

/* Helper: given dx/dy and object position/size, compute neighbour room index and
   landing position. Returns false if no edge crossing (normal in-bounds move). */
static bool object_crosses_edge(int obj_x, int obj_y, int obj_w, int obj_h,
                                 int dx, int dy,
                                 struct DecompressedRoom *room,
                                 size_t *nbr_idx_out, int *new_x_out, int *new_y_out) {
    if (dx == -1 && obj_x == 0)
        { *nbr_idx_out = room->room_west;  *new_x_out = WIDTH_TILES - obj_w; *new_y_out = obj_y; return true; }
    if (dx ==  1 && obj_x + obj_w == WIDTH_TILES)
        { *nbr_idx_out = room->room_east;  *new_x_out = 0;                   *new_y_out = obj_y; return true; }
    if (dy == -1 && obj_y == 0)
        { *nbr_idx_out = room->room_north; *new_x_out = obj_x; *new_y_out = HEIGHT_TILES - obj_h; return true; }
    if (dy ==  1 && obj_y + obj_h == HEIGHT_TILES)
        { *nbr_idx_out = room->room_south; *new_x_out = obj_x; *new_y_out = 0; return true; }
    return false;
}

static void transfer_object(struct RoomObject *object, size_t nbr_idx, int new_x, int new_y) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct DecompressedRoom *nbr  = &state->rooms.rooms[nbr_idx].data;
    nbr->objects = realloc(nbr->objects, (nbr->num_objects + 1) * sizeof(*nbr->objects));
    nbr->objects[nbr->num_objects] = *object;
    nbr->objects[nbr->num_objects].x = new_x;
    nbr->objects[nbr->num_objects].y = new_y;
    if (object->type == BLOCK && object->tiles) {
        size_t n = object->block.width * object->block.height;
        nbr->objects[nbr->num_objects].tiles = malloc(n);
        memcpy(nbr->objects[nbr->num_objects].tiles, object->tiles, n);
    } else {
        nbr->objects[nbr->num_objects].tiles = NULL;
    }
    nbr->num_objects++;
    ARRAY_FREE(state->rooms.rooms[nbr_idx].compressed);
    if (object->type == BLOCK) free(object->tiles);
    size_t obj_i = object - room->objects;
    memmove(object, object + 1, (room->num_objects - obj_i - 1) * sizeof(*object));
    room->num_objects--;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    state->current_level = nbr_idx;
    state->cursors[nbr_idx].x = new_x;
    state->cursors[nbr_idx].y = new_y;
    assert(writeRooms(&state->rooms));
}

static void transfer_switch(struct SwitchObject *switcch, size_t nbr_idx, int new_x, int new_y) {
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    struct DecompressedRoom *nbr  = &state->rooms.rooms[nbr_idx].data;
    int x = switcch->chunks.data[0].x;
    int y = switcch->chunks.data[0].y;
    size_t chunk_n = switcch->chunks.length;
    nbr->switches = realloc(nbr->switches, (nbr->num_switches + 1) * sizeof(struct SwitchObject));
    size_t new_sw_i = nbr->num_switches;
    nbr->switches[new_sw_i].chunks.length   = chunk_n;
    nbr->switches[new_sw_i].chunks.capacity = chunk_n;
    nbr->switches[new_sw_i].chunks.data = malloc(chunk_n * sizeof(struct SwitchChunk));
    memcpy(nbr->switches[new_sw_i].chunks.data, switcch->chunks.data, chunk_n * sizeof(struct SwitchChunk));
    nbr->switches[new_sw_i].chunks.data[0].x = new_x;
    nbr->switches[new_sw_i].chunks.data[0].y = new_y;
    nbr->tiles[TILE_IDX(new_x, new_y)] = room->tiles[TILE_IDX(x, y)];
    room->tiles[TILE_IDX(x, y)] = 0;
    nbr->num_switches++;
    ARRAY_FREE(state->rooms.rooms[nbr_idx].compressed);
    size_t sw_i = switcch - room->switches;
    free(switcch->chunks.data);
    memmove(switcch, switcch + 1, (room->num_switches - sw_i - 1) * sizeof(struct SwitchObject));
    room->num_switches--;
    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
    state->current_level = nbr_idx;
    state->cursors[nbr_idx].x = new_x;
    state->cursors[nbr_idx].y = new_y;
    assert(writeRooms(&state->rooms));
}

static bool sprite_at(struct DecompressedRoom *room, struct RoomObject *exclude, int x, int y) {
    for (size_t k = 0; k < room->num_objects; k++) {
        if (room->objects + k != exclude &&
                room->objects[k].type == SPRITE &&
                room->objects[k].x == x && room->objects[k].y == y)
            return true;
    }
    return false;
}

void move(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool moved = false;
    if (state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        bool sw = false;
        for (size_t i = 0; i < room->num_switches; i++) {
            switcch = room->switches + i;
            if (switcch->chunks.length > 0 && switcch->chunks.data[0].type == PREAMBLE &&
                    x == switcch->chunks.data[0].x && y == switcch->chunks.data[0].y) {
                sw = true;
                break;
            }
        }
        if (sw) {
            size_t nbr_idx; int new_x, new_y;
            if (object_crosses_edge(x, y, 1, 1, dx, dy, room, &nbr_idx, &new_x, &new_y)) {
                if (nbr_idx != state->current_level) {
                    push_undo((uint8_t)nbr_idx, false);
                    push_undo((uint8_t)state->current_level, true);
                    transfer_switch(switcch, nbr_idx, new_x, new_y);
                } else {
                    /* Self-loop: wrap to opposite edge within same room */
                    push_undo((uint8_t)state->current_level, false);
                    room->tiles[TILE_IDX(new_x, new_y)] = room->tiles[TILE_IDX(x, y)];
                    room->tiles[TILE_IDX(x, y)] = 0;
                    switcch->chunks.data[0].x = new_x;
                    switcch->chunks.data[0].y = new_y;
                    state->cursors[state->current_level].x = new_x;
                    state->cursors[state->current_level].y = new_y;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                }
            } else {
                push_undo((uint8_t)state->current_level, false);
                room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
                room->tiles[TILE_IDX(x, y)] = 0;
                switcch->chunks.data[0].x += dx;
                switcch->chunks.data[0].y += dy;
                state->cursors[state->current_level].x += dx;
                state->cursors[state->current_level].y += dy;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }
            moved = true;
        } else {
            struct SwitchChunk *chunk = NULL;
            bool ch = false;
            for (size_t i = 0; i < room->num_switches; i++) {
                switcch = room->switches + i;
                for (size_t c = 1; c < switcch->chunks.length; c++) {
                    chunk = switcch->chunks.data + c;
                    if (chunk->type == TOGGLE_BLOCK &&
                            (chunk->dir == HORIZONTAL ?
                                 y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size :
                                 x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size)) {
                        ch = true;
                        break;
                    }
                }
                if (ch) {
                    int chunk_w = chunk->dir == HORIZONTAL ? (int)chunk->size : 1;
                    int chunk_h = chunk->dir == VERTICAL   ? (int)chunk->size : 1;
                    push_undo((uint8_t)state->current_level, false);
                    if (dx == -1 && chunk->x == 0)
                        chunk->x = WIDTH_TILES - chunk_w;
                    else if (dx ==  1 && chunk->x + chunk_w == WIDTH_TILES)
                        chunk->x = 0;
                    else if (dy == -1 && chunk->y == 0)
                        chunk->y = HEIGHT_TILES - chunk_h;
                    else if (dy ==  1 && chunk->y + chunk_h == HEIGHT_TILES)
                        chunk->y = 0;
                    else {
                        chunk->x += dx;
                        chunk->y += dy;
                    }
                    /* Follow cursor's offset within chunk; clamp on wrap */
                    int cx = x + dx, cy = y + dy;
                    if (cx < chunk->x) cx = chunk->x;
                    if (cx >= chunk->x + chunk_w) cx = chunk->x + chunk_w - 1;
                    if (cy < chunk->y) cy = chunk->y;
                    if (cy >= chunk->y + chunk_h) cy = chunk->y + chunk_h - 1;
                    state->cursors[state->current_level].x = cx;
                    state->cursors[state->current_level].y = cy;
                    ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                    assert(writeRooms(&state->rooms));
                    moved = true;
                    break;
                }
            }
        }
    }
    if (!moved && state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        /* Prefer sprite at cursor over block */
        for (size_t i = 0; i < room->num_objects; i++) {
            struct RoomObject *o = room->objects + i;
            if (o->type == SPRITE && x == o->x && y == o->y) {
                object = o; obj = true; break;
            }
        }
        if (!obj) for (size_t i = 0; i < room->num_objects; i++) {
            struct RoomObject *o = room->objects + i;
            if (o->type == BLOCK &&
                    x >= o->x && x < o->x + o->block.width &&
                    y >= o->y && y < o->y + o->block.height) {
                object = o; obj = true; break;
            }
        }
        if (obj) {
            int obj_w = (object->type == BLOCK) ? object->block.width  : 1;
            int obj_h = (object->type == BLOCK) ? object->block.height : 1;
            size_t nbr_idx; int new_x, new_y;
            if (object_crosses_edge(object->x, object->y, obj_w, obj_h, dx, dy, room,
                                    &nbr_idx, &new_x, &new_y)) {
                if (nbr_idx != state->current_level) {
                    if (object->type == SPRITE &&
                            sprite_at(&state->rooms.rooms[nbr_idx].data, NULL, new_x, new_y)) {
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite would overlap");
                    } else {
                        push_undo((uint8_t)nbr_idx, false);
                        push_undo((uint8_t)state->current_level, true);
                        transfer_object(object, nbr_idx, new_x, new_y);
                    }
                } else {
                    /* Self-loop: wrap to opposite edge within same room */
                    if (object->type == SPRITE && sprite_at(room, object, new_x, new_y)) {
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite would overlap");
                    } else {
                        push_undo((uint8_t)state->current_level, false);
                        object->x = new_x;
                        object->y = new_y;
                        state->cursors[state->current_level].x = new_x;
                        state->cursors[state->current_level].y = new_y;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }
                }
            } else {
                /* Normal in-bounds move */
                bool in_bounds = (int)object->x + dx >= 0 &&
                                 (int)object->x + obj_w + dx <= WIDTH_TILES &&
                                 (int)object->y + dy >= 0 &&
                                 (int)object->y + obj_h + dy <= HEIGHT_TILES;
                if (in_bounds) {
                    if (object->type == SPRITE &&
                            sprite_at(room, object, object->x + dx, object->y + dy)) {
                        snprintf(state->status_message, sizeof(state->status_message),
                                 "Sprite would overlap");
                    } else {
                        push_undo((uint8_t)state->current_level, false);
                        object->x += dx;
                        object->y += dy;
                        state->cursors[state->current_level].x += dx;
                        state->cursors[state->current_level].y += dy;
                        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                        assert(writeRooms(&state->rooms));
                    }
                }
            }
            moved = true;
        }
    }
    if (!moved) {
        /* Plain tile: cross-room or in-bounds */
        size_t nbr_idx; int new_x, new_y;
        if (object_crosses_edge(x, y, 1, 1, dx, dy, room, &nbr_idx, &new_x, &new_y)) {
            if (nbr_idx != state->current_level) {
                push_undo((uint8_t)nbr_idx, false);
                push_undo((uint8_t)state->current_level, true);
                struct DecompressedRoom *nbr = &state->rooms.rooms[nbr_idx].data;
                nbr->tiles[TILE_IDX(new_x, new_y)] = room->tiles[TILE_IDX(x, y)];
                room->tiles[TILE_IDX(x, y)] = 0;
                ARRAY_FREE(state->rooms.rooms[nbr_idx].compressed);
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                state->current_level = nbr_idx;
                state->cursors[nbr_idx].x = new_x;
                state->cursors[nbr_idx].y = new_y;
                assert(writeRooms(&state->rooms));
            } else {
                /* Self-loop: wrap to opposite edge within same room */
                push_undo((uint8_t)state->current_level, false);
                room->tiles[TILE_IDX(new_x, new_y)] = room->tiles[TILE_IDX(x, y)];
                room->tiles[TILE_IDX(x, y)] = 0;
                state->cursors[state->current_level].x = new_x;
                state->cursors[state->current_level].y = new_y;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
            }
        } else if (x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
            push_undo((uint8_t)state->current_level, false);
            room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
            room->tiles[TILE_IDX(x, y)] = 0;
            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
        }
    }
}

void stretch(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool stretched = false;
    if (state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        struct SwitchChunk *chunk = NULL;
        bool ch = false;
        for (size_t i = 0; i < room->num_switches; i ++) {
            switcch = room->switches + i;
            for (size_t c = 1; c < switcch->chunks.length; c ++) {
                chunk = switcch->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK &&
                        y >= chunk->y && x >= chunk->x &&
                        chunk->x + dx >= 0 && chunk->y + dy >= 0) {
                    if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                        // expand this one
                        if (chunk->size < 8 && 
                                ((dy && x == chunk->x && y < chunk->y + chunk->size && chunk->y + chunk->size + dy <= HEIGHT_TILES) ||
                                 (dx && y == chunk->y && x < chunk->x + chunk->size && chunk->x + chunk->size + dx <= WIDTH_TILES))) {
                            ch = true;
                            break;
                        }
                    } else if ((dy && y == chunk->y && x < chunk->x + chunk->size) ||
                            (dx && x == chunk->x && y < chunk->y + chunk->size)) {
                        // create new chunk
                        ch = true;
                        break;
                    }
                }
            }
            if (ch) {
                push_undo((uint8_t)state->current_level, false);
                if ((dy && chunk->dir == VERTICAL) || (dx && chunk->dir == HORIZONTAL)) {
                    // Expand other blocks if next to this?
                    chunk->size ++;
                    if (dy < 0) chunk->y --;
                    if (dx < 0) chunk->x --;
                } else {
                    // expand to the next row/column, making a new chunk
                    // there may already be one here so find the edge of the "block"
                    int _x = chunk->x + dx;
                    int _y = chunk->y + dy;
                    bool conflict = true;
                    while (conflict) {
                        conflict = false;
                        for (size_t i = 1; i < switcch->chunks.length; i ++) {
                            if (switcch->chunks.data[i].x == _x && switcch->chunks.data[i].y == _y &&
                                    switcch->chunks.data[i].type == TOGGLE_BLOCK &&
                                    switcch->chunks.data[i].on == chunk->on &&
                                    switcch->chunks.data[i].off == chunk->off &&
                                    switcch->chunks.data[i].size == chunk->size) {
                                _x += dx;
                                _y += dy;
                                conflict = true;
                                break;
                            }
                        }
                    }
                    ARRAY_ADD(switcch->chunks, ((struct SwitchChunk){ .type = TOGGLE_BLOCK,
                                .x = _x, .y = _y, .size = chunk->size,
                                .on = chunk->on, .off = chunk->off, .dir = chunk->dir }));
                }
                state->cursors[state->current_level].x += dx;
                state->cursors[state->current_level].y += dy;
                ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
                assert(writeRooms(&state->rooms));
                stretched = true;
                break;
            }
        }
    }
    if (!stretched && state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        for (size_t i = 0; i < room->num_objects; i ++) {
            object = room->objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height &&
                    (int)object->x + dx >= 0 && (int)object->x + object->block.width + dx <= WIDTH_TILES &&
                    (int)object->y + dy >= 0 && (int)object->y + object->block.height + dy <= HEIGHT_TILES) {
                if ((dx && object->block.width < 8) || (dy && object->block.height < 4)) {
                    obj = true;
                    break;
                }
            }
        }
        if (obj) {
            assert(object->type == BLOCK);
            assert(object->tiles);
            push_undo((uint8_t)state->current_level, false);
            if (dx) {
                // copy column
                uint8_t *tiles = malloc((object->block.width + 1) * object->block.height);
                assert(tiles != NULL);
                for (size_t _y = 0; _y < object->block.height; _y ++) {
                    if (x - object->x > 0) {
                        memcpy(tiles + _y * (object->block.width + 1),
                                object->tiles + _y * object->block.width,
                                x - object->x);
                    }
                    tiles[_y * (object->block.width + 1) + (x - object->x)] = object->tiles[_y * object->block.width + (x - object->x)];
                    if (dx < 0) {
                        memcpy(tiles + _y * (object->block.width + 1) + (x - object->x) - dx,
                                object->tiles + _y * object->block.width + (x - object->x),
                                object->block.width - (x - object->x));
                    } else {
                        memcpy(tiles + _y * (object->block.width + 1) + (x - object->x) + 1,
                                object->tiles + _y * object->block.width + (x - object->x),
                                object->block.width - (x - object->x));
                    }
                }
                object->block.width ++;
                if (dx < 0) object->x += dx;
                free(object->tiles);
                object->tiles = tiles;
            } else {
                // copy row
                uint8_t *tiles = malloc(object->block.width * (object->block.height + 1));
                memset(tiles, 1, object->block.width * (object->block.height + 1));
                assert(tiles != NULL);
                memcpy(tiles, object->tiles, (y - object->y + 1) * object->block.width);
                memcpy(tiles + (y - object->y + 1) * object->block.width, object->tiles + (y - object->y) * object->block.width, (object->block.height - (y - object->y)) * object->block.width);
                object->block.height ++;
                if (dy < 0) object->y += dy;
                free(object->tiles);
                object->tiles = tiles;
            }
            state->cursors[state->current_level].x += dx;
            state->cursors[state->current_level].y += dy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
            stretched = true;
        }
    }
    if (!stretched && x + dx >= 0 && x + dx < WIDTH_TILES && y + dy >= 0 && y + dy < HEIGHT_TILES) {
        push_undo((uint8_t)state->current_level, false);
        room->tiles[TILE_IDX(x + dx, y + dy)] = room->tiles[TILE_IDX(x, y)];
        state->cursors[state->current_level].x += dx;
        state->cursors[state->current_level].y += dy;
        ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
        assert(writeRooms(&state->rooms));
    }
}

void shrink(int dx, int dy) {
    int x = state->cursors[state->current_level].x;
    int y = state->cursors[state->current_level].y;
    struct DecompressedRoom *room = &state->rooms.rooms[state->current_level].data;
    bool shrank = false;
    if (state->debug.switches) {
        struct SwitchObject *switcch = NULL;
        struct SwitchChunk *chunk = NULL;
        bool ch = false;
        for (size_t i = 0; i < room->num_switches; i++) {
            switcch = room->switches + i;
            for (size_t c = 1; c < switcch->chunks.length; c++) {
                chunk = switcch->chunks.data + c;
                if (chunk->type == TOGGLE_BLOCK && chunk->size > 1 &&
                        ((dy && chunk->dir == VERTICAL &&
                          x == chunk->x && y >= chunk->y && y < chunk->y + chunk->size) ||
                         (dx && chunk->dir == HORIZONTAL &&
                          y == chunk->y && x >= chunk->x && x < chunk->x + chunk->size))) {
                    ch = true;
                    break;
                }
            }
            if (ch) break;
        }
        if (ch) {
            push_undo((uint8_t)state->current_level, false);
            chunk->size--;
            if (dy > 0) chunk->y++;  /* remove top: origin moves down */
            if (dx > 0) chunk->x++;  /* remove left: origin moves right */
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
            shrank = true;
        }
    }
    if (!shrank && state->debug.objects) {
        struct RoomObject *object = NULL;
        bool obj = false;
        for (size_t i = 0; i < room->num_objects; i++) {
            object = room->objects + i;
            if (object->type == BLOCK &&
                    x >= object->x && x < object->x + object->block.width &&
                    y >= object->y && y < object->y + object->block.height &&
                    ((dx && object->block.width > 1) || (dy && object->block.height > 1))) {
                obj = true;
                break;
            }
        }
        if (obj) {
            assert(object->type == BLOCK);
            assert(object->tiles);
            push_undo((uint8_t)state->current_level, false);
            if (dx) {
                /* Press right (dx=1): pull leftmost column inward → remove left edge
                 * Press left (dx=-1): pull rightmost column inward → remove right edge */
                size_t new_w = object->block.width - 1;
                uint8_t *tiles = malloc(new_w * object->block.height);
                assert(tiles != NULL);
                for (size_t _y = 0; _y < object->block.height; _y++) {
                    if (dx > 0) {
                        /* Remove leftmost column: keep cols 1..width-1 */
                        memcpy(tiles + _y * new_w,
                               object->tiles + _y * object->block.width + 1,
                               new_w);
                    } else {
                        /* Remove rightmost column: keep cols 0..new_w-1 */
                        memcpy(tiles + _y * new_w,
                               object->tiles + _y * object->block.width,
                               new_w);
                    }
                }
                if (dx > 0) object->x++;
                object->block.width = new_w;
                free(object->tiles);
                object->tiles = tiles;
            } else {
                /* Press down (dy=1): pull top row inward → remove top edge
                 * Press up (dy=-1): pull bottom row inward → remove bottom edge */
                size_t new_h = object->block.height - 1;
                uint8_t *tiles = malloc(object->block.width * new_h);
                assert(tiles != NULL);
                if (dy > 0) {
                    /* Remove top row: keep rows 1..height-1 */
                    memcpy(tiles, object->tiles + object->block.width,
                           object->block.width * new_h);
                    object->y++;
                } else {
                    /* Remove bottom row: keep rows 0..new_h-1 */
                    memcpy(tiles, object->tiles,
                           object->block.width * new_h);
                }
                object->block.height = new_h;
                free(object->tiles);
                object->tiles = tiles;
            }
            /* Clamp cursor so it stays on the (now smaller) object */
            int cx = state->cursors[state->current_level].x;
            int cy = state->cursors[state->current_level].y;
            if (cx < (int)object->x) cx = (int)object->x;
            if (cx >= (int)(object->x + object->block.width))
                cx = (int)(object->x + object->block.width) - 1;
            if (cy < (int)object->y) cy = (int)object->y;
            if (cy >= (int)(object->y + object->block.height))
                cy = (int)(object->y + object->block.height) - 1;
            state->cursors[state->current_level].x = cx;
            state->cursors[state->current_level].y = cy;
            ARRAY_FREE(state->rooms.rooms[state->current_level].compressed);
            assert(writeRooms(&state->rooms));
            shrank = true;
        }
    }
    /* No tile fallback: shrink on plain tiles is a no-op */
    (void)shrank;
}
