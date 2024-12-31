#ifndef IO_H
#define IO_H

#include <stdio.h>

long filesize(FILE *fp);

#endif /* IO_H */

#ifdef IO_IMPLEMENTATION
long filesize(FILE *fp) {
    long tell = ftell(fp);
    if (fseek(fp, 0, SEEK_END) < 0) return -1;
    long length = ftell(fp);
    if (fseek(fp, tell, SEEK_SET) < 0) return -1;
    return length;
}
#endif /* IO_IMPLEMENTATION */
