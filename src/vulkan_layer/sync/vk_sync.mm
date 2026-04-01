/**
 * @file vk_sync.mm
 * @brief Milestone 7 — Synchronisation primitives and queue submission.
 *
 * VkFence     → MTLSharedEvent + atomic bool.  CPU wait via notifyListener
 *               on the MTLSharedEvent + dispatch_semaphore.
 * VkSemaphore → MTLEvent (binary GPU↔GPU) / MTLSharedEvent (timeline).
 * VkEvent     → CPU-side atomic flag (no GPU-side event needed).
 *
 * Queue submission:
 *   vkQueueSubmit / vkQueueSubmit2 replay each VkCommandBuffer's deferred
 *   command list on a fresh MTLCommandBuffer.  The fence (if any) is signaled
 *   via addCompletedHandler on the last MTLCommandBuffer in the batch.
 *   Wait semaphores are encoded as MTLEvent waits; signal semaphores as
 *   MTLEvent signals.
 *
 * Pipeline barriers:
 *   vkCmdPipelineBarrier / vkCmdPipelineBarrier2 record a deferred command.
 *   At replay, we call textureBarrier / memoryBarrier on the active encoder
 *   or simply flush the encoder boundary (Metal hazard tracking covers most
 *   cross-encoder dependencies).
 */

#include "vk_sync.h"
#include "../device/vk_device.h"
#include "../commands/vk_commands.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>

#include <cstring>
#include <algorithm>

using namespace mvrvb;

