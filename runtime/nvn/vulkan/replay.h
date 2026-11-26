// Android: the Vulkan recording of the game's draws on a thread of its own, the "replay" thread (replay.c).
//
// The render thread (exec.c draw_) still decides everything - pipelines, descriptor sets, copies, uploads, passes - and
// instead of calling the driver for the draw's commands it writes them into a ring of packets: the pipeline bind, the
// dynamic state, the sets with their offsets, the vertex and index buffers, the draw. The replay thread calls the
// driver for them, in order, while the render thread goes on with the next draws: Turnip's vkCmd* were ~20% of the
// render thread (docs/RENDER-THREAD-PLAN.md, option c).
//
// Everything else keeps recording on the render thread: every other vkCmd* into the command buffer the packets go to
// first waits for the ring to empty (vk_rp_install puts a check in front of volk's pointers), and so do vk_cmd() and
// the command pools' calls. The order of the commands is the same as without the thread.
//
// LARECOMP_VK_REPLAY=1: on (default off); LARECOMP_AB=<frames>:replay alternates it (switched between submissions).
// LARECOMP_VK_REPLAY_LOG=1: per frame, every 120 frames - packets, the waits for the ring and who caused them ("[replay]").
// LARECOMP_VK_REPLAY_SPIN_US=<us>: how long the replay thread spins on an empty ring before it sleeps (default 2000).
#pragma once
#include "vk.h"
#include <stdatomic.h>

#ifdef __ANDROID__
// The driver's functions, as volk had them before the checks went in front (the replay thread calls these)
#define RP_FUNCS(X) \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindIndexBuffer) X(vkCmdBindVertexBuffers) X(vkCmdDraw) \
    X(vkCmdDrawIndexed) X(vkCmdDrawIndirect) X(vkCmdDrawIndexedIndirect) X(vkCmdPushConstants) X(vkCmdSetViewport) \
    X(vkCmdSetScissor) X(vkCmdSetDepthBias) X(vkCmdSetBlendConstants) X(vkCmdSetStencilCompareMask) \
    X(vkCmdSetStencilWriteMask) X(vkCmdSetStencilReference) X(vkCmdSetCullMode) X(vkCmdSetFrontFace) \
    X(vkCmdSetPrimitiveTopology) X(vkCmdSetDepthTestEnable) X(vkCmdSetDepthWriteEnable) X(vkCmdSetDepthCompareOp) \
    X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) X(vkCmdSetPrimitiveRestartEnable) X(vkCmdSetDepthBiasEnable) X(vkCmdSetVertexInputEXT) \
    X(vkCmdClearAttachments) X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdNextSubpass) X(vkCmdExecuteCommands) X(vkCmdPipelineBarrier) X(vkCmdPipelineBarrier2) X(vkCmdWriteTimestamp) \
    X(vkCmdWriteTimestamp2) X(vkCmdResetQueryPool) X(vkCmdBeginQuery) X(vkCmdEndQuery) X(vkCmdCopyQueryPoolResults) \
    X(vkCmdCopyBuffer) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) \
    X(vkCmdResolveImage) X(vkCmdFillBuffer) X(vkCmdUpdateBuffer) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) \
    X(vkCmdDispatch) X(vkCmdDispatchIndirect) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) \
    X(vkResetCommandPool) X(vkFreeCommandBuffers)
typedef struct {
#define RP_FIELD(f) PFN_##f f;
    RP_FUNCS(RP_FIELD)
#undef RP_FIELD
} RpReal;
extern RpReal rp_real;

extern bool rp_on;  // packets now (vk_rp_latch: switched only with the ring empty, between submissions)
void vk_rp_install(void);         // device.c, after compat_aliases: the checks in front of volk's pointers
bool vk_rp_installed(void);
void vk_rp_latch(bool want);      // exec.c, between submissions
void vk_rp_drain_at(void *ra);    // until the replay thread has recorded every packet (ra: who asked, for the log)
#define vk_rp_drain() vk_rp_drain_at(__builtin_return_address(0))
void vk_rp_frame(void);           // (the log, once a frame)
int vk_rp_tid(void);              // (the replay thread, for the ADPF session; 0: not started)

