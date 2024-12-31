#ifndef ARRAY_H
#define ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#define C_ARRAY_LEN(a) (sizeof((a)) / sizeof((a)[0]))

#define ARRAY(type) struct { \
        size_t capacity; \
        size_t length; \
        type *data; \
    }

#define ARRAY_ADD(a, datum) do { \
    if ((a).length == (a).capacity) { \
        size_t new_cap = (a).capacity == 0 ? 16 : (a).capacity * 2; \
        void *new_data = realloc((a).data, new_cap * sizeof((a).data[0])); \
        if (new_data == NULL) break; \
        (a).capacity = new_cap; \
        (a).data = new_data; \
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
