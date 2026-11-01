// GPU backing for TotK's MapPhysicalMemory region (on by default on the Turnip placed-memory path: budget below).
#ifdef __ANDROID__
#include "vk.h"
#include "../../platform.h"
#include "../../settings.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define ALIAS_CHUNK (16ull << 20)
#define ALIAS_CHUNKS 256
typedef struct { VkBuffer buf; VkDeviceMemory mem; atomic_bool ready; bool tried; } AliasChunk;
static AliasChunk chunks[ALIAS_CHUNKS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t committed;
static _Atomic uint64_t last_read_seq;
uint64_t vk_vamm_last_read_seq(void) { return atomic_load(&last_read_seq); }

// On by default since 2026-10-30: 1 GB from 1792 MB into the region, where TOTK's vertex data lives (the copies by
// 256 MB: ~2 GB of the ~2.3 GB a 2 s window copied came from 2.0-2.25 GB; from 0 the alias covered almost none of it).
// In-launch A/B in the prologue cave: the render thread 16.7 -> 14.3 ms a frame; a walk with the camera turning showed
// no broken geometry. ~250 MB more resident memory. LARECOMP_VK_ALIAS_MB=0: off; LARECOMP_AB=<n>:vamm alternates it.
static uint64_t budget(void) {
    const char *e = getenv("LARECOMP_VK_ALIAS_MB");
    long mb = e ? strtol(e, NULL, 10) : 1024;
    if (mb < 0) mb = 0;
    if (mb > 1024) mb = 1024;
    return (uint64_t)mb << 20;
}

static bool place_chunk(unsigned i) {
    AliasChunk *c = &chunks[i];
    uint64_t base = ALIAS_BASE + i * ALIAS_CHUNK;
    extern bool kernel_range_unmapped(uint64_t addr, uint64_t size);
    // Never replace pages already owned by the game, even on allocation retry.
    if (!kernel_range_unmapped(base, ALIAS_CHUNK)) return false;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = ALIAS_CHUNK,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(g_vk.dev, &bi, NULL, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_vk.dev, buf, &mr);
    // The allocation must fit entirely in this still-unmapped chunk.
    if (mr.size != ALIAS_CHUNK) { vkDestroyBuffer(g_vk.dev, buf, NULL); return false; }
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_vk.phys, &mp);
    VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    int type = -1;
    for (unsigned j = 0; j < mp.memoryTypeCount; j++)
        if ((mr.memoryTypeBits & (1u << j)) && (mp.memoryTypes[j].propertyFlags & need) == need) { type = (int)j; break; }
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)type};
    VkResult r = type < 0 ? VK_ERROR_FEATURE_NOT_PRESENT : vkAllocateMemory(g_vk.dev, &ai, NULL, &mem);
    if (r == VK_SUCCESS) r = vkBindBufferMemory(g_vk.dev, buf, mem, 0);
    void *ptr = NULL;
    if (r == VK_SUCCESS) {
        VkMemoryMapPlacedInfoEXT pl = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT,
                                      .pPlacedAddress = (void *)(uintptr_t)base};
        VkMemoryMapInfoKHR mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR, .pNext = &pl,
            .flags = VK_MEMORY_MAP_PLACED_BIT_EXT, .memory = mem, .size = VK_WHOLE_SIZE};
        r = vkMapMemory2KHR(g_vk.dev, &mi, &ptr);
    }
    if (r != VK_SUCCESS || ptr != (void *)(uintptr_t)base) {
        vkDestroyBuffer(g_vk.dev, buf, NULL);
        if (mem) vkFreeMemory(g_vk.dev, mem, NULL);
        if (ptr == (void *)(uintptr_t)base) plat_reset_guest(base, base + ALIAS_CHUNK);
        LOG("[vk-alias] placement failed at %llx (%d)\n", (unsigned long long)base, r);
        return false;
    }
    memset(ptr, 0, ALIAS_CHUNK);
    c->buf = buf; c->mem = mem;
    atomic_store_explicit(&c->ready, true, memory_order_release);
    committed += ALIAS_CHUNK;
    LOG("[vk-alias] placed %llx..%llx, total %llu MB\n", (unsigned long long)base,
        (unsigned long long)(base + ALIAS_CHUNK), (unsigned long long)(committed >> 20));
    return true;
}

