#pragma once
/**
 * @file vk_memory.h
 * @brief VkDeviceMemory → MTLBuffer allocator.
 *
 * Allocation strategy:
 *   - All allocations are backed by a dedicated MTLBuffer.
 *   - Small-object sub-allocation from MTLHeaps is deferred to a future
 *     milestone when profiling reveals fragmentation.
 *
 * Memory type → Metal storage mode mapping (2 types, 1 heap):
 *   Type 0: DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT → MTLStorageModeShared
 *   Type 1: DEVICE_LOCAL                                → MTLStorageModePrivate
 *
 * On Apple Silicon's unified memory architecture, Type 0 (Shared) is the
 * primary allocation target.  DXVK maps nearly every buffer, so Shared
 * gives zero-copy CPU+GPU access.  Type 1 (Private) is used for render
 * targets and textures that never need CPU readback.
 */

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace mvrvb {

// ── Forward declarations ──────────────────────────────────────────────────────
struct MvBuffer;
struct MvImage;

// ── Metal storage mode (mirrors MTLStorageMode) ───────────────────────────────
enum class MtlStorageMode : uint32_t {
    Shared  = 0,   ///< CPU + GPU, always coherent (Apple Silicon default)
    Managed = 1,   ///< CPU + GPU, manual sync required (Intel, unused currently)
    Private = 2,   ///< GPU only
    Memoryless = 3,///< Tile memory (transient, unused currently)
};

// ── Allocation descriptor ─────────────────────────────────────────────────────
struct AllocInfo {
    uint64_t         size{};
    uint64_t         alignment{256};
    uint32_t         memoryTypeIndex{};
    bool             dedicated{false};       ///< Force a single allocation
    void*            dedicatedForBuffer{nullptr};  ///< VkBuffer handle for dedicated alloc
    void*            dedicatedForImage{nullptr};   ///< VkImage handle for dedicated alloc
};

// ── Allocated memory block ────────────────────────────────────────────────────
struct MvMemory {
    uint64_t         size{};
    uint64_t         alignment{256};
    uint32_t         memoryTypeIndex{};
    MtlStorageMode   storageMode{MtlStorageMode::Shared};
    void*            mtlBuffer{nullptr};   ///< id<MTLBuffer> (retained via __bridge_retained)
    uint8_t*         mappedPtr{nullptr};   ///< CPU pointer (valid for Shared; null for Private)
    bool             isMapped{false};
    bool             isDedicated{false};
};

// ── Memory manager ────────────────────────────────────────────────────────────
class MemoryManager {
public:
    explicit MemoryManager(void* mtlDevice);
    ~MemoryManager();

    /**
     * @brief Allocate device memory backed by a new MTLBuffer.
     *
     * Type 0 → MTLStorageModeShared  (CPU + GPU, mappable)
     * Type 1 → MTLStorageModePrivate (GPU-only, not mappable)
     */
    VkResult allocate(const VkMemoryAllocateInfo* pAllocInfo,
                      const AllocInfo&            extra,
                      MvMemory**                  ppMemory);

    /// Free a previously allocated MvMemory block.
    void free(MvMemory* pMemory);

    /**
     * @brief Map a memory range to CPU address space.
     *
     * For Private storage (Type 1), returns VK_ERROR_MEMORY_MAP_FAILED.
     * For Shared storage (Type 0), returns the buffer's contents pointer + offset.
     */
    VkResult map(MvMemory* pMemory, uint64_t offset, uint64_t size, void** ppData);

    /// Unmap previously mapped memory.
    void unmap(MvMemory* pMemory);

    /**
     * @brief Flush CPU writes to GPU.
     *
     * On Shared (Apple Silicon), this is a no-op — memory is always coherent.
     * Retained for API compliance and potential future Managed-mode support.
     */
    void flush(MvMemory* pMemory, uint64_t offset, uint64_t size);

    /**
     * @brief Invalidate CPU cache for GPU→CPU readback.
     *
     * On Shared (Apple Silicon), this is a no-op.
     */
    void invalidate(MvMemory* pMemory, uint64_t offset, uint64_t size);

    /// Translate a Vulkan memory type index to a Metal storage mode.
    MtlStorageMode storageMode(uint32_t memTypeIndex) const noexcept;

    /// Total bytes allocated (for profiling overlay).
    [[nodiscard]] uint64_t totalAllocated() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mvrvb