// ═══════════════════════════════════════════════════════════════════════════════
// Internal: replay a single command buffer's deferred commands on an
// MTLCommandBuffer. Defined in vk_commands.mm, but we need it here for
// queue submit.
// ═══════════════════════════════════════════════════════════════════════════════
namespace mvrvb {
void replayCommandBufferOnMTL(MvCommandBuffer* cb, id<MTLCommandBuffer> mtlCB);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Fence ────────────────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

extern "C" {

VkResult mvb_CreateFence(VkDevice device,
                         const VkFenceCreateInfo* pCI,
                         const VkAllocationCallbacks*,
                         VkFence* pFence) {
    @autoreleasepool {
        auto* dev = toMv(device);
        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)dev->mtlDevice;
        auto* fence = new MvFence();
        id<MTLSharedEvent> ev = [mtlDev newSharedEvent];
        fence->mtlSharedEvent = (__bridge_retained void*)ev;
        fence->signaled.store((pCI->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
        fence->eventValue = 0;
        *pFence = reinterpret_cast<VkFence>(fence);
        return VK_SUCCESS;
    }
}

void mvb_DestroyFence(VkDevice, VkFence fence, const VkAllocationCallbacks*) {
    auto* f = toMv(fence);
    if (!f) return;
    if (f->mtlSharedEvent) CFRelease((__bridge CFTypeRef)f->mtlSharedEvent);
    delete f;
}

VkResult mvb_ResetFences(VkDevice, uint32_t count, const VkFence* pFences) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* f = toMv(pFences[i]);
        if (f) {
            f->signaled.store(false);
            // Increment event value so previous listeners don't fire.
            f->eventValue++;
        }
    }
    return VK_SUCCESS;
}

VkResult mvb_GetFenceStatus(VkDevice, VkFence fence) {
    auto* f = toMv(fence);
    return (f && f->signaled.load()) ? VK_SUCCESS : VK_NOT_READY;
}

VkResult mvb_WaitForFences(VkDevice, uint32_t count, const VkFence* pFences,
                           VkBool32 waitAll, uint64_t timeoutNs) {
    @autoreleasepool {
        if (waitAll) {
            // Wait for ALL fences.
            for (uint32_t i = 0; i < count; ++i) {
                auto* f = toMv(pFences[i]);
                if (!f) continue;
                if (f->signaled.load()) continue;

                id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)f->mtlSharedEvent;
                uint64_t targetValue = f->eventValue + 1;

                // Fast path: already signaled on the Metal side.
                if ([ev signaledValue] >= targetValue) {
                    f->signaled.store(true);
                    continue;
                }

                dispatch_semaphore_t sem = dispatch_semaphore_create(0);
                MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];
                [ev notifyListener:listener atValue:targetValue
                             block:^(id<MTLSharedEvent>, uint64_t) {
                    f->signaled.store(true);
                    dispatch_semaphore_signal(sem);
                }];

                dispatch_time_t deadline = (timeoutNs == UINT64_MAX)
                    ? DISPATCH_TIME_FOREVER
                    : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutNs);
                if (dispatch_semaphore_wait(sem, deadline) != 0) {
                    return VK_TIMEOUT;
                }
            }
        } else {
            // Wait for ANY fence.  Spin-poll if multiple fences, since
            // dispatch_semaphore can only block on one at a time.
            if (count == 0) return VK_SUCCESS;
            if (count == 1) {
                return mvb_WaitForFences(VK_NULL_HANDLE, 1, pFences, VK_TRUE, timeoutNs);
            }

            // Set up listeners for all fences; first one to fire unblocks us.
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];

            for (uint32_t i = 0; i < count; ++i) {
                auto* f = toMv(pFences[i]);
                if (!f) continue;
                if (f->signaled.load()) return VK_SUCCESS;  // Already done.

                id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)f->mtlSharedEvent;
                uint64_t targetValue = f->eventValue + 1;
                if ([ev signaledValue] >= targetValue) {
                    f->signaled.store(true);
                    return VK_SUCCESS;
                }

                [ev notifyListener:listener atValue:targetValue
                             block:^(id<MTLSharedEvent>, uint64_t) {
                    f->signaled.store(true);
                    dispatch_semaphore_signal(sem);
                }];
            }

            dispatch_time_t deadline = (timeoutNs == UINT64_MAX)
                ? DISPATCH_TIME_FOREVER
                : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutNs);
            if (dispatch_semaphore_wait(sem, deadline) != 0) {
                return VK_TIMEOUT;
            }
        }
    }
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Semaphore ────────────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_CreateSemaphore(VkDevice device,
                             const VkSemaphoreCreateInfo* pCI,
                             const VkAllocationCallbacks*,
                             VkSemaphore* pSemaphore) {
    @autoreleasepool {
        auto* dev = toMv(device);
        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)dev->mtlDevice;
        auto* sem = new MvSemaphore();

        // Walk pNext chain for timeline semaphore type.
        const void* pNext = pCI->pNext;
        while (pNext) {
            const auto* h = static_cast<const VkBaseInStructure*>(pNext);
            if (h->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
                const auto* ti = reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(h);
                sem->isTimeline = (ti->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE);
                sem->counter    = ti->initialValue;
            }
            pNext = h->pNext;
        }

        if (sem->isTimeline) {
            id<MTLSharedEvent> ev = [mtlDev newSharedEvent];
            [ev setSignaledValue:sem->counter];
            sem->mtlEvent = (__bridge_retained void*)ev;
        } else {
            id<MTLEvent> ev = [mtlDev newEvent];
            sem->mtlEvent = (__bridge_retained void*)ev;
            sem->counter = 0;  // Binary: we'll use counter as signal/wait value
        }
        *pSemaphore = reinterpret_cast<VkSemaphore>(sem);
        return VK_SUCCESS;
    }
}

void mvb_DestroySemaphore(VkDevice, VkSemaphore semaphore, const VkAllocationCallbacks*) {
    auto* s = toMv(semaphore);
    if (!s) return;
    if (s->mtlEvent) CFRelease((__bridge CFTypeRef)s->mtlEvent);
    delete s;
}

VkResult mvb_GetSemaphoreCounterValue(VkDevice, VkSemaphore semaphore, uint64_t* pValue) {
    auto* s = toMv(semaphore);
    if (!s || !s->isTimeline) return VK_ERROR_UNKNOWN;
    id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
    if (pValue) *pValue = [ev signaledValue];
    return VK_SUCCESS;
}