// Called before MapPhysicalMemory publishes a new range or the game writes it.
void vk_vamm_before_map(uint64_t addr, uint64_t size) {
    uint64_t limit = budget();
    if (!limit || addr < ALIAS_BASE || addr >= ALIAS_BASE + ALIAS_SIZE || !size ||
        size > ALIAS_BASE + ALIAS_SIZE - addr) return;
    vk_wait_ready();
    extern bool vk_turnip_path_on(int setting);
    if (!g_vk.map_placed || !vk_turnip_path_on(g_settings.gpu_heap) || getenv("LARECOMP_VK_NO_PLACED")) return;
    uint64_t align = g_vk.placed_align;
    if (!align || ALIAS_CHUNK % align || ALIAS_BASE % align) return;
    pthread_mutex_lock(&lock);
    unsigned first = (unsigned)((addr - ALIAS_BASE) / ALIAS_CHUNK);
    unsigned last = (unsigned)((addr - ALIAS_BASE + size - 1) / ALIAS_CHUNK);
    const char *start_env = getenv("LARECOMP_VK_ALIAS_START_MB");
    long start_mb = start_env ? strtol(start_env, NULL, 10) : 1792;
    if (start_mb < 0) start_mb = 0;
    uint64_t start = (uint64_t)start_mb > (ALIAS_SIZE >> 20) ? ALIAS_SIZE : (uint64_t)start_mb << 20;
    for (unsigned i = first; i <= last && i < ALIAS_CHUNKS; i++) {
        if (i * ALIAS_CHUNK < start) continue;
        if (chunks[i].tried || committed + ALIAS_CHUNK > limit) continue;
        chunks[i].tried = true;
        place_chunk(i);
    }
    pthread_mutex_unlock(&lock);
}

bool vk_vamm_buffer(uint64_t addr, uint64_t size, VkBuffer *buf, VkDeviceSize *off) {
    if (addr < ALIAS_BASE || addr >= ALIAS_BASE + ALIAS_SIZE || !size) return false;
    static int force_copy = -1;
    if (force_copy < 0) force_copy = getenv("LARECOMP_VK_COPY_VERTICES") != NULL;
    if (force_copy) return false;
    static int ab_enabled = -1;
    if (ab_enabled < 0) ab_enabled = getenv("LARECOMP_AB") != NULL;
    if (ab_enabled) { extern int vk_ab(const char *what); if (vk_ab("vamm") == 0) return false; }
    uint64_t offset = addr - ALIAS_BASE;
    unsigned i = (unsigned)(offset / ALIAS_CHUNK);
    uint64_t local = offset % ALIAS_CHUNK;
    if (i >= ALIAS_CHUNKS || size > ALIAS_CHUNK - local ||
        !atomic_load_explicit(&chunks[i].ready, memory_order_acquire)) return false;
    *buf = chunks[i].buf; *off = local;
    atomic_store(&last_read_seq, vk_pending_seq());
    return true;
}

// madvise(DONTNEED) must not discard a driver's placed mapping. Keep its backing,
// wait for GPU readers, and zero just the released bytes. Anonymous pieces keep
// the platform's normal discard behavior.
bool vk_vamm_clear(uint64_t addr, uint64_t size) {
    if (!size || addr > UINT64_MAX - size) return false;
    uint64_t end = addr + size;
    if (end <= ALIAS_BASE || addr >= ALIAS_BASE + ALIAS_SIZE) return false;
    bool any = false;
    for (unsigned i = 0; i < ALIAS_CHUNKS; i++) {
        uint64_t base = ALIAS_BASE + i * ALIAS_CHUNK;
        if (addr < base + ALIAS_CHUNK && base < end && atomic_load_explicit(&chunks[i].ready, memory_order_acquire)) { any = true; break; }
    }
    if (!any) return false;
    extern void vk_async_drain(void), renderer_sync(void);
    vk_async_drain(); renderer_sync();
    for (uint64_t p = addr; p < end;) {
        uint64_t n;
        bool placed = false;
        if (p < ALIAS_BASE) n = ALIAS_BASE - p;
        else if (p >= ALIAS_BASE + ALIAS_SIZE) n = end - p;
        else {
            uint64_t offset = p - ALIAS_BASE;
            n = ALIAS_CHUNK - offset % ALIAS_CHUNK;
            placed = atomic_load_explicit(&chunks[offset / ALIAS_CHUNK].ready, memory_order_acquire);
        }
        if (n > end - p) n = end - p;
        if (placed) memset((void *)(uintptr_t)p, 0, n);
        else plat_discard(p, n); // recursive call finds no placed chunk in this piece
        p += n;
    }
    return true;
}

void vk_vamm_reset(void) { // all guest threads stopped and GPU idle
    for (unsigned i = 0; i < ALIAS_CHUNKS; i++) {
        if (atomic_load(&chunks[i].ready)) {
            atomic_store(&chunks[i].ready, false);
            vkDestroyBuffer(g_vk.dev, chunks[i].buf, NULL);
            vkFreeMemory(g_vk.dev, chunks[i].mem, NULL);
        }
    }
    memset(chunks, 0, sizeof chunks);
    committed = 0;
    atomic_store(&last_read_seq, 0);
}
#endif