// ---- the ring: words; a packet's first word is its header (op, words, aux 16 bits, a 32-bit value), then the
// command buffer
#define RP_WORDS (1u << 17)  // (1 MB: ~25,000 packets, several frames' worth of draws)
enum { RP_WRAP, RP_U32, RP_FACE_U32, RP_STENCIL_OP, RP_DEPTH_BIAS, RP_BLEND, RP_VIEWPORT, RP_SCISSOR, RP_PIPE, RP_SETS,
       RP_IB, RP_VB, RP_DRAW, RP_DRAW_IDX, RP_PUSH, RP_BEGIN_RENDERING, RP_END_RENDERING, RP_FULL_BARRIER, RP_DISPATCH, RP_COPY_I2B, RP_MEM_BARRIER, RP_VI };
extern uint64_t rp_ring[RP_WORDS];
extern uint64_t rp_w;  // the producer's position (published: rp_wpub, by rp_commit)
extern uint64_t rp_free_end;  // (the producer's: where the ring was known free up to, the last time it looked)
extern __thread char rp_tls_mark;  // (its address tells the threads apart)
extern void *rp_owner;  // (the thread writing packets: &its rp_tls_mark - only it publishes them, rp_commit)
extern _Atomic uint64_t rp_wpub, rp_rpos;
extern _Atomic int rp_sleeping;
extern VkCommandBuffer rp_last_cb;  // (the command buffer the packets went to: other commands into it wait for them)
extern uint64_t rp_stat_pkts;
void rp_wrap(uint32_t idx);
void rp_wait_space(uint64_t end);
void rp_wake(void);
#define RP_HDR(op, n, aux, v) ((uint64_t)(op) | (uint64_t)(n) << 8 | (uint64_t)(uint16_t)(aux) << 16 | (uint64_t)(uint32_t)(v) << 32)

static inline uint64_t *rp_reserve(uint32_t n, VkCommandBuffer cb) {
    uint32_t idx = (uint32_t)rp_w & (RP_WORDS - 1);
    if (idx + n > RP_WORDS) rp_wrap(idx), idx = 0;
    if (rp_w + n > rp_free_end) {  // (the replay thread's position read only when the known space runs out)
        rp_free_end = atomic_load_explicit(&rp_rpos, memory_order_acquire) + RP_WORDS;
        if (rp_w + n > rp_free_end) rp_wait_space(rp_w + n);
    }
    rp_last_cb = cb;
    rp_owner = &rp_tls_mark;
    uint64_t *p = &rp_ring[idx];
    p[1] = (uint64_t)(uintptr_t)cb;
    return p;
}
// A packet is written: rp_publish. The replay thread sees the packets written so far at rp_commit - after a draw, a
// dispatch, a pass's end, and before any wait for the ring (vk_rp_drain_at): one store of the position the replay thread
// polls a draw, not one a packet (each store moved that cache line between the two cores)
static inline void rp_publish(uint32_t n) {
    rp_w += n;
    rp_stat_pkts++;
}
static inline void rp_commit(void) {
    atomic_store_explicit(&rp_wpub, rp_w, memory_order_seq_cst);
    if (atomic_load_explicit(&rp_sleeping, memory_order_seq_cst)) rp_wake();
}
static inline void rp_u32(VkCommandBuffer cb, void *fn, uint32_t v) {
    uint64_t *p = rp_reserve(3, cb);
    p[0] = RP_HDR(RP_U32, 3, 0, v);
    p[2] = (uint64_t)(uintptr_t)fn;
    rp_publish(3);
}
static inline void rp_face_u32(VkCommandBuffer cb, void *fn, VkStencilFaceFlags face, uint32_t v) {
    uint64_t *p = rp_reserve(3, cb);
    p[0] = RP_HDR(RP_FACE_U32, 3, face, v);
    p[2] = (uint64_t)(uintptr_t)fn;
    rp_publish(3);
}
static inline uint64_t rp_f2(float a, float b) {
    uint32_t x, y;
    __builtin_memcpy(&x, &a, 4), __builtin_memcpy(&y, &b, 4);
    return x | (uint64_t)y << 32;
}

