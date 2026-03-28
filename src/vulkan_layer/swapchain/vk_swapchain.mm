/**
 * @file vk_swapchain.mm
 * @brief Milestone 8 — VkSwapchainKHR → CAMetalLayer + nextDrawable() presentation.
 *
 * VkSurfaceKHR wraps an NSView + a CAMetalLayer.
 * VkSwapchainKHR wraps the layer's drawable management.
 *
 * Frame lifecycle:
 *   vkAcquireNextImageKHR → [CAMetalLayer nextDrawable] → signal semaphore/fence
 *   vkQueuePresentKHR     → [MTLCommandBuffer presentDrawable:] + commit
 *
 * Surface creation variants:
 *   VK_EXT_metal_surface  — pCAMetalLayer directly from the app
 *   VkMacOSSurfaceMVK     — NSView*, we attach a CAMetalLayer
 *   VK_KHR_win32_surface  — Wine HWND, translated to NSView via macdrv
 *
 * For VR use:
 *   The swapchain is bypassed entirely.
 *   vkQueuePresentKHR routes to the VR compositor instead.
 *   See src/vr_runtime/compositor/ for that path.
 */

#include "vk_swapchain.h"
#include "../device/vk_device.h"
#include "../sync/vk_sync.h"
#include "../format_table/format_table.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/NSView.h>
#import <AppKit/NSWindow.h>

#include <cstring>
#include <algorithm>

using namespace mvrvb;

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

/// Attach a CAMetalLayer to an NSView, returning the layer.
static CAMetalLayer* attachMetalLayer(NSView* view, id<MTLDevice> mtlDev) {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = mtlDev;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;  // Allow transfer dst
    layer.displaySyncEnabled = YES;
    [view setLayer:layer];
    [view setWantsLayer:YES];
    return layer;
}

/// Signal a semaphore and/or fence immediately (for AcquireNextImage).
/// Since nextDrawable is a CPU call and the drawable is immediately ready,
/// we signal synchronously.
static void signalAcquireSyncObjects(VkSemaphore semaphore, VkFence fence,
                                     id<MTLDevice> mtlDev,
                                     id<MTLCommandQueue> mtlQueue) {
    bool needSemaphore = (semaphore != VK_NULL_HANDLE);
    bool needFence     = (fence != VK_NULL_HANDLE);

    if (!needSemaphore && !needFence) return;

    // For semaphores: signal via an empty command buffer on the queue so
    // that the GPU timeline sees the event.
    if (needSemaphore) {
        auto* s = toMv(semaphore);
        if (s && s->mtlEvent) {
            if (s->isTimeline) {
                // Timeline: increment counter.
                id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
                s->counter++;
                id<MTLCommandBuffer> cb = [mtlQueue commandBuffer];
                [cb encodeSignalEvent:ev value:s->counter];
                [cb commit];
            } else {
                // Binary: signal via MTLEvent.
                id<MTLEvent> ev = (__bridge id<MTLEvent>)s->mtlEvent;
                s->counter++;
                id<MTLCommandBuffer> cb = [mtlQueue commandBuffer];
                [cb encodeSignalEvent:ev value:s->counter];
                [cb commit];
            }
        }
    }

    if (needFence) {
        auto* f = toMv(fence);
        if (f) {
            // Signal the fence immediately — the drawable is ready now.
            f->signaled.store(true);
            // Also bump the shared event so WaitForFences listeners fire.
            id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)f->mtlSharedEvent;
            uint64_t nextVal = f->eventValue + 1;
            id<MTLCommandBuffer> cb = [mtlQueue commandBuffer];
            [cb encodeSignalEvent:ev value:nextVal];
            [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
                f->signaled.store(true);
            }];
            [cb commit];
        }
    }
}

/// Resolve an MvDevice from a VkDevice. Needed for getting the command queue
/// for semaphore/fence signaling.
static id<MTLCommandQueue> getQueueFromDevice(VkDevice device) {
    auto* dev = toMv(device);
    if (!dev) return nil;
    return (__bridge id<MTLCommandQueue>)dev->commandQueue;
}

