#ifndef ALLOCATION_H
#define ALLOCATION_H

#include <stddef.h>
typedef struct Allocation {
    void* ptr;                // mem pointer
    size_t size;              // allocated size in bytes
    char tag;                 // the tag for mark-and-sweep
    void (*dtor)(void*);      // destructor
    struct Allocation* next;  // separate chaining
} Allocation;

Allocation* gc_allocation_new(void* ptr, size_t size, void (*dtor)(void*));
void gc_allocation_delete(Allocation* a);

#endif