// ---- the commands draw_ records (rpCmd*: as vkCmd*, into the ring while rp_on)
#define RP_U32_CMD(name, T) \
    static inline void rp##name(VkCommandBuffer cb, T v) { \
        if (!rp_on) vk##name(cb, v); else rp_u32(cb, (void *)rp_real.vk##name, (uint32_t)v); }
RP_U32_CMD(CmdSetCullMode, VkCullModeFlags)
RP_U32_CMD(CmdSetFrontFace, VkFrontFace)
RP_U32_CMD(CmdSetPrimitiveTopology, VkPrimitiveTopology)
RP_U32_CMD(CmdSetDepthTestEnable, VkBool32)
RP_U32_CMD(CmdSetDepthWriteEnable, VkBool32)
RP_U32_CMD(CmdSetDepthCompareOp, VkCompareOp)
RP_U32_CMD(CmdSetStencilTestEnable, VkBool32)
RP_U32_CMD(CmdSetPrimitiveRestartEnable, VkBool32)
RP_U32_CMD(CmdSetDepthBiasEnable, VkBool32)
#undef RP_U32_CMD
#define RP_FACE_CMD(name) \
    static inline void rp##name(VkCommandBuffer cb, VkStencilFaceFlags f, uint32_t v) { \
        if (!rp_on) vk##name(cb, f, v); else rp_face_u32(cb, (void *)rp_real.vk##name, f, v); }