VkResult mvb_SignalSemaphore(VkDevice, const VkSemaphoreSignalInfo* pInfo) {
    auto* s = toMv(pInfo->semaphore);
    if (!s || !s->isTimeline) return VK_ERROR_UNKNOWN;
    id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
    [ev setSignaledValue:pInfo->value];
    s->counter = pInfo->value;
    return VK_SUCCESS;
}

VkResult mvb_WaitSemaphores(VkDevice, const VkSemaphoreWaitInfo* pInfo, uint64_t timeoutNs) {
    @autoreleasepool {
        for (uint32_t i = 0; i < pInfo->semaphoreCount; ++i) {
            auto* s = toMv(pInfo->pSemaphores[i]);
            if (!s || !s->isTimeline) continue;
            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
            uint64_t target = pInfo->pValues[i];
            if ([ev signaledValue] >= target) continue;

            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            MTLSharedEventListener* listener = [[MTLSharedEventListener alloc] init];
            [ev notifyListener:listener atValue:target
                         block:^(id<MTLSharedEvent>, uint64_t) {
                dispatch_semaphore_signal(sem);
            }];

            dispatch_time_t deadline = (timeoutNs == UINT64_MAX)
                ? DISPATCH_TIME_FOREVER
                : dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutNs);
            if (dispatch_semaphore_wait(sem, deadline) != 0) {
                return VK_TIMEOUT;
            }
        }
    }
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Event ────────────────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_CreateEvent(VkDevice, const VkEventCreateInfo*,
                         const VkAllocationCallbacks*, VkEvent* pEvent) {
    auto* ev = new MvEvent();
    *pEvent = reinterpret_cast<VkEvent>(ev);
    return VK_SUCCESS;
}

void mvb_DestroyEvent(VkDevice, VkEvent event, const VkAllocationCallbacks*) {
    delete toMv(event);
}

VkResult mvb_GetEventStatus(VkDevice, VkEvent event) {
    auto* e = toMv(event);
    return (e && e->set.load()) ? VK_EVENT_SET : VK_EVENT_RESET;
}

VkResult mvb_SetEvent(VkDevice, VkEvent event) {
    auto* e = toMv(event);
    if (e) e->set.store(true);
    return VK_SUCCESS;
}

VkResult mvb_ResetEvent(VkDevice, VkEvent event) {
    auto* e = toMv(event);
    if (e) e->set.store(false);
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Queue submission ─────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

/// Encode semaphore waits before command buffer replay.
static void encodeWaitSemaphores(id<MTLCommandBuffer> mtlCB,
                                 uint32_t count,
                                 const VkSemaphore* pSemaphores,
                                 const uint64_t* pValues) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* s = toMv(pSemaphores[i]);
        if (!s || !s->mtlEvent) continue;

        if (s->isTimeline) {
            // Timeline: wait until the event reaches the specified value.
            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
            uint64_t target = pValues ? pValues[i] : s->counter;
            [mtlCB encodeWaitForEvent:ev value:target];
        } else {
            // Binary: wait for the event at the current counter value.
            id<MTLEvent> ev = (__bridge id<MTLEvent>)s->mtlEvent;
            [mtlCB encodeWaitForEvent:ev value:s->counter];
        }
    }
}

/// Encode semaphore signals after command buffer replay.
static void encodeSignalSemaphores(id<MTLCommandBuffer> mtlCB,
                                   uint32_t count,
                                   const VkSemaphore* pSemaphores,
                                   const uint64_t* pValues) {
    for (uint32_t i = 0; i < count; ++i) {
        auto* s = toMv(pSemaphores[i]);
        if (!s || !s->mtlEvent) continue;

        if (s->isTimeline) {
            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
            uint64_t target = pValues ? pValues[i] : s->counter + 1;
            [mtlCB encodeSignalEvent:ev value:target];
            s->counter = target;
        } else {
            id<MTLEvent> ev = (__bridge id<MTLEvent>)s->mtlEvent;
            s->counter++;
            [mtlCB encodeSignalEvent:ev value:s->counter];
        }
    }
}

static void markCommandBufferSubmitted(MvCommandBuffer* cb) {
    if (!cb) return;

    cb->state = CmdBufState::Pending;
    if (cb->oneTimeSubmit) {
        cb->state = CmdBufState::Invalid;
    }
}

