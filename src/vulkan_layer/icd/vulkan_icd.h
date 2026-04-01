#pragma once
/**
 * @file vulkan_icd.h
 * @brief Vulkan Installable Client Driver (ICD) entry points and dispatch table.
 *
 * The Vulkan loader calls vk_icdGetInstanceProcAddr / vk_icdGetPhysicalDeviceProcAddr
 * to get function pointers.  We implement the full Vulkan 1.2 core API plus
 * the VK_KHR_surface, VK_MVK_macos_surface, and VK_KHR_swapchain extensions.
 *
 * Object representation:
 *   All Vulkan handles are 64-bit integers (non-dispatchable) or pointers
 *   (dispatchable: VkInstance, VkPhysicalDevice, VkDevice, VkQueue, VkCommandBuffer).
 *   Dispatchable handles MUST have the loader dispatch table as their first member.
 *
 * Memory layout:
 *   struct MvVkInstance { VkLayerInstanceDispatchTable* loaderTable; ... };
 *   The loaderTable pointer at offset 0 is mandated by the Vulkan loader ABI.
 */

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

#ifndef MVRVB_VK_METAL_SURFACE_CREATE_INFO_EXT_DEFINED
#define MVRVB_VK_METAL_SURFACE_CREATE_INFO_EXT_DEFINED
#ifndef VK_EXT_METAL_SURFACE_EXTENSION_NAME
typedef struct VkMetalSurfaceCreateInfoEXT {
    VkStructureType sType;
    const void*     pNext;
    uint32_t        flags;
    const void*     pLayer;
} VkMetalSurfaceCreateInfoEXT;
#endif
#endif

#ifndef MVRVB_VK_WIN32_SURFACE_CREATE_INFO_KHR_DEFINED
#define MVRVB_VK_WIN32_SURFACE_CREATE_INFO_KHR_DEFINED
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
#endif

// ── ICD export macros ─────────────────────────────────────────────────────────
#define MVVK_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

// ── ICD loader negotiation ────────────────────────────────────────────────────
/// Called by the Vulkan loader to get proc addresses.
MVVK_EXPORT PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
MVVK_EXPORT PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);
MVVK_EXPORT VkResult           vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion);

// ── Vulkan API entry points (full 1.2 core + essential extensions) ───────────

// Instance
MVVK_EXPORT VkResult vkCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
MVVK_EXPORT void     vkDestroyInstance(VkInstance, const VkAllocationCallbacks*);
MVVK_EXPORT VkResult vkEnumerateInstanceExtensionProperties(const char*, uint32_t*, VkExtensionProperties*);
MVVK_EXPORT VkResult vkEnumerateInstanceLayerProperties(uint32_t*, VkLayerProperties*);
MVVK_EXPORT VkResult vkEnumerateInstanceVersion(uint32_t*);

// Physical device
MVVK_EXPORT VkResult vkEnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);
MVVK_EXPORT void     vkGetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures*);
MVVK_EXPORT void     vkGetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
MVVK_EXPORT void     vkGetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties*);
MVVK_EXPORT void     vkGetPhysicalDeviceProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2*);
MVVK_EXPORT void     vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
MVVK_EXPORT void     vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
MVVK_EXPORT void     vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*);
MVVK_EXPORT void     vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties*);
MVVK_EXPORT void     vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice, VkFormat, VkFormatProperties2*);
MVVK_EXPORT VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties*);
MVVK_EXPORT VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
MVVK_EXPORT VkResult vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t*, VkLayerProperties*);

// Logical device
MVVK_EXPORT VkResult vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
MVVK_EXPORT void     vkDestroyDevice(VkDevice, const VkAllocationCallbacks*);
MVVK_EXPORT PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice, const char*);
MVVK_EXPORT void     vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue*);
MVVK_EXPORT void     vkGetDeviceQueue2(VkDevice, const VkDeviceQueueInfo2*, VkQueue*);

// Surface (macOS)
MVVK_EXPORT VkResult vkCreateMacOSSurfaceMVK(VkInstance, const void* /*VkMacOSSurfaceCreateInfoMVK*/, const VkAllocationCallbacks*, VkSurfaceKHR*);
MVVK_EXPORT void     vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*);
MVVK_EXPORT VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);
MVVK_EXPORT VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
MVVK_EXPORT VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
MVVK_EXPORT VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*);