static id<MTLDevice> getMTLDeviceFromDevice(VkDevice device) {
    auto* dev = toMv(device);
    if (!dev) return nil;
    return (__bridge id<MTLDevice>)dev->mtlDevice;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Surface creation ────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

extern "C" {

VkResult mvb_CreateMetalSurfaceEXT(VkInstance,
                                   const VkMetalSurfaceCreateInfoEXT* pCI,
                                   const VkAllocationCallbacks*,
                                   VkSurfaceKHR* pSurface) {
    @autoreleasepool {
        if (!pCI || !pCI->pLayer || !pSurface) return VK_ERROR_INITIALIZATION_FAILED;

        auto* surface = new MvSurface();
        CAMetalLayer* layer = (__bridge CAMetalLayer*)pCI->pLayer;
        surface->metalLayer = (__bridge_retained void*)layer;
        surface->ownsLayer  = false;  // Caller owns the layer.

        // Try to extract the NSView from the layer's delegate or superlayer.
        if (layer.delegate && [layer.delegate isKindOfClass:[NSView class]]) {
            surface->nsView = (__bridge void*)(NSView*)layer.delegate;
        }

        *pSurface = reinterpret_cast<VkSurfaceKHR>(surface);
        MVRVB_LOG_INFO("VkSurface (Metal EXT) created for CAMetalLayer %p", pCI->pLayer);
        return VK_SUCCESS;
    }
}

VkResult mvb_CreateWin32SurfaceKHR(VkInstance instance,
                                   const VkWin32SurfaceCreateInfoKHR* pCI,
                                   const VkAllocationCallbacks*,
                                   VkSurfaceKHR* pSurface) {
    @autoreleasepool {
        if (!pCI || !pSurface) return VK_ERROR_INITIALIZATION_FAILED;

        // Wine/CrossOver translation: the HWND is actually an NSView* or an
        // NSWindow* on the macOS side (macdrv maps HWNDs to Cocoa objects).
        // Try to treat it as an NSView first.
        //
        // The macdrv HWND mapping convention:
        //   HWND → macdrv_win → NSView* (via macdrv_get_cocoa_view)
        //
        // If we're running inside Wine, the HWND pointer is typically a
        // macdrv_win handle. For direct NSView passthrough (common in DXVK
        // with Wine macdrv), the hwnd IS the NSView*.
        void* hwnd = (void*)(uintptr_t)pCI->hwnd;

        auto* surface = new MvSurface();
        id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();

        // Attempt to treat hwnd as NSView*.
        if ([(__bridge id)hwnd isKindOfClass:[NSView class]]) {
            NSView* view = (__bridge NSView*)hwnd;
            surface->nsView = hwnd;
            CAMetalLayer* layer = attachMetalLayer(view, mtlDev);
            surface->metalLayer = (__bridge_retained void*)layer;
            surface->ownsLayer  = true;
        }
        // Try NSWindow*.
        else if ([(__bridge id)hwnd isKindOfClass:[NSWindow class]]) {
            NSWindow* win = (__bridge NSWindow*)hwnd;
            NSView* view = [win contentView];
            surface->nsView = (__bridge void*)view;
            CAMetalLayer* layer = attachMetalLayer(view, mtlDev);
            surface->metalLayer = (__bridge_retained void*)layer;
            surface->ownsLayer  = true;
        }
        else {
            // Fallback: treat as opaque NSView* pointer anyway.
            // This works for most Wine macdrv configurations.
            NSView* view = (__bridge NSView*)hwnd;
            surface->nsView = hwnd;
            CAMetalLayer* layer = attachMetalLayer(view, mtlDev);
            surface->metalLayer = (__bridge_retained void*)layer;
            surface->ownsLayer  = true;
        }

        *pSurface = reinterpret_cast<VkSurfaceKHR>(surface);
        MVRVB_LOG_INFO("VkSurface (Win32/Wine) created for HWND %p", hwnd);
        return VK_SUCCESS;
    }
}

VkResult mvb_CreateMacOSSurfaceMVK(VkInstance,
                                   const void* pCI,
                                   const VkAllocationCallbacks*,
                                   VkSurfaceKHR* pSurface) {
    @autoreleasepool {
        // pCI points to VkMacOSSurfaceCreateInfoMVK.
        struct SurfaceCI {
            VkStructureType sType;
            const void* pNext;
            uint32_t flags;
            const void* pView;
        };
        auto* ci = static_cast<const SurfaceCI*>(pCI);
        if (!ci || !ci->pView || !pSurface) return VK_ERROR_INITIALIZATION_FAILED;

        auto* surface = new MvSurface();
        surface->nsView = const_cast<void*>(ci->pView);

        NSView* view = (__bridge NSView*)ci->pView;
        id<MTLDevice> mtlDev = MTLCreateSystemDefaultDevice();
        CAMetalLayer* layer = attachMetalLayer(view, mtlDev);
        surface->metalLayer = (__bridge_retained void*)layer;
        surface->ownsLayer  = true;

        *pSurface = reinterpret_cast<VkSurfaceKHR>(surface);
        MVRVB_LOG_INFO("VkSurface (MacOS MVK) created for NSView %p", ci->pView);
        return VK_SUCCESS;
    }
}

void mvb_DestroySurfaceKHR(VkInstance, VkSurfaceKHR surface, const VkAllocationCallbacks*) {
    auto* s = toMv(surface);
    if (!s) return;
    if (s->metalLayer) CFRelease((__bridge CFTypeRef)s->metalLayer);
    delete s;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Surface queries ─────────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t,
                                                VkSurfaceKHR, VkBool32* pSupported) {
    // All queue families support presentation on macOS via CAMetalLayer.
    if (pSupported) *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VkResult mvb_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice,
                                                     VkSurfaceKHR surface,
                                                     VkSurfaceCapabilitiesKHR* pCaps) {
    auto* s = toMv(surface);
    CAMetalLayer* layer = s ? (__bridge CAMetalLayer*)s->metalLayer : nil;
    CGSize size = layer ? [layer drawableSize] : CGSizeMake(1920, 1080);

    pCaps->minImageCount           = 2;
    pCaps->maxImageCount           = 3;
    pCaps->currentExtent           = {(uint32_t)size.width, (uint32_t)size.height};
    pCaps->minImageExtent          = {1, 1};
    pCaps->maxImageExtent          = {16384, 16384};
    pCaps->maxImageArrayLayers     = 1;
    pCaps->supportedTransforms     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCaps->currentTransform        = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pCaps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
                                     VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    pCaps->supportedUsageFlags     = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT;
    return VK_SUCCESS;
}

VkResult mvb_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR,
                                                uint32_t* pCount,
                                                VkSurfaceFormatKHR* pFormats) {
    static const VkSurfaceFormatKHR kFormats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB,  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
    };
    static constexpr uint32_t kN = sizeof(kFormats) / sizeof(kFormats[0]);

    if (!pFormats) { *pCount = kN; return VK_SUCCESS; }
    uint32_t n = std::min(*pCount, kN);
    std::memcpy(pFormats, kFormats, n * sizeof(VkSurfaceFormatKHR));
    *pCount = n;
    return (n < kN) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult mvb_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR,
                                                     uint32_t* pCount,
                                                     VkPresentModeKHR* pModes) {
    static const VkPresentModeKHR kModes[] = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static constexpr uint32_t kN = sizeof(kModes) / sizeof(kModes[0]);

    if (!pModes) { *pCount = kN; return VK_SUCCESS; }
    uint32_t n = std::min(*pCount, kN);
    std::memcpy(pModes, kModes, n * sizeof(VkPresentModeKHR));
    *pCount = n;
    return (n < kN) ? VK_INCOMPLETE : VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Swapchain lifecycle ─────────────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_CreateSwapchainKHR(VkDevice device,
                                const VkSwapchainCreateInfoKHR* pCI,
                                const VkAllocationCallbacks*,
                                VkSwapchainKHR* pSwapchain) {
    @autoreleasepool {
        auto* dev = toMv(device);
        (void)dev;
        auto* surface = toMv(pCI->surface);
        if (!surface) return VK_ERROR_INITIALIZATION_FAILED;

        CAMetalLayer* layer = (__bridge CAMetalLayer*)surface->metalLayer;

        auto* sc = new MvSwapchain();
        sc->surface     = surface;
        sc->format      = pCI->imageFormat;
        sc->extent      = pCI->imageExtent;
        sc->presentMode = pCI->presentMode;

        // ── Configure the Metal layer ───────────────────────────────────
        layer.pixelFormat  = vkFormatToMTL(pCI->imageFormat);
        layer.drawableSize = CGSizeMake(pCI->imageExtent.width, pCI->imageExtent.height);
        layer.framebufferOnly = !(pCI->imageUsage &
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
             VK_IMAGE_USAGE_STORAGE_BIT));

        // Image count: CAMetalLayer supports 2 or 3 drawables.
        uint32_t imgCount = std::clamp(pCI->minImageCount, 2u, 3u);
        layer.maximumDrawableCount = imgCount;
        sc->imageCount = imgCount;

        // V-sync control: FIFO = display-synced, IMMEDIATE/MAILBOX = not.
        layer.displaySyncEnabled = (pCI->presentMode == VK_PRESENT_MODE_FIFO_KHR);

        // Pre-allocate image slots (drawables are acquired lazily per-frame).
        sc->images.resize(imgCount);

        // Destroy old swapchain if provided.
        if (pCI->oldSwapchain != VK_NULL_HANDLE) {
            auto* old = toMv(pCI->oldSwapchain);
            for (auto& img : old->images) {
                if (img.drawable) CFRelease((__bridge CFTypeRef)img.drawable);
                img.drawable = nullptr;
            }
            // Don't delete yet — app may still reference images.
        }

        *pSwapchain = reinterpret_cast<VkSwapchainKHR>(sc);
        MVRVB_LOG_INFO("Swapchain created: %ux%u fmt=%u images=%u mode=%u",
                       pCI->imageExtent.width, pCI->imageExtent.height,
                       pCI->imageFormat, imgCount, pCI->presentMode);
        return VK_SUCCESS;
    }
}

