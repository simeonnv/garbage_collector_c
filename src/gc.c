#include "gc.h"
#include "log.h"
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "allocation.h"
#include "allocation_map.h"

#undef LOGLEVEL
#define LOGLEVEL LOGLEVEL_INFO

/*
 * The size of a pointer, no matter the architecture (i love C).
 */
#define PTRSIZE sizeof(char*)

static void** allocated_blocks = NULL;
static size_t allocated_count = 0;

#ifndef GC_NO_GLOBAL_GC
GarbageCollector gc; // global GC object
#endif

/*
 * Apparently the windowc C compiler needs this to work
 */
#if defined(_MSC_VER)
#define __builtin_frame_address(x)  ((void)(x), _AddressOfReturnAddress())
#endif

static void* gc_mcalloc(size_t count, size_t size) {
    if (!count) return malloc(size);
    return calloc(count, size);
}

static bool gc_needs_sweep(GarbageCollector* gc) {
    return gc->allocs->size > gc->allocs->sweep_limit;
}

static void* gc_allocate(GarbageCollector* gc, size_t count, size_t size, void(*dtor)(void*)) {
    /* Allocation logic that generalizes over malloc/calloc. */

    /* Check if we reached the high-water mark and need to clean up */
    if (gc_needs_sweep(gc) && !gc->paused) {
        size_t freed_mem = gc_run(gc);
        LOG_DEBUG("Garbage collection cleaned up %lu bytes.", freed_mem);
    }
    /* With cleanup out of the way, attempt to allocate memory */
    void* ptr = gc_mcalloc(count, size);
    size_t alloc_size = count ? count * size : size;

    /* If allocation fails, force an out-of-policy run to free some memory and try again. */
    if (!ptr && !gc->paused && (errno == EAGAIN || errno == ENOMEM)) {
        gc_run(gc);
        ptr = gc_mcalloc(count, size);
    }
    /* Start managing the memory we received from the system */
    if (ptr) {
        LOG_DEBUG("Allocated %zu bytes at %p", alloc_size, (void*) ptr);
        Allocation* alloc = gc_allocation_map_put(gc->allocs, ptr, alloc_size, dtor);
        /* Deal with metadata allocation failure */
        if (alloc) {
            LOG_DEBUG("Managing %zu bytes at %p", alloc_size, (void*) alloc->ptr);
            ptr = alloc->ptr;
        } else {
            /* We failed to allocate the metadata, fail cleanly. */
            free(ptr);
            ptr = NULL;
        }
    }
    return ptr;
}

static void gc_make_root(GarbageCollector* gc, void* ptr) {
    Allocation* alloc = gc_allocation_map_get(gc->allocs, ptr);
    if (alloc) {
        alloc->tag |= GC_TAG_ROOT;
    }
}

void* gc_malloc_ext(GarbageCollector* gc, size_t size, void(*dtor)(void*)) {
    return gc_allocate(gc, 0, size, dtor);
}

void* gc_malloc(GarbageCollector* gc, size_t size) {
    return gc_malloc_ext(gc, size, NULL);
}

void* gc_malloc_static(GarbageCollector* gc, size_t size, void(*dtor)(void*)) {
    void* ptr = gc_malloc_ext(gc, size, dtor);
    gc_make_root(gc, ptr);
    return ptr;
}

void* gc_make_static(GarbageCollector* gc, void* ptr) {
    gc_make_root(gc, ptr);
    return ptr;
}

void* gc_calloc_ext(GarbageCollector* gc, size_t count, size_t size,
                    void(*dtor)(void*)) {
    return gc_allocate(gc, count, size, dtor);
}

void* gc_calloc(GarbageCollector* gc, size_t count, size_t size) {
    return gc_calloc_ext(gc, count, size, NULL);
}

void* gc_realloc(GarbageCollector* gc, void* p, size_t size) {
    Allocation* alloc = gc_allocation_map_get(gc->allocs, p);
    if (p && !alloc) {
        // the user passed an unknown pointer
        errno = EINVAL;
        return NULL;
    }
    void* q = realloc(p, size);
    if (!q) {
        // realloc failed but p is still valid
        return NULL;
    }
    if (!p) {
        // allocation, not reallocation
        Allocation* alloc = gc_allocation_map_put(gc->allocs, q, size, NULL);
        return alloc->ptr;
    }
    if (p == q) {
        // successful reallocation w/o copy
        alloc->size = size;
    } else {
        // successful reallocation w/ copy
        void (*dtor)(void*) = alloc->dtor;
        gc_allocation_map_remove(gc->allocs, p, true);
        gc_allocation_map_put(gc->allocs, q, size, dtor);
    }
    return q;
}

void gc_free(GarbageCollector* gc, void* ptr) {
    Allocation* alloc = gc_allocation_map_get(gc->allocs, ptr);
    if (alloc) {
        if (alloc->dtor) {
            alloc->dtor(ptr);
        }
        free(ptr);
        gc_allocation_map_remove(gc->allocs, ptr, true);
    } else {
        LOG_WARNING("Ignoring request to free unknown pointer %p", (void*) ptr);
    }
}


void gc_start_ext(GarbageCollector* gc,
    void* bos,
    size_t initial_capacity,
    size_t min_capacity,
    double downsize_load_factor,
    double upsize_load_factor,
    double sweep_factor) {

    double downsize_limit = downsize_load_factor > 0.0 ? downsize_load_factor : 0.2;
    double upsize_limit = upsize_load_factor > 0.0 ? upsize_load_factor : 0.8;
    sweep_factor = sweep_factor > 0.0 ? sweep_factor : 0.5;
    gc->paused = false;
    gc->bos = bos;
    initial_capacity = initial_capacity < min_capacity ? min_capacity : initial_capacity;
    gc->allocs = gc_allocation_map_new(min_capacity, initial_capacity,
                                       sweep_factor, downsize_limit, upsize_limit);
    LOG_DEBUG("Created new garbage collector (cap=%ld, siz=%ld).", gc->allocs->capacity,
              gc->allocs->size);
}

