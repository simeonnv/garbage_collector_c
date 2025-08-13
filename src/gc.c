#include "gc_internal.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <vec.h>

static void** allocated_blocks = NULL;
static size_t allocated_count = 0;

void* gc_malloc(size_t size) {
    if (!allocated_blocks) {
        allocated_blocks = vector_create();
    }
    void* ptr = malloc(size);
    if (ptr) {
        vector_add(&allocated_blocks, ptr);
        allocated_count++;
    }
    return ptr;
}

void gc_collect(void) {
    for (size_t i = 0; i < allocated_count; i++) {
        free(allocated_blocks[i]);
    }
    vector_free(allocated_blocks);
    allocated_blocks = vector_create();
    allocated_count = 0;
}

void gc_free_all(void) {
    gc_collect();
}
