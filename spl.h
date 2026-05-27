#ifndef SPL_H
#define SPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "array.h"

/*
 * Decompress one room's stream from a .SPL file. ROOMS.SPL and BACKS.SPL share
 * this LZ/RLE format; only the start marker differs (ROOMS.SPL uses 0x8F).
 *
 * The caller reads the 4 control markers itself (so it can apply any
 * format-specific checks, e.g. ROOMS.SPL requiring markers[0] == 0x8F) and
 * passes them in. `fp` must be positioned just past those 4 markers, and
 * `size` is the remaining compressed byte budget for this room.
 *
 * Decompressed bytes are appended to *out; the raw bytes consumed are appended
 * to *compressed (may be NULL to skip). Returns false on EOF / corrupt stream;
 * the caller owns freeing the (partially filled) arrays.
 */
bool spl_decompress(FILE *fp, size_t size, const uint8_t markers[4],
                    uint8_array *out, uint8_array *compressed);

#endif /* SPL_H */
