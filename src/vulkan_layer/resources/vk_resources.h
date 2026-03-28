#pragma once
/**
 * @file vk_resources.h
 * @brief VkBuffer, VkImage, VkImageView, VkSampler, VkBufferView,
 *        VkDescriptorSetLayout, VkShaderModule — Metal-backed wrappers.
 *
 * Phase 2 rewrite:
 *   - MvMemory* binding for proper Vulkan create→bind→use lifecycle
 *   - MvBufferView for texel buffers
 *   - MvDescriptorSetLayout with per-binding metadata
 *   - Component swizzle mapping in MvImageView
 *   - MvShaderModule stores SPIR-V bytes for deferred compilation
 *   - Tiling mode on MvImage
 *   - Proper memory requirements reporting
 */

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>

namespace mvrvb {

// Forward declarations
struct MvMemory;
struct MvDescriptorSet;

// ── VkBuffer wrapper ─────────────────────────────────────────────────────────
struct MvBuffer {
    void*              mtlBuffer{nullptr};   ///< id<MTLBuffer> (retained)
    uint64_t           size{0};
    uint64_t           memoryOffset{0};      ///< Offset within bound MvMemory
    VkBufferUsageFlags usage{};
    VkBufferCreateFlags createFlags{};
    MvMemory*          memory{nullptr};      ///< Bound device memory
    bool               isMapped{false};
    // For suballocation: if the buffer is backed by a region of a larger
    // MTLBuffer, mtlBufferOffset records the start offset within that buffer.
    uint64_t           mtlBufferOffset{0};
};

// ── VkBufferView wrapper (texel buffers) ─────────────────────────────────────
struct MvBufferView {
    MvBuffer*   buffer{nullptr};
    void*       mtlTexture{nullptr};    ///< id<MTLTexture> created from buffer
    VkFormat    format{};
    uint64_t    offset{0};
    uint64_t    range{0};
};

// ── VkImage wrapper ──────────────────────────────────────────────────────────
struct MvImage {
    void*              mtlTexture{nullptr};  ///< id<MTLTexture> (retained)
    VkFormat           format{};
    uint32_t           width{1}, height{1}, depth{1};
    uint32_t           mipLevels{1};
    uint32_t           arrayLayers{1};
    uint32_t           samples{1};           ///< VK_SAMPLE_COUNT_*_BIT → numeric
    VkImageType        imageType{VK_IMAGE_TYPE_2D};
    VkImageUsageFlags  usage{};
    VkImageCreateFlags createFlags{};
    VkImageTiling      tiling{VK_IMAGE_TILING_OPTIMAL};
    MvMemory*          memory{nullptr};      ///< Bound device memory
    uint64_t           memoryOffset{0};
    bool               isSwapchainImage{false}; ///< Owned by swapchain, don't destroy
};

// ── VkImageView wrapper ──────────────────────────────────────────────────────
struct MvImageView {
    void*              mtlTexture{nullptr};  ///< id<MTLTexture> (view or original)
    MvImage*           image{nullptr};
    VkFormat           format{};
    VkImageViewType    viewType{VK_IMAGE_VIEW_TYPE_2D};
    VkImageAspectFlags aspectMask{VK_IMAGE_ASPECT_COLOR_BIT};
    uint32_t           baseMipLevel{0};
    uint32_t           levelCount{1};
    uint32_t           baseArrayLayer{0};
    uint32_t           layerCount{1};
    VkComponentMapping swizzle{};            ///< Component swizzle mapping
};

// ── VkSampler wrapper ────────────────────────────────────────────────────────
struct MvSampler {
    void*   mtlSamplerState{nullptr};   ///< id<MTLSamplerState> (retained)
    bool    compareEnable{false};
    float   maxAnisotropy{1.0f};
};

// ── VkShaderModule wrapper ───────────────────────────────────────────────────
struct MvShaderModule {
    std::vector<uint32_t> spirv;             ///< Original SPIR-V bytecode
    void*                 library{nullptr};  ///< id<MTLLibrary> owned by ShaderCache
    void*                 function{nullptr}; ///< id<MTLFunction> owned by ShaderCache
    uint32_t              stage{};           ///< VkShaderStageFlagBits
};

// ── VkDescriptorSetLayout wrapper ────────────────────────────────────────────
struct DescriptorSetLayoutBinding {
    uint32_t             binding{};
    VkDescriptorType     descriptorType{};
    uint32_t             descriptorCount{1};
    VkShaderStageFlags   stageFlags{};
    // Immutable samplers (if any) are stored here.
    std::vector<VkSampler> immutableSamplers;
};

struct MvDescriptorSetLayout {
    std::vector<DescriptorSetLayoutBinding> bindings;
    VkDescriptorSetLayoutCreateFlags        flags{};
};

// ── VkDescriptorPool / VkDescriptorSet ───────────────────────────────────────
struct MvDescriptorSet;

struct MvDescriptorPool {
    uint32_t maxSets{};
    uint32_t allocatedSets{0};
    VkDescriptorPoolCreateFlags flags{};
    std::vector<MvDescriptorSet*> liveSets;
};

struct DescriptorBinding {
    uint32_t binding{};
    uint32_t arrayIndex{0};       ///< Element index within a descriptor array
    VkDescriptorType descriptorType{};
    void*    resource{nullptr};   ///< MvBuffer*, MvImageView*, or MvSampler*
    uint64_t offset{0};
    uint64_t range{0};
    // For COMBINED_IMAGE_SAMPLER, both image and sampler are stored.
    void*    samplerResource{nullptr}; ///< MvSampler* for combined entries
};

struct MvDescriptorSet {
    MvDescriptorPool*              pool{nullptr};
    MvDescriptorSetLayout*         layout{nullptr};
    std::vector<DescriptorBinding> bindings;
};

// ── Handle casts ─────────────────────────────────────────────────────────────
// Vulkan handles are opaque 64-bit values; we reinterpret_cast our Mv* pointers.

inline MvBuffer*              toMv(VkBuffer h)              { return reinterpret_cast<MvBuffer*>(h);              }
inline MvBufferView*          toMv(VkBufferView h)          { return reinterpret_cast<MvBufferView*>(h);          }
inline MvImage*               toMv(VkImage h)               { return reinterpret_cast<MvImage*>(h);               }
inline MvImageView*           toMv(VkImageView h)           { return reinterpret_cast<MvImageView*>(h);           }
inline MvSampler*             toMv(VkSampler h)             { return reinterpret_cast<MvSampler*>(h);             }
inline MvShaderModule*        toMv(VkShaderModule h)        { return reinterpret_cast<MvShaderModule*>(h);        }
inline MvDescriptorSetLayout* toMv(VkDescriptorSetLayout h) { return reinterpret_cast<MvDescriptorSetLayout*>(h); }
inline MvDescriptorPool*      toMv(VkDescriptorPool h)      { return reinterpret_cast<MvDescriptorPool*>(h);      }
inline MvDescriptorSet*       toMv(VkDescriptorSet h)       { return reinterpret_cast<MvDescriptorSet*>(h);       }

inline VkBuffer              toVk(MvBuffer* p)              { return reinterpret_cast<VkBuffer>(p);              }
inline VkBufferView          toVk(MvBufferView* p)          { return reinterpret_cast<VkBufferView>(p);          }
inline VkImage               toVk(MvImage* p)               { return reinterpret_cast<VkImage>(p);               }
inline VkImageView           toVk(MvImageView* p)           { return reinterpret_cast<VkImageView>(p);           }
inline VkSampler             toVk(MvSampler* p)             { return reinterpret_cast<VkSampler>(p);             }
inline VkShaderModule        toVk(MvShaderModule* p)        { return reinterpret_cast<VkShaderModule>(p);        }
inline VkDescriptorSetLayout toVk(MvDescriptorSetLayout* p) { return reinterpret_cast<VkDescriptorSetLayout>(p); }
inline VkDescriptorPool      toVk(MvDescriptorPool* p)      { return reinterpret_cast<VkDescriptorPool>(p);      }
inline VkDescriptorSet       toVk(MvDescriptorSet* p)       { return reinterpret_cast<VkDescriptorSet>(p);       }

} // namespace mvrvb
