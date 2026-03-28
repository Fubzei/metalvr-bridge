/**
 * @file vulkan_icd.cpp
 * @brief ICD loader negotiation and full proc-addr dispatch table.
 *
 * The Vulkan loader (libvulkan.dylib) opens this dylib, finds the three
 * exported symbols below, and calls them to discover all Vulkan entry points.
 *
 * Loader ABI requirements (vulkan_loader_icd interface version 5):
 *   - vk_icdNegotiateLoaderICDInterfaceVersion  → returns supported version
 *   - vk_icdGetInstanceProcAddr                 → instance + global funcs
 *   - vk_icdGetPhysicalDeviceProcAddr           → phys-device funcs
 *
 * The dispatch table is a flat sorted array of { name, fn } pairs searched
 * with std::lower_bound for O(log N) lookup — fast even for 200+ entries.
 */

#include "vulkan_icd.h"
#include "../device/vk_device.h"
#include "../sync/vk_sync.h"
#include "../swapchain/vk_swapchain.h"
#include "../../common/logging.h"

#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>

// ─── Forward-declarations of every Vulkan entry point ────────────────────────
// (Implementations live in the per-subsystem .mm files.)
extern "C" {

// Instance / physical device
VkResult vkCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
void     vkDestroyInstance(VkInstance, const VkAllocationCallbacks*);
VkResult vkEnumerateInstanceExtensionProperties(const char*, uint32_t*, VkExtensionProperties*);
VkResult vkEnumerateInstanceLayerProperties(uint32_t*, VkLayerProperties*);
VkResult vkEnumerateInstanceVersion(uint32_t*);
VkResult vkEnumeratePhysicalDevices(VkInstance, uint32_t*, VkPhysicalDevice*);

void     vkGetPhysicalDeviceFeatures(VkPhysicalDevice, VkPhysicalDeviceFeatures*);
void     vkGetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2*);
void     vkGetPhysicalDeviceProperties(VkPhysicalDevice, VkPhysicalDeviceProperties*);
void     vkGetPhysicalDeviceProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2*);
void     vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
void     vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
void     vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties2*);
void     vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties*);
void     vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice, VkFormat, VkFormatProperties2*);
VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice, VkFormat, VkImageType, VkImageTiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties*);
VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char*, uint32_t*, VkExtensionProperties*);
VkResult vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t*, VkLayerProperties*);

// Logical device / queue
VkResult vkCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
void     vkDestroyDevice(VkDevice, const VkAllocationCallbacks*);
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice, const char*);
void     vkGetDeviceQueue(VkDevice, uint32_t, uint32_t, VkQueue*);
void     vkGetDeviceQueue2(VkDevice, const VkDeviceQueueInfo2*, VkQueue*);
// NOTE: vkQueueSubmit, vkQueueSubmit2, vkQueueWaitIdle, vkDeviceWaitIdle
// → replaced by mvb_* implementations in sync/vk_sync.mm (Milestone 7).

// NOTE: Surface / Swapchain / Presentation
// → replaced by mvb_* implementations in swapchain/vk_swapchain.mm (Milestone 8).

// Memory
VkResult vkAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory*);
void     vkFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*);
VkResult vkMapMemory(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkMemoryMapFlags, void**);
void     vkUnmapMemory(VkDevice, VkDeviceMemory);
VkResult vkFlushMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange*);
VkResult vkInvalidateMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange*);
void     vkGetDeviceMemoryCommitment(VkDevice, VkDeviceMemory, VkDeviceSize*);

// Buffer / image resources
VkResult vkCreateBuffer(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer*);
void     vkDestroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks*);
VkResult vkBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
VkResult vkBindBufferMemory2(VkDevice, uint32_t, const VkBindBufferMemoryInfo*);
void     vkGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements*);
void     vkGetBufferMemoryRequirements2(VkDevice, const VkBufferMemoryRequirementsInfo2*, VkMemoryRequirements2*);
VkResult vkCreateBufferView(VkDevice, const VkBufferViewCreateInfo*, const VkAllocationCallbacks*, VkBufferView*);
void     vkDestroyBufferView(VkDevice, VkBufferView, const VkAllocationCallbacks*);

VkResult vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
void     vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*);
VkResult vkBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize);
VkResult vkBindImageMemory2(VkDevice, uint32_t, const VkBindImageMemoryInfo*);
void     vkGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements*);
void     vkGetImageMemoryRequirements2(VkDevice, const VkImageMemoryRequirementsInfo2*, VkMemoryRequirements2*);
void     vkGetImageSubresourceLayout(VkDevice, VkImage, const VkImageSubresource*, VkSubresourceLayout*);
VkResult vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView*);
void     vkDestroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*);

// Sampler
VkResult vkCreateSampler(VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler*);
void     vkDestroySampler(VkDevice, VkSampler, const VkAllocationCallbacks*);
VkResult vkCreateSamplerYcbcrConversion(VkDevice, const VkSamplerYcbcrConversionCreateInfo*, const VkAllocationCallbacks*, VkSamplerYcbcrConversion*);
void     vkDestroySamplerYcbcrConversion(VkDevice, VkSamplerYcbcrConversion, const VkAllocationCallbacks*);

// Shader module (vk prefix — vk_pipeline.mm)
VkResult vkCreateShaderModule(VkDevice, const VkShaderModuleCreateInfo*, const VkAllocationCallbacks*, VkShaderModule*);
void     vkDestroyShaderModule(VkDevice, VkShaderModule, const VkAllocationCallbacks*);

// Pipeline — Milestone 5 (mvb_ prefix — vk_pipeline.mm)
VkResult mvb_CreatePipelineCache(VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache*);
void     mvb_DestroyPipelineCache(VkDevice, VkPipelineCache, const VkAllocationCallbacks*);
VkResult mvb_GetPipelineCacheData(VkDevice, VkPipelineCache, size_t*, void*);
VkResult mvb_MergePipelineCaches(VkDevice, VkPipelineCache, uint32_t, const VkPipelineCache*);
VkResult mvb_CreateGraphicsPipelines(VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
VkResult mvb_CreateComputePipelines(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const VkAllocationCallbacks*, VkPipeline*);
void     mvb_DestroyPipeline(VkDevice, VkPipeline, const VkAllocationCallbacks*);
VkResult mvb_CreatePipelineLayout(VkDevice, const VkPipelineLayoutCreateInfo*, const VkAllocationCallbacks*, VkPipelineLayout*);
void     mvb_DestroyPipelineLayout(VkDevice, VkPipelineLayout, const VkAllocationCallbacks*);

// Descriptor sets
VkResult mvb_CreateDescriptorSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout*);
void     mvb_DestroyDescriptorSetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*);
VkResult mvb_CreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool*);
void     mvb_DestroyDescriptorPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*);
VkResult mvb_ResetDescriptorPool(VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags);
VkResult mvb_AllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
VkResult mvb_FreeDescriptorSets(VkDevice, VkDescriptorPool, uint32_t, const VkDescriptorSet*);
void     mvb_UpdateDescriptorSets(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const VkCopyDescriptorSet*);
VkResult vkCreateDescriptorUpdateTemplate(VkDevice, const VkDescriptorUpdateTemplateCreateInfo*, const VkAllocationCallbacks*, VkDescriptorUpdateTemplate*);
void     vkDestroyDescriptorUpdateTemplate(VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks*);
void     vkUpdateDescriptorSetWithTemplate(VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void*);

