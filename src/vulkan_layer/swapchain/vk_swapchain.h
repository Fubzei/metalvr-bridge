#pragma once
/**
 * @file vk_swapchain.h
 * @brief Milestone 8 — Swapchain and Presentation.
 *
 * MvSurface    → NSView + CAMetalLayer.
 * MvSwapchain  → CAMetalLayer configuration, drawable management, VkImage wrappers.
 *
 * Frame lifecycle:
 *   vkAcquireNextImageKHR  → [CAMetalLayer nextDrawable] + signal sem/fence
 *   vkQueuePresentKHR      → [MTLCommandBuffer presentDrawable:] + commit
 *
 * Surface creation supports:
 *   VK_EXT_metal_surface  — direct Metal/CAMetalLayer surface
 *   VK_KHR_win32_surface  — Wine HWND → NSView via macdrv bridge
 *   VkMacOSSurfaceMVK     — MoltenVK-style NSView surface
 */

#include <vulkan/vulkan.h>

// VK_EXT_metal_surface: may not be defined on all Vulkan SDK versions.
#ifndef VK_EXT_METAL_SURFACE_EXTENSION_NAME
typedef struct VkMetalSurfaceCreateInfoEXT {
    VkStructureType sType;
    const void*     pNext;
    uint32_t        flags;
    const void*     pLayer; ///< CAMetalLayer*
} VkMetalSurfaceCreateInfoEXT;
#endif

// VK_KHR_win32_surface: only defined when VK_USE_PLATFORM_WIN32_KHR is set.
// On macOS under Wine, the HWND is actually an NSView*/NSWindow* via macdrv.
#ifndef VK_KHR_WIN32_SURFACE_EXTENSION_NAME
typedef void* HWND;
typedef void* HINSTANCE;
typedef struct VkWin32SurfaceCreateInfoKHR {
    VkStructureType sType;
    const void*     pNext;
    uint32_t        flags;
    HINSTANCE       hinstance;
    HWND            hwnd;
} VkWin32SurfaceCreateInfoKHR;
#endif

#include "../resources/vk_resources.h"
#include <vector>
#include <mutex>
#include <cstdint>

namespace mvrvb {

// ── VkSurfaceKHR wrapper ────────────────────────────────────────────────────
struct MvSurface {
    void*    nsView{nullptr};      ///< NSView* (retained via __bridge_retained)
    void*    metalLayer{nullptr};  ///< CAMetalLayer* (retained)
    bool     ownsLayer{true};      ///< Did we create the layer (vs passed in)?
};

// ── Per-image data in the swapchain ─────────────────────────────────────────
struct SwapchainImage {
    MvImage     image;             ///< Thin wrapper around the drawable's MTLTexture
    MvImageView imageView;         ///< View over the drawable texture
    void*       drawable{nullptr}; ///< id<CAMetalDrawable> (retained)
    bool        acquired{false};
};

// ── VkSwapchainKHR wrapper ──────────────────────────────────────────────────
struct MvSwapchain {
    MvSurface*                  surface{nullptr};
    std::vector<SwapchainImage> images;
    uint32_t                    imageCount{0};
    uint32_t                    currentIndex{0};
    VkFormat                    format{VK_FORMAT_B8G8R8A8_UNORM};
    VkExtent2D                  extent{};
    VkPresentModeKHR            presentMode{VK_PRESENT_MODE_FIFO_KHR};
    std::mutex                  mutex;
    bool                        isVR{false}; ///< Route to VR compositor
};

// ── Handle casts ────────────────────────────────────────────────────────────
inline MvSurface*   toMv(VkSurfaceKHR   h) { return reinterpret_cast<MvSurface*>(h);   }
inline MvSwapchain* toMv(VkSwapchainKHR h) { return reinterpret_cast<MvSwapchain*>(h); }
inline VkSurfaceKHR   toVk(MvSurface*   p) { return reinterpret_cast<VkSurfaceKHR>(p);   }
inline VkSwapchainKHR toVk(MvSwapchain* p) { return reinterpret_cast<VkSwapchainKHR>(p); }

} // namespace mvrvb

#ifdef __cplusplus
extern "C" {
#endif

// ── Surface creation ────────────────────────────────────────────────────────
VkResult mvb_CreateMetalSurfaceEXT(VkInstance instance,
                                   const VkMetalSurfaceCreateInfoEXT* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkSurfaceKHR* pSurface);

VkResult mvb_CreateWin32SurfaceKHR(VkInstance instance,
                                   const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkSurfaceKHR* pSurface);

VkResult mvb_CreateMacOSSurfaceMVK(VkInstance instance,
                                   const void* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkSurfaceKHR* pSurface);

void     mvb_DestroySurfaceKHR(VkInstance instance,
                               VkSurfaceKHR surface,
                               const VkAllocationCallbacks* pAllocator);

// ── Surface queries ─────────────────────────────────────────────────────────
VkResult mvb_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                                uint32_t queueFamilyIndex,
                                                VkSurfaceKHR surface,
                                                VkBool32* pSupported);

VkResult mvb_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                                     VkSurfaceKHR surface,
                                                     VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);

VkResult mvb_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                                VkSurfaceKHR surface,
                                                uint32_t* pSurfaceFormatCount,
                                                VkSurfaceFormatKHR* pSurfaceFormats);

VkResult mvb_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                                     VkSurfaceKHR surface,
                                                     uint32_t* pPresentModeCount,
                                                     VkPresentModeKHR* pPresentModes);

// ── Swapchain lifecycle ─────────────────────────────────────────────────────
VkResult mvb_CreateSwapchainKHR(VkDevice device,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                VkSwapchainKHR* pSwapchain);

void     mvb_DestroySwapchainKHR(VkDevice device,
                                 VkSwapchainKHR swapchain,
                                 const VkAllocationCallbacks* pAllocator);

VkResult mvb_GetSwapchainImagesKHR(VkDevice device,
                                   VkSwapchainKHR swapchain,
                                   uint32_t* pSwapchainImageCount,
                                   VkImage* pSwapchainImages);

// ── Image acquisition & presentation ────────────────────────────────────────
VkResult mvb_AcquireNextImageKHR(VkDevice device,
                                 VkSwapchainKHR swapchain,
                                 uint64_t timeout,
                                 VkSemaphore semaphore,
                                 VkFence fence,
                                 uint32_t* pImageIndex);

VkResult mvb_AcquireNextImage2KHR(VkDevice device,
                                  const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                  uint32_t* pImageIndex);

VkResult mvb_QueuePresentKHR(VkQueue queue,
                             const VkPresentInfoKHR* pPresentInfo);

#ifdef __cplusplus
} // extern "C"
#endif
