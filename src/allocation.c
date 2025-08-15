#include "allocation.h"
#include "gc.h"
#include <stddef.h>
#include <stdlib.h>

Allocation* gc_allocation_new(void* ptr, size_t size, void (*dtor)(void*)) {
    Allocation* a = (Allocation*) malloc(sizeof(Allocation));
    a->ptr = ptr;
    a->size = size;
    a->tag = GC_TAG_NONE;
    a->dtor = dtor;
    a->next = NULL;
    return a;
}

void gc_allocation_delete(Allocation* a) {
    free(a);
}
