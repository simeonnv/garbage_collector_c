#ifndef ALLOCATION_MAP_H
#define ALLOCATION_MAP_H

#include "allocation.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AllocationMap {
    size_t capacity;
    size_t min_capacity;
    double downsize_factor;
    double upsize_factor;
    double sweep_factor;
    size_t sweep_limit;
    size_t size;
    Allocation** allocs;
} AllocationMap;



double gc_allocation_map_load_factor(AllocationMap* am);

AllocationMap* gc_allocation_map_new(size_t min_capacity,
    size_t capacity,
    double sweep_factor,
    double downsize_factor,
    double upsize_factor);

void gc_allocation_map_delete(AllocationMap* am);

void gc_allocation_map_resize(AllocationMap* am, size_t new_capacity);

bool gc_allocation_map_resize_to_fit(AllocationMap* am);

Allocation* gc_allocation_map_get(AllocationMap* am, void* ptr);

Allocation* gc_allocation_map_put(AllocationMap* am,
    void* ptr,
    size_t size,
    void (*dtor)(void*));

void gc_allocation_map_remove(AllocationMap* am,
    void* ptr,
    bool allow_resize);

#endif