void mvb_DestroySwapchainKHR(VkDevice, VkSwapchainKHR swapchain, const VkAllocationCallbacks*) {
    auto* sc = toMv(swapchain);
    if (!sc) return;
    for (auto& img : sc->images) {
        if (img.image.mtlTexture)     CFRelease((__bridge CFTypeRef)img.image.mtlTexture);
        if (img.imageView.mtlTexture) CFRelease((__bridge CFTypeRef)img.imageView.mtlTexture);
        if (img.drawable)             CFRelease((__bridge CFTypeRef)img.drawable);
    }
    delete sc;
}

VkResult mvb_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR swapchain,
                                   uint32_t* pCount, VkImage* pImages) {
    auto* sc = toMv(swapchain);
    if (!sc) return VK_ERROR_DEVICE_LOST;
    if (!pImages) { *pCount = (uint32_t)sc->images.size(); return VK_SUCCESS; }

    uint32_t n = std::min(*pCount, (uint32_t)sc->images.size());
    for (uint32_t i = 0; i < n; ++i) {
        pImages[i] = toVk(&sc->images[i].image);
    }
    *pCount = n;
    return (n < (uint32_t)sc->images.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

// ═══════════════════════════════════════════════════════════════════════════════
// ── Image acquisition & presentation ────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════════

VkResult mvb_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                 uint64_t /*timeout*/, VkSemaphore semaphore,
                                 VkFence fence, uint32_t* pImageIndex) {
    @autoreleasepool {
        auto* sc = toMv(swapchain);
        if (!sc || !sc->surface || !sc->surface->metalLayer)
            return VK_ERROR_SURFACE_LOST_KHR;

        CAMetalLayer* layer = (__bridge CAMetalLayer*)sc->surface->metalLayer;

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) {
            MVRVB_LOG_WARN("nextDrawable returned nil (layer not ready)");
            return VK_NOT_READY;
        }

        // Round-robin slot selection.
        uint32_t idx = sc->currentIndex;
        sc->currentIndex = (sc->currentIndex + 1) % (uint32_t)sc->images.size();

        auto& slot = sc->images[idx];

        // Release previous drawable/texture for this slot.
        if (slot.drawable) CFRelease((__bridge CFTypeRef)slot.drawable);
        if (slot.image.mtlTexture) CFRelease((__bridge CFTypeRef)slot.image.mtlTexture);

        slot.drawable         = (__bridge_retained void*)drawable;
        slot.image.mtlTexture = (__bridge_retained void*)[drawable texture];
        slot.image.format     = sc->format;
        slot.image.width      = sc->extent.width;
        slot.image.height     = sc->extent.height;
        slot.image.depth      = 1;
        slot.image.mipLevels  = 1;
        slot.image.arrayLayers = 1;
        slot.acquired         = true;

        *pImageIndex = idx;

        // Signal the semaphore and/or fence so the app knows the image is ready.
        id<MTLCommandQueue> mtlQueue = getQueueFromDevice(device);
        id<MTLDevice> mtlDev = getMTLDeviceFromDevice(device);
        signalAcquireSyncObjects(semaphore, fence, mtlDev, mtlQueue);

        return VK_SUCCESS;
    }
}

