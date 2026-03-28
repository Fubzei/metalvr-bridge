/**
 * @file vk_memory.mm
 * @brief MemoryManager implementation.
 */

#include "vk_memory.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>
#include <atomic>
#include <mutex>

namespace mvrvb {

struct MemoryManager::Impl {
    id<MTLDevice> device;
    bool isUnifiedMemory;
    std::mutex mutex;
    std::atomic<uint64_t> totalBytes{0};
};

MemoryManager::MemoryManager(void* mtlDevice) {
    m_impl = std::make_unique<Impl>();
    m_impl->device = (__bridge id<MTLDevice>)mtlDevice;
    m_impl->isUnifiedMemory = [m_impl->device hasUnifiedMemory];
}

MemoryManager::~MemoryManager() {
    MVRVB_LOG_DEBUG("MemoryManager destroyed; total allocated was %llu bytes",
                    m_impl->totalBytes.load());
}

MtlStorageMode MemoryManager::storageMode(uint32_t typeIdx) const noexcept {
    // Map Vulkan memory type index → Metal storage mode.
    // Matches the two types defined in MvPhysicalDevice::populateMemoryTypes():
    //   Type 0 → DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT → Shared
    //   Type 1 → DEVICE_LOCAL                            → Private
    if (typeIdx == 0) return MtlStorageMode::Shared;
    return MtlStorageMode::Private;
}

VkResult MemoryManager::allocate(const VkMemoryAllocateInfo* pAllocInfo,
                                  const AllocInfo& extra,
                                  MvMemory** ppMemory) {
    if (!pAllocInfo || !ppMemory) return VK_ERROR_INITIALIZATION_FAILED;

    @autoreleasepool {
        MtlStorageMode sm = storageMode(pAllocInfo->memoryTypeIndex);
        uint64_t size = pAllocInfo->allocationSize;
        if (size == 0) return VK_ERROR_OUT_OF_DEVICE_MEMORY;

        auto* mem = new MvMemory();
        mem->size            = size;
        mem->alignment       = extra.alignment;
        mem->memoryTypeIndex = pAllocInfo->memoryTypeIndex;
        mem->storageMode     = sm;
        mem->isDedicated     = extra.dedicated || size >= 4 * 1024 * 1024;

        // Allocate MTLBuffer directly (simplest path, works for all types).
        MTLResourceOptions opts = 0;
        switch (sm) {
            case MtlStorageMode::Shared:     opts = MTLResourceStorageModeShared; break;
            case MtlStorageMode::Managed:    opts = MTLResourceStorageModeManaged; break;
            case MtlStorageMode::Private:    opts = MTLResourceStorageModePrivate; break;
            default:                         opts = MTLResourceStorageModeShared; break;
        }
        opts |= MTLResourceHazardTrackingModeTracked;

        id<MTLBuffer> buf = [m_impl->device newBufferWithLength:size options:opts];
        if (!buf) {
            MVRVB_LOG_ERROR("MTLBuffer allocation failed for %llu bytes", size);
            delete mem;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        [buf setLabel:[NSString stringWithFormat:@"MVB-Mem-%u-%llu",
                        pAllocInfo->memoryTypeIndex, size]];

        mem->mtlBuffer = (__bridge_retained void*)buf;
        if (sm != MtlStorageMode::Private) {
            mem->mappedPtr = static_cast<uint8_t*>([buf contents]);
        }

        m_impl->totalBytes += size;
        *ppMemory = mem;

        MVRVB_LOG_DEBUG("Alloc %llu bytes type=%u storage=%u",
                        size, pAllocInfo->memoryTypeIndex, (uint32_t)sm);
        return VK_SUCCESS;
    }
}

void MemoryManager::free(MvMemory* mem) {
    if (!mem) return;
    if (mem->mtlBuffer) {
        m_impl->totalBytes -= mem->size;
        CFRelease((__bridge CFTypeRef)mem->mtlBuffer);
    }
    delete mem;
}

VkResult MemoryManager::map(MvMemory* mem, uint64_t offset, uint64_t /*size*/, void** ppData) {
    if (!mem) return VK_ERROR_MEMORY_MAP_FAILED;
    if (mem->storageMode == MtlStorageMode::Private) return VK_ERROR_MEMORY_MAP_FAILED;
    if (!mem->mappedPtr) {
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)mem->mtlBuffer;
        mem->mappedPtr = static_cast<uint8_t*>([buf contents]);
    }
    if (!mem->mappedPtr) return VK_ERROR_MEMORY_MAP_FAILED;
    *ppData = mem->mappedPtr + offset;
    mem->isMapped = true;
    return VK_SUCCESS;
}

void MemoryManager::unmap(MvMemory* mem) {
    if (!mem) return;
    mem->isMapped = false;
    // On Apple Silicon Shared memory, no flush needed.
    // On Managed, caller should call flush explicitly.
}

void MemoryManager::flush(MvMemory* mem, uint64_t offset, uint64_t size) {
    if (!mem || mem->storageMode != MtlStorageMode::Managed) return;
    @autoreleasepool {
        id<MTLBuffer> buf = (__bridge id<MTLBuffer>)mem->mtlBuffer;
        [buf didModifyRange:NSMakeRange(offset, size)];
    }
}

void MemoryManager::invalidate(MvMemory* mem, uint64_t offset, uint64_t size) {
    if (!mem || mem->storageMode != MtlStorageMode::Managed) return;
    // On Managed buffers, synchronize GPU writes to CPU.
    // Note: proper sync requires a MTLBlitCommandEncoder synchronizeResource: call
    // before the CPU reads — this is issued by the command submission path.
    (void)offset; (void)size;
}

uint64_t MemoryManager::totalAllocated() const noexcept {
    return m_impl->totalBytes.load();
}

} // namespace mvrvb

