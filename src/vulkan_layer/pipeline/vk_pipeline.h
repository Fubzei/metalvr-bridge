#pragma once
/**
 * @file vk_pipeline.h
 * @brief VkPipeline -> MTLRenderPipelineState / MTLComputePipelineState.
 *
 * Milestone 5 - Pipeline State
 *
 * Graphics pipeline mapping:
 *   VkShaderModule -> MTLFunction (via ShaderCache from Milestone 1)
 *   VkVertexInputState -> MTLVertexDescriptor
 *   VkRasterizationState -> MTLRenderPipelineDescriptor (cull mode, fill mode)
 *   VkColorBlendState -> per-attachment blend descriptors
 *   VkDepthStencilState -> MTLDepthStencilDescriptor
 *   VkMultisampleState -> sampleCount on pipeline descriptor
 *
 * Pipeline layout:
 *   VkPipelineLayout (descriptor set layouts + push constant ranges) is used
 *   to build the Metal argument buffer structure. We store it for use at
 *   descriptor binding time.
 *
 * Pipeline cache:
 *   MvPipelineCache wraps the shader cache's disk persistence with a header
 *   containing a magic number + version for Vulkan-level cache data queries.
 *   Merge operations combine cache entries from multiple MvPipelineCache
 *   instances into the destination's shader cache.
 *
 * All entry points use the mvb_ prefix and are wired into the ICD dispatch
 * table via vulkan_icd.cpp.
 */

#include <vulkan/vulkan.h>
#include "../resources/vk_resources.h"
#include "../../shader_translator/msl_emitter/spirv_to_msl.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations to avoid pulling Metal headers into every TU.
namespace mvrvb {
class ShaderCache;  // from shader_translator/cache/shader_cache.h
}

namespace mvrvb {

struct MvPipelineLayout {
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<VkPushConstantRange>   pushConstRanges;
};

// NOTE: MvShaderModule and MvDescriptorSetLayout are defined in vk_resources.h.

/// Vertex buffers bind at the high end of the Metal buffer space so they do
/// not collide with descriptor-backed resources allocated from slot 1 upward.
static constexpr uint32_t kVertexBufferBaseSlot = msl::kMaxBufferSlots - 1;

/// Wraps the Milestone 1 ShaderCache with a Vulkan-level serialisation header
/// so that vkGetPipelineCacheData / vkMergePipelineCaches can operate on an
/// opaque blob as required by the Vulkan spec.
struct MvPipelineCache {
    static constexpr uint32_t kMagic   = 0x4D564243;  ///< "MVBC"
    static constexpr uint32_t kVersion = 1;

    /// Back-pointer to the device's ShaderCache (not owned).
    ShaderCache* shaderCache{nullptr};

    /// Optional initial data supplied by the application (from a previous run).
    std::vector<uint8_t> initialData;

    /// Protects concurrent getOrCompile calls during pipeline creation.
    mutable std::mutex mu;
};

struct MvPipeline {
    bool isCompute{false};

    void* renderPipelineState{nullptr};   ///< id<MTLRenderPipelineState>
    void* computePipelineState{nullptr};  ///< id<MTLComputePipelineState>
    void* depthStencilState{nullptr};     ///< id<MTLDepthStencilState>

    uint32_t topology{3};      ///< VkPrimitiveTopology -> MTLPrimitiveType at draw
    uint32_t cullMode{0};      ///< VkCullModeFlags
    uint32_t frontFace{0};     ///< VkFrontFace
    uint32_t fillMode{0};      ///< MTLTriangleFillMode
    bool     depthClampEnable{false};
    float    depthBiasConst{0}, depthBiasSlope{0}, depthBiasClamp{0};
    bool     depthBiasEnable{false};

    bool     depthTestEnable{true};
    bool     depthWriteEnable{true};
    uint32_t depthCompareOp{};
    bool     stencilTestEnable{false};

    uint32_t localSizeX{1}, localSizeY{1}, localSizeZ{1};

    bool     hasDynamicViewport{false};
    bool     hasDynamicScissor{false};
    bool     hasDynamicDepthBias{false};
    bool     hasDynamicBlendConstants{false};
    bool     hasDynamicStencilCompareMask{false};
    bool     hasDynamicStencilWriteMask{false};
    bool     hasDynamicStencilReference{false};

    MvPipelineLayout* layout{nullptr};

    msl::MSLReflection vertexReflection;
    msl::MSLReflection fragmentReflection;
    msl::MSLReflection computeReflection;
};

// NOTE: MvShaderModule and MvDescriptorSetLayout toMv/toVk live in
// vk_resources.h.
inline MvPipelineLayout* toMv(VkPipelineLayout h) { return reinterpret_cast<MvPipelineLayout*>(h); }
inline MvPipeline*       toMv(VkPipeline h)       { return reinterpret_cast<MvPipeline*>(h); }
inline MvPipelineCache*  toMv(VkPipelineCache h)  { return reinterpret_cast<MvPipelineCache*>(h); }

inline VkPipelineLayout toVk(MvPipelineLayout* p) { return reinterpret_cast<VkPipelineLayout>(p); }
inline VkPipeline       toVk(MvPipeline* p)       { return reinterpret_cast<VkPipeline>(p); }
inline VkPipelineCache  toVk(MvPipelineCache* p)  { return reinterpret_cast<VkPipelineCache>(p); }

} // namespace mvrvb

#ifdef __cplusplus
extern "C" {
#endif

VkResult mvb_CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                                     uint32_t createInfoCount,
                                     const VkGraphicsPipelineCreateInfo* pCreateInfos,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkPipeline* pPipelines);

VkResult mvb_CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                                    uint32_t createInfoCount,
                                    const VkComputePipelineCreateInfo* pCreateInfos,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkPipeline* pPipelines);

void mvb_DestroyPipeline(VkDevice device, VkPipeline pipeline,
                         const VkAllocationCallbacks* pAllocator);

VkResult mvb_CreatePipelineLayout(VkDevice device,
                                  const VkPipelineLayoutCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator,
                                  VkPipelineLayout* pPipelineLayout);

void mvb_DestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                               const VkAllocationCallbacks* pAllocator);

VkResult mvb_CreatePipelineCache(VkDevice device,
                                 const VkPipelineCacheCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkPipelineCache* pPipelineCache);

void mvb_DestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                              const VkAllocationCallbacks* pAllocator);

VkResult mvb_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                                  size_t* pDataSize, void* pData);

VkResult mvb_MergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                                 uint32_t srcCacheCount,
                                 const VkPipelineCache* pSrcCaches);

#ifdef __cplusplus
} // extern "C"
#endif
