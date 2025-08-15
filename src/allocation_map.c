#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "allocation.h"
#include "allocation_map.h"
#include "log.h"

bool is_prime(size_t n) {
    if (n <= 3)
        return n > 1;
    else if (n % 2==0 || n % 3==0)
        return false;
    else {
        for (size_t i=5; i*i<=n; i+=6) {
            if (n % i == 0 || n%(i + 2) == 0)
                return false;
        }
        return true;
    }
}

size_t next_prime(size_t n) {
    while (!is_prime(n)) ++n;
    return n;
}

double gc_allocation_map_load_factor(AllocationMap* am) {
    return (double) am->size / (double) am->capacity;
}

AllocationMap* gc_allocation_map_new(size_t min_capacity,
        size_t capacity,
        double sweep_factor,
        double downsize_factor,
        double upsize_factor) {
    AllocationMap* am = (AllocationMap*) malloc(sizeof(AllocationMap));
    am->min_capacity = next_prime(min_capacity);
    am->capacity = next_prime(capacity);
    if (am->capacity < am->min_capacity) am->capacity = am->min_capacity;
    am->sweep_factor = sweep_factor;
    am->sweep_limit = (int) (sweep_factor * am->capacity);
    am->downsize_factor = downsize_factor;
    am->upsize_factor = upsize_factor;
    am->allocs = (Allocation**) calloc(am->capacity, sizeof(Allocation*));
    am->size = 0;
    LOG_DEBUG("Created allocation map (cap=%ld, siz=%ld)", am->capacity, am->size);
    return am;
}

void gc_allocation_map_delete(AllocationMap* am) {
    // Iterate over the map
    LOG_DEBUG("Deleting allocation map (cap=%ld, siz=%ld)",
              am->capacity, am->size);
    Allocation *alloc, *tmp;
    for (size_t i = 0; i < am->capacity; ++i) {
        if ((alloc = am->allocs[i])) {
            // Make sure to follow the chain inside a bucket
            while (alloc) {
                tmp = alloc;
                alloc = alloc->next;
                // free the management structure
                gc_allocation_delete(tmp);
            }
        }
    }
    free(am->allocs);
    free(am);
}

size_t gc_hash(void *ptr) {
    return ((uintptr_t)ptr) >> 3;
}

void gc_allocation_map_resize(AllocationMap* am, size_t new_capacity) {
    if (new_capacity <= am->min_capacity) {
        return;
    }

    LOG_DEBUG("Resizing allocation map (cap=%ld, siz=%ld) -> (cap=%ld)",
              am->capacity, am->size, new_capacity);
    Allocation** resized_allocs = calloc(new_capacity, sizeof(Allocation*));

    for (size_t i = 0; i < am->capacity; ++i) {
        Allocation* alloc = am->allocs[i];
        while (alloc) {
            Allocation* next_alloc = alloc->next;
            size_t new_index = gc_hash(alloc->ptr) % new_capacity;
            alloc->next = resized_allocs[new_index];
            resized_allocs[new_index] = alloc;
            alloc = next_alloc;
        }
    }
    free(am->allocs);
    am->capacity = new_capacity;
    am->allocs = resized_allocs;
    am->sweep_limit = am->size + am->sweep_factor * (am->capacity - am->size);
}

bool gc_allocation_map_resize_to_fit(AllocationMap* am) {
    double load_factor = gc_allocation_map_load_factor(am);
    if (load_factor > am->upsize_factor) {
        LOG_DEBUG("Load factor %0.3g > %0.3g. Triggering upsize.",
                  load_factor, am->upsize_factor);
        gc_allocation_map_resize(am, next_prime(am->capacity * 2));
        return true;
    }
    if (load_factor < am->downsize_factor) {
        LOG_DEBUG("Load factor %0.3g < %0.3g. Triggering downsize.",
                  load_factor, am->downsize_factor);
        gc_allocation_map_resize(am, next_prime(am->capacity / 2));
        return true;
    }
    return false;
}

Allocation* gc_allocation_map_get(AllocationMap* am, void* ptr) {
    size_t index = gc_hash(ptr) % am->capacity;
    Allocation* cur = am->allocs[index];
    while(cur) {
        if (cur->ptr == ptr) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

Allocation* gc_allocation_map_put(AllocationMap* am,
        void* ptr,
        size_t size,
        void (*dtor)(void*)) {
    size_t index = gc_hash(ptr) % am->capacity;
    LOG_DEBUG("PUT request for allocation ix=%ld", index);
    Allocation* alloc = gc_allocation_new(ptr, size, dtor);
    Allocation* cur = am->allocs[index];
    Allocation* prev = NULL;
    /* Upsert if ptr is already known (e.g. dtor update). */
    while(cur != NULL) {
        if (cur->ptr == ptr) {
            // found it
            alloc->next = cur->next;
            if (!prev) {
                // position 0
                am->allocs[index] = alloc;
            } else {
                // in the list
                prev->next = alloc;
            }
            gc_allocation_delete(cur);
            LOG_DEBUG("AllocationMap Upsert at ix=%ld", index);
            return alloc;

        }
        prev = cur;
        cur = cur->next;
    }
    cur = am->allocs[index];
    alloc->next = cur;
    am->allocs[index] = alloc;
    am->size++;
    LOG_DEBUG("AllocationMap insert at ix=%ld", index);
    void* p = alloc->ptr;
    if (gc_allocation_map_resize_to_fit(am)) {
        alloc = gc_allocation_map_get(am, p);
    }
    return alloc;
}

void gc_allocation_map_remove(AllocationMap* am,
                                     void* ptr,
                                     bool allow_resize) {
    // ignores unknown keys
    size_t index = gc_hash(ptr) % am->capacity;
    Allocation* cur = am->allocs[index];
    Allocation* prev = NULL;
    Allocation* next;
    while(cur != NULL) {
        next = cur->next;
        if (cur->ptr == ptr) {
            // found it
            if (!prev) {
                // first item in list
                am->allocs[index] = cur->next;
            } else {
                // not the first item in the list
                prev->next = cur->next;
            }
            gc_allocation_delete(cur);
            am->size--;
        } else {
            // move on
            prev = cur;
        }
        cur = next;
    }
    if (allow_resize) {
        gc_allocation_map_resize_to_fit(am);
    }
}