// Render pass / framebuffer
VkResult vkCreateRenderPass(VkDevice, const VkRenderPassCreateInfo*, const VkAllocationCallbacks*, VkRenderPass*);
VkResult vkCreateRenderPass2(VkDevice, const VkRenderPassCreateInfo2*, const VkAllocationCallbacks*, VkRenderPass*);
void     vkDestroyRenderPass(VkDevice, VkRenderPass, const VkAllocationCallbacks*);
void     vkGetRenderAreaGranularity(VkDevice, VkRenderPass, VkExtent2D*);
VkResult vkCreateFramebuffer(VkDevice, const VkFramebufferCreateInfo*, const VkAllocationCallbacks*, VkFramebuffer*);
void     vkDestroyFramebuffer(VkDevice, VkFramebuffer, const VkAllocationCallbacks*);

// Command pool / buffer
VkResult vkCreateCommandPool(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool*);
void     vkDestroyCommandPool(VkDevice, VkCommandPool, const VkAllocationCallbacks*);
VkResult vkResetCommandPool(VkDevice, VkCommandPool, VkCommandPoolResetFlags);
void     vkTrimCommandPool(VkDevice, VkCommandPool, VkCommandPoolTrimFlags);
VkResult vkAllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
void     vkFreeCommandBuffers(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
VkResult vkBeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*);
VkResult vkEndCommandBuffer(VkCommandBuffer);
VkResult vkResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags);

// Draw / dispatch commands
void vkCmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline);
void vkCmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
void vkCmdBindVertexBuffers(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*);
void vkCmdBindVertexBuffers2(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*, const VkDeviceSize*, const VkDeviceSize*);
void vkCmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType);
void vkCmdDraw(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t);
void vkCmdDrawIndexed(VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
void vkCmdDrawIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
void vkCmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
void vkCmdDrawIndirectCount(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
void vkCmdDrawIndexedIndirectCount(VkCommandBuffer, VkBuffer, VkDeviceSize, VkBuffer, VkDeviceSize, uint32_t, uint32_t);
void vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
void vkCmdDispatchBase(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void vkCmdDispatchIndirect(VkCommandBuffer, VkBuffer, VkDeviceSize);

// Dynamic state
void vkCmdSetViewport(VkCommandBuffer, uint32_t, uint32_t, const VkViewport*);
void vkCmdSetScissor(VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*);
void vkCmdSetLineWidth(VkCommandBuffer, float);
void vkCmdSetDepthBias(VkCommandBuffer, float, float, float);
void vkCmdSetBlendConstants(VkCommandBuffer, const float[4]);
void vkCmdSetDepthBounds(VkCommandBuffer, float, float);
void vkCmdSetStencilCompareMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
void vkCmdSetStencilWriteMask(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
void vkCmdSetStencilReference(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
void vkCmdPushConstants(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*);

// Render pass recording
void vkCmdBeginRenderPass(VkCommandBuffer, const VkRenderPassBeginInfo*, VkSubpassContents);
void vkCmdBeginRenderPass2(VkCommandBuffer, const VkRenderPassBeginInfo*, const VkSubpassBeginInfo*);
void vkCmdNextSubpass(VkCommandBuffer, VkSubpassContents);
void vkCmdNextSubpass2(VkCommandBuffer, const VkSubpassBeginInfo*, const VkSubpassEndInfo*);
void vkCmdEndRenderPass(VkCommandBuffer);
void vkCmdEndRenderPass2(VkCommandBuffer, const VkSubpassEndInfo*);
void vkCmdExecuteCommands(VkCommandBuffer, uint32_t, const VkCommandBuffer*);
void vkCmdBeginRendering(VkCommandBuffer, const VkRenderingInfo*);
void vkCmdEndRendering(VkCommandBuffer);

// Copy / blit
void vkCmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*);
void vkCmdCopyBuffer2(VkCommandBuffer, const VkCopyBufferInfo2*);
void vkCmdCopyImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageCopy*);
void vkCmdCopyImage2(VkCommandBuffer, const VkCopyImageInfo2*);
void vkCmdBlitImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageBlit*, VkFilter);
void vkCmdBlitImage2(VkCommandBuffer, const VkBlitImageInfo2*);
void vkCmdCopyBufferToImage(VkCommandBuffer, VkBuffer, VkImage, VkImageLayout, uint32_t, const VkBufferImageCopy*);
void vkCmdCopyBufferToImage2(VkCommandBuffer, const VkCopyBufferToImageInfo2*);
void vkCmdCopyImageToBuffer(VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t, const VkBufferImageCopy*);
void vkCmdCopyImageToBuffer2(VkCommandBuffer, const VkCopyImageToBufferInfo2*);
void vkCmdUpdateBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, const void*);
void vkCmdFillBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, uint32_t);
void vkCmdClearColorImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t, const VkImageSubresourceRange*);
void vkCmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*);
void vkCmdClearAttachments(VkCommandBuffer, uint32_t, const VkClearAttachment*, uint32_t, const VkClearRect*);
void vkCmdResolveImage(VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t, const VkImageResolve*);
void vkCmdResolveImage2(VkCommandBuffer, const VkResolveImageInfo2*);

// Barriers — Milestone 7 (recording in sync/vk_sync.mm, replay in vk_commands.mm)
void vkCmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*);
void vkCmdPipelineBarrier2(VkCommandBuffer, const VkDependencyInfo*);

// Sync primitives — Milestone 7 (mvb_ prefix, sync/vk_sync.mm)
// Fence / Semaphore / Event lifecycle: mvb_ functions declared in vk_sync.h
// Queue submit / wait: mvb_ functions declared in vk_sync.h
// Cmd-level event/wait: vk-prefixed, implemented in sync/vk_sync.mm
void     vkCmdSetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
void     vkCmdSetEvent2(VkCommandBuffer, VkEvent, const VkDependencyInfo*);
void     vkCmdResetEvent(VkCommandBuffer, VkEvent, VkPipelineStageFlags);
void     vkCmdResetEvent2(VkCommandBuffer, VkEvent, VkPipelineStageFlags2);
void     vkCmdWaitEvents(VkCommandBuffer, uint32_t, const VkEvent*, VkPipelineStageFlags, VkPipelineStageFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t, const VkImageMemoryBarrier*);
void     vkCmdWaitEvents2(VkCommandBuffer, uint32_t, const VkEvent*, const VkDependencyInfo*);

