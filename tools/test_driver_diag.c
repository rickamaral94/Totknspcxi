// cc -pthread tools/test_driver_diag.c -o /tmp/test_driver_diag && /tmp/test_driver_diag
#include <pthread.h>
#include "../runtime/nvn/vulkan/driver_diag.h"
static void *run(void *p) {
    unsigned long long id = driver_diag_begin("test", (uintptr_t)p, 12);
    driver_diag_end(id, 0);
    return NULL;
}
int main(void) {
    pthread_t threads[4];
    for (int i = 0; i < 4; ++i) if (pthread_create(threads + i, NULL, run, (void *)(uintptr_t)i)) return 1;
    for (int i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
    return atomic_load(&driver_diag_active) != 0 || atomic_load(&driver_diag_sequence) != 4;
}
