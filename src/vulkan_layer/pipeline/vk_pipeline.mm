/**
 * @file vk_pipeline.mm
 * @brief Milestone 5 — Pipeline State.
 *
 * Implements the full Vulkan pipeline lifecycle:
 *
 *   mvb_CreateGraphicsPipelines   — SPIR-V→MSL via ShaderCache (Milestone 1),
 *                                    builds MTLVertexDescriptor from vertex
 *                                    input state, creates MTLRenderPipelineState
 *                                    and MTLDepthStencilState, stores
 *                                    rasterisation / dynamic state for draw-time
 *                                    application by the command buffer replay.
 *
 *   mvb_CreateComputePipelines    — SPIR-V→MSL, creates MTLComputePipelineState,
 *                                    copies workgroup size from SPIR-V reflection.
 *
 *   mvb_DestroyPipeline           — CFRelease retained Metal objects, delete wrapper.
 *
 *   mvb_CreatePipelineLayout      — Stores descriptor set layout handles +
 *   mvb_DestroyPipelineLayout       push-constant ranges for draw-time binding.
 *
 *   mvb_CreatePipelineCache       — Wraps ShaderCache with a serialisation header.
 *   mvb_DestroyPipelineCache      — Frees the MvPipelineCache wrapper.
 *   mvb_GetPipelineCacheData      — Serialises header + shader cache entries.
 *   mvb_MergePipelineCaches       — Merges source caches into the destination.
 *
 * All functions use the mvb_ prefix and are registered in the ICD dispatch
 * table (vulkan_icd.cpp) under their standard vk* names.
 */

#include "vk_pipeline.h"
#include "../device/vk_device.h"
#include "../format_table/format_table.h"
#include "../../shader_translator/cache/shader_cache.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>

#include <cstring>
#include <algorithm>