// Queries
VkResult vkCreateQueryPool(VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool*);
void     vkDestroyQueryPool(VkDevice, VkQueryPool, const VkAllocationCallbacks*);
VkResult vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags);
void     vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
void     vkCmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags);
void     vkCmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
void     vkCmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t);
void     vkCmdWriteTimestamp2(VkCommandBuffer, VkPipelineStageFlags2, VkQueryPool, uint32_t);
void     vkCmdCopyQueryPoolResults(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags);

// Debug utils
VkResult vkCreateDebugUtilsMessengerEXT(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT*);
void     vkDestroyDebugUtilsMessengerEXT(VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks*);
void     vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer, const VkDebugUtilsLabelEXT*);
void     vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer);
void     vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer, const VkDebugUtilsLabelEXT*);
void     vkSetDebugUtilsObjectNameEXT(VkDevice, const VkDebugUtilsObjectNameInfoEXT*);
void     vkSetDebugUtilsObjectTagEXT(VkDevice, const VkDebugUtilsObjectTagInfoEXT*);
void     vkQueueBeginDebugUtilsLabelEXT(VkQueue, const VkDebugUtilsLabelEXT*);
void     vkQueueEndDebugUtilsLabelEXT(VkQueue);
void     vkQueueInsertDebugUtilsLabelEXT(VkQueue, const VkDebugUtilsLabelEXT*);

// Device address (Vulkan 1.2 / VK_KHR_buffer_device_address)
VkDeviceAddress vkGetBufferDeviceAddress(VkDevice, const VkBufferDeviceAddressInfo*);
VkDeviceAddress vkGetBufferDeviceAddressKHR(VkDevice, const VkBufferDeviceAddressInfo*);
uint64_t        vkGetBufferOpaqueCaptureAddress(VkDevice, const VkBufferDeviceAddressInfo*);
uint64_t        vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice, const VkDeviceMemoryOpaqueCaptureAddressInfo*);

} // extern "C"

// ─── Stub implementations for un-implemented entry points ────────────────────
// These return VK_SUCCESS / no-op so the loader and apps don't crash on
// capability queries or features we haven't wired up yet.

extern "C" {

static void     _stub_void()  {}
static VkResult _stub_ok()    { return VK_SUCCESS; }

// NOTE: Buffer/Image memory, BufferView, ImageSubresource, Sampler YCbCr,
// ShaderModule, and DeviceAddress stubs have been replaced by real
// implementations in vk_resources.mm.

// NOTE: vkCreatePipelineCache, vkDestroyPipelineCache, vkGetPipelineCacheData,
// vkMergePipelineCaches, vkCreateGraphicsPipelines, vkCreateComputePipelines,
// vkDestroyPipeline, vkCreatePipelineLayout, vkDestroyPipelineLayout
// → replaced by mvb_* implementations in vk_pipeline.mm (Milestone 5).

// NOTE: vkCreateDescriptorSetLayout, vkDestroyDescriptorSetLayout,
// vkCreateDescriptorPool, vkDestroyDescriptorPool, vkResetDescriptorPool,
// vkAllocateDescriptorSets, vkFreeDescriptorSets, vkUpdateDescriptorSets
// -> replaced by real implementations in descriptors/vk_descriptors.mm.

VkResult vkCreateDescriptorUpdateTemplate(VkDevice, const VkDescriptorUpdateTemplateCreateInfo*, const VkAllocationCallbacks*, VkDescriptorUpdateTemplate* p) {
    if (p) *p = VK_NULL_HANDLE; return VK_SUCCESS;
}
void vkDestroyDescriptorUpdateTemplate(VkDevice, VkDescriptorUpdateTemplate, const VkAllocationCallbacks*) {}
void vkUpdateDescriptorSetWithTemplate(VkDevice, VkDescriptorSet, VkDescriptorUpdateTemplate, const void*) {}

// NOTE: vkCreateRenderPass, vkCreateRenderPass2, vkDestroyRenderPass,
// vkGetRenderAreaGranularity, vkCreateFramebuffer, vkDestroyFramebuffer,
// vkResetCommandPool, vkTrimCommandPool, vkResetCommandBuffer,
// vkCmdBindVertexBuffers2, vkCmdDrawIndirect, vkCmdDrawIndexedIndirect,
// vkCmdDrawIndirectCount, vkCmdDrawIndexedIndirectCount,
// vkCmdDispatchBase, vkCmdDispatchIndirect,
// vkCmdSetLineWidth, vkCmdSetDepthBounds, vkCmdSetStencilCompareMask,
// vkCmdSetStencilWriteMask, vkCmdSetStencilReference, vkCmdSetBlendConstants,
// vkCmdBeginRenderPass2, vkCmdNextSubpass, vkCmdNextSubpass2,
// vkCmdEndRenderPass2, vkCmdExecuteCommands,
// vkCmdCopyBuffer2, vkCmdCopyImage, vkCmdCopyImage2, vkCmdBlitImage, vkCmdBlitImage2,
// vkCmdCopyBufferToImage2, vkCmdCopyImageToBuffer, vkCmdCopyImageToBuffer2,
// vkCmdUpdateBuffer, vkCmdFillBuffer, vkCmdClearColorImage,
// vkCmdClearDepthStencilImage, vkCmdClearAttachments,
// vkCmdResolveImage, vkCmdResolveImage2,
// vkCmdPipelineBarrier2, vkCmdSetEvent2, vkCmdResetEvent2, vkCmdWaitEvents2,
// vkQueueSubmit2
// → replaced by real implementations in vk_commands.mm / sync/vk_sync.mm.

VkResult vkCreateQueryPool(VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool* p) {
    if (p) *p = reinterpret_cast<VkQueryPool>(new char[8]());
    return VK_SUCCESS;
}
void     vkDestroyQueryPool(VkDevice, VkQueryPool h, const VkAllocationCallbacks*) { delete[] reinterpret_cast<char*>(h); }
VkResult vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags) { return VK_NOT_READY; }
void     vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t) {}
void     vkCmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, VkQueryControlFlags) {}
void     vkCmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t) {}
void     vkCmdWriteTimestamp(VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t) {}
void     vkCmdWriteTimestamp2(VkCommandBuffer, VkPipelineStageFlags2, VkQueryPool, uint32_t) {}
void     vkCmdCopyQueryPoolResults(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags) {}

