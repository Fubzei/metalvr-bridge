/**
 * @file vk_device.mm
 * @brief VkInstance / VkPhysicalDevice / VkDevice implementation.
 *
 * Physical device enumeration:
 *   We call MTLCopyAllDevices() to get all Metal devices on the system.
 *   On Apple Silicon, there is typically one GPU.  On Intel Macs, there may
 *   be both an integrated and a discrete GPU.
 *
 * Device feature reporting:
 *   We query the MTLDevice feature set and translate to VkPhysicalDeviceFeatures.
 *   Capabilities we cannot emulate are reported as VK_FALSE.
 *
 * Memory types (Apple Silicon unified memory):
 *   Type 0: HOST_VISIBLE | HOST_COHERENT | HOST_CACHED  (MTLStorageModeShared)
 *   Type 1: DEVICE_LOCAL                                (MTLStorageModePrivate)
 *   Heap 0: combined (AS has one heap).
 */

#include "vk_device.h"
#include "../../common/logging.h"
#include "../../shader_translator/cache/shader_cache.h"
#include "../memory/vk_memory.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <cstring>

namespace mvrvb {

// ── MvPhysicalDevice ──────────────────────────────────────────────────────────
void MvPhysicalDevice::populateFromMTLDevice() {
    id<MTLDevice> d = (__bridge id<MTLDevice>)mtlDevice;

    // ── Properties ───────────────────────────────────────────────────────────
    properties.apiVersion     = VK_API_VERSION_1_2;
    properties.driverVersion  = VK_MAKE_VERSION(0, 1, 0);
    properties.vendorID       = 0x106B; // Apple
    properties.deviceID       = 0xA001; // Generic Apple GPU
    properties.deviceType     = d.isLowPower ? VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
                                              : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    // Device name from Metal
    std::strncpy(properties.deviceName,
                 [[d name] UTF8String],
                 VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);

    // Pipeline cache UUID based on device name hash
    uint64_t nameHash = 0;
    for (char c : std::string([d.name UTF8String])) {
        nameHash = nameHash * 31 + c;
    }
    std::memcpy(properties.pipelineCacheUUID, &nameHash, 8);

    // ── Limits ───────────────────────────────────────────────────────────────
    VkPhysicalDeviceLimits& lim = properties.limits;
    lim.maxImageDimension1D                  = 16384;
    lim.maxImageDimension2D                  = 16384;
    lim.maxImageDimension3D                  = 2048;
    lim.maxImageDimensionCube                = 16384;
    lim.maxImageArrayLayers                  = 2048;
    lim.maxTexelBufferElements               = 64 * 1024 * 1024;
    lim.maxUniformBufferRange                = 64 * 1024;       // 64 KB constant buffer
    lim.maxStorageBufferRange                = (uint32_t)[d maxBufferLength];
    lim.maxPushConstantsSize                 = 128;             // bytes
    lim.maxMemoryAllocationCount             = 4096;
    lim.maxSamplerAllocationCount            = 1024;
    lim.maxBoundDescriptorSets               = 8;
    lim.maxPerStageDescriptorSamplers        = 16;
    lim.maxPerStageDescriptorUniformBuffers  = 15;
    lim.maxPerStageDescriptorStorageBuffers  = 30;
    lim.maxPerStageDescriptorSampledImages   = 96;
    lim.maxPerStageDescriptorStorageImages   = 8;
    lim.maxDescriptorSetSamplers             = 96;
    lim.maxDescriptorSetUniformBuffers       = 72;
    lim.maxDescriptorSetStorageBuffers       = 24;
    lim.maxDescriptorSetSampledImages        = 192;
    lim.maxDescriptorSetStorageImages        = 24;
    lim.maxVertexInputAttributes             = 31;
    lim.maxVertexInputBindings               = 31;
    lim.maxVertexInputAttributeOffset        = 2047;
    lim.maxVertexInputBindingStride          = 2048;
    lim.maxVertexOutputComponents            = 124;
    lim.maxFragmentInputComponents           = 124;
    lim.maxFragmentOutputAttachments         = 8;
    lim.maxFragmentCombinedOutputResources   = 8;
    lim.maxComputeSharedMemorySize           = 32768; // 32 KB threadgroup memory
    lim.maxComputeWorkGroupCount[0]          = 65535;
    lim.maxComputeWorkGroupCount[1]          = 65535;
    lim.maxComputeWorkGroupCount[2]          = 65535;
    lim.maxComputeWorkGroupInvocations       = 1024;
    lim.maxComputeWorkGroupSize[0]           = 1024;
    lim.maxComputeWorkGroupSize[1]           = 1024;
    lim.maxComputeWorkGroupSize[2]           = 64;
    lim.subPixelPrecisionBits                = 8;
    lim.subTexelPrecisionBits                = 8;
    lim.mipmapPrecisionBits                  = 8;
    lim.maxDrawIndexedIndexValue             = UINT32_MAX;
    lim.maxDrawIndirectCount                 = UINT32_MAX;
    lim.maxSamplerLodBias                    = 15.0f;
    lim.maxSamplerAnisotropy                 = 16.0f;
    lim.maxViewports                         = 1;
    lim.maxViewportDimensions[0]             = 16384;
    lim.maxViewportDimensions[1]             = 16384;
    lim.viewportBoundsRange[0]               = -32768.0f;
    lim.viewportBoundsRange[1]               = 32767.0f;
    lim.minMemoryMapAlignment                = 256;
    lim.minTexelBufferOffsetAlignment        = 4;
    lim.minUniformBufferOffsetAlignment      = 16;
    lim.minStorageBufferOffsetAlignment      = 4;
    lim.minTexelOffset                       = -8;
    lim.maxTexelOffset                       = 7;
    lim.minInterpolationOffset               = -0.5f;
    lim.maxInterpolationOffset               = 0.4375f;
    lim.subPixelInterpolationOffsetBits      = 4;
    lim.maxFramebufferWidth                  = 16384;
    lim.maxFramebufferHeight                 = 16384;
    lim.maxFramebufferLayers                 = 256;
    lim.framebufferColorSampleCounts         =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    lim.framebufferDepthSampleCounts         =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    lim.sampledImageColorSampleCounts        =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    lim.sampledImageIntegerSampleCounts      = VK_SAMPLE_COUNT_1_BIT;
    lim.sampledImageDepthSampleCounts        =
        VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
    lim.storageImageSampleCounts             = VK_SAMPLE_COUNT_1_BIT;
    lim.maxSampleMaskWords                   = 1;
    lim.timestampComputeAndGraphics          = VK_TRUE;
    lim.timestampPeriod                      = 1.0f; // Metal provides 1ns resolution
    lim.maxClipDistances                     = 8;
    lim.maxCullDistances                     = 0;
    lim.maxCombinedClipAndCullDistances      = 8;
    lim.discreteQueuePriorities              = 2;
    lim.pointSizeRange[0]                    = 1.0f;
    lim.pointSizeRange[1]                    = 511.0f;
    lim.lineWidthRange[0]                    = 1.0f;
    lim.lineWidthRange[1]                    = 1.0f;
    lim.pointSizeGranularity                 = 1.0f;
    lim.lineWidthGranularity                 = 0.0f;
    lim.strictLines                          = VK_FALSE;
    lim.standardSampleLocations             = VK_TRUE;
    lim.optimalBufferCopyOffsetAlignment     = 4;
    lim.optimalBufferCopyRowPitchAlignment   = 4;
    lim.nonCoherentAtomSize                  = 64;

    // ── Features ──────────────────────────────────────────────────────────────
    features.robustBufferAccess                      = VK_FALSE;
    features.fullDrawIndexUint32                     = VK_TRUE;
    features.imageCubeArray                          = VK_TRUE;
    features.independentBlend                        = VK_TRUE;
    features.geometryShader                          = VK_FALSE; // emulated
    features.tessellationShader                      = VK_TRUE;
    features.sampleRateShading                       = VK_TRUE;
    features.dualSrcBlend                            = VK_FALSE;
    features.logicOp                                 = VK_FALSE;
    features.multiDrawIndirect                       = VK_TRUE;
    features.drawIndirectFirstInstance               = VK_TRUE;
    features.depthClamp                              = VK_TRUE;
    features.depthBiasClamp                          = VK_TRUE;
    features.fillModeNonSolid                        = VK_TRUE;
    features.depthBounds                             = VK_FALSE;
    features.wideLines                               = VK_FALSE;
    features.largePoints                             = VK_TRUE;
    features.alphaToOne                              = VK_FALSE;
    features.multiViewport                           = VK_FALSE;
    features.samplerAnisotropy                       = VK_TRUE;
    features.textureCompressionETC2                  = [d supportsFamily:MTLGPUFamilyApple1] ? VK_TRUE : VK_FALSE;
    features.textureCompressionASTC_LDR              = [d supportsFamily:MTLGPUFamilyApple1] ? VK_TRUE : VK_FALSE;
    features.textureCompressionBC                    = [d supportsFamily:MTLGPUFamilyMac1]   ? VK_TRUE : VK_FALSE;
    features.occlusionQueryPrecise                   = VK_TRUE;
    features.pipelineStatisticsQuery                 = VK_FALSE;
    features.vertexPipelineStoresAndAtomics          = VK_TRUE;
    features.fragmentStoresAndAtomics                = VK_TRUE;
    features.shaderTessellationAndGeometryPointSize  = VK_FALSE;
    features.shaderImageGatherExtended               = VK_TRUE;
    features.shaderStorageImageExtendedFormats       = VK_TRUE;
    features.shaderStorageImageMultisample           = VK_FALSE;
    features.shaderStorageImageReadWithoutFormat     = VK_TRUE;
    features.shaderStorageImageWriteWithoutFormat    = VK_TRUE;
    features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    features.shaderSampledImageArrayDynamicIndexing  = VK_TRUE;
    features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    features.shaderStorageImageArrayDynamicIndexing  = VK_TRUE;
    features.shaderClipDistance                      = VK_TRUE;
    features.shaderCullDistance                      = VK_FALSE;
    features.shaderFloat64                           = VK_FALSE;
    features.shaderInt64                             = VK_TRUE;
    features.shaderInt16                             = VK_TRUE;
    features.shaderResourceResidency                 = VK_FALSE;
    features.shaderResourceMinLod                    = VK_TRUE;
    features.sparseBinding                           = VK_FALSE;
    features.variableMultisampleRate                 = VK_FALSE;
    features.inheritedQueries                        = VK_FALSE;

    MVRVB_LOG_INFO("Physical device populated: '%s'", properties.deviceName);
}

void MvPhysicalDevice::populateMemoryTypes() {
    id<MTLDevice> d = (__bridge id<MTLDevice>)mtlDevice;

    memProperties = {};

    // ── Heap: single device-local heap ────────────────────────────────────
    // Query the recommended working set size from Metal.  On Apple Silicon
    // this returns the unified memory budget; on discrete GPUs, VRAM size.
    memProperties.memoryHeapCount = 1;
    uint64_t heapSize = [d recommendedMaxWorkingSetSize];
    if (heapSize == 0) heapSize = 8ULL * 1024 * 1024 * 1024; // 8 GB fallback
    memProperties.memoryHeaps[0].size  = heapSize;
    memProperties.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    // ── Two memory types ─────────────────────────────────────────────────
    //
    //  Type 0: DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT
    //          → MTLStorageModeShared   (CPU + GPU, unified on Apple Silicon)
    //          DXVK/Wine allocate almost everything here because they map
    //          buffers frequently.  On unified memory this is zero-copy.
    //
    //  Type 1: DEVICE_LOCAL
    //          → MTLStorageModePrivate  (GPU-only, fastest for render targets
    //            and textures that are never CPU-read)
    //
    memProperties.memoryTypeCount = 2;

    // Type 0 — Shared (mappable)
    memProperties.memoryTypes[0].heapIndex      = 0;
    memProperties.memoryTypes[0].propertyFlags  =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT  |
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Type 1 — Private (GPU-only)
    memProperties.memoryTypes[1].heapIndex      = 0;
    memProperties.memoryTypes[1].propertyFlags  =
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

// ── MvInstance ────────────────────────────────────────────────────────────────
MvInstance::MvInstance() {
    @autoreleasepool {
        NSArray<id<MTLDevice>>* devices = MTLCopyAllDevices();
        for (id<MTLDevice> d in devices) {
            auto* phys = new MvPhysicalDevice();
            phys->mtlDevice = (__bridge_retained void*)d;
            phys->populateFromMTLDevice();
            phys->populateMemoryTypes();
            physicalDevices.push_back(phys);
        }
        MVRVB_LOG_INFO("Found %zu Metal device(s)", physicalDevices.size());
    }
}

MvInstance::~MvInstance() {
    for (auto* p : physicalDevices) {
        if (p->mtlDevice) CFRelease((__bridge CFTypeRef)p->mtlDevice);
        delete p;
    }
}

// ── MvDevice ──────────────────────────────────────────────────────────────────
MvDevice::~MvDevice() {
    for (auto* q : queues) delete q;
    // ARC handles Metal objects when compiled with ObjC++.
}

} // namespace mvrvb

// ── ICD entry points ──────────────────────────────────────────────────────────
#include "../icd/vulkan_icd.h"
#include "../memory/vk_memory.h"
#include "../../shader_translator/cache/shader_cache.h"

using namespace mvrvb;

extern "C" {

VkResult vkCreateInstance(const VkInstanceCreateInfo* pCI,
                           const VkAllocationCallbacks*,
                           VkInstance* pInstance) {
    if (!pCI || !pInstance) return VK_ERROR_INITIALIZATION_FAILED;

    auto* inst = new MvInstance();
    inst->apiVersionRequested = pCI->pApplicationInfo ?
                                pCI->pApplicationInfo->apiVersion :
                                VK_API_VERSION_1_0;
    if (pCI->pApplicationInfo && pCI->pApplicationInfo->pApplicationName)
        inst->appName = pCI->pApplicationInfo->pApplicationName;

    *pInstance = toVk(inst);
    MVRVB_LOG_INFO("vkCreateInstance OK — app='%s', apiVersion=%u.%u",
                   inst->appName.c_str(),
                   VK_VERSION_MAJOR(inst->apiVersionRequested),
                   VK_VERSION_MINOR(inst->apiVersionRequested));
    return VK_SUCCESS;
}

void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) {
    if (!instance) return;
    delete toMv(instance);
}

VkResult vkEnumerateInstanceVersion(uint32_t* pVersion) {
    if (pVersion) *pVersion = VK_API_VERSION_1_2;
    return VK_SUCCESS;
}

VkResult vkEnumerateInstanceExtensionProperties(const char* /*layer*/,
                                                  uint32_t* pCount,
                                                  VkExtensionProperties* pProps) {
    static const VkExtensionProperties kExts[] = {
        {"VK_KHR_surface",              25},
        {"VK_MVK_macos_surface",         3},
        {"VK_KHR_get_surface_capabilities2", 1},
        {"VK_EXT_debug_utils",           2},
        {"VK_KHR_portability_enumeration", 1},
    };
    static constexpr uint32_t kExtCount = sizeof(kExts)/sizeof(kExts[0]);
    if (!pProps) { *pCount = kExtCount; return VK_SUCCESS; }
    uint32_t n = std::min(*pCount, kExtCount);
    std::memcpy(pProps, kExts, n * sizeof(VkExtensionProperties));
    *pCount = n;
    return (n < kExtCount) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult vkEnumerateInstanceLayerProperties(uint32_t* pCount, VkLayerProperties*) {
    if (pCount) *pCount = 0;
    return VK_SUCCESS;
}

VkResult vkEnumeratePhysicalDevices(VkInstance instance,
                                     uint32_t* pCount,
                                     VkPhysicalDevice* pDevices) {
    auto* inst = toMv(instance);
    if (!inst) return VK_ERROR_INITIALIZATION_FAILED;
    if (!pDevices) { *pCount = (uint32_t)inst->physicalDevices.size(); return VK_SUCCESS; }
    uint32_t n = std::min(*pCount, (uint32_t)inst->physicalDevices.size());
    for (uint32_t i = 0; i < n; ++i)
        pDevices[i] = toVk(inst->physicalDevices[i]);
    *pCount = n;
    return (n < inst->physicalDevices.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    if (p) *p = toMv(pd)->properties;
}

void vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) {
    if (p) p->properties = toMv(pd)->properties;
}

void vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures* f) {
    if (f) *f = toMv(pd)->features;
}

void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2* f) {
    if (f) f->features = toMv(pd)->features;
}

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* p) {
    if (p) *p = toMv(pd)->memProperties;
}

void vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2* p) {
    if (p) p->memoryProperties = toMv(pd)->memProperties;
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice,
                                               uint32_t* pCount,
                                               VkQueueFamilyProperties* pProps) {
    // Metal has one universal command queue family.
    if (!pProps) { *pCount = 1; return; }
    if (*pCount >= 1) {
        pProps[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                               VK_QUEUE_TRANSFER_BIT;
        pProps[0].queueCount                 = 8;
        pProps[0].timestampValidBits         = 64;
        pProps[0].minImageTransferGranularity = {1, 1, 1};
        *pCount = 1;
    }
}

void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice,
                                          VkFormat format,
                                          VkFormatProperties* pProps) {
    if (!pProps) return;
    using namespace mvrvb;
    MTLPixelFormat mtl = vkFormatToMTL(format);
    bool supported = (mtl != 0); // MTLPixelFormatInvalid = 0

    VkFormatFeatureFlags common = 0;
    if (supported) {
        common = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT          |
                 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT          |
                 VK_FORMAT_FEATURE_TRANSFER_SRC_BIT           |
                 VK_FORMAT_FEATURE_TRANSFER_DST_BIT           |
                 VK_FORMAT_FEATURE_BLIT_SRC_BIT               |
                 VK_FORMAT_FEATURE_BLIT_DST_BIT               |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        const auto& fi = getFormatInfo(format);
        if (!fi.isCompressed && !fi.isDepth && !fi.isStencil) {
            common |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT       |
                      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                      VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
        }
        if (fi.isDepth || fi.isStencil) {
            common |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
    }
    pProps->linearTilingFeatures  = supported ? common : 0;
    pProps->optimalTilingFeatures = supported ? common : 0;
    pProps->bufferFeatures        = supported ? VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                                                VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT : 0;
}

VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice,
                                               const char*,
                                               uint32_t* pCount,
                                               VkExtensionProperties* pProps) {
    static const VkExtensionProperties kExts[] = {
        {"VK_KHR_swapchain",                        70},
        {"VK_KHR_maintenance1",                      2},
        {"VK_KHR_maintenance2",                      1},
        {"VK_KHR_maintenance3",                      1},
        {"VK_KHR_get_memory_requirements2",          1},
        {"VK_KHR_bind_memory2",                      1},
        {"VK_KHR_dedicated_allocation",              3},
        {"VK_KHR_image_format_list",                 1},
        {"VK_KHR_sampler_mirror_clamp_to_edge",      3},
        {"VK_KHR_multiview",                         1},
        {"VK_KHR_dynamic_rendering",                 1},
        {"VK_KHR_timeline_semaphore",                2},
        {"VK_KHR_synchronization2",                  1},
        {"VK_KHR_8bit_storage",                      1},
        {"VK_KHR_16bit_storage",                     1},
        {"VK_KHR_buffer_device_address",             1},
        {"VK_KHR_spirv_1_4",                         1},
        {"VK_KHR_shader_float_controls",             4},
        {"VK_KHR_depth_stencil_resolve",             1},
        {"VK_EXT_descriptor_indexing",               2},
        {"VK_EXT_host_query_reset",                  1},
        {"VK_EXT_scalar_block_layout",               1},
        {"VK_EXT_sampler_filter_minmax",             2},
        {"VK_EXT_shader_viewport_index_layer",       1},
        {"VK_KHR_portability_subset",                1},
    };
    static constexpr uint32_t kN = sizeof(kExts)/sizeof(kExts[0]);
    if (!pProps) { *pCount = kN; return VK_SUCCESS; }
    uint32_t n = std::min(*pCount, kN);
    std::memcpy(pProps, kExts, n * sizeof(VkExtensionProperties));
    *pCount = n;
    return (n < kN) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t* pCount, VkLayerProperties*) {
    if (pCount) *pCount = 0;
    return VK_SUCCESS;
}

