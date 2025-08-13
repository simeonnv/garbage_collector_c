#include "gc.h"
#include <stdio.h>

int main(void) {
    char *data = gc_malloc(100);
    if (data) {
        printf("Allocated 100 bytes.\n");
    }
    gc_free_all();
    printf("Memory freed.\n");
    return 0;
}