VkResult vkCreateDebugUtilsMessengerEXT(VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* p) {
    if (p) *p = reinterpret_cast<VkDebugUtilsMessengerEXT>(new char[8]());
    return VK_SUCCESS;
}
void vkDestroyDebugUtilsMessengerEXT(VkInstance, VkDebugUtilsMessengerEXT h, const VkAllocationCallbacks*) { delete[] reinterpret_cast<char*>(h); }
// NOTE: vkCmdInsertDebugUtilsLabelEXT → vk_commands.mm
void vkSetDebugUtilsObjectNameEXT(VkDevice, const VkDebugUtilsObjectNameInfoEXT*) {}
void vkSetDebugUtilsObjectTagEXT(VkDevice, const VkDebugUtilsObjectTagInfoEXT*) {}
void vkQueueBeginDebugUtilsLabelEXT(VkQueue, const VkDebugUtilsLabelEXT*) {}
void vkQueueEndDebugUtilsLabelEXT(VkQueue) {}
void vkQueueInsertDebugUtilsLabelEXT(VkQueue, const VkDebugUtilsLabelEXT*) {}

// NOTE: vkQueueSubmit2 → sync/vk_sync.mm
// NOTE: vkAcquireNextImage2KHR → swapchain/vk_swapchain.mm (Milestone 8)

// NOTE: vkGetBufferDeviceAddress, vkGetBufferDeviceAddressKHR,
// vkGetBufferOpaqueCaptureAddress, vkGetDeviceMemoryOpaqueCaptureAddress → vk_resources.mm

} // extern "C"

// ─── Proc-addr dispatch table ─────────────────────────────────────────────────

