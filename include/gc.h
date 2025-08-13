#ifndef GC_H
#define GC_H

#include <stddef.h>

void* gc_malloc(size_t size);
void  gc_collect(void);
void  gc_free_all(void);

#endif