// Queue submission
MVVK_EXPORT VkResult vkQueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
MVVK_EXPORT VkResult vkQueueSubmit2(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence);
MVVK_EXPORT VkResult vkQueueWaitIdle(VkQueue);
MVVK_EXPORT VkResult vkDeviceWaitIdle(VkDevice);
MVVK_EXPORT VkResult vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR*);

// Pipeline — Milestone 5 (mvb_ prefix, wired via dispatch table)
VkResult mvb_CreateGraphicsPipelines(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
VkResult mvb_CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
void     mvb_DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*);
VkResult mvb_CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
void     mvb_DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*);
VkResult mvb_CreatePipelineCache(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*);
void     mvb_DestroyPipelineCache(VkDevice, VkPipelineCache, const VkAllocationCallbacks*);
VkResult mvb_GetPipelineCacheData(VkDevice, VkPipelineCache, size_t*, void*);
VkResult mvb_MergePipelineCaches(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*);

// Descriptors — Milestone 6 (mvb_ prefix, wired via dispatch table)
VkResult mvb_CreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*);
void     mvb_DestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*);
VkResult mvb_CreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
void     mvb_DestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*);
VkResult mvb_ResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);
VkResult mvb_AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
VkResult mvb_FreeDescriptorSets(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*);
void     mvb_UpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*);

// Synchronization — Milestone 7 (mvb_ prefix, wired via dispatch table)
VkResult mvb_CreateFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence*);
void     mvb_DestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*);
VkResult mvb_ResetFences(VkDevice, uint32_t, const VkFence*);
VkResult mvb_GetFenceStatus(VkDevice, VkFence);
VkResult mvb_WaitForFences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t);
VkResult mvb_CreateSemaphore(VkDevice, const VkSemaphoreCreateInfo*, const VkAllocationCallbacks*, VkSemaphore*);
void     mvb_DestroySemaphore(VkDevice, VkSemaphore, const VkAllocationCallbacks*);
VkResult mvb_GetSemaphoreCounterValue(VkDevice, VkSemaphore, uint64_t*);
VkResult mvb_SignalSemaphore(VkDevice, const VkSemaphoreSignalInfo*);
VkResult mvb_WaitSemaphores(VkDevice, const VkSemaphoreWaitInfo*, uint64_t);
VkResult mvb_CreateEvent(VkDevice, const VkEventCreateInfo*, const VkAllocationCallbacks*, VkEvent*);
void     mvb_DestroyEvent(VkDevice, VkEvent, const VkAllocationCallbacks*);
VkResult mvb_GetEventStatus(VkDevice, VkEvent);
VkResult mvb_SetEvent(VkDevice, VkEvent);
VkResult mvb_ResetEvent(VkDevice, VkEvent);
VkResult mvb_QueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
VkResult mvb_QueueSubmit2(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence);
VkResult mvb_QueueWaitIdle(VkQueue);
VkResult mvb_DeviceWaitIdle(VkDevice);

// Swapchain & Presentation — Milestone 8 (mvb_ prefix, wired via dispatch table)
VkResult mvb_CreateMetalSurfaceEXT(VkInstance, const VkMetalSurfaceCreateInfoEXT*, const VkAllocationCallbacks*, VkSurfaceKHR*);
VkResult mvb_CreateWin32SurfaceKHR(VkInstance, const VkWin32SurfaceCreateInfoKHR*, const VkAllocationCallbacks*, VkSurfaceKHR*);
VkResult mvb_CreateMacOSSurfaceMVK(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*);
void     mvb_DestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*);
VkResult mvb_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32*);
VkResult mvb_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR*);
VkResult mvb_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkSurfaceFormatKHR*);
VkResult mvb_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, VkPresentModeKHR*);
VkResult mvb_CreateSwapchainKHR(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*, VkSwapchainKHR*);
void     mvb_DestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks*);
VkResult mvb_GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t*, VkImage*);
VkResult mvb_AcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t*);
VkResult mvb_AcquireNextImage2KHR(VkDevice, const VkAcquireNextImageInfoKHR*, uint32_t*);
VkResult mvb_QueuePresentKHR(VkQueue, const VkPresentInfoKHR*);

#ifdef __cplusplus
} // extern "C"
#endif

namespace mvrvb {

/// Register all our Vulkan entry points into an opaque proc-addr lookup table.
/// Called once at library init.
void registerICDProcAddrs();

/// Retrieve a procedure address by name (both instance- and device-level).
PFN_vkVoidFunction getICDProcAddr(const char* name) noexcept;

} // namespace mvrvb
