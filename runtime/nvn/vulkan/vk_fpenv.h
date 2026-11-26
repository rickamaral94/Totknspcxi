// The host's floating-point mode for Vulkan's side (device.c).
//
// The game's code sets FPCR itself (tools/lifter.py lifts MSR FPCR to rt_set_fpcr: its SDK's threads run with
// flush-to-zero), and it stays set through the NVN calls the game makes on its threads - and Linux copies it to a thread
// created there. So the driver's shader compiler ran with the game's flush-to-zero: on a vkCreate*Pipelines or
// vkCreateShaderModule made on a game thread, on the render, replay and translator threads, and on the driver's own
// threads made at vkCreateDevice. Swap2-Native measured on Qualcomm's driver (891.11) that pipelines compiled so give other
// results than the same SPIR-V compiled with FPCR 0 (a lit scene ~2^12 too bright at a bloom pass), through
// igawa6/peanuts-native's notes (2026-11). Turnip: not measured. This is our own implementation of the idea.
//
// Every Vulkan-side thread the host creates sets FPCR 0 when it starts (vk_fp_thread), and the calls that compile set it
// around themselves on any thread, putting the caller's back (vk_fp_host / vk_fp_put). The game's code keeps its FPCR.
// LARECOMP_VK_GUEST_FPCR=1 (diagnostic): the inherited FPCR kept everywhere, and no pipeline cache file read or written.
#pragma once
#include <stdbool.h>
#include <stdint.h>

uint64_t vk_fp_host(const char *where);  // FPCR 0 on this thread; returns what to put back (0: nothing)
void vk_fp_put(uint64_t fpcr);           // puts vk_fp_host's value back
void vk_fp_thread(const char *name);     // at a host thread's start: FPCR 0 for good
bool vk_fp_guest_kept(void);             // LARECOMP_VK_GUEST_FPCR=1