VkResult mvb_AcquireNextImage2KHR(VkDevice device,
                                  const VkAcquireNextImageInfoKHR* pInfo,
                                  uint32_t* pImageIndex) {
    if (!pInfo) return VK_ERROR_INITIALIZATION_FAILED;
    return mvb_AcquireNextImageKHR(device, pInfo->swapchain, pInfo->timeout,
                                   pInfo->semaphore, pInfo->fence, pImageIndex);
}

VkResult mvb_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPI) {
    @autoreleasepool {
        auto* q = toMv(queue);
        id<MTLCommandQueue> mtlQ = (__bridge id<MTLCommandQueue>)q->queue;

        // Wait on all wait semaphores before presenting.
        // We encode semaphore waits on the present command buffer.
        for (uint32_t si = 0; si < pPI->swapchainCount; ++si) {
            auto* sc = toMv(pPI->pSwapchains[si]);
            uint32_t idx = pPI->pImageIndices[si];
            if (!sc || idx >= (uint32_t)sc->images.size()) {
                if (pPI->pResults) pPI->pResults[si] = VK_ERROR_DEVICE_LOST;
                continue;
            }

            auto& slot = sc->images[idx];
            if (!slot.drawable) {
                if (pPI->pResults) pPI->pResults[si] = VK_ERROR_DEVICE_LOST;
                continue;
            }

            id<MTLCommandBuffer> presentCB = [mtlQ commandBuffer];
            [presentCB setLabel:@"MVB-Present"];

            // Encode wait semaphores on the first swapchain's present CB.
            if (si == 0 && pPI->waitSemaphoreCount > 0) {
                for (uint32_t w = 0; w < pPI->waitSemaphoreCount; ++w) {
                    auto* s = toMv(pPI->pWaitSemaphores[w]);
                    if (!s || !s->mtlEvent) continue;
                    if (s->isTimeline) {
                        id<MTLSharedEvent> ev = (__bridge id<MTLSharedEvent>)s->mtlEvent;
                        [presentCB encodeWaitForEvent:ev value:s->counter];
                    } else {
                        id<MTLEvent> ev = (__bridge id<MTLEvent>)s->mtlEvent;
                        [presentCB encodeWaitForEvent:ev value:s->counter];
                    }
                }
            }

            [presentCB presentDrawable:(__bridge id<CAMetalDrawable>)slot.drawable];
            [presentCB commit];

            // Release the drawable reference — Metal retains it internally
            // until presentation completes.
            CFRelease((__bridge CFTypeRef)slot.drawable);
            slot.drawable = nullptr;
            slot.acquired = false;

            if (pPI->pResults) pPI->pResults[si] = VK_SUCCESS;
        }
    }
    return VK_SUCCESS;
}

} // extern "C"
