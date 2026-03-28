#pragma once
/**
 * @file vk_sync.h
 * @brief Milestone 7 - Synchronisation primitives.
 *
 * VkFence     → MTLSharedEvent + atomic bool + dispatch_semaphore for CPU wait.
 * VkSemaphore → MTLEvent (binary) / MTLSharedEvent (timeline).
 * VkEvent     → CPU-side atomic flag.
 *
 * Queue submission (vkQueueSubmit / vkQueueSubmit2) replays deferred command
 * buffers on Metal command buffers and signals the fence via addCompletedHandler.
 *
 * Pipeline barriers are translated to Metal memory barriers on the active
 * encoder (render / compute / blit).
 */

#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>

namespace mvrvb {

// ── VkFence wrapper ─────────────────────────────────────────────────────────
struct MvFence {
    void*             mtlSharedEvent{nullptr}; ///< id<MTLSharedEvent>
    std::atomic<bool> signaled{false};
    uint64_t          eventValue{0};           ///< Current signaled-value counter
};

// ── VkSemaphore wrapper ─────────────────────────────────────────────────────
struct MvSemaphore {
    void*    mtlEvent{nullptr};  ///< id<MTLEvent> (binary) or id<MTLSharedEvent> (timeline)
    bool     isTimeline{false};
    uint64_t counter{0};         ///< Only meaningful for timeline semaphores
};

// ── VkEvent wrapper ─────────────────────────────────────────────────────────
struct MvEvent {
    std::atomic<bool> set{false};
};

// ── Handle casts ────────────────────────────────────────────────────────────
inline MvFence*     toMv(VkFence h)     { return reinterpret_cast<MvFence*>(h);     }
inline MvSemaphore* toMv(VkSemaphore h) { return reinterpret_cast<MvSemaphore*>(h); }
inline MvEvent*     toMv(VkEvent h)     { return reinterpret_cast<MvEvent*>(h);     }

inline VkFence     toVk(MvFence* p)     { return reinterpret_cast<VkFence>(p);     }
inline VkSemaphore toVk(MvSemaphore* p) { return reinterpret_cast<VkSemaphore>(p); }
inline VkEvent     toVk(MvEvent* p)     { return reinterpret_cast<VkEvent>(p);     }

} // namespace mvrvb

#ifdef __cplusplus
extern "C" {
#endif

// ── Fence ───────────────────────────────────────────────────────────────────
VkResult mvb_CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo,
                         const VkAllocationCallbacks* pAllocator, VkFence* pFence);
void     mvb_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks* pAllocator);
VkResult mvb_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences);
VkResult mvb_GetFenceStatus(VkDevice device, VkFence fence);
VkResult mvb_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences,
                           VkBool32 waitAll, uint64_t timeout);

// ── Semaphore ───────────────────────────────────────────────────────────────
VkResult mvb_CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);
void     mvb_DestroySemaphore(VkDevice device, VkSemaphore semaphore,
                              const VkAllocationCallbacks* pAllocator);
VkResult mvb_GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue);
VkResult mvb_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo* pSignalInfo);
VkResult mvb_WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout);

// ── Event ───────────────────────────────────────────────────────────────────
VkResult mvb_CreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo,
                         const VkAllocationCallbacks* pAllocator, VkEvent* pEvent);
void     mvb_DestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator);
VkResult mvb_GetEventStatus(VkDevice device, VkEvent event);
VkResult mvb_SetEvent(VkDevice device, VkEvent event);
VkResult mvb_ResetEvent(VkDevice device, VkEvent event);

// ── Queue submission ────────────────────────────────────────────────────────
VkResult mvb_QueueSubmit(VkQueue queue, uint32_t submitCount,
                         const VkSubmitInfo* pSubmits, VkFence fence);
VkResult mvb_QueueSubmit2(VkQueue queue, uint32_t submitCount,
                          const VkSubmitInfo2* pSubmits, VkFence fence);
VkResult mvb_QueueWaitIdle(VkQueue queue);
VkResult mvb_DeviceWaitIdle(VkDevice device);

#ifdef __cplusplus
} // extern "C"
#endif