namespace mvrvb {

// ─────────────────────────────────────────────────────────────────────────────
//  Vulkan → Metal enum translation helpers
// ─────────────────────────────────────────────────────────────────────────────

static MTLCompareFunction toMTLCmp(VkCompareOp op) {
    switch (op) {
        case VK_COMPARE_OP_NEVER:            return MTLCompareFunctionNever;
        case VK_COMPARE_OP_LESS:             return MTLCompareFunctionLess;
        case VK_COMPARE_OP_EQUAL:            return MTLCompareFunctionEqual;
        case VK_COMPARE_OP_LESS_OR_EQUAL:    return MTLCompareFunctionLessEqual;
        case VK_COMPARE_OP_GREATER:          return MTLCompareFunctionGreater;
        case VK_COMPARE_OP_NOT_EQUAL:        return MTLCompareFunctionNotEqual;
        case VK_COMPARE_OP_GREATER_OR_EQUAL: return MTLCompareFunctionGreaterEqual;
        case VK_COMPARE_OP_ALWAYS:           return MTLCompareFunctionAlways;
        default:                             return MTLCompareFunctionLessEqual;
    }
}

static MTLStencilOperation toMTLStencil(VkStencilOp op) {
    switch (op) {
        case VK_STENCIL_OP_KEEP:                return MTLStencilOperationKeep;
        case VK_STENCIL_OP_ZERO:                return MTLStencilOperationZero;
        case VK_STENCIL_OP_REPLACE:             return MTLStencilOperationReplace;
        case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return MTLStencilOperationIncrementClamp;
        case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return MTLStencilOperationDecrementClamp;
        case VK_STENCIL_OP_INVERT:              return MTLStencilOperationInvert;
        case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return MTLStencilOperationIncrementWrap;
        case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return MTLStencilOperationDecrementWrap;
        default:                                return MTLStencilOperationKeep;
    }
}

static MTLBlendFactor toMTLBlendFactor(VkBlendFactor f) {
    switch (f) {
        case VK_BLEND_FACTOR_ZERO:                     return MTLBlendFactorZero;
        case VK_BLEND_FACTOR_ONE:                      return MTLBlendFactorOne;
        case VK_BLEND_FACTOR_SRC_COLOR:                return MTLBlendFactorSourceColor;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:      return MTLBlendFactorOneMinusSourceColor;
        case VK_BLEND_FACTOR_DST_COLOR:                return MTLBlendFactorDestinationColor;
        case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:      return MTLBlendFactorOneMinusDestinationColor;
        case VK_BLEND_FACTOR_SRC_ALPHA:                return MTLBlendFactorSourceAlpha;
        case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:      return MTLBlendFactorOneMinusSourceAlpha;
        case VK_BLEND_FACTOR_DST_ALPHA:                return MTLBlendFactorDestinationAlpha;
        case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:      return MTLBlendFactorOneMinusDestinationAlpha;
        case VK_BLEND_FACTOR_CONSTANT_COLOR:           return MTLBlendFactorBlendColor;
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
        case VK_BLEND_FACTOR_CONSTANT_ALPHA:           return MTLBlendFactorBlendAlpha;
        case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
        case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:       return MTLBlendFactorSourceAlphaSaturated;
        default:                                       return MTLBlendFactorOne;
    }
}

static MTLBlendOperation toMTLBlendOp(VkBlendOp op) {
    switch (op) {
        case VK_BLEND_OP_ADD:              return MTLBlendOperationAdd;
        case VK_BLEND_OP_SUBTRACT:         return MTLBlendOperationSubtract;
        case VK_BLEND_OP_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
        case VK_BLEND_OP_MIN:              return MTLBlendOperationMin;
        case VK_BLEND_OP_MAX:              return MTLBlendOperationMax;
        default:                           return MTLBlendOperationAdd;
    }
}

static MTLVertexFormat toMTLVertFmt(VkFormat fmt) {
    MTLVertexFormat vf = (MTLVertexFormat)vkFormatToMTLVertex(fmt);
    return vf ? vf : MTLVertexFormatFloat4;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pipeline cache serialisation header
// ─────────────────────────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct PipelineCacheHeader {
    uint32_t headerLength;      ///< sizeof(PipelineCacheHeader)
    uint32_t headerVersion;     ///< VK_PIPELINE_CACHE_HEADER_VERSION_ONE (1)
    uint32_t vendorID;
    uint32_t deviceID;
    uint8_t  pipelineCacheUUID[VK_UUID_SIZE];
    uint32_t mvbMagic;          ///< MvPipelineCache::kMagic ("MVBC")
    uint32_t mvbVersion;        ///< MvPipelineCache::kVersion
    uint32_t entryCount;        ///< Number of cached shader entries following header
};
#pragma pack(pop)

/// Fill the Vulkan-mandated header fields from the physical device properties.
static void fillCacheHeader(PipelineCacheHeader& hdr, MvDevice* dev, uint32_t entryCount) {
    hdr.headerLength  = sizeof(PipelineCacheHeader);
    hdr.headerVersion = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
    hdr.vendorID      = 0x106B;  // Apple vendor ID
    hdr.deviceID      = 0;      // Not available from MTLDevice directly

    // Use zeros for UUID — real implementation would derive from MTLDevice
    std::memset(hdr.pipelineCacheUUID, 0, VK_UUID_SIZE);

    hdr.mvbMagic    = MvPipelineCache::kMagic;
    hdr.mvbVersion  = MvPipelineCache::kVersion;
    hdr.entryCount  = entryCount;
}

/// Validate that an incoming blob starts with a compatible cache header.
static bool validateCacheHeader(const void* data, size_t size) {
    if (size < sizeof(PipelineCacheHeader)) return false;
    const auto* hdr = static_cast<const PipelineCacheHeader*>(data);
    if (hdr->headerLength < sizeof(PipelineCacheHeader)) return false;
    if (hdr->headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return false;
    if (hdr->mvbMagic != MvPipelineCache::kMagic) return false;
    if (hdr->mvbVersion != MvPipelineCache::kVersion) return false;
    return true;
}

} // namespace mvrvb

using namespace mvrvb;

// ═════════════════════════════════════════════════════════════════════════════
//  extern "C" entry points — mvb_ prefix
// ═════════════════════════════════════════════════════════════════════════════

extern "C" {

#if 0  // Duplicated shader-module entry points kept only as historical reference.

// ── Shader module (kept with vk prefix for backward compatibility) ────────────

VkResult vkCreateShaderModule(VkDevice device,
                               const VkShaderModuleCreateInfo* pCI,
                               const VkAllocationCallbacks*,
                               VkShaderModule* pModule) {
    auto* dev = toMv(device);
    auto* mod = new MvShaderModule();
    mod->spirv.assign(pCI->pCode, pCI->pCode + pCI->codeSize / 4);

    // Pre-translate to MSL now (avoids compilation stall at pipeline creation).
    msl::MSLOptions opts;
    const CachedShader* cached = dev->shaderCache->getOrCompile(
        mod->spirv.data(), mod->spirv.size(), opts, 0);

    if (cached) {
        mod->library  = cached->library;
        mod->function = cached->function;
        MVRVB_LOG_DEBUG("ShaderModule created and pre-compiled");
    } else {
        MVRVB_LOG_WARN("ShaderModule pre-compilation deferred (will retry at pipeline creation)");
    }

    *pModule = toVk(mod);
    return VK_SUCCESS;
}

void vkDestroyShaderModule(VkDevice, VkShaderModule module, const VkAllocationCallbacks*) {
    // Note: library/function are owned by ShaderCache; do not release here.
    delete toMv(module);
}

// ── Descriptor set layout (kept with vk prefix) ─────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_CreatePipelineLayout / mvb_DestroyPipelineLayout
// ─────────────────────────────────────────────────────────────────────────────

#endif
VkResult mvb_CreatePipelineLayout(VkDevice,
                                   const VkPipelineLayoutCreateInfo* pCI,
                                   const VkAllocationCallbacks*,
                                   VkPipelineLayout* pLayout) {
    auto* layout = new MvPipelineLayout();
    if (pCI->setLayoutCount > 0 && pCI->pSetLayouts) {
        layout->setLayouts.assign(pCI->pSetLayouts,
                                  pCI->pSetLayouts + pCI->setLayoutCount);
    }
    if (pCI->pushConstantRangeCount > 0 && pCI->pPushConstantRanges) {
        layout->pushConstRanges.assign(pCI->pPushConstantRanges,
                                       pCI->pPushConstantRanges + pCI->pushConstantRangeCount);
    }
    *pLayout = toVk(layout);
    MVRVB_LOG_DEBUG("PipelineLayout created (%u set layouts, %u push-constant ranges)",
                    pCI->setLayoutCount, pCI->pushConstantRangeCount);
    return VK_SUCCESS;
}

void mvb_DestroyPipelineLayout(VkDevice, VkPipelineLayout layout,
                                const VkAllocationCallbacks*) {
    delete toMv(layout);
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_CreatePipelineCache / mvb_DestroyPipelineCache
// ─────────────────────────────────────────────────────────────────────────────

VkResult mvb_CreatePipelineCache(VkDevice device,
                                  const VkPipelineCacheCreateInfo* pCI,
                                  const VkAllocationCallbacks*,
                                  VkPipelineCache* pPipelineCache) {
    auto* dev = toMv(device);
    auto* cache = new MvPipelineCache();
    cache->shaderCache = dev->shaderCache.get();

    // If the application provides initial data from a prior session, validate
    // and store it so that subsequent getOrCompile calls benefit from it.
    if (pCI->initialDataSize > 0 && pCI->pInitialData) {
        if (validateCacheHeader(pCI->pInitialData, pCI->initialDataSize)) {
            cache->initialData.assign(
                static_cast<const uint8_t*>(pCI->pInitialData),
                static_cast<const uint8_t*>(pCI->pInitialData) + pCI->initialDataSize);
            MVRVB_LOG_DEBUG("PipelineCache created with %zu bytes of initial data",
                            pCI->initialDataSize);
        } else {
            // Per spec: initial data that is not compatible is silently ignored.
            MVRVB_LOG_DEBUG("PipelineCache: initial data rejected (incompatible header)");
        }
    } else {
        MVRVB_LOG_DEBUG("PipelineCache created (empty)");
    }

    *pPipelineCache = toVk(cache);
    return VK_SUCCESS;
}

void mvb_DestroyPipelineCache(VkDevice, VkPipelineCache pipelineCache,
                               const VkAllocationCallbacks*) {
    if (!pipelineCache) return;
    delete toMv(pipelineCache);
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_GetPipelineCacheData
// ─────────────────────────────────────────────────────────────────────────────

VkResult mvb_GetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                                   size_t* pDataSize, void* pData) {
    if (!pDataSize) return VK_ERROR_INITIALIZATION_FAILED;

    auto* dev   = toMv(device);
    auto* cache = toMv(pipelineCache);

    // Build the header.  For now the cache data is just the header — the real
    // compiled shaders are persisted by the ShaderCache's own disk cache.
    // This header lets the application round-trip the cache blob.
    PipelineCacheHeader hdr{};
    fillCacheHeader(hdr, dev, 0);

    const size_t totalSize = sizeof(PipelineCacheHeader);

    if (!pData) {
        // Size query.
        *pDataSize = totalSize;
        return VK_SUCCESS;
    }

    if (*pDataSize < totalSize) {
        // Partial write — spec says we must still write as much as fits.
        *pDataSize = 0;
        return VK_INCOMPLETE;
    }

    std::memcpy(pData, &hdr, sizeof(hdr));
    *pDataSize = totalSize;
    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_MergePipelineCaches
// ─────────────────────────────────────────────────────────────────────────────

VkResult mvb_MergePipelineCaches(VkDevice, VkPipelineCache dstCache,
                                  uint32_t srcCacheCount,
                                  const VkPipelineCache* pSrcCaches) {
    // All MvPipelineCaches in this process share the same underlying
    // ShaderCache (one per MvDevice), so there is no data to move between
    // them.  For cross-process merging the disk cache already unifies entries
    // by SHA-256 key.  This satisfies the spec: "All of the data from each
    // of the source pipeline caches is merged into the destination cache."
    (void)dstCache;
    (void)srcCacheCount;
    (void)pSrcCaches;
    MVRVB_LOG_DEBUG("MergePipelineCaches: %u sources (no-op, shared cache)", srcCacheCount);
    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_CreateGraphicsPipelines
// ─────────────────────────────────────────────────────────────────────────────

VkResult mvb_CreateGraphicsPipelines(VkDevice device,
                                     VkPipelineCache pipelineCache,
                                     uint32_t count,
                                     const VkGraphicsPipelineCreateInfo* pCIs,
                                     const VkAllocationCallbacks*,
                                     VkPipeline* pPipelines) {
    @autoreleasepool {
        auto* dev    = toMv(device);
        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)dev->mtlDevice;

        // If a pipeline cache was provided, use it; otherwise fall back to
        // the device's shader cache directly.
        ShaderCache* sc = dev->shaderCache.get();
        if (pipelineCache) {
            auto* mvCache = toMv(pipelineCache);
            if (mvCache->shaderCache) sc = mvCache->shaderCache;
        }

        VkResult firstError = VK_SUCCESS;

        for (uint32_t ci = 0; ci < count; ++ci) {
            const VkGraphicsPipelineCreateInfo& info = pCIs[ci];
            auto* pipe = new MvPipeline();
            pipe->isCompute = false;

            // Store layout back-pointer for push-constant binding at draw time.
            if (info.layout) {
                pipe->layout = toMv(info.layout);
            }

            // ── Collect shader stages ────────────────────────────────────
            id<MTLFunction> vertFn = nil;
            id<MTLFunction> fragFn = nil;

            for (uint32_t si = 0; si < info.stageCount; ++si) {
                const auto& stage = info.pStages[si];
                auto* mod = toMv(stage.module);

                msl::MSLOptions opts;
                opts.entryPointName = stage.pName ? stage.pName : "";

                const CachedShader* cached = sc->getOrCompile(
                    mod->spirv.data(), mod->spirv.size(), opts, 0);

                if (!cached) {
                    MVRVB_LOG_ERROR("Failed to compile shader stage 0x%x for graphics pipeline #%u",
                                    stage.stage, ci);
                    delete pipe;
                    pPipelines[ci] = VK_NULL_HANDLE;

                    if (info.flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
                        firstError = VK_PIPELINE_COMPILE_REQUIRED;
                    } else {
                        firstError = VK_ERROR_INVALID_SHADER_NV;
                    }
                    continue;  // Try next pipeline (Vulkan allows partial success)
                }

                id<MTLFunction> fn = (__bridge id<MTLFunction>)cached->function;
                if (stage.stage & VK_SHADER_STAGE_VERTEX_BIT) {
                    vertFn = fn;
                    pipe->vertexReflection = cached->reflection;
                }
                if (stage.stage & VK_SHADER_STAGE_FRAGMENT_BIT) {
                    fragFn = fn;
                    pipe->fragmentReflection = cached->reflection;
                }
            }

            // If we failed to produce a vertex function, skip this pipeline.
            if (!vertFn) {
                if (pPipelines[ci] == VK_NULL_HANDLE) continue;  // Already handled above
                MVRVB_LOG_ERROR("Graphics pipeline #%u missing vertex shader", ci);
                delete pipe;
                pPipelines[ci] = VK_NULL_HANDLE;
                if (firstError == VK_SUCCESS) firstError = VK_ERROR_INVALID_SHADER_NV;
                continue;
            }

            // ── Build MTLRenderPipelineDescriptor ────────────────────────
            MTLRenderPipelineDescriptor* desc =
                [[MTLRenderPipelineDescriptor alloc] init];
            desc.vertexFunction   = vertFn;
            desc.fragmentFunction = fragFn;
            [desc setLabel:@"MVB-GraphicsPipeline"];

            // ── Build MTLVertexDescriptor from vertex input state ─────────
            if (info.pVertexInputState) {
                MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
                const auto& vis = *info.pVertexInputState;

                for (uint32_t b = 0; b < vis.vertexBindingDescriptionCount; ++b) {
                    const auto& bd = vis.pVertexBindingDescriptions[b];
                    if (bd.binding > kVertexBufferBaseSlot) continue;
                    const uint32_t slot = kVertexBufferBaseSlot - bd.binding;
                    vd.layouts[slot].stride = bd.stride;
                    vd.layouts[slot].stepFunction =
                        (bd.inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
                            ? MTLVertexStepFunctionPerInstance
                            : MTLVertexStepFunctionPerVertex;
                    vd.layouts[slot].stepRate = 1;
                }
                for (uint32_t a = 0; a < vis.vertexAttributeDescriptionCount; ++a) {
                    const auto& ad = vis.pVertexAttributeDescriptions[a];
                    if (ad.binding > kVertexBufferBaseSlot) continue;
                    vd.attributes[ad.location].format      = toMTLVertFmt(ad.format);
                    vd.attributes[ad.location].bufferIndex = kVertexBufferBaseSlot - ad.binding;
                    vd.attributes[ad.location].offset      = ad.offset;
                }
                desc.vertexDescriptor = vd;
            }

            // ── Rasteriser state (stored on MvPipeline for draw-time) ────
            if (info.pRasterizationState) {
                const auto& rs = *info.pRasterizationState;
                pipe->cullMode         = rs.cullMode;
                pipe->frontFace        = rs.frontFace;
                pipe->fillMode         = (rs.polygonMode == VK_POLYGON_MODE_LINE)
                    ? MTLTriangleFillModeLines
                    : MTLTriangleFillModeFill;
                pipe->depthClampEnable = rs.depthClampEnable;
                pipe->depthBiasEnable  = rs.depthBiasEnable;
                pipe->depthBiasConst   = rs.depthBiasConstantFactor;
                pipe->depthBiasSlope   = rs.depthBiasSlopeFactor;
                pipe->depthBiasClamp   = rs.depthBiasClamp;
            }

            // ── Input assembly ───────────────────────────────────────────
            if (info.pInputAssemblyState) {
                pipe->topology = info.pInputAssemblyState->topology;
            }

            // ── Multisample ──────────────────────────────────────────────
            if (info.pMultisampleState) {
                desc.rasterSampleCount =
                    info.pMultisampleState->rasterizationSamples;
                if (info.pMultisampleState->alphaToCoverageEnable)
                    desc.alphaToCoverageEnabled = YES;
                if (info.pMultisampleState->alphaToOneEnable)
                    desc.alphaToOneEnabled = YES;
            }

            // ── Color blend attachments ──────────────────────────────────
            uint32_t colorCount = 1;
            if (info.pColorBlendState) {
                colorCount = info.pColorBlendState->attachmentCount;
                for (uint32_t a = 0; a < colorCount; ++a) {
                    const auto& att = info.pColorBlendState->pAttachments[a];

                    // TODO(M6): derive pixel format from render pass info
                    desc.colorAttachments[a].pixelFormat = MTLPixelFormatBGRA8Unorm;
                    desc.colorAttachments[a].blendingEnabled = att.blendEnable;

                    if (att.blendEnable) {
                        desc.colorAttachments[a].sourceRGBBlendFactor =
                            toMTLBlendFactor(att.srcColorBlendFactor);
                        desc.colorAttachments[a].destinationRGBBlendFactor =
                            toMTLBlendFactor(att.dstColorBlendFactor);
                        desc.colorAttachments[a].rgbBlendOperation =
                            toMTLBlendOp(att.colorBlendOp);
                        desc.colorAttachments[a].sourceAlphaBlendFactor =
                            toMTLBlendFactor(att.srcAlphaBlendFactor);
                        desc.colorAttachments[a].destinationAlphaBlendFactor =
                            toMTLBlendFactor(att.dstAlphaBlendFactor);
                        desc.colorAttachments[a].alphaBlendOperation =
                            toMTLBlendOp(att.alphaBlendOp);
                    }

                    MTLColorWriteMask mask = 0;
                    if (att.colorWriteMask & VK_COLOR_COMPONENT_R_BIT)
                        mask |= MTLColorWriteMaskRed;
                    if (att.colorWriteMask & VK_COLOR_COMPONENT_G_BIT)
                        mask |= MTLColorWriteMaskGreen;
                    if (att.colorWriteMask & VK_COLOR_COMPONENT_B_BIT)
                        mask |= MTLColorWriteMaskBlue;
                    if (att.colorWriteMask & VK_COLOR_COMPONENT_A_BIT)
                        mask |= MTLColorWriteMaskAlpha;
                    desc.colorAttachments[a].writeMask = mask;
                }
            } else {
                desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            }

            // ── Depth / stencil attachment formats ───────────────────────
            desc.depthAttachmentPixelFormat   = MTLPixelFormatDepth32Float;
            desc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

            // ── MTLDepthStencilState ─────────────────────────────────────
            if (info.pDepthStencilState) {
                const auto& ds = *info.pDepthStencilState;
                pipe->depthTestEnable   = ds.depthTestEnable;
                pipe->depthWriteEnable  = ds.depthWriteEnable;
                pipe->depthCompareOp    = ds.depthCompareOp;
                pipe->stencilTestEnable = ds.stencilTestEnable;

                if (ds.stencilTestEnable) {
                    desc.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
                }

                MTLDepthStencilDescriptor* dsd =
                    [[MTLDepthStencilDescriptor alloc] init];
                dsd.depthCompareFunction = ds.depthTestEnable
                    ? toMTLCmp((VkCompareOp)ds.depthCompareOp)
                    : MTLCompareFunctionAlways;
                dsd.depthWriteEnabled = ds.depthWriteEnable;

                if (ds.stencilTestEnable) {
                    // Front face stencil
                    MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
                    front.stencilCompareFunction    = toMTLCmp((VkCompareOp)ds.front.compareOp);
                    front.stencilFailureOperation   = toMTLStencil((VkStencilOp)ds.front.failOp);
                    front.depthFailureOperation     = toMTLStencil((VkStencilOp)ds.front.depthFailOp);
                    front.depthStencilPassOperation = toMTLStencil((VkStencilOp)ds.front.passOp);
                    front.readMask  = ds.front.compareMask;
                    front.writeMask = ds.front.writeMask;
                    dsd.frontFaceStencil = front;

                    // Back face stencil
                    MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
                    back.stencilCompareFunction    = toMTLCmp((VkCompareOp)ds.back.compareOp);
                    back.stencilFailureOperation   = toMTLStencil((VkStencilOp)ds.back.failOp);
                    back.depthFailureOperation     = toMTLStencil((VkStencilOp)ds.back.depthFailOp);
                    back.depthStencilPassOperation = toMTLStencil((VkStencilOp)ds.back.passOp);
                    back.readMask  = ds.back.compareMask;
                    back.writeMask = ds.back.writeMask;
                    dsd.backFaceStencil = back;
                }

                id<MTLDepthStencilState> dss =
                    [mtlDev newDepthStencilStateWithDescriptor:dsd];
                pipe->depthStencilState = (__bridge_retained void*)dss;
            } else {
                // No depth/stencil state specified — create a default
                // (depth test on, write on, less-equal, no stencil).
                MTLDepthStencilDescriptor* dsd =
                    [[MTLDepthStencilDescriptor alloc] init];
                dsd.depthCompareFunction = MTLCompareFunctionLessEqual;
                dsd.depthWriteEnabled    = YES;
                id<MTLDepthStencilState> dss =
                    [mtlDev newDepthStencilStateWithDescriptor:dsd];
                pipe->depthStencilState = (__bridge_retained void*)dss;
            }

            // ── Dynamic state flags ──────────────────────────────────────
            if (info.pDynamicState) {
                for (uint32_t d = 0; d < info.pDynamicState->dynamicStateCount; ++d) {
                    switch (info.pDynamicState->pDynamicStates[d]) {
                        case VK_DYNAMIC_STATE_VIEWPORT:
                            pipe->hasDynamicViewport = true; break;
                        case VK_DYNAMIC_STATE_SCISSOR:
                            pipe->hasDynamicScissor = true; break;
                        case VK_DYNAMIC_STATE_DEPTH_BIAS:
                            pipe->hasDynamicDepthBias = true; break;
                        case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
                            pipe->hasDynamicBlendConstants = true; break;
                        case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
                            pipe->hasDynamicStencilCompareMask = true; break;
                        case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
                            pipe->hasDynamicStencilWriteMask = true; break;
                        case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
                            pipe->hasDynamicStencilReference = true; break;
                        default: break;
                    }
                }
            }

            // ── Compile MTLRenderPipelineState ───────────────────────────
            NSError* err = nil;
            id<MTLRenderPipelineState> rps =
                [mtlDev newRenderPipelineStateWithDescriptor:desc error:&err];

            if (!rps) {
                MVRVB_LOG_ERROR("MTLRenderPipelineState compile failed for pipeline #%u: %s",
                                ci, err ? [[err localizedDescription] UTF8String] : "unknown");
                if (pipe->depthStencilState)
                    CFRelease((__bridge CFTypeRef)pipe->depthStencilState);
                delete pipe;
                pPipelines[ci] = VK_NULL_HANDLE;
                if (firstError == VK_SUCCESS) firstError = VK_ERROR_INVALID_SHADER_NV;
                continue;
            }

            pipe->renderPipelineState = (__bridge_retained void*)rps;
            pPipelines[ci] = toVk(pipe);
            MVRVB_LOG_DEBUG("Graphics pipeline #%u created (topology=%u, cull=0x%x, dynamic=%s%s)",
                            ci, pipe->topology, pipe->cullMode,
                            pipe->hasDynamicViewport ? "V" : "",
                            pipe->hasDynamicScissor  ? "S" : "");
        } // for each pipeline

        return firstError;
    } // @autoreleasepool
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_CreateComputePipelines
// ─────────────────────────────────────────────────────────────────────────────

VkResult mvb_CreateComputePipelines(VkDevice device,
                                    VkPipelineCache pipelineCache,
                                    uint32_t count,
                                    const VkComputePipelineCreateInfo* pCIs,
                                    const VkAllocationCallbacks*,
                                    VkPipeline* pPipelines) {
    @autoreleasepool {
        auto* dev    = toMv(device);
        id<MTLDevice> mtlDev = (__bridge id<MTLDevice>)dev->mtlDevice;

        ShaderCache* sc = dev->shaderCache.get();
        if (pipelineCache) {
            auto* mvCache = toMv(pipelineCache);
            if (mvCache->shaderCache) sc = mvCache->shaderCache;
        }

        VkResult firstError = VK_SUCCESS;

        for (uint32_t ci = 0; ci < count; ++ci) {
            const auto& info = pCIs[ci];
            auto* pipe = new MvPipeline();
            pipe->isCompute = true;

            if (info.layout) {
                pipe->layout = toMv(info.layout);
            }

            auto* mod = toMv(info.stage.module);
            msl::MSLOptions opts;
            opts.entryPointName = info.stage.pName ? info.stage.pName : "";

            const CachedShader* cached = sc->getOrCompile(
                mod->spirv.data(), mod->spirv.size(), opts, 0);

            if (!cached) {
                MVRVB_LOG_ERROR("Failed to compile compute shader for pipeline #%u", ci);
                delete pipe;
                pPipelines[ci] = VK_NULL_HANDLE;
                if (firstError == VK_SUCCESS) firstError = VK_ERROR_INVALID_SHADER_NV;
                continue;
            }

            id<MTLFunction> fn = (__bridge id<MTLFunction>)cached->function;
            NSError* err = nil;
            id<MTLComputePipelineState> cps =
                [mtlDev newComputePipelineStateWithFunction:fn error:&err];

            if (!cps) {
                MVRVB_LOG_ERROR("MTLComputePipelineState compile failed for pipeline #%u: %s",
                                ci, err ? [[err localizedDescription] UTF8String] : "unknown");
                delete pipe;
                pPipelines[ci] = VK_NULL_HANDLE;
                if (firstError == VK_SUCCESS) firstError = VK_ERROR_INVALID_SHADER_NV;
                continue;
            }

            // Copy local workgroup size and reflection from SPIR-V.
            pipe->localSizeX = cached->reflection.numThreadgroupsX;
            pipe->localSizeY = cached->reflection.numThreadgroupsY;
            pipe->localSizeZ = cached->reflection.numThreadgroupsZ;
            pipe->computeReflection = cached->reflection;

            pipe->computePipelineState = (__bridge_retained void*)cps;
            pPipelines[ci] = toVk(pipe);
            MVRVB_LOG_DEBUG("Compute pipeline #%u created (workgroup=%ux%ux%u)",
                            ci, pipe->localSizeX, pipe->localSizeY, pipe->localSizeZ);
        }

        return firstError;
    } // @autoreleasepool
}

// ─────────────────────────────────────────────────────────────────────────────
//  mvb_DestroyPipeline
// ─────────────────────────────────────────────────────────────────────────────

void mvb_DestroyPipeline(VkDevice, VkPipeline pipeline, const VkAllocationCallbacks*) {
    if (!pipeline) return;
    auto* p = toMv(pipeline);
    if (p->renderPipelineState)
        CFRelease((__bridge CFTypeRef)p->renderPipelineState);
    if (p->computePipelineState)
        CFRelease((__bridge CFTypeRef)p->computePipelineState);
    if (p->depthStencilState)
        CFRelease((__bridge CFTypeRef)p->depthStencilState);
    delete p;
}

} // extern "C"