// ── VkDeviceMemory Vulkan entry points ────────────────────────────────────────
#include "../device/vk_device.h"
#include <cstring>
using namespace mvrvb;

extern "C" {

VkResult vkAllocateMemory(VkDevice device,
                           const VkMemoryAllocateInfo* pAllocInfo,
                           const VkAllocationCallbacks*,
                           VkDeviceMemory* pMemory) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager) return VK_ERROR_DEVICE_LOST;
    AllocInfo extra;
    // Check for dedicated allocation extension.
    const void* pNext = pAllocInfo->pNext;
    while (pNext) {
        const auto* h = static_cast<const VkBaseInStructure*>(pNext);
        if (h->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO) {
            extra.dedicated = true;
        }
        pNext = h->pNext;
    }
    MvMemory* mem = nullptr;
    VkResult r = dev->memManager->allocate(pAllocInfo, extra, &mem);
    if (r == VK_SUCCESS) *pMemory = reinterpret_cast<VkDeviceMemory>(mem);
    return r;
}

void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks*) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager || !memory) return;
    dev->memManager->free(reinterpret_cast<MvMemory*>(memory));
}

VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory,
                     VkDeviceSize offset, VkDeviceSize size,
                     VkMemoryMapFlags, void** ppData) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager) return VK_ERROR_DEVICE_LOST;
    return dev->memManager->map(reinterpret_cast<MvMemory*>(memory), offset, size, ppData);
}

void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager) return;
    dev->memManager->unmap(reinterpret_cast<MvMemory*>(memory));
}

VkResult vkFlushMappedMemoryRanges(VkDevice device, uint32_t count,
                                    const VkMappedMemoryRange* pRanges) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager) return VK_ERROR_DEVICE_LOST;
    for (uint32_t i = 0; i < count; ++i) {
        dev->memManager->flush(reinterpret_cast<MvMemory*>(pRanges[i].memory),
                               pRanges[i].offset, pRanges[i].size);
    }
    return VK_SUCCESS;
}

VkResult vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t count,
                                         const VkMappedMemoryRange* pRanges) {
    auto* dev = toMv(device);
    if (!dev || !dev->memManager) return VK_ERROR_DEVICE_LOST;
    for (uint32_t i = 0; i < count; ++i) {
        dev->memManager->invalidate(reinterpret_cast<MvMemory*>(pRanges[i].memory),
                                    pRanges[i].offset, pRanges[i].size);
    }
    return VK_SUCCESS;
}

} // extern "C"