VkResult vkCreateDevice(VkPhysicalDevice physDev,
                         const VkDeviceCreateInfo* pCI,
                         const VkAllocationCallbacks*,
                         VkDevice* pDevice) {
    @autoreleasepool {
        auto* phys = toMv(physDev);
        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)phys->mtlDevice;

        auto* dev = new MvDevice();
        dev->mtlDevice    = (__bridge_retained void*)mtlDev;
        dev->commandQueue = (__bridge_retained void*)[mtlDev newCommandQueue];
        dev->blitQueue    = (__bridge_retained void*)[mtlDev newCommandQueue];
        dev->computeQueue = (__bridge_retained void*)[mtlDev newCommandQueue];
        dev->hasUnifiedMemory = [mtlDev hasUnifiedMemory];
        dev->maxBufferLength  = (uint64_t)[mtlDev maxBufferLength];
        dev->supportsBC       = [mtlDev supportsFamily:MTLGPUFamilyMac1];

        [[(__bridge id<MTLCommandQueue>)dev->commandQueue setLabel:@"MetalVR-Graphics"]];
        [[(__bridge id<MTLCommandQueue>)dev->blitQueue    setLabel:@"MetalVR-Transfer"]];
        [[(__bridge id<MTLCommandQueue>)dev->computeQueue setLabel:@"MetalVR-Compute"]];

        // Create queues for each requested queue family.
        for (uint32_t i = 0; i < pCI->queueCreateInfoCount; ++i) {
            const auto& qci = pCI->pQueueCreateInfos[i];
            for (uint32_t j = 0; j < qci.queueCount; ++j) {
                auto* q = new MvQueue();
                q->queue       = (__bridge_retained void*)[mtlDev newCommandQueue];
                q->familyIndex = qci.queueFamilyIndex;
                q->queueIndex  = j;
                dev->queues.push_back(q);
            }
        }

        // Set up shader cache.
        ShaderCacheConfig scfg;
        dev->shaderCache = std::make_unique<ShaderCache>(dev->mtlDevice, scfg);

        // Set up memory manager.
        dev->memManager = std::make_unique<MemoryManager>(dev->mtlDevice);

        *pDevice = toVk(dev);
        MVRVB_LOG_INFO("VkDevice created on '%s' (%s memory)",
                       [mtlDev.name UTF8String],
                       dev->hasUnifiedMemory ? "unified" : "discrete");
        return VK_SUCCESS;
    }
}

