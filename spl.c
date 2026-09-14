#include "spl.h"

bool spl_decompress(FILE *fp, size_t size, const uint8_t markers[4],
                    uint8_array *out, uint8_array *compressed) {
#define read_next(dst) do { \
        int next_val = fgetc(fp); \
        if (next_val == EOF) return false; \
        size--; \
        if (compressed != NULL) ARRAY_ADD(*compressed, (uint8_t)next_val); \
        (dst) = (uint8_t)next_val; \
} while (false)

    while (size) {
        uint8_t val;
        read_next(val);

        if (val == markers[1]) {
            /* RLE */
            uint8_t next;
            read_next(next);
            if (next < 0x80) {
                /* repeat the last output byte (next + 2) times */
                if (out->length < 1) return false;
                uint8_t copy = out->data[out->length - 1];
                for (int i = 0; i < next + 2; i++) ARRAY_ADD(*out, copy);
            } else if (next != 0x80) {
                /* copy abs(next) bytes from out - 0x20 */
                if (out->length < 0x20) return false;
                for (int i = 0; i < -(int8_t)next; i++) {
                    uint8_t copy = out->data[out->length - 0x20];
                    ARRAY_ADD(*out, copy);
                }
            } else {
                ARRAY_ADD(*out, val);
            }
        } else if (val == markers[2]) {
            /* LZ */
            uint8_t next;
            read_next(next);
            if (next < 0x80) {
                /* store markers[0] (next + 2) times */
                for (int i = 0; i < next + 2; i++) ARRAY_ADD(*out, markers[0]);
            } else if (next != 0x80) {
                /* copy abs(next)+1 bytes from out - back */
                uint8_t back;
                read_next(back);
                if (out->length < back) return false;
                for (int i = 0; i < -(int8_t)next + 1; i++) {
                    uint8_t copy = out->data[out->length - back];
                    ARRAY_ADD(*out, copy);
                }
            } else {
                ARRAY_ADD(*out, val);
            }
        } else if (val == markers[3]) {
            /* larger back-index LZ */
            uint8_t next;
            read_next(next);
            if (next < 0x80) {
                uint16_t back;
                read_next(back);
                back |= 0x200;
                if (out->length < back) return false;
                for (int i = 0; i < next + 2; i++) {
                    uint8_t copy = out->data[out->length - back];
                    ARRAY_ADD(*out, copy);
                }
            } else if (next != 0x80) {
                uint16_t back;
                read_next(back);
                back |= 0x100;
                if (out->length < back) return false;
                for (int i = 0; i < -(int8_t)next + 1; i++) {
                    uint8_t copy = out->data[out->length - back];
                    ARRAY_ADD(*out, copy);
                }
            } else {
                ARRAY_ADD(*out, val);
            }
        } else {
            /* literal */
            ARRAY_ADD(*out, val);
        }
    }
    return true;

#undef read_next
}

/* The compressor writeRoom() grew: marker[1..3] are picked to dodge the data,
 * then each step emits the longest of: a fixed 0x20-back copy, a run, a run
 * of marker0, an LZ copy (near / mid / far by distance), an escaped marker, or
 * the literal byte. */
