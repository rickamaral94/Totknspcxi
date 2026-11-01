// Host-independent regression checks of the production write-back policy.
#include "nvn/vulkan/writeback_policy.h"
#define CHECK(expr) do { if (!(expr)) return __LINE__; } while (0)
int main(void) {
    const uint64_t legacy = 4096, slot = 512 * 1024;
    CHECK(wb_size_allowed(4096, 0, false, legacy, slot));
    CHECK(!wb_size_allowed(4097, 0, false, legacy, slot));
    CHECK(wb_size_allowed(128 * 128 * 4, 4, false, legacy, slot));
    CHECK(wb_size_allowed(65536, 2, false, legacy, slot));
    CHECK(!wb_size_allowed(65537, 4, false, legacy, slot));
    CHECK(!wb_size_allowed(65536, 0x21, false, legacy, slot));
    CHECK(wb_size_allowed(slot, 0, true, legacy, slot));
    CHECK(!wb_size_allowed(slot + 1, 4, true, legacy, slot));
    CHECK(wb_size_allowed(8192, 0, false, 8192, slot));
    CHECK(wb_small_wait_target(10, 0, 20) == 10);
    CHECK(wb_small_wait_target(10, 8, 20) == 10);
    CHECK(wb_small_wait_target(10, 15, 20) == 15);
    CHECK(wb_small_wait_target(10, 20, 20) == 20);
    CHECK(wb_small_wait_target(10, 25, 20) == 20);
    return 0;
}