void gc_start(GarbageCollector* gc, void* bos) {
    gc_start_ext(gc, bos, 1024, 1024, 0.2, 0.8, 0.5);
}

void gc_pause(GarbageCollector* gc)
{
    gc->paused = true;
}

void gc_resume(GarbageCollector* gc)
{
    gc->paused = false;
}

void gc_mark_alloc(GarbageCollector* gc, void* ptr)
{
    Allocation* alloc = gc_allocation_map_get(gc->allocs, ptr);
    /* Mark if alloc exists and is not tagged already, otherwise skip */
    if (alloc && !(alloc->tag & GC_TAG_MARK)) {
        LOG_DEBUG("Marking allocation (ptr=%p)", ptr);
        alloc->tag |= GC_TAG_MARK;
        /* Iterate over allocation contents and mark them as well */
        LOG_DEBUG("Checking allocation (ptr=%p, size=%lu) contents", ptr, alloc->size);
        for (char* p = (char*) alloc->ptr;
                p <= (char*) alloc->ptr + alloc->size - PTRSIZE;
                ++p) {
            LOG_DEBUG("Checking allocation (ptr=%p) @%lu with value %p",
                      ptr, p-((char*) alloc->ptr), *(void**)p);
            gc_mark_alloc(gc, *(void**)p);
        }
    }
}

void gc_mark_stack(GarbageCollector* gc)
{
    LOG_DEBUG("Marking the stack (gc@%p) in increments of %ld \n", (void*) gc, sizeof(char));
    void *tos = __builtin_frame_address(0);
    void *bos = gc->bos;
    printf("Top of stack is %p, bottom is %p \n", tos, bos);
    /* The stack grows towards smaller memory addresses, hence we scan tos->bos.
     * Stop scanning once the distance between tos & bos is too small to hold a valid pointer */
    for (char* p = tos; p >= (char*)bos + PTRSIZE; p--) {
         gc_mark_alloc(gc, *(void**)p);
    }
}

void gc_mark_roots(GarbageCollector* gc)
{
    LOG_DEBUG("Marking roots%s", "");
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        Allocation* chunk = gc->allocs->allocs[i];
        while (chunk) {
            if (chunk->tag & GC_TAG_ROOT) {
                LOG_DEBUG("Marking root @ %p", chunk->ptr);
                gc_mark_alloc(gc, chunk->ptr);
            }
            chunk = chunk->next;
        }
    }
}

void gc_mark(GarbageCollector* gc)
{
    /* Note: We only look at the stack and the heap, and ignore BSS. */
    LOG_DEBUG("Initiating GC mark (gc@%p)", (void*) gc);
    /* Scan the heap for roots */
    gc_mark_roots(gc);
    /* Dump registers onto stack and scan the stack */
    void (*volatile _mark_stack)(GarbageCollector*) = gc_mark_stack;
    jmp_buf ctx;
    memset(&ctx, 0, sizeof(jmp_buf));
    setjmp(ctx);
    _mark_stack(gc);
}

size_t gc_sweep(GarbageCollector* gc)
{
    LOG_DEBUG("Initiating GC sweep (gc@%p)", (void*) gc);
    size_t total = 0;
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        Allocation* chunk = gc->allocs->allocs[i];
        Allocation* next = NULL;
        /* Iterate over separate chaining */
        while (chunk) {
            if (chunk->tag & GC_TAG_MARK) {
                LOG_DEBUG("Found used allocation %p (ptr=%p)", (void*) chunk, (void*) chunk->ptr);
                /* unmark */
                chunk->tag &= ~GC_TAG_MARK;
                chunk = chunk->next;
            } else {
                LOG_DEBUG("Found unused allocation %p (%lu bytes @ ptr=%p)", (void*) chunk, chunk->size, (void*) chunk->ptr);
                /* no reference to this chunk, hence delete it */
                total += chunk->size;
                if (chunk->dtor) {
                    chunk->dtor(chunk->ptr);
                }
                free(chunk->ptr);
                /* and remove it from the bookkeeping */
                next = chunk->next;
                gc_allocation_map_remove(gc->allocs, chunk->ptr, false);
                chunk = next;
            }
        }
    }
    gc_allocation_map_resize_to_fit(gc->allocs);
    return total;
}

void gc_unroot_roots(GarbageCollector* gc)
{
    LOG_DEBUG("Unmarking roots%s", "");
    for (size_t i = 0; i < gc->allocs->capacity; ++i) {
        Allocation* chunk = gc->allocs->allocs[i];
        while (chunk) {
            if (chunk->tag & GC_TAG_ROOT) {
                chunk->tag &= ~GC_TAG_ROOT;
            }
            chunk = chunk->next;
        }
    }
}

size_t gc_stop(GarbageCollector* gc)
{
    gc_unroot_roots(gc);
    size_t collected = gc_sweep(gc);
    gc_allocation_map_delete(gc->allocs);
    return collected;
}

size_t gc_run(GarbageCollector* gc)
{
    LOG_DEBUG("Initiating GC run (gc@%p)", (void*) gc);
    gc_mark(gc);
    return gc_sweep(gc);
}

char* gc_strdup (GarbageCollector* gc, const char* s)
{
    size_t len = strlen(s) + 1;
    void *new = gc_malloc(gc, len);

    if (new == NULL) {
        return NULL;
    }
    return (char*) memcpy(new, s, len);
}
