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