static void encodeFenceSignal(id<MTLCommandBuffer> mtlCB, MvFence* fence) {
    if (!mtlCB || !fence || !fence->mtlSharedEvent) return;

    fence->signaled.store(false);
    const uint64_t nextValue = fence->eventValue + 1;
    id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)fence->mtlSharedEvent;
    [mtlCB encodeSignalEvent:event value:nextValue];
    [mtlCB addCompletedHandler:^(id<MTLCommandBuffer>) {
        fence->signaled.store(true);
    }];
}

VkResult mvb_QueueSubmit(VkQueue queue, uint32_t submitCount,
                         const VkSubmitInfo* pSubmits, VkFence fence) {
    @autoreleasepool {
        auto* q = toMv(queue);
        if (!q || !q->queue) {
            MVRVB_LOG_ERROR("QueueSubmit failed: queue=%p", (void*)q);
            return VK_ERROR_DEVICE_LOST;
        }

        id<MTLCommandQueue> mtlQueue = (__bridge id<MTLCommandQueue>)q->queue;
        auto* submitFence = toMv(fence);

        MVRVB_LOG_INFO("QueueSubmit: submitCount=%u fence=%s",
                       submitCount,
                       submitFence ? "yes" : "no");

        for (uint32_t s = 0; s < submitCount; ++s) {
            const auto& submit = pSubmits[s];
            const bool isLastSubmit = (s == submitCount - 1);
            uint32_t firstReplayableCB = UINT32_MAX;
            uint32_t lastReplayableCB = UINT32_MAX;
            uint32_t replayableCount = 0;
            size_t recordedCommands = 0;

            for (uint32_t c = 0; c < submit.commandBufferCount; ++c) {
                auto* cb = toMv(submit.pCommandBuffers[c]);
                if (!cb || cb->commands.empty()) continue;

                replayableCount++;
                recordedCommands += cb->commands.size();
                if (firstReplayableCB == UINT32_MAX) {
                    firstReplayableCB = c;
                }
                lastReplayableCB = c;
            }

            MVRVB_LOG_DEBUG("QueueSubmit[%u]: cmdBuffers=%u replayable=%u commands=%zu waitSemaphores=%u signalSemaphores=%u",
                            s,
                            submit.commandBufferCount,
                            replayableCount,
                            recordedCommands,
                            submit.waitSemaphoreCount,
                            submit.signalSemaphoreCount);

            if (firstReplayableCB == UINT32_MAX) {
                const bool needsCarrierCB =
                    submit.waitSemaphoreCount > 0 ||
                    submit.signalSemaphoreCount > 0 ||
                    (isLastSubmit && submitFence != nullptr);

                if (needsCarrierCB) {
                    MVRVB_LOG_DEBUG("QueueSubmit[%u]: using carrier command buffer for synchronization-only submit",
                                    s);
                    id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
                    [mtlCB setLabel:@"MVB-Submit"];

                    if (submit.waitSemaphoreCount > 0) {
                        encodeWaitSemaphores(mtlCB, submit.waitSemaphoreCount,
                                             submit.pWaitSemaphores, nullptr);
                    }
                    if (submit.signalSemaphoreCount > 0) {
                        encodeSignalSemaphores(mtlCB, submit.signalSemaphoreCount,
                                               submit.pSignalSemaphores, nullptr);
                    }
                    if (isLastSubmit && submitFence) {
                        encodeFenceSignal(mtlCB, submitFence);
                    }

                    [mtlCB commit];
                }

                for (uint32_t c = 0; c < submit.commandBufferCount; ++c) {
                    markCommandBufferSubmitted(toMv(submit.pCommandBuffers[c]));
                }
                continue;
            }

            for (uint32_t c = 0; c < submit.commandBufferCount; ++c) {
                auto* cb = toMv(submit.pCommandBuffers[c]);
                if (!cb) continue;
                if (cb->commands.empty()) {
                    markCommandBufferSubmitted(cb);
                    continue;
                }

                id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
                [mtlCB setLabel:@"MVB-Submit"];

                // Wait semaphores (before first command buffer in this submit).
                if (c == firstReplayableCB && submit.waitSemaphoreCount > 0) {
                    encodeWaitSemaphores(mtlCB, submit.waitSemaphoreCount,
                                         submit.pWaitSemaphores, nullptr);
                }

                // Replay the deferred commands.
                replayCommandBufferOnMTL(cb, mtlCB);

                // Signal semaphores (after last command buffer in this submit).
                if (c == lastReplayableCB && submit.signalSemaphoreCount > 0) {
                    encodeSignalSemaphores(mtlCB, submit.signalSemaphoreCount,
                                           submit.pSignalSemaphores, nullptr);
                }

                // Fence signaling: attach to the very last command buffer
                // of the very last submit.
                if (isLastSubmit && c == lastReplayableCB && submitFence) {
                    encodeFenceSignal(mtlCB, submitFence);
                }

                [mtlCB commit];
                markCommandBufferSubmitted(cb);
            }
        }

        // Handle empty submits with just a fence signal.
        if (submitCount == 0 && submitFence) {
            MVRVB_LOG_DEBUG("QueueSubmit: issuing fence-only submit");
            id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
            [mtlCB setLabel:@"MVB-Submit"];
            encodeFenceSignal(mtlCB, submitFence);
            [mtlCB commit];
        }
    }
    return VK_SUCCESS;
}

