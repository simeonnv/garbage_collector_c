#include "gc.h"

int main() {
    // init the gc
    void *bos = __builtin_frame_address(0);
    gc_start(&gc, bos);

    //allocate some memory
    int* buff = gc_malloc(&gc, sizeof(int) * 16);

    // oh nooooo i forgot to free my memmory nooooo
    buff = NULL;

    // welp clean it up
    gc_run(&gc);

    return 0;
}