namespace mvrvb {

struct ProcEntry {
    const char*        name;
    PFN_vkVoidFunction fn;
    bool operator<(const ProcEntry& o) const { return std::strcmp(name, o.name) < 0; }
    bool operator<(const char* n)      const { return std::strcmp(name, n) < 0; }
};

// clang-format off
static const ProcEntry kProcTable[] = {
    // ── A ──────────────────────────────────────────────────────────────────
    {"vkAcquireNextImage2KHR",               (PFN_vkVoidFunction)mvb_AcquireNextImage2KHR},
    {"vkAcquireNextImageKHR",                (PFN_vkVoidFunction)mvb_AcquireNextImageKHR},
    {"vkAllocateCommandBuffers",             (PFN_vkVoidFunction)vkAllocateCommandBuffers},
    {"vkAllocateDescriptorSets",             (PFN_vkVoidFunction)mvb_AllocateDescriptorSets},
    {"vkAllocateMemory",                     (PFN_vkVoidFunction)vkAllocateMemory},
    // ── B ──────────────────────────────────────────────────────────────────
    {"vkBeginCommandBuffer",                 (PFN_vkVoidFunction)vkBeginCommandBuffer},
    {"vkBindBufferMemory",                   (PFN_vkVoidFunction)vkBindBufferMemory},
    {"vkBindBufferMemory2",                  (PFN_vkVoidFunction)vkBindBufferMemory2},
    {"vkBindBufferMemory2KHR",               (PFN_vkVoidFunction)vkBindBufferMemory2},
    {"vkBindImageMemory",                    (PFN_vkVoidFunction)vkBindImageMemory},
    {"vkBindImageMemory2",                   (PFN_vkVoidFunction)vkBindImageMemory2},
    {"vkBindImageMemory2KHR",                (PFN_vkVoidFunction)vkBindImageMemory2},
    // ── C ──────────────────────────────────────────────────────────────────
    {"vkCmdBeginDebugUtilsLabelEXT",         (PFN_vkVoidFunction)vkCmdBeginDebugUtilsLabelEXT},
    {"vkCmdBeginQuery",                      (PFN_vkVoidFunction)vkCmdBeginQuery},
    {"vkCmdBeginRenderPass",                 (PFN_vkVoidFunction)vkCmdBeginRenderPass},
    {"vkCmdBeginRenderPass2",                (PFN_vkVoidFunction)vkCmdBeginRenderPass2},
    {"vkCmdBeginRenderPass2KHR",             (PFN_vkVoidFunction)vkCmdBeginRenderPass2},
    {"vkCmdBeginRendering",                  (PFN_vkVoidFunction)vkCmdBeginRendering},
    {"vkCmdBeginRenderingKHR",               (PFN_vkVoidFunction)vkCmdBeginRendering},
    {"vkCmdBindDescriptorSets",              (PFN_vkVoidFunction)vkCmdBindDescriptorSets},
    {"vkCmdBindIndexBuffer",                 (PFN_vkVoidFunction)vkCmdBindIndexBuffer},
    {"vkCmdBindPipeline",                    (PFN_vkVoidFunction)vkCmdBindPipeline},
    {"vkCmdBindVertexBuffers",               (PFN_vkVoidFunction)vkCmdBindVertexBuffers},
    {"vkCmdBindVertexBuffers2",              (PFN_vkVoidFunction)vkCmdBindVertexBuffers2},
    {"vkCmdBindVertexBuffers2EXT",           (PFN_vkVoidFunction)vkCmdBindVertexBuffers2},
    {"vkCmdBlitImage",                       (PFN_vkVoidFunction)vkCmdBlitImage},
    {"vkCmdBlitImage2",                      (PFN_vkVoidFunction)vkCmdBlitImage2},
    {"vkCmdBlitImage2KHR",                   (PFN_vkVoidFunction)vkCmdBlitImage2},
    {"vkCmdClearAttachments",                (PFN_vkVoidFunction)vkCmdClearAttachments},
    {"vkCmdClearColorImage",                 (PFN_vkVoidFunction)vkCmdClearColorImage},
    {"vkCmdClearDepthStencilImage",          (PFN_vkVoidFunction)vkCmdClearDepthStencilImage},
    {"vkCmdCopyBuffer",                      (PFN_vkVoidFunction)vkCmdCopyBuffer},
    {"vkCmdCopyBuffer2",                     (PFN_vkVoidFunction)vkCmdCopyBuffer2},
    {"vkCmdCopyBuffer2KHR",                  (PFN_vkVoidFunction)vkCmdCopyBuffer2},
    {"vkCmdCopyBufferToImage",               (PFN_vkVoidFunction)vkCmdCopyBufferToImage},
    {"vkCmdCopyBufferToImage2",              (PFN_vkVoidFunction)vkCmdCopyBufferToImage2},
    {"vkCmdCopyBufferToImage2KHR",           (PFN_vkVoidFunction)vkCmdCopyBufferToImage2},
    {"vkCmdCopyImage",                       (PFN_vkVoidFunction)vkCmdCopyImage},
    {"vkCmdCopyImage2",                      (PFN_vkVoidFunction)vkCmdCopyImage2},
    {"vkCmdCopyImage2KHR",                   (PFN_vkVoidFunction)vkCmdCopyImage2},
    {"vkCmdCopyImageToBuffer",               (PFN_vkVoidFunction)vkCmdCopyImageToBuffer},
    {"vkCmdCopyImageToBuffer2",              (PFN_vkVoidFunction)vkCmdCopyImageToBuffer2},
    {"vkCmdCopyImageToBuffer2KHR",           (PFN_vkVoidFunction)vkCmdCopyImageToBuffer2},
    {"vkCmdCopyQueryPoolResults",            (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults},
    {"vkCmdDispatch",                        (PFN_vkVoidFunction)vkCmdDispatch},
    {"vkCmdDispatchBase",                    (PFN_vkVoidFunction)vkCmdDispatchBase},
    {"vkCmdDispatchBaseKHR",                 (PFN_vkVoidFunction)vkCmdDispatchBase},
    {"vkCmdDispatchIndirect",                (PFN_vkVoidFunction)vkCmdDispatchIndirect},
    {"vkCmdDraw",                            (PFN_vkVoidFunction)vkCmdDraw},
    {"vkCmdDrawIndexed",                     (PFN_vkVoidFunction)vkCmdDrawIndexed},
    {"vkCmdDrawIndexedIndirect",             (PFN_vkVoidFunction)vkCmdDrawIndexedIndirect},
    {"vkCmdDrawIndexedIndirectCount",        (PFN_vkVoidFunction)vkCmdDrawIndexedIndirectCount},
    {"vkCmdDrawIndexedIndirectCountKHR",     (PFN_vkVoidFunction)vkCmdDrawIndexedIndirectCount},
    {"vkCmdDrawIndirect",                    (PFN_vkVoidFunction)vkCmdDrawIndirect},
    {"vkCmdDrawIndirectCount",               (PFN_vkVoidFunction)vkCmdDrawIndirectCount},
    {"vkCmdDrawIndirectCountKHR",            (PFN_vkVoidFunction)vkCmdDrawIndirectCount},
    {"vkCmdEndDebugUtilsLabelEXT",           (PFN_vkVoidFunction)vkCmdEndDebugUtilsLabelEXT},
    {"vkCmdEndQuery",                        (PFN_vkVoidFunction)vkCmdEndQuery},
    {"vkCmdEndRenderPass",                   (PFN_vkVoidFunction)vkCmdEndRenderPass},
    {"vkCmdEndRenderPass2",                  (PFN_vkVoidFunction)vkCmdEndRenderPass2},
    {"vkCmdEndRenderPass2KHR",               (PFN_vkVoidFunction)vkCmdEndRenderPass2},
    {"vkCmdEndRendering",                    (PFN_vkVoidFunction)vkCmdEndRendering},
    {"vkCmdEndRenderingKHR",                 (PFN_vkVoidFunction)vkCmdEndRendering},
    {"vkCmdExecuteCommands",                 (PFN_vkVoidFunction)vkCmdExecuteCommands},
    {"vkCmdFillBuffer",                      (PFN_vkVoidFunction)vkCmdFillBuffer},
    {"vkCmdInsertDebugUtilsLabelEXT",        (PFN_vkVoidFunction)vkCmdInsertDebugUtilsLabelEXT},
    {"vkCmdNextSubpass",                     (PFN_vkVoidFunction)vkCmdNextSubpass},
    {"vkCmdNextSubpass2",                    (PFN_vkVoidFunction)vkCmdNextSubpass2},
    {"vkCmdNextSubpass2KHR",                 (PFN_vkVoidFunction)vkCmdNextSubpass2},
    {"vkCmdPipelineBarrier",                 (PFN_vkVoidFunction)vkCmdPipelineBarrier},
    {"vkCmdPipelineBarrier2",                (PFN_vkVoidFunction)vkCmdPipelineBarrier2},
    {"vkCmdPipelineBarrier2KHR",             (PFN_vkVoidFunction)vkCmdPipelineBarrier2},
    {"vkCmdPushConstants",                   (PFN_vkVoidFunction)vkCmdPushConstants},
    {"vkCmdResetEvent",                      (PFN_vkVoidFunction)vkCmdResetEvent},
    {"vkCmdResetEvent2",                     (PFN_vkVoidFunction)vkCmdResetEvent2},
    {"vkCmdResetEvent2KHR",                  (PFN_vkVoidFunction)vkCmdResetEvent2},
    {"vkCmdResetQueryPool",                  (PFN_vkVoidFunction)vkCmdResetQueryPool},
    {"vkCmdResolveImage",                    (PFN_vkVoidFunction)vkCmdResolveImage},
    {"vkCmdResolveImage2",                   (PFN_vkVoidFunction)vkCmdResolveImage2},
    {"vkCmdResolveImage2KHR",                (PFN_vkVoidFunction)vkCmdResolveImage2},
    {"vkCmdSetBlendConstants",               (PFN_vkVoidFunction)vkCmdSetBlendConstants},
    {"vkCmdSetDepthBias",                    (PFN_vkVoidFunction)vkCmdSetDepthBias},
    {"vkCmdSetDepthBounds",                  (PFN_vkVoidFunction)vkCmdSetDepthBounds},
    {"vkCmdSetEvent",                        (PFN_vkVoidFunction)vkCmdSetEvent},
    {"vkCmdSetEvent2",                       (PFN_vkVoidFunction)vkCmdSetEvent2},
    {"vkCmdSetEvent2KHR",                    (PFN_vkVoidFunction)vkCmdSetEvent2},
    {"vkCmdSetLineWidth",                    (PFN_vkVoidFunction)vkCmdSetLineWidth},
    {"vkCmdSetScissor",                      (PFN_vkVoidFunction)vkCmdSetScissor},
    {"vkCmdSetStencilCompareMask",           (PFN_vkVoidFunction)vkCmdSetStencilCompareMask},
    {"vkCmdSetStencilReference",             (PFN_vkVoidFunction)vkCmdSetStencilReference},
    {"vkCmdSetStencilWriteMask",             (PFN_vkVoidFunction)vkCmdSetStencilWriteMask},
    {"vkCmdSetViewport",                     (PFN_vkVoidFunction)vkCmdSetViewport},
    {"vkCmdUpdateBuffer",                    (PFN_vkVoidFunction)vkCmdUpdateBuffer},
    {"vkCmdWaitEvents",                      (PFN_vkVoidFunction)vkCmdWaitEvents},
    {"vkCmdWaitEvents2",                     (PFN_vkVoidFunction)vkCmdWaitEvents2},
    {"vkCmdWaitEvents2KHR",                  (PFN_vkVoidFunction)vkCmdWaitEvents2},
    {"vkCmdWriteTimestamp",                  (PFN_vkVoidFunction)vkCmdWriteTimestamp},
    {"vkCmdWriteTimestamp2",                 (PFN_vkVoidFunction)vkCmdWriteTimestamp2},
    {"vkCmdWriteTimestamp2KHR",              (PFN_vkVoidFunction)vkCmdWriteTimestamp2},
    {"vkCreateBuffer",                       (PFN_vkVoidFunction)vkCreateBuffer},
    {"vkCreateBufferView",                   (PFN_vkVoidFunction)vkCreateBufferView},
    {"vkCreateCommandPool",                  (PFN_vkVoidFunction)vkCreateCommandPool},
    {"vkCreateComputePipelines",             (PFN_vkVoidFunction)mvb_CreateComputePipelines},
    {"vkCreateDebugUtilsMessengerEXT",       (PFN_vkVoidFunction)vkCreateDebugUtilsMessengerEXT},
    {"vkCreateDescriptorPool",               (PFN_vkVoidFunction)mvb_CreateDescriptorPool},
    {"vkCreateDescriptorSetLayout",          (PFN_vkVoidFunction)mvb_CreateDescriptorSetLayout},
    {"vkCreateDescriptorUpdateTemplate",     (PFN_vkVoidFunction)vkCreateDescriptorUpdateTemplate},
    {"vkCreateDescriptorUpdateTemplateKHR",  (PFN_vkVoidFunction)vkCreateDescriptorUpdateTemplate},
    {"vkCreateDevice",                       (PFN_vkVoidFunction)vkCreateDevice},
    {"vkCreateEvent",                        (PFN_vkVoidFunction)mvb_CreateEvent},
    {"vkCreateFence",                        (PFN_vkVoidFunction)mvb_CreateFence},
    {"vkCreateFramebuffer",                  (PFN_vkVoidFunction)vkCreateFramebuffer},
    {"vkCreateGraphicsPipelines",            (PFN_vkVoidFunction)mvb_CreateGraphicsPipelines},
    {"vkCreateImage",                        (PFN_vkVoidFunction)vkCreateImage},
    {"vkCreateImageView",                    (PFN_vkVoidFunction)vkCreateImageView},
    {"vkCreateInstance",                     (PFN_vkVoidFunction)vkCreateInstance},
    {"vkCreateMacOSSurfaceMVK",              (PFN_vkVoidFunction)mvb_CreateMacOSSurfaceMVK},
    {"vkCreateMetalSurfaceEXT",              (PFN_vkVoidFunction)mvb_CreateMetalSurfaceEXT},
    {"vkCreatePipelineCache",                (PFN_vkVoidFunction)mvb_CreatePipelineCache},
    {"vkCreatePipelineLayout",               (PFN_vkVoidFunction)mvb_CreatePipelineLayout},
    {"vkCreateQueryPool",                    (PFN_vkVoidFunction)vkCreateQueryPool},
    {"vkCreateRenderPass",                   (PFN_vkVoidFunction)vkCreateRenderPass},
    {"vkCreateRenderPass2",                  (PFN_vkVoidFunction)vkCreateRenderPass2},
    {"vkCreateRenderPass2KHR",               (PFN_vkVoidFunction)vkCreateRenderPass2},
    {"vkCreateSampler",                      (PFN_vkVoidFunction)vkCreateSampler},
    {"vkCreateSamplerYcbcrConversion",       (PFN_vkVoidFunction)vkCreateSamplerYcbcrConversion},
    {"vkCreateSamplerYcbcrConversionKHR",    (PFN_vkVoidFunction)vkCreateSamplerYcbcrConversion},
    {"vkCreateSemaphore",                    (PFN_vkVoidFunction)mvb_CreateSemaphore},
    {"vkCreateShaderModule",                 (PFN_vkVoidFunction)vkCreateShaderModule},
    {"vkCreateSwapchainKHR",                 (PFN_vkVoidFunction)mvb_CreateSwapchainKHR},
    {"vkCreateWin32SurfaceKHR",              (PFN_vkVoidFunction)mvb_CreateWin32SurfaceKHR},
    // ── D ──────────────────────────────────────────────────────────────────
    {"vkDestroyBuffer",                      (PFN_vkVoidFunction)vkDestroyBuffer},
    {"vkDestroyBufferView",                  (PFN_vkVoidFunction)vkDestroyBufferView},
    {"vkDestroyCommandPool",                 (PFN_vkVoidFunction)vkDestroyCommandPool},
    {"vkDestroyDebugUtilsMessengerEXT",      (PFN_vkVoidFunction)vkDestroyDebugUtilsMessengerEXT},
    {"vkDestroyDescriptorPool",              (PFN_vkVoidFunction)mvb_DestroyDescriptorPool},
    {"vkDestroyDescriptorSetLayout",         (PFN_vkVoidFunction)mvb_DestroyDescriptorSetLayout},
    {"vkDestroyDescriptorUpdateTemplate",    (PFN_vkVoidFunction)vkDestroyDescriptorUpdateTemplate},
    {"vkDestroyDescriptorUpdateTemplateKHR", (PFN_vkVoidFunction)vkDestroyDescriptorUpdateTemplate},
    {"vkDestroyDevice",                      (PFN_vkVoidFunction)vkDestroyDevice},
    {"vkDestroyEvent",                       (PFN_vkVoidFunction)mvb_DestroyEvent},
    {"vkDestroyFence",                       (PFN_vkVoidFunction)mvb_DestroyFence},
    {"vkDestroyFramebuffer",                 (PFN_vkVoidFunction)vkDestroyFramebuffer},
    {"vkDestroyImage",                       (PFN_vkVoidFunction)vkDestroyImage},
    {"vkDestroyImageView",                   (PFN_vkVoidFunction)vkDestroyImageView},
    {"vkDestroyInstance",                    (PFN_vkVoidFunction)vkDestroyInstance},
    {"vkDestroyPipeline",                    (PFN_vkVoidFunction)mvb_DestroyPipeline},
    {"vkDestroyPipelineCache",               (PFN_vkVoidFunction)mvb_DestroyPipelineCache},
    {"vkDestroyPipelineLayout",              (PFN_vkVoidFunction)mvb_DestroyPipelineLayout},
    {"vkDestroyQueryPool",                   (PFN_vkVoidFunction)vkDestroyQueryPool},
    {"vkDestroyRenderPass",                  (PFN_vkVoidFunction)vkDestroyRenderPass},
    {"vkDestroySampler",                     (PFN_vkVoidFunction)vkDestroySampler},
    {"vkDestroySamplerYcbcrConversion",      (PFN_vkVoidFunction)vkDestroySamplerYcbcrConversion},
    {"vkDestroySamplerYcbcrConversionKHR",   (PFN_vkVoidFunction)vkDestroySamplerYcbcrConversion},
    {"vkDestroySemaphore",                   (PFN_vkVoidFunction)mvb_DestroySemaphore},
    {"vkDestroyShaderModule",                (PFN_vkVoidFunction)vkDestroyShaderModule},
    {"vkDestroySurfaceKHR",                  (PFN_vkVoidFunction)mvb_DestroySurfaceKHR},
    {"vkDestroySwapchainKHR",                (PFN_vkVoidFunction)mvb_DestroySwapchainKHR},
    {"vkDeviceWaitIdle",                     (PFN_vkVoidFunction)mvb_DeviceWaitIdle},
    // ── E ──────────────────────────────────────────────────────────────────
    {"vkEndCommandBuffer",                   (PFN_vkVoidFunction)vkEndCommandBuffer},
    {"vkEnumerateDeviceExtensionProperties", (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties},
    {"vkEnumerateDeviceLayerProperties",     (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties},
    {"vkEnumerateInstanceExtensionProperties",(PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties},
    {"vkEnumerateInstanceLayerProperties",   (PFN_vkVoidFunction)vkEnumerateInstanceLayerProperties},
    {"vkEnumerateInstanceVersion",           (PFN_vkVoidFunction)vkEnumerateInstanceVersion},
    {"vkEnumeratePhysicalDevices",           (PFN_vkVoidFunction)vkEnumeratePhysicalDevices},
    // ── F ──────────────────────────────────────────────────────────────────
    {"vkFlushMappedMemoryRanges",            (PFN_vkVoidFunction)vkFlushMappedMemoryRanges},
    {"vkFreeCommandBuffers",                 (PFN_vkVoidFunction)vkFreeCommandBuffers},
    {"vkFreeDescriptorSets",                 (PFN_vkVoidFunction)mvb_FreeDescriptorSets},
    {"vkFreeMemory",                         (PFN_vkVoidFunction)vkFreeMemory},
    // ── G ──────────────────────────────────────────────────────────────────
    {"vkGetBufferDeviceAddress",             (PFN_vkVoidFunction)vkGetBufferDeviceAddress},
    {"vkGetBufferDeviceAddressEXT",          (PFN_vkVoidFunction)vkGetBufferDeviceAddress},
    {"vkGetBufferDeviceAddressKHR",          (PFN_vkVoidFunction)vkGetBufferDeviceAddressKHR},
    {"vkGetBufferMemoryRequirements",        (PFN_vkVoidFunction)vkGetBufferMemoryRequirements},
    {"vkGetBufferMemoryRequirements2",       (PFN_vkVoidFunction)vkGetBufferMemoryRequirements2},
    {"vkGetBufferMemoryRequirements2KHR",    (PFN_vkVoidFunction)vkGetBufferMemoryRequirements2},
    {"vkGetBufferOpaqueCaptureAddress",      (PFN_vkVoidFunction)vkGetBufferOpaqueCaptureAddress},
    {"vkGetBufferOpaqueCaptureAddressKHR",   (PFN_vkVoidFunction)vkGetBufferOpaqueCaptureAddress},
    {"vkGetDeviceMemoryCommitment",          (PFN_vkVoidFunction)vkGetDeviceMemoryCommitment},
    {"vkGetDeviceMemoryOpaqueCaptureAddress",(PFN_vkVoidFunction)vkGetDeviceMemoryOpaqueCaptureAddress},
    {"vkGetDeviceMemoryOpaqueCaptureAddressKHR",(PFN_vkVoidFunction)vkGetDeviceMemoryOpaqueCaptureAddress},
    {"vkGetDeviceProcAddr",                  (PFN_vkVoidFunction)vkGetDeviceProcAddr},
    {"vkGetDeviceQueue",                     (PFN_vkVoidFunction)vkGetDeviceQueue},
    {"vkGetDeviceQueue2",                    (PFN_vkVoidFunction)vkGetDeviceQueue2},
    {"vkGetEventStatus",                     (PFN_vkVoidFunction)mvb_GetEventStatus},
    {"vkGetFenceStatus",                     (PFN_vkVoidFunction)mvb_GetFenceStatus},
    {"vkGetImageMemoryRequirements",         (PFN_vkVoidFunction)vkGetImageMemoryRequirements},
    {"vkGetImageMemoryRequirements2",        (PFN_vkVoidFunction)vkGetImageMemoryRequirements2},
    {"vkGetImageMemoryRequirements2KHR",     (PFN_vkVoidFunction)vkGetImageMemoryRequirements2},
    {"vkGetImageSubresourceLayout",          (PFN_vkVoidFunction)vkGetImageSubresourceLayout},
    {"vkGetPhysicalDeviceFeatures",          (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures},
    {"vkGetPhysicalDeviceFeatures2",         (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2},
    {"vkGetPhysicalDeviceFeatures2KHR",      (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2},
    {"vkGetPhysicalDeviceFormatProperties",  (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties},
    {"vkGetPhysicalDeviceFormatProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties2},
    {"vkGetPhysicalDeviceFormatProperties2KHR",(PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties2},
    {"vkGetPhysicalDeviceImageFormatProperties",(PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties},
    {"vkGetPhysicalDeviceMemoryProperties",  (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties},
    {"vkGetPhysicalDeviceMemoryProperties2", (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties2},
    {"vkGetPhysicalDeviceMemoryProperties2KHR",(PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties2},
    {"vkGetPhysicalDeviceProperties",        (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties},
    {"vkGetPhysicalDeviceProperties2",       (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2},
    {"vkGetPhysicalDeviceProperties2KHR",    (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2},
    {"vkGetPhysicalDeviceQueueFamilyProperties",(PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties},
    {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR",(PFN_vkVoidFunction)mvb_GetPhysicalDeviceSurfaceCapabilitiesKHR},
    {"vkGetPhysicalDeviceSurfaceFormatsKHR", (PFN_vkVoidFunction)mvb_GetPhysicalDeviceSurfaceFormatsKHR},
    {"vkGetPhysicalDeviceSurfacePresentModesKHR",(PFN_vkVoidFunction)mvb_GetPhysicalDeviceSurfacePresentModesKHR},
    {"vkGetPhysicalDeviceSurfaceSupportKHR", (PFN_vkVoidFunction)mvb_GetPhysicalDeviceSurfaceSupportKHR},
    {"vkGetPipelineCacheData",               (PFN_vkVoidFunction)mvb_GetPipelineCacheData},
    {"vkGetQueryPoolResults",                (PFN_vkVoidFunction)vkGetQueryPoolResults},
    {"vkGetRenderAreaGranularity",           (PFN_vkVoidFunction)vkGetRenderAreaGranularity},
    {"vkGetSemaphoreCounterValue",           (PFN_vkVoidFunction)mvb_GetSemaphoreCounterValue},
    {"vkGetSemaphoreCounterValueKHR",        (PFN_vkVoidFunction)mvb_GetSemaphoreCounterValue},
    {"vkGetSwapchainImagesKHR",              (PFN_vkVoidFunction)mvb_GetSwapchainImagesKHR},
    // ── I ──────────────────────────────────────────────────────────────────
    {"vkInvalidateMappedMemoryRanges",       (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges},
    // ── M ──────────────────────────────────────────────────────────────────
    {"vkMapMemory",                          (PFN_vkVoidFunction)vkMapMemory},
    {"vkMergePipelineCaches",                (PFN_vkVoidFunction)mvb_MergePipelineCaches},
    // ── Q ──────────────────────────────────────────────────────────────────
    {"vkQueueBeginDebugUtilsLabelEXT",       (PFN_vkVoidFunction)vkQueueBeginDebugUtilsLabelEXT},
    {"vkQueueEndDebugUtilsLabelEXT",         (PFN_vkVoidFunction)vkQueueEndDebugUtilsLabelEXT},
    {"vkQueueInsertDebugUtilsLabelEXT",      (PFN_vkVoidFunction)vkQueueInsertDebugUtilsLabelEXT},
    {"vkQueuePresentKHR",                    (PFN_vkVoidFunction)mvb_QueuePresentKHR},
    {"vkQueueSubmit",                        (PFN_vkVoidFunction)mvb_QueueSubmit},
    {"vkQueueSubmit2",                       (PFN_vkVoidFunction)mvb_QueueSubmit2},
    {"vkQueueSubmit2KHR",                    (PFN_vkVoidFunction)mvb_QueueSubmit2},
    {"vkQueueWaitIdle",                      (PFN_vkVoidFunction)mvb_QueueWaitIdle},
    // ── R ──────────────────────────────────────────────────────────────────
    {"vkResetCommandBuffer",                 (PFN_vkVoidFunction)vkResetCommandBuffer},
    {"vkResetCommandPool",                   (PFN_vkVoidFunction)vkResetCommandPool},
    {"vkResetDescriptorPool",                (PFN_vkVoidFunction)mvb_ResetDescriptorPool},
    {"vkResetEvent",                         (PFN_vkVoidFunction)mvb_ResetEvent},
    {"vkResetFences",                        (PFN_vkVoidFunction)mvb_ResetFences},
    // ── S ──────────────────────────────────────────────────────────────────
    {"vkSetDebugUtilsObjectNameEXT",         (PFN_vkVoidFunction)vkSetDebugUtilsObjectNameEXT},
    {"vkSetDebugUtilsObjectTagEXT",          (PFN_vkVoidFunction)vkSetDebugUtilsObjectTagEXT},
    {"vkSetEvent",                           (PFN_vkVoidFunction)mvb_SetEvent},
    {"vkSignalSemaphore",                    (PFN_vkVoidFunction)mvb_SignalSemaphore},
    {"vkSignalSemaphoreKHR",                 (PFN_vkVoidFunction)mvb_SignalSemaphore},
    // ── T ──────────────────────────────────────────────────────────────────
    {"vkTrimCommandPool",                    (PFN_vkVoidFunction)vkTrimCommandPool},
    {"vkTrimCommandPoolKHR",                 (PFN_vkVoidFunction)vkTrimCommandPool},
    // ── U ──────────────────────────────────────────────────────────────────
    {"vkUnmapMemory",                        (PFN_vkVoidFunction)vkUnmapMemory},
    {"vkUpdateDescriptorSetWithTemplate",    (PFN_vkVoidFunction)vkUpdateDescriptorSetWithTemplate},
    {"vkUpdateDescriptorSetWithTemplateKHR", (PFN_vkVoidFunction)vkUpdateDescriptorSetWithTemplate},
    {"vkUpdateDescriptorSets",               (PFN_vkVoidFunction)mvb_UpdateDescriptorSets},
    // ── W ──────────────────────────────────────────────────────────────────
    {"vkWaitForFences",                      (PFN_vkVoidFunction)mvb_WaitForFences},
    {"vkWaitSemaphores",                     (PFN_vkVoidFunction)mvb_WaitSemaphores},
    {"vkWaitSemaphoresKHR",                  (PFN_vkVoidFunction)mvb_WaitSemaphores},
};
// clang-format on

static constexpr size_t kProcCount = sizeof(kProcTable) / sizeof(kProcTable[0]);

PFN_vkVoidFunction getICDProcAddr(const char* name) noexcept {
    if (!name) return nullptr;
    // Binary search — table is sorted lexicographically by construction.
    auto it = std::lower_bound(kProcTable, kProcTable + kProcCount, name,
        [](const ProcEntry& e, const char* n){ return std::strcmp(e.name, n) < 0; });
    if (it != kProcTable + kProcCount && std::strcmp(it->name, name) == 0)
        return it->fn;
    return nullptr;
}

void registerICDProcAddrs() {
    // Nothing to do at runtime — the table is statically initialised.
    MVRVB_LOG_DEBUG("ICD proc-addr table: %zu entries", kProcCount);
}

} // namespace mvrvb

// ─── ICD loader negotiation entry points ─────────────────────────────────────

extern "C" {

/**
 * vk_icdNegotiateLoaderICDInterfaceVersion
 *
 * The loader calls this first with its preferred version (5 or 6).
 * We support up to version 5 (Vulkan 1.2 loader ABI).
 */
MVVK_EXPORT VkResult vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pVersion) {
    static constexpr uint32_t kMinVersion = 2;
    static constexpr uint32_t kMaxVersion = 5;
    if (!pVersion) return VK_ERROR_INITIALIZATION_FAILED;
    if (*pVersion < kMinVersion) {
        MVRVB_LOG_ERROR("Loader ICD interface version %u too old (need >= %u)", *pVersion, kMinVersion);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    *pVersion = std::min(*pVersion, kMaxVersion);
    MVRVB_LOG_INFO("ICD interface version negotiated: %u", *pVersion);
    return VK_SUCCESS;
}

/**
 * vk_icdGetInstanceProcAddr
 *
 * Called by the loader for both global commands (instance == NULL) and
 * instance-level commands.  Also used by vkGetDeviceProcAddr internally.
 */
MVVK_EXPORT PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance /*instance*/,
                                                           const char* pName) {
    return mvrvb::getICDProcAddr(pName);
}

/**
 * vk_icdGetPhysicalDeviceProcAddr
 *
 * The loader uses this to intercept physical-device extension functions.
 * We forward to the same table.
 */
MVVK_EXPORT PFN_vkVoidFunction vk_icdGetPhysicalDeviceProcAddr(VkInstance /*instance*/,
                                                                 const char* pName) {
    return mvrvb::getICDProcAddr(pName);
}

/**
 * vkGetDeviceProcAddr
 *
 * Device-level proc addr lookup (called by the application directly or via
 * the loader).  Same table — device-level 