VkResult mvb_QueueSubmit2(VkQueue queue, uint32_t submitCount,
                          const VkSubmitInfo2* pSubmits, VkFence fence) {
    @autoreleasepool {
        auto* q = toMv(queue);
        if (!q || !q->queue) {
            MVRVB_LOG_ERROR("QueueSubmit2 failed: queue=%p", (void*)q);
            return VK_ERROR_DEVICE_LOST;
        }

        id<MTLCommandQueue> mtlQueue = (__bridge id<MTLCommandQueue>)q->queue;
        auto* submitFence = toMv(fence);

        MVRVB_LOG_INFO("QueueSubmit2: submitCount=%u fence=%s",
                       submitCount,
                       submitFence ? "yes" : "no");

        for (uint32_t s = 0; s < submitCount; ++s) {
            const auto& submit = pSubmits[s];
            const bool isLastSubmit = (s == submitCount - 1);
            uint32_t firstReplayableCB = UINT32_MAX;
            uint32_t lastReplayableCB = UINT32_MAX;
            uint32_t replayableCount = 0;
            size_t recordedCommands = 0;

            for (uint32_t c = 0; c < submit.commandBufferInfoCount; ++c) {
                auto* cb = toMv(submit.pCommandBufferInfos[c].commandBuffer);
                if (!cb || cb->commands.empty()) continue;

                replayableCount++;
                recordedCommands += cb->commands.size();
                if (firstReplayableCB == UINT32_MAX) {
                    firstReplayableCB = c;
                }
                lastReplayableCB = c;
            }

            MVRVB_LOG_DEBUG("QueueSubmit2[%u]: cmdBuffers=%u replayable=%u commands=%zu waitSemaphores=%u signalSemaphores=%u",
                            s,
                            submit.commandBufferInfoCount,
                            replayableCount,
                            recordedCommands,
                            submit.waitSemaphoreInfoCount,
                            submit.signalSemaphoreInfoCount);

            if (firstReplayableCB == UINT32_MAX) {
                const bool needsCarrierCB =
                    submit.waitSemaphoreInfoCount > 0 ||
                    submit.signalSemaphoreInfoCount > 0 ||
                    (isLastSubmit && submitFence != nullptr);

                if (needsCarrierCB) {
                    MVRVB_LOG_DEBUG("QueueSubmit2[%u]: using carrier command buffer for synchronization-only submit",
                                    s);
                    id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
                    [mtlCB setLabel:@"MVB-Submit2"];

                    for (uint32_t w = 0; w < submit.waitSemaphoreInfoCount; ++w) {
                        const auto& si = submit.pWaitSemaphoreInfos[w];
                        auto* sem = toMv(si.semaphore);
                        if (!sem || !sem->mtlEvent) continue;

                        if (sem->isTimeline) {
                            [mtlCB encodeWaitForEvent:(__bridge id<MTLSharedEvent>)sem->mtlEvent
                                                 value:si.value];
                        } else {
                            [mtlCB encodeWaitForEvent:(__bridge id<MTLEvent>)sem->mtlEvent
                                                 value:sem->counter];
                        }
                    }

                    for (uint32_t sg = 0; sg < submit.signalSemaphoreInfoCount; ++sg) {
                        const auto& si = submit.pSignalSemaphoreInfos[sg];
                        auto* sem = toMv(si.semaphore);
                        if (!sem || !sem->mtlEvent) continue;

                        if (sem->isTimeline) {
                            [mtlCB encodeSignalEvent:(__bridge id<MTLSharedEvent>)sem->mtlEvent
                                                 value:si.value];
                            sem->counter = si.value;
                        } else {
                            sem->counter++;
                            [mtlCB encodeSignalEvent:(__bridge id<MTLEvent>)sem->mtlEvent
                                                 value:sem->counter];
                        }
                    }

                    if (isLastSubmit && submitFence) {
                        encodeFenceSignal(mtlCB, submitFence);
                    }

                    [mtlCB commit];
                }

                for (uint32_t c = 0; c < submit.commandBufferInfoCount; ++c) {
                    markCommandBufferSubmitted(toMv(submit.pCommandBufferInfos[c].commandBuffer));
                }
                continue;
            }

            for (uint32_t c = 0; c < submit.commandBufferInfoCount; ++c) {
                auto* cb = toMv(submit.pCommandBufferInfos[c].commandBuffer);
                if (!cb) continue;
                if (cb->commands.empty()) {
                    markCommandBufferSubmitted(cb);
                    continue;
                }

                id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
                [mtlCB setLabel:@"MVB-Submit2"];

                // Wait semaphores (before first CB).
                if (c == firstReplayableCB && submit.waitSemaphoreInfoCount > 0) {
                    for (uint32_t w = 0; w < submit.waitSemaphoreInfoCount; ++w) {
                        const auto& si = submit.pWaitSemaphoreInfos[w];
                        auto* sem = toMv(si.semaphore);
                        if (!sem || !sem->mtlEvent) continue;

                        if (sem->isTimeline) {
                            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)sem->mtlEvent;
                            [mtlCB encodeWaitForEvent:ev value:si.value];
                        } else {
                            id<MTLEvent> ev = (__bridge id<MTLEvent>)sem->mtlEvent;
                            [mtlCB encodeWaitForEvent:ev value:sem->counter];
                        }
                    }
                }

                // Replay the deferred commands.
                replayCommandBufferOnMTL(cb, mtlCB);

                // Signal semaphores (after last CB).
                if (c == lastReplayableCB && submit.signalSemaphoreInfoCount > 0) {
                    for (uint32_t sg = 0; sg < submit.signalSemaphoreInfoCount; ++sg) {
                        const auto& si = submit.pSignalSemaphoreInfos[sg];
                        auto* sem = toMv(si.semaphore);
                        if (!sem || !sem->mtlEvent) continue;

                        if (sem->isTimeline) {
                            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)sem->mtlEvent;
                            [mtlCB encodeSignalEvent:ev value:si.value];
                            sem->counter = si.value;
                        } else {
                            id<MTLEvent> ev = (__bridge id<MTLEvent>)sem->mtlEvent;
                            sem->counter++;
                            [mtlCB encodeSignalEvent:ev value:sem->counter];
                        }
                    }
                }

                // Fence signaling.
                if (isLastSubmit && c == lastReplayableCB && submitFence) {
                    encodeFenceSignal(mtlCB, submitFence);
                }

                [mtlCB commit];
                markCommandBufferSubmitted(cb);
            }
        }

        // Handle empty submits with just a fence signal.
        if (submitCount == 0 && submitFence) {
            MVRVB_LOG_DEBUG("QueueSubmit2: issuing fence-only submit");
            id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
            [mtlCB setLabel:@"MVB-Submit2"];
            encodeFenceSignal(mtlCB, submitFence);
            [mtlCB commit];
        }
    }
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Queue / device idle ─────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_QueueWaitIdle(VkQueue queue) {
    @autoreleasepool {
        auto* q = toMv(queue);
        if (!q || !q->queue) return VK_SUCCESS;
        id<MTLCommandQueue> mtlQueue = (__bridge id<MTLCommandQueue>)q->queue;

        MVRVB_LOG_DEBUG("QueueWaitIdle: waiting for queue=%p", (void*)q);

        // Submit an empty command buffer and wait for it to complete.
        // This guarantees all previously submitted work is done.
        id<MTLCommandBuffer> mtlCB = [mtlQueue commandBuffer];
        [mtlCB setLabel:@"MVB-WaitIdle"];
        [mtlCB commit];
        [mtlCB waitUntilCompleted];
        MVRVB_LOG_DEBUG("QueueWaitIdle complete: queue=%p", (void*)q);
    }
    return VK_SUCCESS;
}

