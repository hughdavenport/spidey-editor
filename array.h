#ifndef ARRAY_H
#define ARRAY_H

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#define C_ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))

#define ARRAY(type) struct { \
        size_t capacity; \
        size_t length; \
        type *data; \
    }

#define ARRAY_ENSURE(a, size) do { \
    if ((size) <= (a).capacity) break; \
    void *new_data = realloc((a).data, (size) * sizeof((a).data[0])); \
    assert(new_data != NULL); \
    memset((uint8_t*)new_data + (a).capacity * sizeof((a).data[0]), 0, (size - (a).capacity) * sizeof((a).data[0])); \
    (a).capacity = (size); \
    (a).data = new_data; \
} while (false)

#define ARRAY_ADD(a, datum) do { \
    if ((a).length == (a).capacity) { \
        size_t new_cap = (a).capacity == 0 ? 16 : (a).capacity * 2; \
        ARRAY_ENSURE((a), new_cap); \
    } \
    (a).data[(a).length ++] = (datum); \
} while (false)

#define ARRAY_FREE(a) do { \
    free((a).data); \
    (a).data = NULL; \
    (a).capacity = 0; \
    (a).length = 0; \
} while (false)
#endif /* ARRAY_H */
