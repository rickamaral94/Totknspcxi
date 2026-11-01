#!/usr/bin/env python3
"""Exercise production VAMM placement policy with fake Vulkan calls on Android.
Usage: python tools/test_vamm_memory.py <NDK-bin> <adb-serial>
"""
from pathlib import Path
import re,subprocess,sys
root=Path(__file__).resolve().parents[1]
out=root/'build/vamm-memory-tests';out.mkdir(parents=True,exist_ok=True)
s=(root/'runtime/nvn/vulkan/alias_memory.c').read_text()
body=s[s.index('#define ALIAS_CHUNK '):s.rindex('#endif')]
k=(root/'runtime/kernel.c').read_text();a=k.index('bool kernel_range_unmapped(');b=k.index('#define PHYSICAL',a)
prefix=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
static uint8_t pages[64u<<20] __attribute__((aligned(65536)));
#define ALIAS_BASE ((uint64_t)(uintptr_t)pages)
#define ALIAS_SIZE sizeof pages
#define LOG(...) ((void)0)
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
#define VK_ERROR_FEATURE_NOT_PRESENT -1
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 2
#define VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT 3
#define VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR 4
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 1
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT 2
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT 4
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 8
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 1
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 2
#define VK_MEMORY_PROPERTY_HOST_CACHED_BIT 4
#define VK_MEMORY_MAP_PLACED_BIT_EXT 1
#define VK_WHOLE_SIZE UINT64_MAX
typedef uint64_t VkBuffer,VkDeviceMemory,VkDeviceSize;
typedef unsigned VkMemoryPropertyFlags;
typedef int VkResult;
typedef struct { int sType; uint64_t size; unsigned usage; } VkBufferCreateInfo;
typedef struct { uint64_t size; unsigned memoryTypeBits; } VkMemoryRequirements;
typedef struct { unsigned propertyFlags; } MemoryType;
typedef struct { unsigned memoryTypeCount; MemoryType memoryTypes[1]; } VkPhysicalDeviceMemoryProperties;
typedef struct { int sType; uint64_t allocationSize; unsigned memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { int sType; void *pPlacedAddress; } VkMemoryMapPlacedInfoEXT;
typedef struct { int sType; const VkMemoryMapPlacedInfoEXT *pNext; unsigned flags; VkDeviceMemory memory; uint64_t size; } VkMemoryMapInfoKHR;
static struct { int dev,phys,map_placed; uint64_t placed_align; } g_vk={0,0,1,65536};
static struct { int gpu_heap; } g_settings;
static int maps,allocated,freed,destroyed,syncs,drains,fail_map,anonymous_discards;
static uint64_t vk_pending_seq(void) { return 123; }
static int ab_mode = -1;
int vk_ab(const char *what) { (void)what; return ab_mode; }
static void vk_wait_ready(void) {}
static bool vk_turnip_path_on(int setting) { (void)setting; return true; }
static int vkCreateBuffer(int d,const VkBufferCreateInfo *b,void *a,VkBuffer *out) { (void)d;(void)b;(void)a; *out=++allocated; return 0; }
static void vkGetBufferMemoryRequirements(int d,VkBuffer b,VkMemoryRequirements *r) { (void)d;(void)b; *r=(VkMemoryRequirements){16u<<20,1}; }
static void vkGetPhysicalDeviceMemoryProperties(int p,VkPhysicalDeviceMemoryProperties *out) { (void)p; *out=(VkPhysicalDeviceMemoryProperties){1,{{7}}}; }
static int vkAllocateMemory(int d,const VkMemoryAllocateInfo *a,void *x,VkDeviceMemory *m) { (void)d;(void)a;(void)x; *m=allocated; return 0; }
static int vkBindBufferMemory(int d,VkBuffer b,VkDeviceMemory m,int off) { (void)d;(void)b;(void)m;(void)off;return 0; }
static int vkMapMemory2KHR(int d,const VkMemoryMapInfoKHR *m,void **p) { (void)d; if(fail_map) return -1; *p=m->pNext->pPlacedAddress; maps++; return 0; }
static void vkDestroyBuffer(int d,VkBuffer b,void *a) { (void)d;(void)b;(void)a;destroyed++; }
static void vkFreeMemory(int d,VkDeviceMemory m,void *a) { (void)d;(void)m;(void)a;freed++; }
static bool plat_reset_guest(uint64_t lo,uint64_t hi) { memset((void *)(uintptr_t)lo,0,hi-lo); return true; }
void vk_async_drain(void) { drains++; }
void renderer_sync(void) { syncs++; }
bool vk_vamm_clear(uint64_t addr,uint64_t size);
static void plat_discard(uint64_t addr,uint64_t size) { if(!vk_vamm_clear(addr,size)) { memset((void *)(uintptr_t)addr,0,size); anonymous_discards++; } }
typedef struct { uint64_t addr,size; unsigned type,perm; } Region;
static Region regions[3]; static int nregions;
static pthread_mutex_t g_rlock=PTHREAD_MUTEX_INITIALIZER;
"""
test=r"""
int main(void) {
 const uint64_t chunk=16u<<20;
 uint64_t base=ALIAS_BASE; VkBuffer buffer; VkDeviceSize offset;
 unsetenv("LARECOMP_VK_ALIAS_MB");
 vk_vamm_before_map(base,128u<<10); assert(!maps);
 setenv("LARECOMP_VK_ALIAS_MB","32",1);
 regions[0]=(Region){base,4096,7,3};nregions=1;pages[0]=0xa5;
 assert(!kernel_range_unmapped(base,chunk));
 assert(kernel_range_unmapped(base+4096,chunk-4096));
 assert(!kernel_range_unmapped(UINT64_MAX-1,8));
 vk_vamm_before_map(base,128u<<10); assert(!maps && pages[0]==0xa5);
 memset(pages+chunk,0xc3,chunk);
 vk_vamm_before_map(base+chunk+4096,128u<<10);
 assert(maps==1 && committed==chunk && pages[chunk]==0 && pages[2*chunk-1]==0);
 pages[chunk+12]=0x42;
 vk_vamm_before_map(base+chunk,chunk); assert(maps==1 && pages[chunk+12]==0x42);
 setenv("LARECOMP_AB","240:vamm",1);
 assert(vk_vamm_buffer(base+chunk+12,200,&buffer,&offset) && offset==12);
 ab_mode=0; assert(!vk_vamm_buffer(base+chunk+12,200,&buffer,&offset));
 ab_mode=1; assert(vk_vamm_buffer(base+chunk+12,200,&buffer,&offset));
 ab_mode=-1;
 assert(vk_vamm_last_read_seq()==123);
 assert(!vk_vamm_buffer(base+chunk-1,2,&buffer,&offset));
 assert(!vk_vamm_buffer(base+2*chunk-1,2,&buffer,&offset));
 assert(!vk_vamm_buffer(base+chunk,UINT64_MAX,&buffer,&offset));
 assert(!vk_vamm_buffer(base+chunk,0,&buffer,&offset));
 memset(pages+chunk-32,0x77,64);
 assert(vk_vamm_clear(base+chunk-16,32));
 assert(syncs==1 && drains==1 && anonymous_discards==1);
 for(unsigned i=0;i<64;i++) assert(pages[chunk-32+i]==(i>=16 && i<48 ? 0:0x77));
 vk_vamm_before_map(base+2*chunk,128u<<10); assert(maps==2 && committed==2*chunk);
 vk_vamm_before_map(base+3*chunk,128u<<10); assert(maps==2); // budget
 vk_vamm_reset(); assert(!committed && freed==destroyed && !vk_vamm_last_read_seq());
 assert(!vk_vamm_buffer(base+chunk,16,&buffer,&offset));
 setenv("LARECOMP_VK_ALIAS_START_MB","48",1);
 vk_vamm_before_map(base+chunk,128u<<10); assert(!committed);
 vk_vamm_before_map(base+3*chunk,128u<<10); assert(committed==chunk);
 vk_vamm_reset(); unsetenv("LARECOMP_VK_ALIAS_START_MB");
 fail_map=1; pages[chunk]=0x5a;
 vk_vamm_before_map(base+chunk,128u<<10); assert(pages[chunk]==0x5a && !committed);
 assert(!vk_vamm_buffer(base+chunk,16,&buffer,&offset));
 vk_vamm_reset(); assert(freed==destroyed && destroyed==allocated);
 return 0;
}
"""
(out/'test.c').write_text(prefix+k[a:b]+body+test)
subprocess.run([str(Path(sys.argv[1])/'clang.exe'),'--target=aarch64-linux-android30','-O2',str(out/'test.c'),'-o',str(out/'test')],check=True)
adb=['adb','-s',sys.argv[2]];remote='/data/local/tmp/totkrecompandroid-tests/vamm_memory'
subprocess.run(adb+['shell','mkdir','-p','/data/local/tmp/totkrecompandroid-tests'],check=True)
subprocess.run(adb+['push',str(out/'test'),remote],check=True)
subprocess.run(adb+['shell','chmod','755',remote],check=True)
subprocess.run(adb+['shell',remote],check=True)
print('PASS: mapped-page preservation, initialization, bounds, mixed discard, budget, reset and failure fallback')