VkResult mvb_DeviceWaitIdle(VkDevice device) {
    auto* dev = toMv(device);
    if (!dev) return VK_SUCCESS;
    MVRVB_LOG_DEBUG("DeviceWaitIdle: queueCount=%zu", dev->queues.size());
    for (auto* q : dev->queues) {
        mvb_QueueWaitIdle(toVk(q));
    }
    return VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Command recording for sync primitives ────────────────────────────────────
// These are recorded as deferred commands; replay happens in vk_commands.mm.
// ═══════════════════════════════════════════════════════════════════════════════

void vkCmdSetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags) {
    DeferredCmd cmd(CmdTag::SetEvent);
    cmd.setEvent.event = event;
    toMv(cb)->record(std::move(cmd));
}

void vkCmdSetEvent2(VkCommandBuffer cb, VkEvent event, const VkDependencyInfo*) {
    DeferredCmd cmd(CmdTag::SetEvent);
    cmd.setEvent.event = event;
    toMv(cb)->record(std::move(cmd));
}

void vkCmdResetEvent(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags) {
    DeferredCmd cmd(CmdTag::ResetEvent);
    cmd.resetEvent.event = event;
    toMv(cb)->record(std::move(cmd));
}

void vkCmdResetEvent2(VkCommandBuffer cb, VkEvent event, VkPipelineStageFlags2) {
    DeferredCmd cmd(CmdTag::ResetEvent);
    cmd.resetEvent.event = event;
    toMv(cb)->record(std::move(cmd));
}