size_t spl_compress(const uint8_t *in, size_t n, uint8_t marker0, uint8_t *out, size_t cap) {
#define log(...) do {} while (false)
    size_t d_idx = 0;
    size_t c_len = 0;
    if (cap < 5) return 0;
    out[c_len++] = marker0;
    out[c_len++] = '\0';
    out[c_len++] = '\0';
    out[c_len++] = '\0';

    // Really any markers can be chosen. But this chooses 3 that are hopefully not used
    // It will only be the same as a used byte if it overwraps
    for (size_t i = 0; i < n; ++ i) {
        if (in[i] == out[1]) {
            if (in[i] == 254) {
                out[1] = 1;
                break;
            }
            out[1] ++;
            i = 0;
            continue;
        }
    }
    out[2] = out[1] + 1;
    for (size_t i = 0; i < n; ++ i) {
        if (in[i] == out[2]) {
            if (in[i] == 254) {
                out[2] = 2;
                break;
            }
            out[2] ++;
            i = 0;
            continue;
        }
    }
    out[3] = out[2] + 1;
    for (size_t i = 0; i < n; ++ i) {
        if (in[i] == out[3]) {
            if (in[i] == 254) {
                out[3] = 3;
                break;
            }
            out[3] ++;
            i = 0;
            continue;
        }
    }
    out[4] = out[3] + 1;
    for (size_t i = 0; i < n; ++ i) {
        if (in[i] == out[4]) {
            if (in[i] == 254) {
                out[4] = 4;
                break;
            }
            out[4] ++;
            i = 0;
            continue;
        }
    }

    // Options for outputting
    // 1- Output as is, if byte not one of the markers
    // 2- Output RLE encoded byte, send marker[1] and length < 0x80. Copies last in byte length + 2 times
    // 3- Copy fixed size block, send marker[1] and (int8_t)length < 0 != 0x80. Copy last 0x20 bytes -(int8_t)length times
    // 4- Output marker[1], send marker[1] and 0x80
    // 5- Output marker[0], send marker[2] and length < 0x80. Copies room marker (0x8f, but could be different?) length + 2 times
    // 6- LZ Output, send marker[2] and (int8_t)length < 0 != 0x80 and backindex. Copy last backindex bytes -(int8_t)length + 1 times
    // 7- Output marker[2], send marker[2] and 0x80
    // 8- LZ Far output, send marker[3] and length < 0x80 and backindex. Copy 0x200 | backindex bytes length + 2 times
    // 9- LZ Mid output, send marker[3] and (int8_t)length < 0 != 0x80 and backindex. Copy 0x100 | backindex bytes -(int8_t)length + 1 times
    // 10- Output marker[3], send marker[3] and 0x80
    while (d_idx < n) {
        if (c_len + 3 > cap) return 0;          /* every step emits at most 3 bytes */
        uint8_t byte = in[d_idx++];
        uint8_t length = 1;
        log("%s:%d: Compressing byte %x @ %lu. Compressed so far %lu.\n",
                __FILE__, __LINE__, byte, d_idx - 1, c_len);
        uint8_t best = 0;
        uint16_t best_back = 0;
        for (size_t back = 1; back < d_idx - 1 && back < 0x300; back ++) {
            if (in[d_idx - back - 1] == byte) {
                length = 1;
                while (d_idx + length - 1 < n && length - 1 < 0x7e) {
                    if (back == 0) break;
                    if (in[d_idx + length - 1] != in[d_idx - back + (length % back) - 1]) {
                        break;
                    }
                    length ++;
                }
                if (length > best) {
                    if (back < 0x100 && length - 2 > 0x7e) length = 0x80;
                    best = length;
                    best_back = back;
                    if (length - 1 >= 0x7e) break;
                }
            }
        }
        length = 1;
        if (d_idx >= 0x20 + 1 && in[d_idx - 0x20 - 1] == byte) {
            while (d_idx + length - 1 < n && length < 0x7f) {
                if (in[d_idx + length - 1] != in[d_idx - 0x20 + (length % 0x20) - 1]) {
                    break;
                }
                length ++;
            }
            // 3- Copy fixed size block, send marker[1] and (int8_t)length < 0 != 0x80. Copy last 0x20 bytes -(int8_t)length times
            log("%s:%d: Fixed size 0x20 block, length %u (%x).\n",
                    __FILE__, __LINE__, length, -(int8_t)length);
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                out[c_len++] = out[1];
                out[c_len++] = -(int8_t)length;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        }
        length = 1;
        if (d_idx >= 2 && byte == in[d_idx - 2]) {
            while (d_idx + length - 1 < n && length - 2 < 0x7e) {
                if (in[d_idx + length - 1] != byte) break;
                length ++;
            }
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                // 2- Output RLE encoded byte, send marker[1] and length < 0x80. Copies last in byte length + 2 times
                log("%s:%d: RLE length %u (%x).\n",
                        __FILE__, __LINE__, length, length - 2);
                out[c_len++] = out[1];
                out[c_len++] = length - 2;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        }
        if (byte == out[0]) {
            while (d_idx + length - 1 < n && length - 2 < 0x7e) {
                if (in[d_idx + length - 1] != byte) break;
                length ++;
            }
            if (length > 2 && (best < 3 || (best - 3) < (length - 2))) {
                // 5- Output marker[0], send marker[2] and length < 0x80. Copies room marker (0x8f, but could be different?) length + 2 times
                log("%s:%d: RLE marker[0] length %u (%x).\n",
                        __FILE__, __LINE__, length, length - 2);
                out[c_len++] = out[2];
                out[c_len++] = length - 2;
                d_idx += length - 1; // We had already read 1 at start of loop
                continue;
            }
        } 
        if (best > 3) { // Is it worth it to compress?
            if (best_back < 0x100) {
                // 6- LZ Output, send marker[2] and (int8_t)length < 0 != 0x80 and backindex. Copy last backindex bytes -(int8_t)length + 1 times
                log("%s:%d: LZ length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                out[c_len++] = out[2];
                out[c_len++] = -(int8_t)(best - 1);
                out[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x200) {
                // 9- LZ Mid output, send marker[3] and (int8_t)length < 0 != 0x80 and backindex. Copy 0x100 | backindex bytes -(int8_t)length + 1 times
                log("%s:%d: LZ Mid length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, -(int8_t)(best - 1), best_back, best_back);
                out[c_len++] = out[3];
                out[c_len++] = -(int8_t)(best - 1);
                out[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            } else if (best_back < 0x300) {
                // 8- LZ Far output, send marker[3] and length < 0x80 and backindex. Copy 0x200 | backindex bytes length + 2 times
                log("%s:%d: LZ Far length %u (%x), back %u (%x).\n",
                        __FILE__, __LINE__, best, best - 2, best_back, best_back);
                out[c_len++] = out[3];
                out[c_len++] = best - 2;
                out[c_len++] = (uint8_t)(best_back & 0xFF);
                d_idx += best - 1; // We already read 1 at start of loop
                continue;
            }
        }

        log("%s:%d: Output as is (or escaped if one of markers).\n", __FILE__, __LINE__);
        if (byte == out[1]) {
            // 4- Output marker[1], send marker[1] and 0x80
            out[c_len++] = out[1];
            out[c_len++] = 0x80;
            continue;
        } else if (byte == out[2]) {
            // 7- Output marker[2], send marker[2] and 0x80
            out[c_len++] = out[2];
            out[c_len++] = 0x80;
            continue;
        } else if (byte == out[3]) {
            // 10- Output marker[3], send marker[3] and 0x80
            out[c_len++] = out[3];
            out[c_len++] = 0x80;
            continue;
        }
        // 1- Output as is, if byte not one of the markers
        out[c_len++] = byte;
    }

    return c_len;
#undef log
}