void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks*) {
    if (!device) return;
    auto* dev = toMv(device);
    // Release Metal objects
    if (dev->commandQueue) CFRelease((__bridge CFTypeRef)dev->commandQueue);
    if (dev->blitQueue)    CFRelease((__bridge CFTypeRef)dev->blitQueue);
    if (dev->computeQueue) CFRelease((__bridge CFTypeRef)dev->computeQueue);
    if (dev->mtlDevice)    CFRelease((__bridge CFTypeRef)dev->mtlDevice);
    for (auto* q : dev->queues) {
        if (q->queue) CFRelease((__bridge CFTypeRef)q->queue);
        delete q;
    }
    dev->queues.clear();
    delete dev;
}

void vkGetDeviceQueue(VkDevice device, uint32_t family, uint32_t idx, VkQueue* pQueue) {
    if (!pQueue) return;
    auto* dev = toMv(device);
    for (auto* q : dev->queues) {
        if (q->familyIndex == family && q->queueIndex == idx) {
            *pQueue = toVk(q);
            return;
        }
    }
    *pQueue = VK_NULL_HANDLE;
}

void vkGetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* pInfo, VkQueue* pQueue) {
    if (pInfo) vkGetDeviceQueue(device, pInfo->queueFamilyIndex, pInfo->queueIndex, pQueue);
}

// NOTE: vkDeviceWaitIdle, vkQueueWaitIdle → sync/vk_sync.mm

} // extern "C"