RP_FACE_CMD(CmdSetStencilCompareMask)
RP_FACE_CMD(CmdSetStencilWriteMask)
RP_FACE_CMD(CmdSetStencilReference)
#undef RP_FACE_CMD
static inline void rpCmdSetStencilOp(VkCommandBuffer cb, VkStencilFaceFlags f, VkStencilOp fail, VkStencilOp pass,
                                     VkStencilOp dfail, VkCompareOp cmp) {
    if (!rp_on) { vkCmdSetStencilOp(cb, f, fail, pass, dfail, cmp); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_STENCIL_OP, 4, f, 0);
    p[2] = (uint32_t)fail | (uint64_t)(uint32_t)pass << 32;
    p[3] = (uint32_t)dfail | (uint64_t)(uint32_t)cmp << 32;
    rp_publish(4);
}
static inline void rpCmdSetDepthBias(VkCommandBuffer cb, float c, float clamp, float slope) {
    if (!rp_on) { vkCmdSetDepthBias(cb, c, clamp, slope); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_DEPTH_BIAS, 4, 0, 0);
    p[2] = rp_f2(c, clamp);
    p[3] = rp_f2(slope, 0);
    rp_publish(4);
}
static inline void rpCmdSetBlendConstants(VkCommandBuffer cb, const float bc[4]) {
    if (!rp_on) { vkCmdSetBlendConstants(cb, bc); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_BLEND, 4, 0, 0);
    p[2] = rp_f2(bc[0], bc[1]);
    p[3] = rp_f2(bc[2], bc[3]);
    rp_publish(4);
}
static inline void rpCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkViewport *v) {
    if (!rp_on || n != 1) { if (rp_on) vk_rp_drain(); vkCmdSetViewport(cb, first, n, v); return; }
    uint64_t *p = rp_reserve(5, cb);
    p[0] = RP_HDR(RP_VIEWPORT, 5, 0, first);
    __builtin_memcpy(&p[2], v, sizeof *v);  // (6 floats: 3 words)
    rp_publish(5);
}
static inline void rpCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkRect2D *r) {
    if (!rp_on || n != 1) { if (rp_on) vk_rp_drain(); vkCmdSetScissor(cb, first, n, r); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_SCISSOR, 4, 0, first);
    __builtin_memcpy(&p[2], r, sizeof *r);  // (2 words)
    rp_publish(4);
}
static inline void rpCmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipeline pipe) {
    if (!rp_on) { vkCmdBindPipeline(cb, bp, pipe); return; }
    uint64_t *p = rp_reserve(3, cb);
    p[0] = RP_HDR(RP_PIPE, 3, 0, bp);
    p[2] = (uint64_t)(uintptr_t)pipe;
    rp_publish(3);
}
// (header aux: first set | count << 8 | bind point << 12; value: the dynamic offsets' count)
static inline void rpCmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint bp, VkPipelineLayout layout, uint32_t first,
                                           uint32_t nsets, const VkDescriptorSet *sets, uint32_t ndyn, const uint32_t *dyn) {
    if (!rp_on || bp > VK_PIPELINE_BIND_POINT_COMPUTE || nsets > 8 || first > 255 || ndyn > 128) {
        if (rp_on) vk_rp_drain();
        vkCmdBindDescriptorSets(cb, bp, layout, first, nsets, sets, ndyn, dyn);
        return;
    }
    uint32_t n = 3 + nsets + (ndyn + 1) / 2;
    uint64_t *p = rp_reserve(n, cb);
    p[0] = RP_HDR(RP_SETS, n, first | nsets << 8 | (uint32_t)bp << 12, ndyn);
    p[2] = (uint64_t)(uintptr_t)layout;
    for (uint32_t i = 0; i < nsets; i++) p[3 + i] = (uint64_t)(uintptr_t)sets[i];
    if (ndyn) {
        p[3 + nsets + (ndyn - 1) / 2] = 0;  // (an odd count's last word)
        __builtin_memcpy(&p[3 + nsets], dyn, ndyn * 4);
    }
    rp_publish(n);
}
// (vkCmdSetVertexInputEXT, from BOTWRecomp 8146cfa; header aux: bindings | attributes << 8. The descriptions are copied as
// they are, 4 words each with a NULL pNext: at most 17 + 32 of them, 198 words)
#define RP_VI_W(t) ((uint32_t)((sizeof(t) + 7) / 8))
static inline void rpCmdSetVertexInput(VkCommandBuffer cb, uint32_t nb, const VkVertexInputBindingDescription2EXT *vb, uint32_t na,
                                       const VkVertexInputAttributeDescription2EXT *va) {
    if (!rp_on || nb > 17 || na > 32) {
        if (rp_on) vk_rp_drain();
        vkCmdSetVertexInputEXT(cb, nb, vb, na, va);
        return;
    }
    uint32_t n = 2 + nb * RP_VI_W(VkVertexInputBindingDescription2EXT) + na * RP_VI_W(VkVertexInputAttributeDescription2EXT);
    uint64_t *p = rp_reserve(n, cb);
    p[0] = RP_HDR(RP_VI, n, nb | na << 8, 0);
    if (nb) __builtin_memcpy(&p[2], vb, nb * sizeof *vb);
    if (na) __builtin_memcpy(&p[2 + nb * RP_VI_W(VkVertexInputBindingDescription2EXT)], va, na * sizeof *va);
    rp_publish(n);
}
static inline void rpCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize off, VkIndexType t) {
    if (!rp_on) { vkCmdBindIndexBuffer(cb, buf, off, t); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_IB, 4, 0, t);
    p[2] = (uint64_t)(uintptr_t)buf;
    p[3] = off;
    rp_publish(4);
}
static inline void rpCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkBuffer *bufs,
                                          const VkDeviceSize *offs) {
    if (!rp_on || n > 16) { if (rp_on) vk_rp_drain(); vkCmdBindVertexBuffers(cb, first, n, bufs, offs); return; }
    uint32_t w = 2 + 2 * n;
    uint64_t *p = rp_reserve(w, cb);
    p[0] = RP_HDR(RP_VB, w, n, first);
    for (uint32_t i = 0; i < n; i++) p[2 + i] = (uint64_t)(uintptr_t)bufs[i], p[2 + n + i] = offs[i];
    rp_publish(w);
}
static inline void rpCmdDraw(VkCommandBuffer cb, uint32_t count, uint32_t inst, uint32_t first, uint32_t first_inst) {
    if (!rp_on) { vkCmdDraw(cb, count, inst, first, first_inst); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_DRAW, 4, 0, 0);
    p[2] = count | (uint64_t)inst << 32;
    p[3] = first | (uint64_t)first_inst << 32;
    rp_publish(4);
    rp_commit();
}
static inline void rpCmdDrawIndexed(VkCommandBuffer cb, uint32_t count, uint32_t inst, uint32_t first, int32_t vofs,
                                    uint32_t first_inst) {
    if (!rp_on) { vkCmdDrawIndexed(cb, count, inst, first, vofs, first_inst); return; }
    uint64_t *p = rp_reserve(5, cb);
    p[0] = RP_HDR(RP_DRAW_IDX, 5, 0, first_inst);
    p[2] = count | (uint64_t)inst << 32;
    p[3] = first | (uint64_t)(uint32_t)vofs << 32;
    p[4] = 0;
    rp_publish(5);
    rp_commit();
}
static inline void rpCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t off,
                                      uint32_t size, const void *data) {
    if (!rp_on || size > 256) { if (rp_on) vk_rp_drain(); vkCmdPushConstants(cb, layout, stages, off, size, data); return; }
    uint32_t n = 4 + (size + 7) / 8;
    uint64_t *p = rp_reserve(n, cb);
    p[n - 1] = 0;  // (the last word's padding; first: a size of 0 has none)
    p[0] = RP_HDR(RP_PUSH, n, off, size);
    p[2] = (uint64_t)(uintptr_t)layout;
    p[3] = stages;
    __builtin_memcpy(&p[4], data, size);
    rp_publish(n);
}
// ---- between passes (exec.c begin_pass / vk_end_pass, with dynamic rendering): the end, the full barrier, the begin
static inline void rpCmdEndRendering(VkCommandBuffer cb) {
    if (!rp_on) { vkCmdEndRendering(cb); return; }
    uint64_t *p = rp_reserve(2, cb);
    p[0] = RP_HDR(RP_END_RENDERING, 2, 0, 0);
    rp_publish(2);
    rp_commit();
}
static inline void rpFullBarrier(VkCommandBuffer cb) {  // (= vk_full_barrier)
    if (!rp_on) { extern void vk_full_barrier(VkCommandBuffer cb); vk_full_barrier(cb); return; }
    uint64_t *p = rp_reserve(2, cb);
    p[0] = RP_HDR(RP_FULL_BARRIER, 2, 0, 0);
    rp_publish(2);
}
static inline void rpCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z) {
    if (!rp_on) { vkCmdDispatch(cb, x, y, z); return; }
    uint64_t *p = rp_reserve(4, cb);
    p[0] = RP_HDR(RP_DISPATCH, 4, 0, z);
    p[2] = x | (uint64_t)y << 32;
    p[3] = 0;
    rp_publish(4);
    rp_commit();
}
// (one region)
static inline void rpCmdCopyImageToBuffer(VkCommandBuffer cb, VkImage img, VkImageLayout layout, VkBuffer buf,
                                          const VkBufferImageCopy *c) {
    if (!rp_on) { vkCmdCopyImageToBuffer(cb, img, layout, buf, 1, c); return; }
    uint32_t n = 4 + (uint32_t)((sizeof *c + 7) / 8);
    uint64_t *p = rp_reserve(n, cb);
    p[0] = RP_HDR(RP_COPY_I2B, n, 0, layout);
    p[2] = (uint64_t)(uintptr_t)img;
    p[3] = (uint64_t)(uintptr_t)buf;
    p[n - 1] = 0;
    __builtin_memcpy(&p[4], c, sizeof *c);
    rp_publish(n);
}
// (vkCmdPipelineBarrier2 with one memory barrier)
static inline void rpMemBarrier(VkCommandBuffer cb, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds,
                                VkAccessFlags2 da) {
    if (!rp_on) {
        VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = ss, .srcAccessMask = sa,
                               .dstStageMask = ds, .dstAccessMask = da};
        VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
        vkCmdPipelineBarrier2(cb, &di);
        return;
    }
    uint64_t *p = rp_reserve(6, cb);
    p[0] = RP_HDR(RP_MEM_BARRIER, 6, 0, 0);
    p[2] = ss, p[3] = sa, p[4] = ds, p[5] = da;
    rp_publish(6);
    rp_commit();
}
#define RP_SZW(t) ((uint32_t)((sizeof(t) + 7) / 8))
// (header aux: color attachments | depth << 8 | stencil << 9; the structures copied, their pointers set again by the replay)
static inline bool rpCmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *ri) {
    if (!rp_on || ri->pNext || ri->colorAttachmentCount > 8) return false;
    const VkRenderingAttachmentInfo *d = ri->pDepthAttachment, *st = ri->pStencilAttachment;
    for (uint32_t i = 0; i < ri->colorAttachmentCount; i++) if (ri->pColorAttachments[i].pNext) return false;
    if ((d && d->pNext) || (st && st->pNext)) return false;
    uint32_t na = ri->colorAttachmentCount + (d ? 1 : 0) + (st ? 1 : 0);
    uint32_t n = 2 + RP_SZW(VkRenderingInfo) + na * RP_SZW(VkRenderingAttachmentInfo);
    uint64_t *p = rp_reserve(n, cb);
    p[0] = RP_HDR(RP_BEGIN_RENDERING, n, ri->colorAttachmentCount | (d ? 1u : 0u) << 8 | (st ? 1u : 0u) << 9, 0);
    __builtin_memcpy(&p[2], ri, sizeof *ri);
    uint64_t *a = &p[2 + RP_SZW(VkRenderingInfo)];
    for (uint32_t i = 0; i < ri->colorAttachmentCount; i++, a += RP_SZW(VkRenderingAttachmentInfo))
        __builtin_memcpy(a, &ri->pColorAttachments[i], sizeof *d);
    if (d) __builtin_memcpy(a, d, sizeof *d), a += RP_SZW(VkRenderingAttachmentInfo);
    if (st) __builtin_memcpy(a, st, sizeof *st);
    rp_publish(n);
    return true;
}
#else
#define rp_on false
static inline bool rpCmdBeginRendering(VkCommandBuffer cb, const VkRenderingInfo *ri) { (void)cb, (void)ri; return false; }
#define rpCmdEndRendering vkCmdEndRendering
#define rpCmdDispatch vkCmdDispatch
#define rpCmdCopyImageToBuffer(cb, img, layout, buf, c) vkCmdCopyImageToBuffer(cb, img, layout, buf, 1, c)
static inline void rpMemBarrier(VkCommandBuffer cb, VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds,
                                VkAccessFlags2 da) {
    VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = ss, .srcAccessMask = sa,
                           .dstStageMask = ds, .dstAccessMask = da};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
    vkCmdPipelineBarrier2(cb, &di);
}
#define rpFullBarrier vk_full_barrier
static inline bool vk_rp_installed(void) { return false; }
static inline void vk_rp_latch(bool want) { (void)want; }
static inline void vk_rp_frame(void) {}
static inline int vk_rp_tid(void) { return 0; }
#define vk_rp_drain() ((void)0)
#define rpCmdSetCullMode vkCmdSetCullMode
#define rpCmdSetFrontFace vkCmdSetFrontFace
#define rpCmdSetPrimitiveTopology vkCmdSetPrimitiveTopology
#define rpCmdSetDepthTestEnable vkCmdSetDepthTestEnable
#define rpCmdSetDepthWriteEnable vkCmdSetDepthWriteEnable
#define rpCmdSetDepthCompareOp vkCmdSetDepthCompareOp
#define rpCmdSetStencilTestEnable vkCmdSetStencilTestEnable
#define rpCmdSetPrimitiveRestartEnable vkCmdSetPrimitiveRestartEnable
#define rpCmdSetDepthBiasEnable vkCmdSetDepthBiasEnable
#define rpCmdSetVertexInput vkCmdSetVertexInputEXT
#define rpCmdSetStencilCompareMask vkCmdSetStencilCompareMask
#define rpCmdSetStencilWriteMask vkCmdSetStencilWriteMask
#define rpCmdSetStencilReference vkCmdSetStencilReference
#define rpCmdSetStencilOp vkCmdSetStencilOp
#define rpCmdSetDepthBias vkCmdSetDepthBias
#define rpCmdSetBlendConstants vkCmdSetBlendConstants
#define rpCmdSetViewport vkCmdSetViewport
#define rpCmdSetScissor vkCmdSetScissor
#define rpCmdBindPipeline vkCmdBindPipeline
#define rpCmdBindDescriptorSets vkCmdBindDescriptorSets
#define rpCmdBindIndexBuffer vkCmdBindIndexBuffer
#define rpCmdBindVertexBuffers vkCmdBindVertexBuffers
#define rpCmdDraw vkCmdDraw
#define rpCmdDrawIndexed vkCmdDrawIndexed
#define rpCmdPushConstants vkCmdPushConstants
#endif