void vkCmdWaitEvents(VkCommandBuffer, uint32_t, const VkEvent*,
                     VkPipelineStageFlags, VkPipelineStageFlags,
                     uint32_t, const VkMemoryBarrier*,
                     uint32_t, const VkBufferMemoryBarrier*,
                     uint32_t, const VkImageMemoryBarrier*) {
    // Events are CPU-side only in our implementation; memory barriers are
    // handled by Metal's automatic hazard tracking.  No-op.
}

void vkCmdWaitEvents2(VkCommandBuffer, uint32_t, const VkEvent*,
                      const VkDependencyInfo*) {
    // No-op — same reasoning as vkCmdWaitEvents.
}

// ── Pipeline barriers (recording) ───────────────────────────────────────────
// The recording side — just pushes a DeferredCmd.  The replay side is in
// vk_commands.mm (case CmdTag::PipelineBarrier / PipelineBarrier2).

void vkCmdPipelineBarrier(VkCommandBuffer cb,
                          VkPipelineStageFlags, VkPipelineStageFlags,
                          VkDependencyFlags,
                          uint32_t, const VkMemoryBarrier*,
                          uint32_t, const VkBufferMemoryBarrier*,
                          uint32_t, const VkImageMemoryBarrier*) {
    DeferredCmd cmd(CmdTag::PipelineBarrier);
    toMv(cb)->record(std::move(cmd));
}

void vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo*) {
    DeferredCmd cmd(CmdTag::PipelineBarrier2);
    toMv(cb)->record(std::move(cmd));
}

} // extern "C"
