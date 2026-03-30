/**
 * @file vk_commands.mm
 * @brief Deferred-encoding command buffer system.
 *
 * Recording model:
 *   All vkCmd* calls push a tagged DeferredCmd into MvCommandBuffer::commands.
 *   No Metal encoders are created during recording.
 *
 * Replay model (vkQueueSubmit / vkQueueSubmit2):
 *   1. Create a MTLCommandBuffer from the device's command queue.
 *   2. Iterate the deferred command list.
 *   3. Create/switch Metal encoders on the fly:
 *        BeginRenderPass / BeginRendering → close active encoder, open render encoder
 *        EndRenderPass / EndRendering     → close render encoder
 *        CopyBuffer, CopyImage, etc.     → ensure blit encoder
 *        Dispatch                         → ensure compute encoder
 *   4. Commit the MTLCommandBuffer.
 *
 * Thread safety:
 *   One MvCommandBuffer per recording thread (Vulkan spec).
 *   MvCommandPool is internally synchronized via mutex.
 */

#include "vk_commands.h"
#include "../device/vk_device.h"
#include "../format_table/format_table.h"
#include "../pipeline/vk_pipeline.h"
#include "../resources/vk_resources.h"
#include "../sync/vk_sync.h"
#include "../memory/vk_memory.h"
#include "../../shader_translator/msl_emitter/spirv_to_msl.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>

#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mvrvb {

// ═══════════════════════════════════════════════════════════════════════════════
// Enum translation helpers (used during replay)
// ═══════════════════════════════════════════════════════════════════════════════

static MTLLoadAction toMTLLoad(VkAttachmentLoadOp op) {
    switch (op) {
        case VK_ATTACHMENT_LOAD_OP_LOAD:      return MTLLoadActionLoad;
        case VK_ATTACHMENT_LOAD_OP_CLEAR:     return MTLLoadActionClear;
        case VK_ATTACHMENT_LOAD_OP_DONT_CARE: return MTLLoadActionDontCare;
        default:                              return MTLLoadActionDontCare;
    }
}

static MTLStoreAction toMTLStore(VkAttachmentStoreOp op) {
    switch (op) {
        case VK_ATTACHMENT_STORE_OP_STORE:     return MTLStoreActionStore;
        case VK_ATTACHMENT_STORE_OP_DONT_CARE: return MTLStoreActionDontCare;
        default:                               return MTLStoreActionDontCare;
    }
}

static MTLPrimitiveType toMTLPrimitive(VkPrimitiveTopology topo) {
    switch (topo) {
        case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:     return MTLPrimitiveTypePoint;
        case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:      return MTLPrimitiveTypeLine;
        case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:     return MTLPrimitiveTypeLineStrip;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  return MTLPrimitiveTypeTriangle;
        case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return MTLPrimitiveTypeTriangleStrip;
        default:                                   return MTLPrimitiveTypeTriangle;
    }
}

static const char* bindPointName(VkPipelineBindPoint bindPoint) {
    switch (bindPoint) {
        case VK_PIPELINE_BIND_POINT_GRAPHICS: return "graphics";
        case VK_PIPELINE_BIND_POINT_COMPUTE:  return "compute";
        default:                              return "other";
    }
}

static const char* commandBufferLevelName(VkCommandBufferLevel level) {
    switch (level) {
        case VK_COMMAND_BUFFER_LEVEL_PRIMARY: return "primary";
        case VK_COMMAND_BUFFER_LEVEL_SECONDARY: return "secondary";
        default: return "unknown";
    }
}

static const char* filterName(VkFilter filter) {
    switch (filter) {
        case VK_FILTER_NEAREST: return "nearest";
        case VK_FILTER_LINEAR: return "linear";
        default: return "other";
    }
}

static const char* encoderTypeName(EncoderType type) {
    switch (type) {
        case EncoderType::None: return "none";
        case EncoderType::Render: return "render";
        case EncoderType::Compute: return "compute";
        case EncoderType::Blit: return "blit";
        default: return "unknown";
    }
}

static MTLIndexType toMTLIndex(VkIndexType t) {
    return (t == VK_INDEX_TYPE_UINT32) ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
}

static bool isDepthFormat(VkFormat fmt) {
    return fmt == VK_FORMAT_D16_UNORM       || fmt == VK_FORMAT_D32_SFLOAT ||
           fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           fmt == VK_FORMAT_D16_UNORM_S8_UINT;
}

static bool hasStencilComponent(VkFormat fmt) {
    return fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           fmt == VK_FORMAT_D16_UNORM_S8_UINT || fmt == VK_FORMAT_S8_UINT;
}

enum class TransferPipelineKind : uint8_t {
    BlitFloat,
    BlitUInt,
    BlitSInt,
    ResolveFloat,
    ResolveUInt,
    ResolveSInt,
};

struct TransferRegionUniforms {
    float dstOrigin[2]{};
    float dstExtent[2]{};
    float srcOrigin[2]{};
    float srcExtent[2]{};
    float srcTextureSize[2]{};
    uint32_t sampleCount{1};
    uint32_t _pad[3]{};
};

struct TransferFillParams {
    uint32_t wordCount{0};
    uint32_t pattern{0};
};

struct TransferPipelineEntry {
    MTLPixelFormat      pixelFormat{MTLPixelFormatInvalid};
    TransferPipelineKind kind{TransferPipelineKind::BlitFloat};
    void*               pipeline{nullptr};
};

struct TransferHelperState {
    std::mutex                      mutex;
    void*                           library{nullptr};
    void*                           nearestSampler{nullptr};
    void*                           linearSampler{nullptr};
    void*                           fillBufferPipeline{nullptr};
    std::vector<TransferPipelineEntry> renderPipelines;
};

static TransferHelperState& transferHelperState() {
    static TransferHelperState state;
    return state;
}

static const char* transferHelperMSL() {
    return R"MSL(
#include <metal_stdlib>
using namespace metal;

struct TransferRegionUniforms {
    float2 dstOrigin;
    float2 dstExtent;
    float2 srcOrigin;
    float2 srcExtent;
    float2 srcTextureSize;
    uint sampleCount;
    uint3 _pad;
};

struct FillParams {
    uint wordCount;
    uint pattern;
};

struct FullscreenVertexOut {
    float4 position [[position]];
};

vertex FullscreenVertexOut mvb_transfer_fullscreen_vs(uint vertexId [[vertex_id]]) {
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    FullscreenVertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    return out;
}

static inline float2 source_texel_coord(float2 fragPos,
                                        constant TransferRegionUniforms& u) {
    float2 local = ((fragPos - u.dstOrigin) + float2(0.5)) / u.dstExtent;
    return u.srcOrigin + local * u.srcExtent;
}

static inline uint2 clamp_texel_coord(float2 texelCoord, float2 texSize) {
    float2 maxCoord = max(texSize - float2(1.0), float2(0.0));
    return uint2(clamp(floor(texelCoord), float2(0.0), maxCoord));
}

fragment float4 mvb_transfer_blit_float_fs(
        float4 position [[position]],
        texture2d<float, access::sample> src [[texture(0)]],
        sampler srcSampler [[sampler(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    float2 texel = source_texel_coord(position.xy, u);
    float2 uv = (texel + float2(0.5)) / u.srcTextureSize;
    return src.sample(srcSampler, uv);
}

fragment uint4 mvb_transfer_blit_uint_fs(
        float4 position [[position]],
        texture2d<uint, access::read> src [[texture(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    return src.read(clamp_texel_coord(source_texel_coord(position.xy, u), u.srcTextureSize));
}

fragment int4 mvb_transfer_blit_int_fs(
        float4 position [[position]],
        texture2d<int, access::read> src [[texture(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    return src.read(clamp_texel_coord(source_texel_coord(position.xy, u), u.srcTextureSize));
}

fragment float4 mvb_transfer_resolve_float_fs(
        float4 position [[position]],
        texture2d_ms<float, access::read> src [[texture(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    uint2 coord = clamp_texel_coord(source_texel_coord(position.xy, u), u.srcTextureSize);
    uint count = max(u.sampleCount, 1u);
    float4 accum = float4(0.0);
    for (uint sample = 0; sample < count; ++sample) {
        accum += src.read(coord, sample);
    }
    return accum / float(count);
}

fragment uint4 mvb_transfer_resolve_uint_fs(
        float4 position [[position]],
        texture2d_ms<uint, access::read> src [[texture(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    uint2 coord = clamp_texel_coord(source_texel_coord(position.xy, u), u.srcTextureSize);
    return src.read(coord, 0);
}

fragment int4 mvb_transfer_resolve_int_fs(
        float4 position [[position]],
        texture2d_ms<int, access::read> src [[texture(0)]],
        constant TransferRegionUniforms& u [[buffer(0)]]) {
    uint2 coord = clamp_texel_coord(source_texel_coord(position.xy, u), u.srcTextureSize);
    return src.read(coord, 0);
}

kernel void mvb_transfer_fill_buffer_cs(device uint* dst [[buffer(0)]],
                                        constant FillParams& params [[buffer(1)]],
                                        uint gid [[thread_position_in_grid]]) {
    if (gid < params.wordCount) {
        dst[gid] = params.pattern;
    }
}
)MSL";
}

static id<MTLLibrary> ensureTransferLibraryLocked(MvDevice* device,
                                                  TransferHelperState& state) {
    if (state.library) {
        return (__bridge id<MTLLibrary>)state.library;
    }
    if (!device || !device->mtlDevice) return nil;

    NSString* source = [NSString stringWithUTF8String:transferHelperMSL()];
    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    options.languageVersion = MTLLanguageVersion2_4;
    options.fastMathEnabled = YES;

    NSError* error = nil;
    id<MTLLibrary> library = [device->mtlDevice newLibraryWithSource:source
                                                             options:options
                                                               error:&error];
    if (!library) {
        MVRVB_LOG_ERROR("Transfer helper shader compile failed: %s",
                        error ? [[error localizedDescription] UTF8String] : "(unknown)");
        return nil;
    }

    state.library = (__bridge_retained void*)library;
    return library;
}

static const char* transferFragmentName(TransferPipelineKind kind) {
    switch (kind) {
        case TransferPipelineKind::BlitFloat:    return "mvb_transfer_blit_float_fs";
        case TransferPipelineKind::BlitUInt:     return "mvb_transfer_blit_uint_fs";
        case TransferPipelineKind::BlitSInt:     return "mvb_transfer_blit_int_fs";
        case TransferPipelineKind::ResolveFloat: return "mvb_transfer_resolve_float_fs";
        case TransferPipelineKind::ResolveUInt:  return "mvb_transfer_resolve_uint_fs";
        case TransferPipelineKind::ResolveSInt:  return "mvb_transfer_resolve_int_fs";
    }

    return "mvb_transfer_blit_float_fs";
}

static TransferPipelineKind blitPipelineKindForFormat(const FormatInfo& info) {
    if (info.isUInt) return TransferPipelineKind::BlitUInt;
    if (info.isSInt) return TransferPipelineKind::BlitSInt;
    return TransferPipelineKind::BlitFloat;
}

static TransferPipelineKind resolvePipelineKindForFormat(const FormatInfo& info) {
    if (info.isUInt) return TransferPipelineKind::ResolveUInt;
    if (info.isSInt) return TransferPipelineKind::ResolveSInt;
    return TransferPipelineKind::ResolveFloat;
}

static bool isIntegralFormat(const FormatInfo& info) {
    return info.isUInt || info.isSInt;
}

static bool areTransferColorClassesCompatible(const FormatInfo& srcInfo,
                                              const FormatInfo& dstInfo) {
    if (srcInfo.isUInt) return dstInfo.isUInt;
    if (srcInfo.isSInt) return dstInfo.isSInt;
    return !dstInfo.isUInt && !dstInfo.isSInt;
}

static id<MTLSamplerState> ensureTransferSampler(MvDevice* device, VkFilter filter) {
    auto& state = transferHelperState();
    std::lock_guard<std::mutex> lock(state.mutex);

    void*& samplerSlot = (filter == VK_FILTER_LINEAR) ? state.linearSampler : state.nearestSampler;
    if (samplerSlot) {
        return (__bridge id<MTLSamplerState>)samplerSlot;
    }
    if (!device || !device->mtlDevice) return nil;

    MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
    desc.minFilter = (filter == VK_FILTER_LINEAR)
        ? MTLSamplerMinMagFilterLinear
        : MTLSamplerMinMagFilterNearest;
    desc.magFilter = desc.minFilter;
    desc.mipFilter = MTLSamplerMipFilterNearest;
    desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.tAddressMode = MTLSamplerAddressModeClampToEdge;

    id<MTLSamplerState> sampler = [device->mtlDevice newSamplerStateWithDescriptor:desc];
    if (sampler) {
        samplerSlot = (__bridge_retained void*)sampler;
    }
    return sampler;
}

static id<MTLComputePipelineState> ensureTransferFillBufferPipeline(MvDevice* device) {
    auto& state = transferHelperState();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (state.fillBufferPipeline) {
        return (__bridge id<MTLComputePipelineState>)state.fillBufferPipeline;
    }

    id<MTLLibrary> library = ensureTransferLibraryLocked(device, state);
    if (!library || !device || !device->mtlDevice) return nil;

    id<MTLFunction> function =
        [library newFunctionWithName:@"mvb_transfer_fill_buffer_cs"];
    if (!function) {
        MVRVB_LOG_ERROR("Transfer helper fill kernel lookup failed");
        return nil;
    }

    NSError* error = nil;
    id<MTLComputePipelineState> pipeline =
        [device->mtlDevice newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline) {
        MVRVB_LOG_ERROR("Transfer fill pipeline creation failed: %s",
                        error ? [[error localizedDescription] UTF8String] : "(unknown)");
        return nil;
    }

    state.fillBufferPipeline = (__bridge_retained void*)pipeline;
    return pipeline;
}

static id<MTLRenderPipelineState> ensureTransferRenderPipeline(
        MvDevice* device,
        MTLPixelFormat dstFormat,
        TransferPipelineKind kind) {
    auto& state = transferHelperState();
    std::lock_guard<std::mutex> lock(state.mutex);

    for (const auto& entry : state.renderPipelines) {
        if (entry.pixelFormat == dstFormat && entry.kind == kind && entry.pipeline) {
            return (__bridge id<MTLRenderPipelineState>)entry.pipeline;
        }
    }

    id<MTLLibrary> library = ensureTransferLibraryLocked(device, state);
    if (!library || !device || !device->mtlDevice) return nil;

    id<MTLFunction> vs = [library newFunctionWithName:@"mvb_transfer_fullscreen_vs"];
    id<MTLFunction> fs =
        [library newFunctionWithName:[NSString stringWithUTF8String:transferFragmentName(kind)]];
    if (!vs || !fs) {
        MVRVB_LOG_ERROR("Transfer helper function lookup failed");
        return nil;
    }

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.rasterSampleCount = 1;
    desc.colorAttachments[0].pixelFormat = dstFormat;

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [device->mtlDevice newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipeline) {
        MVRVB_LOG_ERROR("Transfer render pipeline creation failed: %s",
                        error ? [[error localizedDescription] UTF8String] : "(unknown)");
        return nil;
    }

    state.renderPipelines.push_back({dstFormat, kind, (__bridge_retained void*)pipeline});
    return pipeline;
}

static uint32_t mipExtent(uint32_t baseExtent, uint32_t mipLevel) {
    return std::max(1u, baseExtent >> mipLevel);
}

static uint32_t resolveRangeLevelCount(const MvImage* image,
                                       const VkImageSubresourceRange& range) {
    if (!image) return 0;
    if (range.levelCount == VK_REMAINING_MIP_LEVELS) {
        return image->mipLevels > range.baseMipLevel
            ? image->mipLevels - range.baseMipLevel
            : 0u;
    }
    return range.levelCount;
}

static uint32_t resolveRangeLayerCount(const MvImage* image,
                                       const VkImageSubresourceRange& range,
                                       uint32_t mipLevel) {
    if (!image) return 0;
    if (image->imageType == VK_IMAGE_TYPE_3D) {
        return mipExtent(image->depth, mipLevel);
    }
    if (range.layerCount == VK_REMAINING_ARRAY_LAYERS) {
        return image->arrayLayers > range.baseArrayLayer
            ? image->arrayLayers - range.baseArrayLayer
            : 0u;
    }
    return range.layerCount;
}

static bool resolveTransferSlices(const MvImage* image,
                                  const VkImageSubresourceLayers& subresource,
                                  const VkOffset3D offsets[2],
                                  uint32_t* count,
                                  int32_t* start,
                                  int32_t* step) {
    if (!image || !count || !start || !step) return false;

    if (image->imageType == VK_IMAGE_TYPE_3D) {
        *count = static_cast<uint32_t>(std::abs(offsets[1].z - offsets[0].z));
        *start = offsets[0].z;
        *step = (offsets[1].z >= offsets[0].z) ? 1 : -1;
    } else {
        *count = subresource.layerCount;
        *start = static_cast<int32_t>(subresource.baseArrayLayer);
        *step = 1;
    }

    return *count > 0;
}

static uint64_t resolvedBufferRangeSize(const MvBuffer* buffer,
                                        VkDeviceSize dstOffset,
                                        VkDeviceSize size) {
    if (!buffer || dstOffset >= buffer->size) return 0;
    if (size == VK_WHOLE_SIZE) return buffer->size - dstOffset;
    return std::min<uint64_t>(size, buffer->size - dstOffset);
}

static bool isRepeatedBytePattern(uint32_t pattern) {
    const uint8_t byteValue = static_cast<uint8_t>(pattern & 0xFF);
    return pattern == (uint32_t(byteValue) * 0x01010101u);
}

static bool buildTransferRegionUniforms(const VkOffset3D srcOffsets[2],
                                        const VkOffset3D dstOffsets[2],
                                        uint32_t srcWidth,
                                        uint32_t srcHeight,
                                        uint32_t sampleCount,
                                        TransferRegionUniforms* uniforms,
                                        MTLViewport* viewport,
                                        MTLScissorRect* scissor) {
    if (!uniforms || !viewport || !scissor) return false;

    const uint32_t dstWidth =
        static_cast<uint32_t>(std::abs(dstOffsets[1].x - dstOffsets[0].x));
    const uint32_t dstHeight =
        static_cast<uint32_t>(std::abs(dstOffsets[1].y - dstOffsets[0].y));
    if (dstWidth == 0 || dstHeight == 0) return false;

    const int32_t dstMinX = std::min(dstOffsets[0].x, dstOffsets[1].x);
    const int32_t dstMinY = std::min(dstOffsets[0].y, dstOffsets[1].y);

    uniforms->dstOrigin[0] = static_cast<float>(dstMinX);
    uniforms->dstOrigin[1] = static_cast<float>(dstMinY);
    uniforms->dstExtent[0] = static_cast<float>(dstWidth);
    uniforms->dstExtent[1] = static_cast<float>(dstHeight);
    uniforms->srcOrigin[0] = static_cast<float>(srcOffsets[0].x);
    uniforms->srcOrigin[1] = static_cast<float>(srcOffsets[0].y);
    uniforms->srcExtent[0] = static_cast<float>(srcOffsets[1].x - srcOffsets[0].x);
    uniforms->srcExtent[1] = static_cast<float>(srcOffsets[1].y - srcOffsets[0].y);
    uniforms->srcTextureSize[0] = static_cast<float>(srcWidth);
    uniforms->srcTextureSize[1] = static_cast<float>(srcHeight);
    uniforms->sampleCount = sampleCount;

    viewport->originX = static_cast<double>(dstMinX);
    viewport->originY = static_cast<double>(dstMinY);
    viewport->width = static_cast<double>(dstWidth);
    viewport->height = static_cast<double>(dstHeight);
    viewport->znear = 0.0;
    viewport->zfar = 1.0;

    scissor->x = static_cast<NSUInteger>(std::max(0, dstMinX));
    scissor->y = static_cast<NSUInteger>(std::max(0, dstMinY));
    scissor->width = dstWidth;
    scissor->height = dstHeight;
    return true;
}

static id<MTLTexture> createSingleSliceTextureView(id<MTLTexture> texture,
                                                   MTLPixelFormat format,
                                                   MTLTextureType textureType,
                                                   uint32_t mipLevel,
                                                   uint32_t slice) {
    if (!texture) return nil;
    return [texture newTextureViewWithPixelFormat:format
                                      textureType:textureType
                                           levels:NSMakeRange(mipLevel, 1)
                                           slices:NSMakeRange(slice, 1)];
}

static bool encodeColorBlitRegion(MvDevice* device,
                                  id<MTLCommandBuffer> mtlCB,
                                  const MvImage* srcImage,
                                  const MvImage* dstImage,
                                  const VkImageBlit& region,
                                  VkFilter filter) {
    if (!device || !mtlCB || !srcImage || !dstImage || !srcImage->mtlTexture || !dstImage->mtlTexture) {
        return false;
    }

    const FormatInfo& srcInfo = getFormatInfo(srcImage->format);
    const FormatInfo& dstInfo = getFormatInfo(dstImage->format);
    if (srcInfo.isDepth || srcInfo.isStencil || dstInfo.isDepth || dstInfo.isStencil ||
        srcInfo.isCompressed || dstInfo.isCompressed || !dstInfo.isRenderable ||
        !areTransferColorClassesCompatible(srcInfo, dstInfo)) {
        return false;
    }

    if (filter == VK_FILTER_LINEAR && (isIntegralFormat(srcInfo) || !srcInfo.isFilterable)) {
        MVRVB_LOG_WARN("BlitImage requested linear filtering on a non-filterable format; "
                       "falling back to nearest");
        filter = VK_FILTER_NEAREST;
    }

    uint32_t sliceCount = 0;
    int32_t srcSliceStart = 0;
    int32_t srcSliceStep = 1;
    int32_t dstSliceStart = 0;
    int32_t dstSliceStep = 1;
    if (!resolveTransferSlices(srcImage, region.srcSubresource, region.srcOffsets,
                               &sliceCount, &srcSliceStart, &srcSliceStep)) {
        return false;
    }

    uint32_t dstSliceCount = 0;
    if (!resolveTransferSlices(dstImage, region.dstSubresource, region.dstOffsets,
                               &dstSliceCount, &dstSliceStart, &dstSliceStep) ||
        dstSliceCount != sliceCount) {
        return false;
    }

    id<MTLTexture> srcTexture = (__bridge id<MTLTexture>)srcImage->mtlTexture;
    id<MTLTexture> dstTexture = (__bridge id<MTLTexture>)dstImage->mtlTexture;
    const MTLPixelFormat srcFormat = vkFormatToMTL(srcImage->format);
    const MTLPixelFormat dstFormat = vkFormatToMTL(dstImage->format);
    const TransferPipelineKind pipelineKind = blitPipelineKindForFormat(srcInfo);
    id<MTLRenderPipelineState> pipeline =
        ensureTransferRenderPipeline(device, dstFormat, pipelineKind);
    if (!pipeline) return false;

    id<MTLSamplerState> sampler = nil;
    if (!isIntegralFormat(srcInfo)) {
        sampler = ensureTransferSampler(device, filter);
        if (!sampler) return false;
    }

    const uint32_t srcMipWidth = mipExtent(srcImage->width, region.srcSubresource.mipLevel);
    const uint32_t srcMipHeight = mipExtent(srcImage->height, region.srcSubresource.mipLevel);

    for (uint32_t sliceIndex = 0; sliceIndex < sliceCount; ++sliceIndex) {
        const uint32_t srcSlice = static_cast<uint32_t>(srcSliceStart + srcSliceStep * int32_t(sliceIndex));
        const uint32_t dstSlice = static_cast<uint32_t>(dstSliceStart + dstSliceStep * int32_t(sliceIndex));

        @autoreleasepool {
            id<MTLTexture> srcView = createSingleSliceTextureView(
                srcTexture,
                srcFormat,
                MTLTextureType2D,
                region.srcSubresource.mipLevel,
                srcSlice);
            id<MTLTexture> dstView = createSingleSliceTextureView(
                dstTexture,
                dstFormat,
                MTLTextureType2D,
                region.dstSubresource.mipLevel,
                dstSlice);
            if (!srcView || !dstView) {
                MVRVB_LOG_WARN("BlitImage could not create temporary texture views");
                return false;
            }

            TransferRegionUniforms uniforms{};
            MTLViewport viewport{};
            MTLScissorRect scissor{};
            if (!buildTransferRegionUniforms(region.srcOffsets,
                                             region.dstOffsets,
                                             srcMipWidth,
                                             srcMipHeight,
                                             1,
                                             &uniforms,
                                             &viewport,
                                             &scissor)) {
                continue;
            }

            MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
            passDesc.colorAttachments[0].texture = dstView;
            passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> enc =
                [mtlCB renderCommandEncoderWithDescriptor:passDesc];
            [enc setRenderPipelineState:pipeline];
            [enc setViewport:viewport];
            [enc setScissorRect:scissor];
            [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [enc setFragmentTexture:srcView atIndex:0];
            if (sampler) {
                [enc setFragmentSamplerState:sampler atIndex:0];
            }
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
        }
    }

    return true;
}

static bool encodeColorResolveRegion(MvDevice* device,
                                     id<MTLCommandBuffer> mtlCB,
                                     const MvImage* srcImage,
                                     const MvImage* dstImage,
                                     const VkImageResolve& region) {
    if (!device || !mtlCB || !srcImage || !dstImage || !srcImage->mtlTexture || !dstImage->mtlTexture) {
        return false;
    }

    const FormatInfo& srcInfo = getFormatInfo(srcImage->format);
    const FormatInfo& dstInfo = getFormatInfo(dstImage->format);
    if (srcInfo.isDepth || srcInfo.isStencil || dstInfo.isDepth || dstInfo.isStencil ||
        srcInfo.isCompressed || dstInfo.isCompressed || !dstInfo.isRenderable ||
        !areTransferColorClassesCompatible(srcInfo, dstInfo)) {
        return false;
    }

    const uint32_t sliceCount = region.srcSubresource.layerCount;
    if (sliceCount == 0 || sliceCount != region.dstSubresource.layerCount) {
        return false;
    }

    id<MTLTexture> srcTexture = (__bridge id<MTLTexture>)srcImage->mtlTexture;
    id<MTLTexture> dstTexture = (__bridge id<MTLTexture>)dstImage->mtlTexture;
    const TransferPipelineKind pipelineKind = resolvePipelineKindForFormat(srcInfo);
    id<MTLRenderPipelineState> pipeline =
        ensureTransferRenderPipeline(device, vkFormatToMTL(dstImage->format), pipelineKind);
    if (!pipeline) return false;

    VkOffset3D srcOffsets[2] = {
        region.srcOffset,
        VkOffset3D {
            region.srcOffset.x + static_cast<int32_t>(region.extent.width),
            region.srcOffset.y + static_cast<int32_t>(region.extent.height),
            region.srcOffset.z + static_cast<int32_t>(region.extent.depth)
        }
    };
    VkOffset3D dstOffsets[2] = {
        region.dstOffset,
        VkOffset3D {
            region.dstOffset.x + static_cast<int32_t>(region.extent.width),
            region.dstOffset.y + static_cast<int32_t>(region.extent.height),
            region.dstOffset.z + static_cast<int32_t>(region.extent.depth)
        }
    };

    const uint32_t srcMipWidth = mipExtent(srcImage->width, region.srcSubresource.mipLevel);
    const uint32_t srcMipHeight = mipExtent(srcImage->height, region.srcSubresource.mipLevel);

    for (uint32_t layer = 0; layer < sliceCount; ++layer) {
        @autoreleasepool {
            id<MTLTexture> srcView = createSingleSliceTextureView(
                srcTexture,
                vkFormatToMTL(srcImage->format),
                MTLTextureType2DMultisample,
                region.srcSubresource.mipLevel,
                region.srcSubresource.baseArrayLayer + layer);
            id<MTLTexture> dstView = createSingleSliceTextureView(
                dstTexture,
                vkFormatToMTL(dstImage->format),
                MTLTextureType2D,
                region.dstSubresource.mipLevel,
                region.dstSubresource.baseArrayLayer + layer);
            if (!srcView || !dstView) {
                MVRVB_LOG_WARN("ResolveImage could not create temporary texture views");
                return false;
            }

            MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
            passDesc.colorAttachments[0].texture = dstView;
            passDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> enc =
                [mtlCB renderCommandEncoderWithDescriptor:passDesc];

            TransferRegionUniforms uniforms{};
            MTLViewport viewport{};
            MTLScissorRect scissor{};
            if (!buildTransferRegionUniforms(srcOffsets,
                                             dstOffsets,
                                             srcMipWidth,
                                             srcMipHeight,
                                             srcImage->samples,
                                             &uniforms,
                                             &viewport,
                                             &scissor)) {
                [enc endEncoding];
                continue;
            }

            [enc setRenderPipelineState:pipeline];
            [enc setViewport:viewport];
            [enc setScissorRect:scissor];
            [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [enc setFragmentTexture:srcView atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [enc endEncoding];
        }
    }

    return true;
}

static MTLClearColor toMTLClearColor(const VkClearColorValue& color, VkFormat format) {
    const FormatInfo& info = getFormatInfo(format);
    if (info.isUInt) {
        return MTLClearColorMake(color.uint32[0], color.uint32[1], color.uint32[2], color.uint32[3]);
    }
    if (info.isSInt) {
        return MTLClearColorMake(color.int32[0], color.int32[1], color.int32[2], color.int32[3]);
    }
    return MTLClearColorMake(color.float32[0], color.float32[1], color.float32[2], color.float32[3]);
}

// ═══════════════════════════════════════════════════════════════════════════════
// MvCommandPool implementation
// ═══════════════════════════════════════════════════════════════════════════════

MvCommandBuffer* MvCommandPool::acquire(VkCommandBufferLevel level) {
    std::lock_guard<std::mutex> lk(mutex);
    MvCommandBuffer* cb = nullptr;
    if (!freeList.empty()) {
        cb = freeList.back();
        freeList.pop_back();
    } else {
        cb = new MvCommandBuffer();
        allocated.push_back(cb);
    }
    cb->pool  = this;
    cb->level = level;
    cb->state = CmdBufState::Initial;
    cb->commands.clear();
    cb->inlineDataBlobs.clear();
    return cb;
}

void MvCommandPool::release(MvCommandBuffer* cb) {
    if (!cb) return;
    std::lock_guard<std::mutex> lk(mutex);
    cb->reset();
    freeList.push_back(cb);
}

void MvCommandPool::resetAll() {
    std::lock_guard<std::mutex> lk(mutex);
    for (auto* cb : allocated) {
        cb->reset();
    }
    // Move all allocated back to free list
    freeList.insert(freeList.end(), allocated.begin(), allocated.end());
    allocated.clear();
}

MvCommandPool::~MvCommandPool() {
    for (auto* cb : allocated) delete cb;
    for (auto* cb : freeList)  delete cb;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Replay engine — state tracking during Metal command encoding
// ═══════════════════════════════════════════════════════════════════════════════

struct BoundDescriptorSetState {
    VkDescriptorSet set{VK_NULL_HANDLE};
    uint32_t dynamicOffsetCount{0};
    uint32_t dynamicOffsets[kMaxDynamicOffsets]{};
};

struct ReplayState {
    MvDevice*                      device{nullptr};
    const MvCommandBuffer*         sourceCommandBuffer{nullptr};
    id<MTLCommandBuffer>          mtlCB{nil};
    id<MTLRenderCommandEncoder>   renderEnc{nil};
    id<MTLComputeCommandEncoder>  computeEnc{nil};
    id<MTLBlitCommandEncoder>     blitEnc{nil};
    EncoderType                   activeEncoder{EncoderType::None};

    // Cached pipeline state for draw calls
    MvPipeline*                   boundGraphicsPipeline{nullptr};
    MvPipeline*                   boundComputePipeline{nullptr};
    bool                          graphicsPipelineDirty{false};
    bool                          computePipelineDirty{false};

    // Dirty state applied before each draw
    bool                          viewportDirty{false};
    VkViewport                    viewport{};
    bool                          scissorDirty{false};
    VkRect2D                      scissor{};
    bool                          depthBiasDirty{false};
    float                         depthBiasConstant{0}, depthBiasClamp{0}, depthBiasSlope{0};
    bool                          blendConstDirty{false};
    float                         blendConstants[4]{};
    bool                          stencilRefDirty{false};
    uint32_t                      stencilRefFront{0}, stencilRefBack{0};

    // Push constants (single block, max 256 bytes)
    uint8_t                       pushConstData[kMaxPushConstantBytes]{};
    uint32_t                      pushConstSize{0};
    bool                          pushConstDirty{false};

    // Vertex buffers
    VkBuffer                      vertexBuffers[kMaxVertexBindings]{};
    VkDeviceSize                  vertexOffsets[kMaxVertexBindings]{};
    uint32_t                      vertexBindingsDirty{0}; // bitmask

    // Index buffer
    VkBuffer                      indexBuffer{VK_NULL_HANDLE};
    VkDeviceSize                  indexOffset{0};
    VkIndexType                   indexType{VK_INDEX_TYPE_UINT16};

    // ── Descriptor sets (Milestone 6) ────────────────────────────────────
    BoundDescriptorSetState       graphicsDescriptorSets[kMaxDescriptorSets]{};
    BoundDescriptorSetState       computeDescriptorSets[kMaxDescriptorSets]{};
    bool                          graphicsDescriptorsDirty{false};
    bool                          computeDescriptorsDirty{false};

    // ── Encoder management ──────────────────────────────────────────────
    void endActiveEncoder() {
        switch (activeEncoder) {
            case EncoderType::Render:  if (renderEnc)  { [renderEnc  endEncoding]; renderEnc  = nil; } break;
            case EncoderType::Compute: if (computeEnc) { [computeEnc endEncoding]; computeEnc = nil; } break;
            case EncoderType::Blit:    if (blitEnc)    { [blitEnc    endEncoding]; blitEnc    = nil; } break;
            default: break;
        }
        activeEncoder = EncoderType::None;
    }

    void ensureBlitEncoder() {
        if (activeEncoder == EncoderType::Blit && blitEnc) return;
        endActiveEncoder();
        blitEnc = [mtlCB blitCommandEncoder];
        activeEncoder = EncoderType::Blit;
    }

    void ensureComputeEncoder() {
        if (activeEncoder == EncoderType::Compute && computeEnc) return;
        endActiveEncoder();
        computeEnc = [mtlCB computeCommandEncoder];
        activeEncoder = EncoderType::Compute;
        markComputeEncoderStateDirty();
    }

    static bool isDynamicDescriptorType(VkDescriptorType type) {
        return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
               type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    }

    static uint32_t slotForArrayElement(uint32_t baseSlot,
                                        uint32_t arrayIndex,
                                        uint32_t slotLimit) {
        if (baseSlot >= slotLimit || arrayIndex >= slotLimit - baseSlot) {
            return UINT32_MAX;
        }

        return baseSlot + arrayIndex;
    }

    static const msl::BufferBinding* findBufferBinding(const msl::MSLReflection& reflection,
                                                       uint32_t setIndex,
                                                       const DescriptorBinding& descriptor) {
        for (const auto& binding : reflection.buffers) {
            if (binding.isPushConst) continue;
            if (binding.set == setIndex && binding.binding == descriptor.binding) {
                return &binding;
            }
        }

        return nullptr;
    }

    static const msl::TextureBinding* findTextureBinding(const msl::MSLReflection& reflection,
                                                         uint32_t setIndex,
                                                         const DescriptorBinding& descriptor) {
        for (const auto& binding : reflection.textures) {
            if (binding.set == setIndex && binding.binding == descriptor.binding) {
                return &binding;
            }
        }

        return nullptr;
    }

    static const msl::SamplerBinding* findSamplerBinding(const msl::MSLReflection& reflection,
                                                         uint32_t setIndex,
                                                         const DescriptorBinding& descriptor) {
        for (const auto& binding : reflection.samplers) {
            if (binding.set == setIndex && binding.binding == descriptor.binding) {
                return &binding;
            }
        }

        return nullptr;
    }

    BoundDescriptorSetState* descriptorSetsForBindPoint(VkPipelineBindPoint bindPoint) {
        if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
            return computeDescriptorSets;
        }

        return graphicsDescriptorSets;
    }

    const BoundDescriptorSetState* descriptorSetsForBindPoint(VkPipelineBindPoint bindPoint) const {
        if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
            return computeDescriptorSets;
        }

        return graphicsDescriptorSets;
    }

    bool& descriptorsDirtyForBindPoint(VkPipelineBindPoint bindPoint) {
        if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
            return computeDescriptorsDirty;
        }

        return graphicsDescriptorsDirty;
    }

    static uint32_t countDynamicDescriptors(const MvDescriptorSetLayout* layout) {
        if (!layout) return 0;

        uint32_t count = 0;
        for (const auto& binding : layout->bindings) {
            if (isDynamicDescriptorType(binding.descriptorType)) {
                count += binding.descriptorCount;
            }
        }

        return count;
    }

    static uint32_t countDirtyVertexBindings(uint32_t dirtyMask) {
        uint32_t count = 0;
        while (dirtyMask) {
            count += dirtyMask & 1u;
            dirtyMask >>= 1u;
        }
        return count;
    }

    bool hasAnyBoundDescriptorSets(VkPipelineBindPoint bindPoint) const {
        const auto* descriptorSets = descriptorSetsForBindPoint(bindPoint);
        for (uint32_t i = 0; i < kMaxDescriptorSets; ++i) {
            if (descriptorSets[i].set != VK_NULL_HANDLE) {
                return true;
            }
        }

        return false;
    }

    void markRenderEncoderStateDirty() {
        if (boundGraphicsPipeline) {
            graphicsPipelineDirty = true;
        }
        if (viewport.width > 0.0f || viewport.height > 0.0f) {
            viewportDirty = true;
        }
        if (scissor.extent.width > 0 || scissor.extent.height > 0) {
            scissorDirty = true;
        }
        if (pushConstSize > 0) {
            pushConstDirty = true;
        }
        if (hasAnyBoundDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS)) {
            graphicsDescriptorsDirty = true;
        }

        for (uint32_t i = 0; i < kMaxVertexBindings; ++i) {
            if (vertexBuffers[i] != VK_NULL_HANDLE) {
                vertexBindingsDirty |= (1u << i);
            }
        }

        if (boundGraphicsPipeline && boundGraphicsPipeline->hasDynamicDepthBias) {
            depthBiasDirty = true;
        }
        if (boundGraphicsPipeline && boundGraphicsPipeline->hasDynamicBlendConstants) {
            blendConstDirty = true;
        }
        if (boundGraphicsPipeline && boundGraphicsPipeline->stencilTestEnable) {
            stencilRefDirty = true;
        }
    }

    void markComputeEncoderStateDirty() {
        if (boundComputePipeline) {
            computePipelineDirty = true;
        }
        if (pushConstSize > 0) {
            pushConstDirty = true;
        }
        if (hasAnyBoundDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE)) {
            computeDescriptorsDirty = true;
        }
    }

    // ── Flush descriptor bindings to a render encoder ───────────────────
    #if 0
    void flushDescriptorsRender(MvPipeline* pipe) {
        if (!renderEnc || !pipe || !descriptorsDirty) return;

        // Walk through each bound descriptor set.
        uint32_t dynIdx = 0;  // index into dynamicOffsets[]
        uint32_t setEnd = descFirstSet + boundDescSetCount;
        for (uint32_t s = descFirstSet; s < setEnd && s < kMaxDescriptorSets; ++s) {
            auto* ds = reinterpret_cast<MvDescriptorSet*>(boundDescSets[s]);
            if (!ds) continue;

            for (const auto& b : ds->bindings) {
                switch (b.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                        auto* buf = reinterpret_cast<MvBuffer*>(b.resource);
                        if (!buf || !buf->mtlBuffer) break;

                        // Find Metal slot from reflection.
                        uint32_t metalSlot = UINT32_MAX;
                        for (const auto& rb : pipe->vertexReflection.buffers) {
                            if (rb.set == s && rb.binding == b.binding) {
                                metalSlot = rb.metalSlot; break;
                            }
                        }
                        // Also check fragment reflection
                        uint32_t fragSlot = UINT32_MAX;
                        for (const auto& rb : pipe->fragmentReflection.buffers) {
                            if (rb.set == s && rb.binding == b.binding) {
                                fragSlot = rb.metalSlot; break;
                            }
                        }

                        uint64_t off = b.offset + buf->mtlBufferOffset;
                        // Apply dynamic offset for *_DYNAMIC types.
                        if ((b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                             b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
                            dynIdx < dynamicOffsetCount) {
                            off += dynamicOffsets[dynIdx++];
                        }

                        id<MTLBuffer> mtl = (__bridge id<MTLBuffer>)buf->mtlBuffer;
                        if (metalSlot != UINT32_MAX)
                            [renderEnc setVertexBuffer:mtl offset:off atIndex:metalSlot];
                        if (fragSlot != UINT32_MAX)
                            [renderEnc setFragmentBuffer:mtl offset:off atIndex:fragSlot];
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        auto* iv = reinterpret_cast<MvImageView*>(b.resource);
                        auto* sp = reinterpret_cast<MvSampler*>(b.samplerResource);

                        // Find texture + sampler slots from vertex/fragment reflection.
                        for (const auto* refl : {&pipe->vertexReflection, &pipe->fragmentReflection}) {
                            for (const auto& tb : refl->textures) {
                                if (tb.set == s && tb.binding == b.binding) {
                                    if (iv && iv->mtlTexture) {
                                        id<MTLTexture> tex = (__bridge id<MTLTexture>)iv->mtlTexture;
                                        if (refl == &pipe->vertexReflection)
                                            [renderEnc setVertexTexture:tex atIndex:tb.metalTextureSlot];
                                        else
                                            [renderEnc setFragmentTexture:tex atIndex:tb.metalTextureSlot];
                                    }
                                    if (sp && sp->mtlSamplerState && tb.metalSamplerSlot != UINT32_MAX) {
                                        id<MTLSamplerState> sam = (__bridge id<MTLSamplerState>)sp->mtlSamplerState;
                                        if (refl == &pipe->vertexReflection)
                                            [renderEnc setVertexSamplerState:sam atIndex:tb.metalSamplerSlot];
                                        else
                                            [renderEnc setFragmentSamplerState:sam atIndex:tb.metalSamplerSlot];
                                    }
                                    break;
                                }
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                        auto* iv = reinterpret_cast<MvImageView*>(b.resource);
                        if (!iv || !iv->mtlTexture) break;
                        id<MTLTexture> tex = (__bridge id<MTLTexture>)iv->mtlTexture;

                        for (const auto* refl : {&pipe->vertexReflection, &pipe->fragmentReflection}) {
                            for (const auto& tb : refl->textures) {
                                if (tb.set == s && tb.binding == b.binding) {
                                    if (refl == &pipe->vertexReflection)
                                        [renderEnc setVertexTexture:tex atIndex:tb.metalTextureSlot];
                                    else
                                        [renderEnc setFragmentTexture:tex atIndex:tb.metalTextureSlot];
                                    break;
                                }
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        auto* sp = reinterpret_cast<MvSampler*>(b.samplerResource);
                        if (!sp || !sp->mtlSamplerState) break;
                        id<MTLSamplerState> sam = (__bridge id<MTLSamplerState>)sp->mtlSamplerState;

                        for (const auto* refl : {&pipe->vertexReflection, &pipe->fragmentReflection}) {
                            for (const auto& tb : refl->textures) {
                                if (tb.set == s && tb.binding == b.binding &&
                                    tb.metalSamplerSlot != UINT32_MAX) {
                                    if (refl == &pipe->vertexReflection)
                                        [renderEnc setVertexSamplerState:sam atIndex:tb.metalSamplerSlot];
                                    else
                                        [renderEnc setFragmentSamplerState:sam atIndex:tb.metalSamplerSlot];
                                    break;
                                }
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                        auto* bv = reinterpret_cast<MvBufferView*>(b.resource);
                        if (!bv || !bv->mtlTexture) break;
                        id<MTLTexture> tex = (__bridge id<MTLTexture>)bv->mtlTexture;

                        for (const auto* refl : {&pipe->vertexReflection, &pipe->fragmentReflection}) {
                            for (const auto& tb : refl->textures) {
                                if (tb.set == s && tb.binding == b.binding) {
                                    if (refl == &pipe->vertexReflection)
                                        [renderEnc setVertexTexture:tex atIndex:tb.metalTextureSlot];
                                    else
                                        [renderEnc setFragmentTexture:tex atIndex:tb.metalTextureSlot];
                                    break;
                                }
                            }
                        }
                        break;
                    }

                    default: break;
                }
            }
        }
        descriptorsDirty = false;
    }

    // ── Flush descriptor bindings to a compute encoder ───────────────────
    void flushDescriptorsCompute(MvPipeline* pipe) {
        if (!computeEnc || !pipe || !descriptorsDirty) return;

        uint32_t dynIdx = 0;
        uint32_t setEnd = descFirstSet + boundDescSetCount;
        for (uint32_t s = descFirstSet; s < setEnd && s < kMaxDescriptorSets; ++s) {
            auto* ds = reinterpret_cast<MvDescriptorSet*>(boundDescSets[s]);
            if (!ds) continue;

            for (const auto& b : ds->bindings) {
                switch (b.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                        auto* buf = reinterpret_cast<MvBuffer*>(b.resource);
                        if (!buf || !buf->mtlBuffer) break;

                        uint32_t metalSlot = UINT32_MAX;
                        for (const auto& rb : pipe->computeReflection.buffers) {
                            if (rb.set == s && rb.binding == b.binding) {
                                metalSlot = rb.metalSlot; break;
                            }
                        }

                        uint64_t off = b.offset + buf->mtlBufferOffset;
                        if ((b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                             b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
                            dynIdx < dynamicOffsetCount) {
                            off += dynamicOffsets[dynIdx++];
                        }

                        if (metalSlot != UINT32_MAX) {
                            [computeEnc setBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                                           offset:off atIndex:metalSlot];
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        auto* iv = reinterpret_cast<MvImageView*>(b.resource);
                        auto* sp = reinterpret_cast<MvSampler*>(b.samplerResource);

                        for (const auto& tb : pipe->computeReflection.textures) {
                            if (tb.set == s && tb.binding == b.binding) {
                                if (iv && iv->mtlTexture)
                                    [computeEnc setTexture:(__bridge id<MTLTexture>)iv->mtlTexture
                                                   atIndex:tb.metalTextureSlot];
                                if (sp && sp->mtlSamplerState && tb.metalSamplerSlot != UINT32_MAX)
                                    [computeEnc setSamplerState:(__bridge id<MTLSamplerState>)sp->mtlSamplerState
                                                        atIndex:tb.metalSamplerSlot];
                                break;
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                        auto* iv = reinterpret_cast<MvImageView*>(b.resource);
                        if (!iv || !iv->mtlTexture) break;
                        for (const auto& tb : pipe->computeReflection.textures) {
                            if (tb.set == s && tb.binding == b.binding) {
                                [computeEnc setTexture:(__bridge id<MTLTexture>)iv->mtlTexture
                                               atIndex:tb.metalTextureSlot];
                                break;
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        auto* sp = reinterpret_cast<MvSampler*>(b.samplerResource);
                        if (!sp || !sp->mtlSamplerState) break;
                        for (const auto& tb : pipe->computeReflection.textures) {
                            if (tb.set == s && tb.binding == b.binding &&
                                tb.metalSamplerSlot != UINT32_MAX) {
                                [computeEnc setSamplerState:(__bridge id<MTLSamplerState>)sp->mtlSamplerState
                                                    atIndex:tb.metalSamplerSlot];
                                break;
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                        auto* bv = reinterpret_cast<MvBufferView*>(b.resource);
                        if (!bv || !bv->mtlTexture) break;
                        for (const auto& tb : pipe->computeReflection.textures) {
                            if (tb.set == s && tb.binding == b.binding) {
                                [computeEnc setTexture:(__bridge id<MTLTexture>)bv->mtlTexture
                                               atIndex:tb.metalTextureSlot];
                                break;
                            }
                        }
                        break;
                    }

                    default: break;
                }
            }
        }
        descriptorsDirty = false;
    }

    // ── Flush dirty render state before a draw call ─────────────────────
    #endif  // Disabled superseded descriptor binding implementation.

    void flushDescriptorsRender(MvPipeline* pipe) {
        if (!graphicsDescriptorsDirty) return;
        if (!renderEnc) {
            MVRVB_LOG_WARN(
                "Replay flushDescriptorsRender skipped: dirty descriptors but render encoder unavailable");
            return;
        }
        if (!pipe) {
            MVRVB_LOG_WARN(
                "Replay flushDescriptorsRender skipped: dirty descriptors but no bound graphics pipeline");
            return;
        }

        const auto* descriptorSets = descriptorSetsForBindPoint(VK_PIPELINE_BIND_POINT_GRAPHICS);
        uint32_t boundSetCount = 0;
        uint32_t visitedDescriptorCount = 0;
        uint32_t dynamicOffsetApplyCount = 0;

        for (uint32_t setIndex = 0; setIndex < kMaxDescriptorSets; ++setIndex) {
            const auto& setState = descriptorSets[setIndex];
            auto* ds = toMv(setState.set);
            if (!ds) continue;
            ++boundSetCount;

            uint32_t dynamicOffsetIndex = 0;
            for (const auto& descriptor : ds->bindings) {
                ++visitedDescriptorCount;
                switch (descriptor.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                        auto* buf = reinterpret_cast<MvBuffer*>(descriptor.resource);
                        if (!buf || !buf->mtlBuffer) break;

                        const auto* vertexBinding =
                            findBufferBinding(pipe->vertexReflection, setIndex, descriptor);
                        const auto* fragmentBinding =
                            findBufferBinding(pipe->fragmentReflection, setIndex, descriptor);

                        uint64_t offset = descriptor.offset + buf->mtlBufferOffset;
                        if (isDynamicDescriptorType(descriptor.descriptorType) &&
                            dynamicOffsetIndex < setState.dynamicOffsetCount) {
                            offset += setState.dynamicOffsets[dynamicOffsetIndex++];
                            ++dynamicOffsetApplyCount;
                        }

                        id<MTLBuffer> buffer = (__bridge id<MTLBuffer>)buf->mtlBuffer;
                        if (vertexBinding) {
                            const uint32_t slot = slotForArrayElement(
                                vertexBinding->metalSlot,
                                descriptor.arrayIndex,
                                msl::kMaxBufferSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setVertexBuffer:buffer offset:offset atIndex:slot];
                            }
                        }
                        if (fragmentBinding) {
                            const uint32_t slot = slotForArrayElement(
                                fragmentBinding->metalSlot,
                                descriptor.arrayIndex,
                                msl::kMaxBufferSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setFragmentBuffer:buffer offset:offset atIndex:slot];
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        auto* iv = reinterpret_cast<MvImageView*>(descriptor.resource);
                        auto* sp = reinterpret_cast<MvSampler*>(descriptor.samplerResource);
                        const auto* vertexTexture =
                            findTextureBinding(pipe->vertexReflection, setIndex, descriptor);
                        const auto* fragmentTexture =
                            findTextureBinding(pipe->fragmentReflection, setIndex, descriptor);
                        const auto* vertexSampler =
                            findSamplerBinding(pipe->vertexReflection, setIndex, descriptor);
                        const auto* fragmentSampler =
                            findSamplerBinding(pipe->fragmentReflection, setIndex, descriptor);

                        if (iv && iv->mtlTexture) {
                            id<MTLTexture> texture = (__bridge id<MTLTexture>)iv->mtlTexture;
                            if (vertexTexture) {
                                const uint32_t slot = slotForArrayElement(
                                    vertexTexture->metalTextureSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxTextureSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setVertexTexture:texture atIndex:slot];
                                }
                            }
                            if (fragmentTexture) {
                                const uint32_t slot = slotForArrayElement(
                                    fragmentTexture->metalTextureSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxTextureSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setFragmentTexture:texture atIndex:slot];
                                }
                            }
                        }

                        if (sp && sp->mtlSamplerState) {
                            id<MTLSamplerState> sampler =
                                (__bridge id<MTLSamplerState>)sp->mtlSamplerState;

                            if (vertexTexture && vertexTexture->metalSamplerSlot != UINT32_MAX) {
                                const uint32_t slot = slotForArrayElement(
                                    vertexTexture->metalSamplerSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxSamplerSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setVertexSamplerState:sampler atIndex:slot];
                                }
                            } else if (vertexSampler) {
                                const uint32_t slot = slotForArrayElement(
                                    vertexSampler->metalSamplerSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxSamplerSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setVertexSamplerState:sampler atIndex:slot];
                                }
                            }

                            if (fragmentTexture && fragmentTexture->metalSamplerSlot != UINT32_MAX) {
                                const uint32_t slot = slotForArrayElement(
                                    fragmentTexture->metalSamplerSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxSamplerSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setFragmentSamplerState:sampler atIndex:slot];
                                }
                            } else if (fragmentSampler) {
                                const uint32_t slot = slotForArrayElement(
                                    fragmentSampler->metalSamplerSlot,
                                    descriptor.arrayIndex,
                                    msl::kMaxSamplerSlots);
                                if (slot != UINT32_MAX) {
                                    [renderEnc setFragmentSamplerState:sampler atIndex:slot];
                                }
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                        auto* iv = reinterpret_cast<MvImageView*>(descriptor.resource);
                        if (!iv || !iv->mtlTexture) break;

                        id<MTLTexture> texture = (__bridge id<MTLTexture>)iv->mtlTexture;
                        if (const auto* vertexBinding =
                                findTextureBinding(pipe->vertexReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                vertexBinding->metalTextureSlot,
                                descriptor.arrayIndex,
                                msl::kMaxTextureSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setVertexTexture:texture atIndex:slot];
                            }
                        }
                        if (const auto* fragmentBinding =
                                findTextureBinding(pipe->fragmentReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                fragmentBinding->metalTextureSlot,
                                descriptor.arrayIndex,
                                msl::kMaxTextureSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setFragmentTexture:texture atIndex:slot];
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        auto* sp = reinterpret_cast<MvSampler*>(descriptor.samplerResource);
                        if (!sp || !sp->mtlSamplerState) break;

                        id<MTLSamplerState> sampler =
                            (__bridge id<MTLSamplerState>)sp->mtlSamplerState;
                        if (const auto* vertexBinding =
                                findSamplerBinding(pipe->vertexReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                vertexBinding->metalSamplerSlot,
                                descriptor.arrayIndex,
                                msl::kMaxSamplerSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setVertexSamplerState:sampler atIndex:slot];
                            }
                        }
                        if (const auto* fragmentBinding =
                                findSamplerBinding(pipe->fragmentReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                fragmentBinding->metalSamplerSlot,
                                descriptor.arrayIndex,
                                msl::kMaxSamplerSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setFragmentSamplerState:sampler atIndex:slot];
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                        auto* bv = reinterpret_cast<MvBufferView*>(descriptor.resource);
                        if (!bv || !bv->mtlTexture) break;

                        id<MTLTexture> texture = (__bridge id<MTLTexture>)bv->mtlTexture;
                        if (const auto* vertexBinding =
                                findTextureBinding(pipe->vertexReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                vertexBinding->metalTextureSlot,
                                descriptor.arrayIndex,
                                msl::kMaxTextureSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setVertexTexture:texture atIndex:slot];
                            }
                        }
                        if (const auto* fragmentBinding =
                                findTextureBinding(pipe->fragmentReflection, setIndex, descriptor)) {
                            const uint32_t slot = slotForArrayElement(
                                fragmentBinding->metalTextureSlot,
                                descriptor.arrayIndex,
                                msl::kMaxTextureSlots);
                            if (slot != UINT32_MAX) {
                                [renderEnc setFragmentTexture:texture atIndex:slot];
                            }
                        }
                        break;
                    }

                    default:
                        break;
                }
            }
        }

        MVRVB_LOG_DEBUG("Replay flushDescriptorsRender: sets=%u descriptors=%u dynamicOffsets=%u",
                        boundSetCount,
                        visitedDescriptorCount,
                        dynamicOffsetApplyCount);
        graphicsDescriptorsDirty = false;
    }

    void flushDescriptorsCompute(MvPipeline* pipe) {
        if (!computeDescriptorsDirty) return;
        if (!computeEnc) {
            MVRVB_LOG_WARN(
                "Replay flushDescriptorsCompute skipped: dirty descriptors but compute encoder unavailable");
            return;
        }
        if (!pipe) {
            MVRVB_LOG_WARN(
                "Replay flushDescriptorsCompute skipped: dirty descriptors but no bound compute pipeline");
            return;
        }

        const auto* descriptorSets = descriptorSetsForBindPoint(VK_PIPELINE_BIND_POINT_COMPUTE);
        uint32_t boundSetCount = 0;
        uint32_t visitedDescriptorCount = 0;
        uint32_t dynamicOffsetApplyCount = 0;

        for (uint32_t setIndex = 0; setIndex < kMaxDescriptorSets; ++setIndex) {
            const auto& setState = descriptorSets[setIndex];
            auto* ds = toMv(setState.set);
            if (!ds) continue;
            ++boundSetCount;

            uint32_t dynamicOffsetIndex = 0;
            for (const auto& descriptor : ds->bindings) {
                ++visitedDescriptorCount;
                switch (descriptor.descriptorType) {
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
                        auto* buf = reinterpret_cast<MvBuffer*>(descriptor.resource);
                        if (!buf || !buf->mtlBuffer) break;

                        const auto* bufferBinding =
                            findBufferBinding(pipe->computeReflection, setIndex, descriptor);
                        if (!bufferBinding) break;

                        uint64_t offset = descriptor.offset + buf->mtlBufferOffset;
                        if (isDynamicDescriptorType(descriptor.descriptorType) &&
                            dynamicOffsetIndex < setState.dynamicOffsetCount) {
                            offset += setState.dynamicOffsets[dynamicOffsetIndex++];
                            ++dynamicOffsetApplyCount;
                        }

                        const uint32_t slot = slotForArrayElement(
                            bufferBinding->metalSlot,
                            descriptor.arrayIndex,
                            msl::kMaxBufferSlots);
                        if (slot != UINT32_MAX) {
                            [computeEnc setBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                                           offset:offset
                                          atIndex:slot];
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                        auto* iv = reinterpret_cast<MvImageView*>(descriptor.resource);
                        auto* sp = reinterpret_cast<MvSampler*>(descriptor.samplerResource);
                        const auto* textureBinding =
                            findTextureBinding(pipe->computeReflection, setIndex, descriptor);
                        const auto* samplerBinding =
                            findSamplerBinding(pipe->computeReflection, setIndex, descriptor);

                        if (iv && iv->mtlTexture && textureBinding) {
                            const uint32_t textureSlot = slotForArrayElement(
                                textureBinding->metalTextureSlot,
                                descriptor.arrayIndex,
                                msl::kMaxTextureSlots);
                            if (textureSlot != UINT32_MAX) {
                                [computeEnc setTexture:(__bridge id<MTLTexture>)iv->mtlTexture
                                               atIndex:textureSlot];
                            }
                        }

                        if (sp && sp->mtlSamplerState) {
                            uint32_t baseSamplerSlot = UINT32_MAX;
                            if (textureBinding && textureBinding->metalSamplerSlot != UINT32_MAX) {
                                baseSamplerSlot = textureBinding->metalSamplerSlot;
                            } else if (samplerBinding) {
                                baseSamplerSlot = samplerBinding->metalSamplerSlot;
                            }

                            const uint32_t samplerSlot = slotForArrayElement(
                                baseSamplerSlot,
                                descriptor.arrayIndex,
                                msl::kMaxSamplerSlots);
                            if (samplerSlot != UINT32_MAX) {
                                [computeEnc setSamplerState:
                                    (__bridge id<MTLSamplerState>)sp->mtlSamplerState
                                                 atIndex:samplerSlot];
                            }
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
                        auto* iv = reinterpret_cast<MvImageView*>(descriptor.resource);
                        const auto* textureBinding =
                            findTextureBinding(pipe->computeReflection, setIndex, descriptor);
                        if (!iv || !iv->mtlTexture || !textureBinding) break;

                        const uint32_t slot = slotForArrayElement(
                            textureBinding->metalTextureSlot,
                            descriptor.arrayIndex,
                            msl::kMaxTextureSlots);
                        if (slot != UINT32_MAX) {
                            [computeEnc setTexture:(__bridge id<MTLTexture>)iv->mtlTexture
                                           atIndex:slot];
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        auto* sp = reinterpret_cast<MvSampler*>(descriptor.samplerResource);
                        const auto* samplerBinding =
                            findSamplerBinding(pipe->computeReflection, setIndex, descriptor);
                        if (!sp || !sp->mtlSamplerState || !samplerBinding) break;

                        const uint32_t slot = slotForArrayElement(
                            samplerBinding->metalSamplerSlot,
                            descriptor.arrayIndex,
                            msl::kMaxSamplerSlots);
                        if (slot != UINT32_MAX) {
                            [computeEnc setSamplerState:
                                (__bridge id<MTLSamplerState>)sp->mtlSamplerState
                                             atIndex:slot];
                        }
                        break;
                    }

                    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
                        auto* bv = reinterpret_cast<MvBufferView*>(descriptor.resource);
                        const auto* textureBinding =
                            findTextureBinding(pipe->computeReflection, setIndex, descriptor);
                        if (!bv || !bv->mtlTexture || !textureBinding) break;

                        const uint32_t slot = slotForArrayElement(
                            textureBinding->metalTextureSlot,
                            descriptor.arrayIndex,
                            msl::kMaxTextureSlots);
                        if (slot != UINT32_MAX) {
                            [computeEnc setTexture:(__bridge id<MTLTexture>)bv->mtlTexture
                                           atIndex:slot];
                        }
                        break;
                    }

                    default:
                        break;
                }
            }
        }

        MVRVB_LOG_DEBUG("Replay flushDescriptorsCompute: sets=%u descriptors=%u dynamicOffsets=%u",
                        boundSetCount,
                        visitedDescriptorCount,
                        dynamicOffsetApplyCount);
        computeDescriptorsDirty = false;
    }

    void flushRenderState() {
        if (!renderEnc) return;
        const bool hadGraphicsPipelineDirty = graphicsPipelineDirty;
        const bool hadGraphicsDescriptorsDirty = graphicsDescriptorsDirty;
        const bool hadViewportDirty = viewportDirty;
        const bool hadScissorDirty = scissorDirty;
        const bool hadDepthBiasDirty = depthBiasDirty;
        const bool hadBlendConstDirty = blendConstDirty;
        const bool hadStencilRefDirty = stencilRefDirty;
        const bool hadPushConstDirty = pushConstDirty && pushConstSize > 0;
        const uint32_t dirtyVertexBindingCount = countDirtyVertexBindings(vertexBindingsDirty);

        if (boundGraphicsPipeline) {
            auto* p = boundGraphicsPipeline;
            if (hadGraphicsPipelineDirty && !p->renderPipelineState) {
                MVRVB_LOG_WARN(
                    "Replay flushRenderState: bound graphics pipeline is missing MTLRenderPipelineState");
            }
            if (p->renderPipelineState)
                [renderEnc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)p->renderPipelineState];
            if (p->depthStencilState)
                [renderEnc setDepthStencilState:(__bridge id<MTLDepthStencilState>)p->depthStencilState];
            [renderEnc setCullMode:(MTLCullMode)p->cullMode];
            [renderEnc setFrontFacingWinding:p->frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE
                                           ? MTLWindingCounterClockwise : MTLWindingClockwise];
            [renderEnc setTriangleFillMode:(MTLTriangleFillMode)p->fillMode];
            if (p->depthBiasEnable && !p->hasDynamicDepthBias) {
                [renderEnc setDepthBias:p->depthBiasConst
                             slopeScale:p->depthBiasSlope
                                  clamp:p->depthBiasClamp];
            }
        } else if (hadGraphicsPipelineDirty || hadGraphicsDescriptorsDirty ||
                   hadViewportDirty || hadScissorDirty || hadDepthBiasDirty ||
                   hadBlendConstDirty || hadStencilRefDirty || hadPushConstDirty ||
                   dirtyVertexBindingCount > 0) {
            MVRVB_LOG_WARN(
                "Replay flushRenderState: state flush requested without a bound graphics pipeline");
        }

        flushDescriptorsRender(boundGraphicsPipeline);

        if (viewportDirty) {
            MTLViewport vp;
            vp.originX = viewport.x;
            vp.originY = viewport.y;
            vp.width   = viewport.width;
            vp.height  = viewport.height;
            vp.znear   = viewport.minDepth;
            vp.zfar    = viewport.maxDepth;
            [renderEnc setViewport:vp];
            viewportDirty = false;
        }

        if (scissorDirty) {
            MTLScissorRect sr;
            sr.x      = std::max(0, scissor.offset.x);
            sr.y      = std::max(0, scissor.offset.y);
            sr.width  = scissor.extent.width;
            sr.height = scissor.extent.height;
            [renderEnc setScissorRect:sr];
            scissorDirty = false;
        }

        if (depthBiasDirty) {
            [renderEnc setDepthBias:depthBiasConstant slopeScale:depthBiasSlope clamp:depthBiasClamp];
            depthBiasDirty = false;
        }

        if (blendConstDirty) {
            [renderEnc setBlendColorRed:blendConstants[0] green:blendConstants[1]
                                   blue:blendConstants[2] alpha:blendConstants[3]];
            blendConstDirty = false;
        }

        if (stencilRefDirty) {
            [renderEnc setStencilFrontReferenceValue:stencilRefFront backReferenceValue:stencilRefBack];
            stencilRefDirty = false;
        }

        if (pushConstDirty && pushConstSize > 0) {
            [renderEnc setVertexBytes:pushConstData
                               length:pushConstSize
                              atIndex:msl::kPushConstantBufferSlot];
            [renderEnc setFragmentBytes:pushConstData
                                 length:pushConstSize
                                atIndex:msl::kPushConstantBufferSlot];
            pushConstDirty = false;
        }

        // Flush vertex buffer bindings
        if (vertexBindingsDirty) {
            for (uint32_t i = 0; i < kMaxVertexBindings; ++i) {
                if (!(vertexBindingsDirty & (1u << i))) continue;
                auto* buf = reinterpret_cast<MvBuffer*>(vertexBuffers[i]);
                if (buf && buf->mtlBuffer) {
                    // Vertex buffers bind above descriptor-backed buffers.
                    [renderEnc setVertexBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                                        offset:vertexOffsets[i]
                                       atIndex:kVertexBufferBaseSlot - i];
                }
            }
            vertexBindingsDirty = 0;
        }

        if (hadGraphicsPipelineDirty || hadGraphicsDescriptorsDirty ||
            hadViewportDirty || hadScissorDirty || hadDepthBiasDirty ||
            hadBlendConstDirty || hadStencilRefDirty || hadPushConstDirty ||
            dirtyVertexBindingCount > 0) {
            MVRVB_LOG_DEBUG(
                "Replay flushRenderState: pipelineDirty=%s descriptorsDirty=%s viewport=%s scissor=%s depthBias=%s blend=%s stencilRef=%s pushConstBytes=%u vertexBindings=%u",
                hadGraphicsPipelineDirty ? "yes" : "no",
                hadGraphicsDescriptorsDirty ? "yes" : "no",
                hadViewportDirty ? "yes" : "no",
                hadScissorDirty ? "yes" : "no",
                hadDepthBiasDirty ? "yes" : "no",
                hadBlendConstDirty ? "yes" : "no",
                hadStencilRefDirty ? "yes" : "no",
                hadPushConstDirty ? pushConstSize : 0u,
                dirtyVertexBindingCount);
        }
        graphicsPipelineDirty = false;
    }

    // ── Flush compute state before a dispatch ───────────────────────────
    void flushComputeState() {
        if (!computeEnc) return;
        const bool hadComputePipelineDirty = computePipelineDirty;
        const bool hadComputeDescriptorsDirty = computeDescriptorsDirty;
        const bool hadPushConstDirty = pushConstDirty && pushConstSize > 0;
        if (boundComputePipeline && boundComputePipeline->computePipelineState) {
            [computeEnc setComputePipelineState:
                (__bridge id<MTLComputePipelineState>)boundComputePipeline->computePipelineState];
        } else if (boundComputePipeline && hadComputePipelineDirty) {
            MVRVB_LOG_WARN(
                "Replay flushComputeState: bound compute pipeline is missing MTLComputePipelineState");
        } else if (!boundComputePipeline &&
                   (hadComputePipelineDirty || hadComputeDescriptorsDirty || hadPushConstDirty)) {
            MVRVB_LOG_WARN(
                "Replay flushComputeState: state flush requested without a bound compute pipeline");
        }
        flushDescriptorsCompute(boundComputePipeline);
        if (pushConstDirty && pushConstSize > 0) {
            [computeEnc setBytes:pushConstData
                          length:pushConstSize
                         atIndex:msl::kPushConstantBufferSlot];
            pushConstDirty = false;
        }
        if (hadComputePipelineDirty || hadComputeDescriptorsDirty || hadPushConstDirty) {
            MVRVB_LOG_DEBUG(
                "Replay flushComputeState: pipelineDirty=%s descriptorsDirty=%s pushConstBytes=%u",
                hadComputePipelineDirty ? "yes" : "no",
                hadComputeDescriptorsDirty ? "yes" : "no",
                hadPushConstDirty ? pushConstSize : 0u);
        }
        computePipelineDirty = false;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Replay: build MTLRenderPassDescriptor from BeginRenderPass data
// ═══════════════════════════════════════════════════════════════════════════════

static MTLRenderPassDescriptor* buildRenderPassDescFromLegacy(
        const DeferredCmd& cmd) {
    const auto& data = cmd.beginRenderPass;
    auto* rp = toMv(data.renderPass);
    auto* fb = toMv(data.framebuffer);
    if (!rp || !fb) return nil;

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor new];
    uint32_t colorIdx = 0;

    for (uint32_t i = 0; i < rp->attachments.size() && i < fb->imageViews.size(); ++i) {
        const auto& att = rp->attachments[i];
        MvImageView* iv = toMv(fb->imageViews[i]);
        id<MTLTexture> tex = iv ? (__bridge id<MTLTexture>)iv->mtlTexture : nil;

        if (isDepthFormat(att.format)) {
            desc.depthAttachment.texture     = tex;
            desc.depthAttachment.loadAction  = toMTLLoad(att.loadOp);
            desc.depthAttachment.storeAction = toMTLStore(att.storeOp);
            if (att.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && data.clearValueCount > i) {
                desc.depthAttachment.clearDepth = data.clearValues[i].depthStencil.depth;
            }
            if (hasStencilComponent(att.format)) {
                desc.stencilAttachment.texture     = tex;
                desc.stencilAttachment.loadAction  = toMTLLoad(att.stencilLoadOp);
                desc.stencilAttachment.storeAction = toMTLStore(att.stencilStoreOp);
                if (att.stencilLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && data.clearValueCount > i) {
                    desc.stencilAttachment.clearStencil = data.clearValues[i].depthStencil.stencil;
                }
            }
        } else {
            desc.colorAttachments[colorIdx].texture     = tex;
            desc.colorAttachments[colorIdx].loadAction  = toMTLLoad(att.loadOp);
            desc.colorAttachments[colorIdx].storeAction = toMTLStore(att.storeOp);
            if (att.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && data.clearValueCount > i) {
                const auto& cv = data.clearValues[i].color.float32;
                desc.colorAttachments[colorIdx].clearColor =
                    MTLClearColorMake(cv[0], cv[1], cv[2], cv[3]);
            }
            ++colorIdx;
        }
    }
    return desc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Replay: build MTLRenderPassDescriptor from BeginRendering (dynamic rendering)
// ═══════════════════════════════════════════════════════════════════════════════

static MTLRenderPassDescriptor* buildRenderPassDescFromDynamic(
        const DeferredCmd& cmd) {
    const auto& data = cmd.beginRendering;
    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor new];

    for (uint32_t i = 0; i < data.colorAttachmentCount && i < kMaxColorAttachments; ++i) {
        const auto& ca = data.colorAttachments[i];
        MvImageView* iv = toMv(ca.imageView);
        id<MTLTexture> tex = iv ? (__bridge id<MTLTexture>)iv->mtlTexture : nil;
        desc.colorAttachments[i].texture     = tex;
        desc.colorAttachments[i].loadAction  = toMTLLoad(ca.loadOp);
        desc.colorAttachments[i].storeAction = toMTLStore(ca.storeOp);
        if (ca.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            const auto& cv = ca.clearValue.color.float32;
            desc.colorAttachments[i].clearColor = MTLClearColorMake(cv[0], cv[1], cv[2], cv[3]);
        }
        if (ca.resolveMode != VK_RESOLVE_MODE_NONE && ca.resolveImageView != VK_NULL_HANDLE) {
            MvImageView* riv = toMv(ca.resolveImageView);
            desc.colorAttachments[i].resolveTexture = riv ? (__bridge id<MTLTexture>)riv->mtlTexture : nil;
            desc.colorAttachments[i].storeAction = MTLStoreActionMultisampleResolve;
        }
    }

    if (data.hasDepth) {
        MvImageView* iv = toMv(data.depthAttachment.imageView);
        id<MTLTexture> tex = iv ? (__bridge id<MTLTexture>)iv->mtlTexture : nil;
        desc.depthAttachment.texture     = tex;
        desc.depthAttachment.loadAction  = toMTLLoad(data.depthAttachment.loadOp);
        desc.depthAttachment.storeAction = toMTLStore(data.depthAttachment.storeOp);
        if (data.depthAttachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            desc.depthAttachment.clearDepth = data.depthAttachment.clearValue.depthStencil.depth;
        }
    }

    if (data.hasStencil) {
        MvImageView* iv = toMv(data.stencilAttachment.imageView);
        id<MTLTexture> tex = iv ? (__bridge id<MTLTexture>)iv->mtlTexture : nil;
        desc.stencilAttachment.texture     = tex;
        desc.stencilAttachment.loadAction  = toMTLLoad(data.stencilAttachment.loadOp);
        desc.stencilAttachment.storeAction = toMTLStore(data.stencilAttachment.storeOp);
        if (data.stencilAttachment.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            desc.stencilAttachment.clearStencil = data.stencilAttachment.clearValue.depthStencil.stencil;
        }
    }

    return desc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Replay: execute one deferred command against Metal encoders
// ═══════════════════════════════════════════════════════════════════════════════

static void replayCommandList(const MvCommandBuffer* cb, ReplayState& rs);

static void replayCommand(const DeferredCmd& cmd, ReplayState& rs) {
    switch (cmd.tag) {

    // ── Render pass (legacy) ────────────────────────────────────────────
    case CmdTag::BeginRenderPass: {
        MVRVB_LOG_DEBUG("Replay BeginRenderPass: renderArea=%ux%u clearValues=%u",
                        cmd.beginRenderPass.renderArea.extent.width,
                        cmd.beginRenderPass.renderArea.extent.height,
                        cmd.beginRenderPass.clearValueCount);
        rs.endActiveEncoder();
        @autoreleasepool {
            MTLRenderPassDescriptor* desc = buildRenderPassDescFromLegacy(cmd);
            if (desc) {
                rs.renderEnc = [rs.mtlCB renderCommandEncoderWithDescriptor:desc];
                rs.activeEncoder = EncoderType::Render;
                rs.markRenderEncoderStateDirty();
            } else {
                MVRVB_LOG_WARN("Replay BeginRenderPass skipped: descriptor build failed");
            }
        }
        break;
    }
    case CmdTag::EndRenderPass: {
        MVRVB_LOG_DEBUG("Replay EndRenderPass");
        rs.endActiveEncoder();
        break;
    }
    case CmdTag::NextSubpass: {
        // Metal has no subpass concept; end current encoder.
        // Multi-subpass would need separate render passes.
        // For now, treat as no-op (single subpass covers DXVK usage).
        break;
    }

    // ── Render pass (dynamic rendering) ─────────────────────────────────
    case CmdTag::BeginRendering: {
        MVRVB_LOG_DEBUG("Replay BeginRendering: renderArea=%ux%u colorAttachments=%u depth=%s stencil=%s",
                        cmd.beginRendering.renderArea.extent.width,
                        cmd.beginRendering.renderArea.extent.height,
                        cmd.beginRendering.colorAttachmentCount,
                        cmd.beginRendering.hasDepth ? "yes" : "no",
                        cmd.beginRendering.hasStencil ? "yes" : "no");
        rs.endActiveEncoder();
        @autoreleasepool {
            MTLRenderPassDescriptor* desc = buildRenderPassDescFromDynamic(cmd);
            if (desc) {
                rs.renderEnc = [rs.mtlCB renderCommandEncoderWithDescriptor:desc];
                rs.activeEncoder = EncoderType::Render;
                rs.markRenderEncoderStateDirty();
            } else {
                MVRVB_LOG_WARN("Replay BeginRendering skipped: descriptor build failed");
            }
        }
        break;
    }
    case CmdTag::EndRendering: {
        MVRVB_LOG_DEBUG("Replay EndRendering");
        rs.endActiveEncoder();
        break;
    }

    // ── Pipeline / state binding ────────────────────────────────────────
    case CmdTag::BindPipeline: {
        auto* pipe = reinterpret_cast<MvPipeline*>(cmd.bindPipeline.pipeline);
        MVRVB_LOG_DEBUG("Replay BindPipeline: bindPoint=%s pipeline=%p",
                        bindPointName(cmd.bindPipeline.bindPoint),
                        (void*)pipe);
        if (cmd.bindPipeline.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
            rs.boundComputePipeline = pipe;
            rs.computePipelineDirty = true;
        } else {
            rs.boundGraphicsPipeline = pipe;
            rs.graphicsPipelineDirty = true;
        }
        if (rs.hasAnyBoundDescriptorSets(cmd.bindPipeline.bindPoint)) {
            rs.descriptorsDirtyForBindPoint(cmd.bindPipeline.bindPoint) = true;
        }
        break;
    }
    case CmdTag::BindDescriptorSets: {
        const auto& d = cmd.bindDescriptorSets;
        auto* boundSets = rs.descriptorSetsForBindPoint(d.bindPoint);
        uint32_t dynamicOffsetIndex = 0;
        for (uint32_t i = 0; i < d.setCount && (d.firstSet + i) < kMaxDescriptorSets; ++i) {
            const uint32_t setIndex = d.firstSet + i;
            auto& boundSet = boundSets[setIndex];
            boundSet.set = d.sets[i];
            boundSet.dynamicOffsetCount = 0;

            auto* descriptorSet = toMv(d.sets[i]);
            const uint32_t expectedDynamicOffsets =
                ReplayState::countDynamicDescriptors(descriptorSet ? descriptorSet->layout : nullptr);
            const uint32_t availableDynamicOffsets = std::min(
                expectedDynamicOffsets,
                d.dynamicOffsetCount > dynamicOffsetIndex
                    ? d.dynamicOffsetCount - dynamicOffsetIndex
                    : 0u);
            boundSet.dynamicOffsetCount = std::min(availableDynamicOffsets, kMaxDynamicOffsets);

            for (uint32_t j = 0; j < boundSet.dynamicOffsetCount; ++j) {
                boundSet.dynamicOffsets[j] = d.dynamicOffsets[dynamicOffsetIndex + j];
            }
            dynamicOffsetIndex += availableDynamicOffsets;
        }
        MVRVB_LOG_DEBUG("Replay BindDescriptorSets: bindPoint=%s firstSet=%u setCount=%u dynamicOffsets=%u",
                        bindPointName(d.bindPoint),
                        d.firstSet,
                        d.setCount,
                        d.dynamicOffsetCount);
        rs.descriptorsDirtyForBindPoint(d.bindPoint) = true;
        break;
    }
    case CmdTag::BindVertexBuffers: {
        const auto& d = cmd.bindVertexBuffers;
        for (uint32_t i = 0; i < d.bindingCount && (d.firstBinding + i) < kMaxVertexBindings; ++i) {
            uint32_t slot = d.firstBinding + i;
            rs.vertexBuffers[slot] = d.buffers[i];
            rs.vertexOffsets[slot] = d.offsets[i];
            rs.vertexBindingsDirty |= (1u << slot);
        }
        break;
    }
    case CmdTag::BindIndexBuffer: {
        rs.indexBuffer = cmd.bindIndexBuffer.buffer;
        rs.indexOffset = cmd.bindIndexBuffer.offset;
        rs.indexType   = cmd.bindIndexBuffer.indexType;
        break;
    }
    case CmdTag::PushConstants: {
        const auto& d = cmd.pushConstants;
        uint32_t end = d.offset + d.size;
        if (end > kMaxPushConstantBytes) end = kMaxPushConstantBytes;
        std::memcpy(rs.pushConstData + d.offset, d.data, end - d.offset);
        rs.pushConstSize = std::max(rs.pushConstSize, end);
        rs.pushConstDirty = true;
        break;
    }

    // ── Dynamic state ───────────────────────────────────────────────────
    case CmdTag::SetViewport: {
        rs.viewport = cmd.setViewport.viewports[0];
        rs.viewportDirty = true;
        break;
    }
    case CmdTag::SetScissor: {
        rs.scissor = cmd.setScissor.scissors[0];
        rs.scissorDirty = true;
        break;
    }
    case CmdTag::SetDepthBias: {
        rs.depthBiasConstant = cmd.setDepthBias.constantFactor;
        rs.depthBiasClamp    = cmd.setDepthBias.clamp;
        rs.depthBiasSlope    = cmd.setDepthBias.slopeFactor;
        rs.depthBiasDirty    = true;
        break;
    }
    case CmdTag::SetBlendConstants: {
        std::memcpy(rs.blendConstants, cmd.setBlendConstants.constants, 4 * sizeof(float));
        rs.blendConstDirty = true;
        break;
    }
    case CmdTag::SetStencilCompareMask: {
        // Metal doesn't expose per-face compare mask at draw time.
        // Compare mask is baked into the depth-stencil state.
        break;
    }
    case CmdTag::SetStencilWriteMask: {
        // Same as compare mask — baked into depth-stencil state.
        break;
    }
    case CmdTag::SetStencilReference: {
        const auto& d = cmd.setStencilValue;
        if (d.faceMask & VK_STENCIL_FACE_FRONT_BIT) rs.stencilRefFront = d.value;
        if (d.faceMask & VK_STENCIL_FACE_BACK_BIT)  rs.stencilRefBack  = d.value;
        rs.stencilRefDirty = true;
        break;
    }
    case CmdTag::SetLineWidth: {
        // Metal line width is always 1.0; silently ignore.
        break;
    }
    case CmdTag::SetDepthBounds: {
        // Metal doesn't support depth bounds testing; no-op.
        break;
    }

    // ── Draw commands ───────────────────────────────────────────────────
    case CmdTag::Draw: {
        if (rs.activeEncoder != EncoderType::Render || !rs.renderEnc) {
            MVRVB_LOG_WARN("Replay Draw skipped: render encoder unavailable");
            break;
        }
        rs.flushRenderState();
        if (!rs.boundGraphicsPipeline || !rs.boundGraphicsPipeline->renderPipelineState) {
            MVRVB_LOG_WARN("Replay Draw proceeding without a valid graphics pipeline state");
        }
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        if (rs.boundGraphicsPipeline)
            prim = toMTLPrimitive((VkPrimitiveTopology)rs.boundGraphicsPipeline->topology);
        MVRVB_LOG_DEBUG("Replay Draw: vertices=%u instances=%u firstVertex=%u firstInstance=%u",
                        cmd.draw.vertexCount,
                        cmd.draw.instanceCount,
                        cmd.draw.firstVertex,
                        cmd.draw.firstInstance);
        [rs.renderEnc drawPrimitives:prim
                         vertexStart:cmd.draw.firstVertex
                         vertexCount:cmd.draw.vertexCount
                       instanceCount:cmd.draw.instanceCount
                        baseInstance:cmd.draw.firstInstance];
        break;
    }
    case CmdTag::DrawIndexed: {
        if (rs.activeEncoder != EncoderType::Render || !rs.renderEnc) {
            MVRVB_LOG_WARN("Replay DrawIndexed skipped: render encoder unavailable");
            break;
        }
        rs.flushRenderState();
        if (!rs.boundGraphicsPipeline || !rs.boundGraphicsPipeline->renderPipelineState) {
            MVRVB_LOG_WARN("Replay DrawIndexed proceeding without a valid graphics pipeline state");
        }
        auto* ib = reinterpret_cast<MvBuffer*>(rs.indexBuffer);
        if (!ib || !ib->mtlBuffer) {
            MVRVB_LOG_WARN("Replay DrawIndexed skipped: index buffer unavailable");
            break;
        }
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        if (rs.boundGraphicsPipeline)
            prim = toMTLPrimitive((VkPrimitiveTopology)rs.boundGraphicsPipeline->topology);
        MTLIndexType mtlIdxType = toMTLIndex(rs.indexType);
        uint32_t idxSize = (rs.indexType == VK_INDEX_TYPE_UINT32) ? 4 : 2;
        MVRVB_LOG_DEBUG("Replay DrawIndexed: indices=%u instances=%u firstIndex=%u vertexOffset=%d firstInstance=%u",
                        cmd.drawIndexed.indexCount,
                        cmd.drawIndexed.instanceCount,
                        cmd.drawIndexed.firstIndex,
                        cmd.drawIndexed.vertexOffset,
                        cmd.drawIndexed.firstInstance);
        [rs.renderEnc drawIndexedPrimitives:prim
                                 indexCount:cmd.drawIndexed.indexCount
                                  indexType:mtlIdxType
                                indexBuffer:(__bridge id<MTLBuffer>)ib->mtlBuffer
                          indexBufferOffset:rs.indexOffset + cmd.drawIndexed.firstIndex * idxSize
                              instanceCount:cmd.drawIndexed.instanceCount
                                 baseVertex:cmd.drawIndexed.vertexOffset
                               baseInstance:cmd.drawIndexed.firstInstance];
        break;
    }
    case CmdTag::DrawIndirect: {
        if (rs.activeEncoder != EncoderType::Render || !rs.renderEnc) break;
        rs.flushRenderState();
        auto* buf = reinterpret_cast<MvBuffer*>(cmd.drawIndirect.buffer);
        if (!buf || !buf->mtlBuffer) break;
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        if (rs.boundGraphicsPipeline)
            prim = toMTLPrimitive((VkPrimitiveTopology)rs.boundGraphicsPipeline->topology);
        for (uint32_t d = 0; d < cmd.drawIndirect.drawCount; ++d) {
            [rs.renderEnc drawPrimitives:prim
                          indirectBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                    indirectBufferOffset:cmd.drawIndirect.offset + d * cmd.drawIndirect.stride];
        }
        break;
    }
    case CmdTag::DrawIndexedIndirect: {
        if (rs.activeEncoder != EncoderType::Render || !rs.renderEnc) break;
        rs.flushRenderState();
        auto* buf = reinterpret_cast<MvBuffer*>(cmd.drawIndirect.buffer);
        auto* ib  = reinterpret_cast<MvBuffer*>(rs.indexBuffer);
        if (!buf || !buf->mtlBuffer || !ib || !ib->mtlBuffer) break;
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        if (rs.boundGraphicsPipeline)
            prim = toMTLPrimitive((VkPrimitiveTopology)rs.boundGraphicsPipeline->topology);
        MTLIndexType mtlIdxType = toMTLIndex(rs.indexType);
        for (uint32_t d = 0; d < cmd.drawIndirect.drawCount; ++d) {
            [rs.renderEnc drawIndexedPrimitives:prim
                                      indexType:mtlIdxType
                                    indexBuffer:(__bridge id<MTLBuffer>)ib->mtlBuffer
                              indexBufferOffset:rs.indexOffset
                                 indirectBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                           indirectBufferOffset:cmd.drawIndirect.offset + d * cmd.drawIndirect.stride];
        }
        break;
    }
    case CmdTag::DrawIndirectCount:
    case CmdTag::DrawIndexedIndirectCount: {
        // Metal doesn't have native indirect-count support.
        // Emit up to maxDrawCount draws; GPU validation will cull extra.
        // TODO: implement via ICB or argument buffer for true indirect count
        if (rs.activeEncoder != EncoderType::Render || !rs.renderEnc) break;
        rs.flushRenderState();
        auto* buf = reinterpret_cast<MvBuffer*>(cmd.drawIndirectCount.buffer);
        if (!buf || !buf->mtlBuffer) break;
        MTLPrimitiveType prim = MTLPrimitiveTypeTriangle;
        if (rs.boundGraphicsPipeline)
            prim = toMTLPrimitive((VkPrimitiveTopology)rs.boundGraphicsPipeline->topology);
        if (cmd.tag == CmdTag::DrawIndirectCount) {
            for (uint32_t d = 0; d < cmd.drawIndirectCount.maxDrawCount; ++d) {
                [rs.renderEnc drawPrimitives:prim
                              indirectBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                        indirectBufferOffset:cmd.drawIndirectCount.offset + d * cmd.drawIndirectCount.stride];
            }
        } else {
            auto* ib = reinterpret_cast<MvBuffer*>(rs.indexBuffer);
            if (!ib || !ib->mtlBuffer) break;
            MTLIndexType mtlIdxType = toMTLIndex(rs.indexType);
            for (uint32_t d = 0; d < cmd.drawIndirectCount.maxDrawCount; ++d) {
                [rs.renderEnc drawIndexedPrimitives:prim
                                          indexType:mtlIdxType
                                        indexBuffer:(__bridge id<MTLBuffer>)ib->mtlBuffer
                                  indexBufferOffset:rs.indexOffset
                                     indirectBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                               indirectBufferOffset:cmd.drawIndirectCount.offset + d * cmd.drawIndirectCount.stride];
            }
        }
        break;
    }

    // ── Compute ─────────────────────────────────────────────────────────
    case CmdTag::Dispatch: {
        rs.ensureComputeEncoder();
        rs.flushComputeState();
        if (rs.boundComputePipeline && !rs.boundComputePipeline->computePipelineState) {
            MVRVB_LOG_WARN("Replay Dispatch proceeding without a valid compute pipeline state");
        }
        if (!rs.boundComputePipeline || !rs.computeEnc) {
            MVRVB_LOG_WARN("Replay Dispatch skipped: compute pipeline or encoder unavailable");
            break;
        }
        // TODO: query threadExecutionWidth for optimal group size
        MTLSize threadsPerGroup = MTLSizeMake(
            std::min(cmd.dispatch.x, 64u),
            std::min(cmd.dispatch.y, 1u),
            std::min(cmd.dispatch.z, 1u));
        MTLSize threadgroups = MTLSizeMake(
            (cmd.dispatch.x + threadsPerGroup.width - 1) / threadsPerGroup.width,
            (cmd.dispatch.y + threadsPerGroup.height - 1) / threadsPerGroup.height,
            (cmd.dispatch.z + threadsPerGroup.depth - 1) / threadsPerGroup.depth);
        MVRVB_LOG_DEBUG("Replay Dispatch: groups=%ux%ux%u threadsPerGroup=%ux%ux%u",
                        cmd.dispatch.x,
                        cmd.dispatch.y,
                        cmd.dispatch.z,
                        (uint32_t)threadsPerGroup.width,
                        (uint32_t)threadsPerGroup.height,
                        (uint32_t)threadsPerGroup.depth);
        [rs.computeEnc dispatchThreadgroups:threadgroups
                      threadsPerThreadgroup:threadsPerGroup];
        break;
    }
    case CmdTag::DispatchIndirect: {
        rs.ensureComputeEncoder();
        rs.flushComputeState();
        auto* buf = reinterpret_cast<MvBuffer*>(cmd.dispatchIndirect.buffer);
        if (!buf || !buf->mtlBuffer || !rs.computeEnc) break;
        MTLSize threadsPerGroup = MTLSizeMake(64, 1, 1);
        [rs.computeEnc dispatchThreadgroupsWithIndirectBuffer:(__bridge id<MTLBuffer>)buf->mtlBuffer
                                         indirectBufferOffset:cmd.dispatchIndirect.offset
                                        threadsPerThreadgroup:threadsPerGroup];
        break;
    }

    // ── Transfer: CopyBuffer ────────────────────────────────────────────
    case CmdTag::CopyBuffer: {
        rs.ensureBlitEncoder();
        auto* src = reinterpret_cast<MvBuffer*>(cmd.copyBuffer.srcBuffer);
        auto* dst = reinterpret_cast<MvBuffer*>(cmd.copyBuffer.dstBuffer);
        if (!src || !dst || !src->mtlBuffer || !dst->mtlBuffer || !rs.blitEnc) {
            MVRVB_LOG_WARN("Replay CopyBuffer skipped: source, destination, or blit encoder unavailable");
            break;
        }
        uint64_t totalBytes = 0;
        for (uint32_t r = 0; r < cmd.copyBuffer.regionCount; ++r) {
            const auto& region = cmd.copyBuffer.regions[r];
            totalBytes += region.size;
            [rs.blitEnc copyFromBuffer:(__bridge id<MTLBuffer>)src->mtlBuffer
                          sourceOffset:region.srcOffset + src->mtlBufferOffset
                              toBuffer:(__bridge id<MTLBuffer>)dst->mtlBuffer
                     destinationOffset:region.dstOffset + dst->mtlBufferOffset
                                  size:region.size];
        }
        MVRVB_LOG_DEBUG("Replay CopyBuffer: regions=%u totalBytes=%llu",
                        cmd.copyBuffer.regionCount,
                        static_cast<unsigned long long>(totalBytes));
        break;
    }

    // ── Transfer: CopyImage ─────────────────────────────────────────────
    case CmdTag::CopyImage: {
        rs.ensureBlitEncoder();
        auto* srcImg = reinterpret_cast<MvImage*>(cmd.copyImage.srcImage);
        auto* dstImg = reinterpret_cast<MvImage*>(cmd.copyImage.dstImage);
        if (!srcImg || !dstImg || !srcImg->mtlTexture || !dstImg->mtlTexture || !rs.blitEnc) {
            MVRVB_LOG_WARN("Replay CopyImage skipped: source, destination, or blit encoder unavailable");
            break;
        }
        id<MTLTexture> srcTex = (__bridge id<MTLTexture>)srcImg->mtlTexture;
        id<MTLTexture> dstTex = (__bridge id<MTLTexture>)dstImg->mtlTexture;
        uint32_t totalLayerCopies = 0;
        for (uint32_t r = 0; r < cmd.copyImage.regionCount; ++r) {
            const auto& region = cmd.copyImage.regions[r];
            uint32_t layerCount = region.srcSubresource.layerCount;
            if (layerCount == VK_REMAINING_ARRAY_LAYERS)
                layerCount = srcImg->arrayLayers - region.srcSubresource.baseArrayLayer;
            totalLayerCopies += layerCount;
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                [rs.blitEnc copyFromTexture:srcTex
                                sourceSlice:region.srcSubresource.baseArrayLayer + layer
                                sourceLevel:region.srcSubresource.mipLevel
                               sourceOrigin:MTLOriginMake(region.srcOffset.x, region.srcOffset.y, region.srcOffset.z)
                                 sourceSize:MTLSizeMake(region.extent.width, region.extent.height, region.extent.depth)
                                  toTexture:dstTex
                           destinationSlice:region.dstSubresource.baseArrayLayer + layer
                           destinationLevel:region.dstSubresource.mipLevel
                          destinationOrigin:MTLOriginMake(region.dstOffset.x, region.dstOffset.y, region.dstOffset.z)];
            }
        }
        MVRVB_LOG_DEBUG("Replay CopyImage: regions=%u layerCopies=%u",
                        cmd.copyImage.regionCount,
                        totalLayerCopies);
        break;
    }

    // ── Transfer: CopyBufferToImage ─────────────────────────────────────
    case CmdTag::CopyBufferToImage: {
        rs.ensureBlitEncoder();
        auto* srcBuf = reinterpret_cast<MvBuffer*>(cmd.copyBufferToImage.srcBuffer);
        auto* dstImg = reinterpret_cast<MvImage*>(cmd.copyBufferToImage.dstImage);
        if (!srcBuf || !dstImg || !srcBuf->mtlBuffer || !dstImg->mtlTexture || !rs.blitEnc) {
            MVRVB_LOG_WARN("Replay CopyBufferToImage skipped: source, destination, or blit encoder unavailable");
            break;
        }
        id<MTLBuffer>  mtlBuf = (__bridge id<MTLBuffer>)srcBuf->mtlBuffer;
        id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)dstImg->mtlTexture;
        uint64_t totalBytes = 0;
        for (uint32_t r = 0; r < cmd.copyBufferToImage.regionCount; ++r) {
            const auto& region = cmd.copyBufferToImage.regions[r];
            uint32_t bpp = formatBytesPerPixel(dstImg->format);
            uint32_t width  = region.imageExtent.width;
            uint32_t height = region.imageExtent.height;
            uint32_t rowLength = region.bufferRowLength ? region.bufferRowLength : width;
            uint32_t imgHeight = region.bufferImageHeight ? region.bufferImageHeight : height;
            uint32_t bytesPerRow   = rowLength * bpp;
            uint32_t bytesPerImage = bytesPerRow * imgHeight;
            uint32_t layerCount = region.imageSubresource.layerCount;
            if (layerCount == VK_REMAINING_ARRAY_LAYERS)
                layerCount = dstImg->arrayLayers - region.imageSubresource.baseArrayLayer;
            totalBytes += static_cast<uint64_t>(bytesPerImage) * layerCount;
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                [rs.blitEnc copyFromBuffer:mtlBuf
                              sourceOffset:region.bufferOffset + srcBuf->mtlBufferOffset + layer * bytesPerImage
                         sourceBytesPerRow:bytesPerRow
                       sourceBytesPerImage:bytesPerImage
                                sourceSize:MTLSizeMake(width, height, region.imageExtent.depth)
                                 toTexture:mtlTex
                          destinationSlice:region.imageSubresource.baseArrayLayer + layer
                          destinationLevel:region.imageSubresource.mipLevel
                         destinationOrigin:MTLOriginMake(region.imageOffset.x, region.imageOffset.y, region.imageOffset.z)];
            }
        }
        MVRVB_LOG_DEBUG("Replay CopyBufferToImage: regions=%u totalBytes=%llu",
                        cmd.copyBufferToImage.regionCount,
                        static_cast<unsigned long long>(totalBytes));
        break;
    }

    // ── Transfer: CopyImageToBuffer ─────────────────────────────────────
    case CmdTag::CopyImageToBuffer: {
        rs.ensureBlitEncoder();
        auto* srcImg = reinterpret_cast<MvImage*>(cmd.copyImageToBuffer.srcImage);
        auto* dstBuf = reinterpret_cast<MvBuffer*>(cmd.copyImageToBuffer.dstBuffer);
        if (!srcImg || !dstBuf || !srcImg->mtlTexture || !dstBuf->mtlBuffer || !rs.blitEnc) {
            MVRVB_LOG_WARN("Replay CopyImageToBuffer skipped: source, destination, or blit encoder unavailable");
            break;
        }
        id<MTLTexture> mtlTex = (__bridge id<MTLTexture>)srcImg->mtlTexture;
        id<MTLBuffer>  mtlBuf = (__bridge id<MTLBuffer>)dstBuf->mtlBuffer;
        uint64_t totalBytes = 0;
        for (uint32_t r = 0; r < cmd.copyImageToBuffer.regionCount; ++r) {
            const auto& region = cmd.copyImageToBuffer.regions[r];
            uint32_t bpp = formatBytesPerPixel(srcImg->format);
            uint32_t width  = region.imageExtent.width;
            uint32_t height = region.imageExtent.height;
            uint32_t rowLength = region.bufferRowLength ? region.bufferRowLength : width;
            uint32_t imgHeight = region.bufferImageHeight ? region.bufferImageHeight : height;
            uint32_t bytesPerRow   = rowLength * bpp;
            uint32_t bytesPerImage = bytesPerRow * imgHeight;
            uint32_t layerCount = region.imageSubresource.layerCount;
            if (layerCount == VK_REMAINING_ARRAY_LAYERS)
                layerCount = srcImg->arrayLayers - region.imageSubresource.baseArrayLayer;
            totalBytes += static_cast<uint64_t>(bytesPerImage) * layerCount;
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                [rs.blitEnc copyFromTexture:mtlTex
                                sourceSlice:region.imageSubresource.baseArrayLayer + layer
                                sourceLevel:region.imageSubresource.mipLevel
                               sourceOrigin:MTLOriginMake(region.imageOffset.x, region.imageOffset.y, region.imageOffset.z)
                                 sourceSize:MTLSizeMake(width, height, region.imageExtent.depth)
                                   toBuffer:mtlBuf
                          destinationOffset:region.bufferOffset + dstBuf->mtlBufferOffset + layer * bytesPerImage
                     destinationBytesPerRow:bytesPerRow
                   destinationBytesPerImage:bytesPerImage];
            }
        }
        MVRVB_LOG_DEBUG("Replay CopyImageToBuffer: regions=%u totalBytes=%llu",
                        cmd.copyImageToBuffer.regionCount,
                        static_cast<unsigned long long>(totalBytes));
        break;
    }

    // ── Transfer: BlitImage (render-pass based for filtered scaling) ────
    case CmdTag::BlitImage: {
        rs.endActiveEncoder(); // Must end any active encoder before using our own render passes
        auto* srcImg = reinterpret_cast<MvImage*>(cmd.blitImage.srcImage);
        auto* dstImg = reinterpret_cast<MvImage*>(cmd.blitImage.dstImage);
        if (!srcImg || !dstImg) {
            MVRVB_LOG_WARN("Replay BlitImage skipped: source or destination image unavailable");
            break;
        }
        uint32_t failedRegions = 0;
        for (uint32_t r = 0; r < cmd.blitImage.regionCount; ++r) {
            if (!encodeColorBlitRegion(rs.device, rs.mtlCB, srcImg, dstImg,
                                       cmd.blitImage.regions[r], cmd.blitImage.filter)) {
                ++failedRegions;
                MVRVB_LOG_WARN("Replay BlitImage region %u failed to encode", r);
            }
        }
        MVRVB_LOG_DEBUG("Replay BlitImage: regions=%u filter=%s failedRegions=%u",
                        cmd.blitImage.regionCount,
                        filterName(cmd.blitImage.filter),
                        failedRegions);
        break;
    }

    // ── Transfer: FillBuffer (compute shader for 4-byte pattern) ────────
    case CmdTag::FillBuffer: {
        auto* dst = reinterpret_cast<MvBuffer*>(cmd.fillBuffer.dstBuffer);
        if (!dst || !dst->mtlBuffer || !rs.device) {
            MVRVB_LOG_WARN("Replay FillBuffer skipped: destination buffer or device unavailable");
            break;
        }

        uint64_t resolvedSize = resolvedBufferRangeSize(dst, cmd.fillBuffer.dstOffset, cmd.fillBuffer.size);
        if (resolvedSize == 0) {
            MVRVB_LOG_WARN("Replay FillBuffer skipped: resolved size is zero");
            break;
        }

        // If pattern is a repeated byte, use Metal's native fillBuffer
        if (isRepeatedBytePattern(cmd.fillBuffer.data)) {
            rs.ensureBlitEncoder();
            if (!rs.blitEnc) {
                MVRVB_LOG_WARN("Replay FillBuffer skipped: blit encoder unavailable for repeated-byte path");
                break;
            }
            uint8_t byteVal = static_cast<uint8_t>(cmd.fillBuffer.data & 0xFF);
            [rs.blitEnc fillBuffer:(__bridge id<MTLBuffer>)dst->mtlBuffer
                             range:NSMakeRange(cmd.fillBuffer.dstOffset + dst->mtlBufferOffset, resolvedSize)
                             value:byteVal];
            MVRVB_LOG_DEBUG("Replay FillBuffer: mode=blit offset=%llu size=%llu pattern=0x%08x",
                            static_cast<unsigned long long>(cmd.fillBuffer.dstOffset),
                            static_cast<unsigned long long>(resolvedSize),
                            cmd.fillBuffer.data);
        } else {
            // Use compute shader for 4-byte pattern fill
            rs.endActiveEncoder();
            id<MTLComputePipelineState> pipeline = ensureTransferFillBufferPipeline(rs.device);
            if (!pipeline) {
                MVRVB_LOG_WARN("Replay FillBuffer skipped: fill pipeline unavailable");
                break;
            }
            id<MTLComputeCommandEncoder> enc = [rs.mtlCB computeCommandEncoder];
            if (!enc) {
                MVRVB_LOG_WARN("Replay FillBuffer skipped: compute encoder unavailable");
                break;
            }

            TransferFillParams params;
            params.wordCount = static_cast<uint32_t>(resolvedSize / 4);
            params.pattern = cmd.fillBuffer.data;

            id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)dst->mtlBuffer;
            [enc setComputePipelineState:pipeline];
            [enc setBuffer:mtlBuf offset:cmd.fillBuffer.dstOffset + dst->mtlBufferOffset atIndex:0];
            [enc setBytes:&params length:sizeof(params) atIndex:1];
            NSUInteger threadWidth = [pipeline threadExecutionWidth];
            MTLSize threads = MTLSizeMake(params.wordCount, 1, 1);
            MTLSize tgSize  = MTLSizeMake(threadWidth, 1, 1);
            [enc dispatchThreads:threads threadsPerThreadgroup:tgSize];
            [enc endEncoding];
            MVRVB_LOG_DEBUG("Replay FillBuffer: mode=compute offset=%llu size=%llu words=%u pattern=0x%08x",
                            static_cast<unsigned long long>(cmd.fillBuffer.dstOffset),
                            static_cast<unsigned long long>(resolvedSize),
                            params.wordCount,
                            cmd.fillBuffer.data);
        }
        break;
    }

    // ── Transfer: UpdateBuffer (inline data → staging buffer → blit) ────
    case CmdTag::UpdateBuffer: {
        auto* dst = reinterpret_cast<MvBuffer*>(cmd.updateBuffer.dstBuffer);
        if (!dst || !dst->mtlBuffer || !rs.device || !rs.device->mtlDevice) {
            MVRVB_LOG_WARN("Replay UpdateBuffer skipped: destination buffer or Metal device unavailable");
            break;
        }
        const auto* blob = rs.sourceCommandBuffer->inlineData(cmd.updateBuffer.dataBlobIndex);
        if (!blob || blob->empty()) {
            MVRVB_LOG_WARN("Replay UpdateBuffer skipped: inline data blob unavailable");
            break;
        }

        rs.ensureBlitEncoder();
        if (!rs.blitEnc) {
            MVRVB_LOG_WARN("Replay UpdateBuffer skipped: blit encoder unavailable");
            break;
        }
        // Create a small staging buffer for the inline data
        id<MTLBuffer> staging = [(__bridge id<MTLDevice>)rs.device->mtlDevice
            newBufferWithBytes:blob->data()
                        length:blob->size()
                       options:MTLResourceStorageModeShared];
        if (!staging) {
            MVRVB_LOG_WARN("Replay UpdateBuffer skipped: staging buffer allocation failed");
            break;
        }
        [rs.blitEnc copyFromBuffer:staging
                      sourceOffset:0
                          toBuffer:(__bridge id<MTLBuffer>)dst->mtlBuffer
                 destinationOffset:cmd.updateBuffer.dstOffset + dst->mtlBufferOffset
                              size:cmd.updateBuffer.dataSize];
        MVRVB_LOG_DEBUG("Replay UpdateBuffer: dstOffset=%llu dataSize=%u blobBytes=%zu",
                        static_cast<unsigned long long>(cmd.updateBuffer.dstOffset),
                        cmd.updateBuffer.dataSize,
                        blob->size());
        break;
    }

    // ── Transfer: ResolveImage (MSAA → non-MSAA via render pass) ────────
    case CmdTag::ResolveImage: {
        rs.endActiveEncoder();
        auto* srcImg = reinterpret_cast<MvImage*>(cmd.resolveImage.srcImage);
        auto* dstImg = reinterpret_cast<MvImage*>(cmd.resolveImage.dstImage);
        if (!srcImg || !dstImg) {
            MVRVB_LOG_WARN("Replay ResolveImage skipped: source or destination image unavailable");
            break;
        }
        uint32_t failedRegions = 0;
        for (uint32_t r = 0; r < cmd.resolveImage.regionCount; ++r) {
            if (!encodeColorResolveRegion(rs.device, rs.mtlCB, srcImg, dstImg,
                                          cmd.resolveImage.regions[r])) {
                ++failedRegions;
                MVRVB_LOG_WARN("Replay ResolveImage region %u failed to encode", r);
            }
        }
        MVRVB_LOG_DEBUG("Replay ResolveImage: regions=%u failedRegions=%u",
                        cmd.resolveImage.regionCount,
                        failedRegions);
        break;
    }

    // ── Transfer: ClearColorImage (outside render pass) ─────────────────
    case CmdTag::ClearColorImage: {
        rs.endActiveEncoder();
        auto* img = reinterpret_cast<MvImage*>(cmd.clearColorImage.image);
        if (!img || !img->mtlTexture) {
            MVRVB_LOG_WARN("Replay ClearColorImage skipped: image or Metal texture unavailable");
            break;
        }
        id<MTLTexture> texture = (__bridge id<MTLTexture>)img->mtlTexture;
        MTLClearColor clearColor = toMTLClearColor(cmd.clearColorImage.color, img->format);

        for (uint32_t r = 0; r < cmd.clearColorImage.rangeCount; ++r) {
            const auto& range = cmd.clearColorImage.ranges[r];
            uint32_t levelCount = resolveRangeLevelCount(img, range);
            for (uint32_t level = 0; level < levelCount; ++level) {
                uint32_t mip = range.baseMipLevel + level;
                uint32_t layerCount = resolveRangeLayerCount(img, range, mip);
                for (uint32_t layer = 0; layer < layerCount; ++layer) {
                    uint32_t slice = (img->imageType == VK_IMAGE_TYPE_3D) ? layer : range.baseArrayLayer + layer;
                    @autoreleasepool {
                        id<MTLTexture> view = createSingleSliceTextureView(
                            texture, vkFormatToMTL(img->format), MTLTextureType2D, mip, slice);
                        if (!view) continue;
                        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
                        desc.colorAttachments[0].texture    = view;
                        desc.colorAttachments[0].loadAction = MTLLoadActionClear;
                        desc.colorAttachments[0].storeAction = MTLStoreActionStore;
                        desc.colorAttachments[0].clearColor = clearColor;
                        id<MTLRenderCommandEncoder> enc =
                            [rs.mtlCB renderCommandEncoderWithDescriptor:desc];
                        [enc endEncoding];
                    }
                }
            }
        }
        MVRVB_LOG_DEBUG("Replay ClearColorImage: ranges=%u format=%d",
                        cmd.clearColorImage.rangeCount,
                        img->format);
        break;
    }

    // ── Transfer: ClearDepthStencilImage (outside render pass) ──────────
    case CmdTag::ClearDepthStencilImage: {
        rs.endActiveEncoder();
        auto* img = reinterpret_cast<MvImage*>(cmd.clearDepthStencilImage.image);
        if (!img || !img->mtlTexture) {
            MVRVB_LOG_WARN("Replay ClearDepthStencilImage skipped: image or Metal texture unavailable");
            break;
        }
        id<MTLTexture> texture = (__bridge id<MTLTexture>)img->mtlTexture;
        MTLPixelFormat mtlFmt = vkFormatToMTL(img->format);
        bool hasDepth   = isDepthFormat(img->format);
        bool hasStencil = hasStencilComponent(img->format);

        for (uint32_t r = 0; r < cmd.clearDepthStencilImage.rangeCount; ++r) {
            const auto& range = cmd.clearDepthStencilImage.ranges[r];
            uint32_t levelCount = resolveRangeLevelCount(img, range);
            for (uint32_t level = 0; level < levelCount; ++level) {
                uint32_t mip = range.baseMipLevel + level;
                uint32_t layerCount = resolveRangeLayerCount(img, range, mip);
                for (uint32_t layer = 0; layer < layerCount; ++layer) {
                    uint32_t slice = range.baseArrayLayer + layer;
                    @autoreleasepool {
                        id<MTLTexture> view = createSingleSliceTextureView(
                            texture, mtlFmt, MTLTextureType2D, mip, slice);
                        if (!view) continue;
                        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
                        if (hasDepth) {
                            desc.depthAttachment.texture     = view;
                            desc.depthAttachment.loadAction  = MTLLoadActionClear;
                            desc.depthAttachment.storeAction = MTLStoreActionStore;
                            desc.depthAttachment.clearDepth  = cmd.clearDepthStencilImage.value.depth;
                        }
                        if (hasStencil) {
                            desc.stencilAttachment.texture       = view;
                            desc.stencilAttachment.loadAction    = MTLLoadActionClear;
                            desc.stencilAttachment.storeAction   = MTLStoreActionStore;
                            desc.stencilAttachment.clearStencil  = cmd.clearDepthStencilImage.value.stencil;
                        }
                        id<MTLRenderCommandEncoder> enc =
                            [rs.mtlCB renderCommandEncoderWithDescriptor:desc];
                        [enc endEncoding];
                    }
                }
            }
        }
        MVRVB_LOG_DEBUG("Replay ClearDepthStencilImage: ranges=%u depth=%s stencil=%s",
                        cmd.clearDepthStencilImage.rangeCount,
                        hasDepth ? "yes" : "no",
                        hasStencil ? "yes" : "no");
        break;
    }

    // ── ClearAttachments (inside render pass) ───────────────────────────
    case CmdTag::ClearAttachments: {
        // TODO: implement mid-render-pass clears via setScissorRect + draw
        // For now, this is a no-op since Metal render passes clear on load
        MVRVB_LOG_WARN("vkCmdClearAttachments: mid-render-pass clear not yet implemented (attachments=%u rects=%u)",
                       cmd.clearAttachments.attachmentCount,
                       cmd.clearAttachments.rectCount);
        break;
    }

    // ── Synchronization ─────────────────────────────────────────────────
    case CmdTag::PipelineBarrier: {
        // Metal handles most hazards automatically.
        // Insert a memory barrier on the active encoder if present.
        MVRVB_LOG_DEBUG("Replay PipelineBarrier: activeEncoder=%s",
                        encoderTypeName(rs.activeEncoder));
        if (rs.activeEncoder == EncoderType::Render && rs.renderEnc) {
            if (@available(macOS 10.14, *)) {
                [rs.renderEnc memoryBarrierWithScope:(MTLBarrierScopeBuffers | MTLBarrierScopeTextures)
                                         afterStages:MTLRenderStageFragment
                                        beforeStages:(MTLRenderStageVertex | MTLRenderStageFragment)];
            }
        } else if (rs.activeEncoder == EncoderType::Compute && rs.computeEnc) {
            [rs.computeEnc memoryBarrierWithScope:MTLBarrierScopeBuffers];
        }
        // Blit encoder has implicit ordering
        break;
    }
    case CmdTag::PipelineBarrier2: {
        // Same as PipelineBarrier — Metal handles hazards automatically
        MVRVB_LOG_DEBUG("Replay PipelineBarrier2: activeEncoder=%s",
                        encoderTypeName(rs.activeEncoder));
        if (rs.activeEncoder == EncoderType::Render && rs.renderEnc) {
            if (@available(macOS 10.14, *)) {
                [rs.renderEnc memoryBarrierWithScope:(MTLBarrierScopeBuffers | MTLBarrierScopeTextures)
                                         afterStages:MTLRenderStageFragment
                                        beforeStages:(MTLRenderStageVertex | MTLRenderStageFragment)];
            }
        } else if (rs.activeEncoder == EncoderType::Compute && rs.computeEnc) {
            [rs.computeEnc memoryBarrierWithScope:MTLBarrierScopeBuffers];
        }
        break;
    }
    case CmdTag::SetEvent: {
        auto* ev = toMv(cmd.setEvent.event);
        if (ev) {
            ev->set.store(true, std::memory_order_release);
            MVRVB_LOG_DEBUG("Replay SetEvent: event=%p state=set", static_cast<void*>(ev));
        } else {
            MVRVB_LOG_WARN("Replay SetEvent skipped: event unavailable");
        }
        break;
    }
    case CmdTag::ResetEvent: {
        auto* ev = toMv(cmd.resetEvent.event);
        if (ev) {
            ev->set.store(false, std::memory_order_release);
            MVRVB_LOG_DEBUG("Replay ResetEvent: event=%p state=reset", static_cast<void*>(ev));
        } else {
            MVRVB_LOG_WARN("Replay ResetEvent skipped: event unavailable");
        }
        break;
    }

    // ── ExecuteCommands (secondary command buffers) ─────────────────────
    case CmdTag::ExecuteCommands: {
        MVRVB_LOG_DEBUG("Replay ExecuteCommands: secondaryCount=%u",
                        cmd.executeCommands.commandBufferCount);
        for (uint32_t i = 0; i < cmd.executeCommands.commandBufferCount; ++i) {
            auto* secondary = toMv(cmd.executeCommands.commandBuffers[i]);
            if (secondary) {
                replayCommandList(secondary, rs);
            } else {
                MVRVB_LOG_WARN("Replay ExecuteCommands skipped secondary[%u]: command buffer unavailable", i);
            }
        }
        break;
    }

    default:
        break;
    } // switch
}

// ═══════════════════════════════════════════════════════════════════════════════
// Replay a full command list
// ═══════════════════════════════════════════════════════════════════════════════

static void replayCommandList(const MvCommandBuffer* cb, ReplayState& rs) {
    if (!cb) return;
    const MvCommandBuffer* savedSource = rs.sourceCommandBuffer;
    const bool isNestedSecondary =
        (cb->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) && (savedSource != cb);
    if (isNestedSecondary) {
        MVRVB_LOG_DEBUG("ReplayCommandList enter: level=%s commands=%zu",
                        commandBufferLevelName(cb->level),
                        cb->commands.size());
    }
    rs.sourceCommandBuffer = cb;
    for (const auto& cmd : cb->commands) {
        replayCommand(cmd, rs);
    }
    rs.sourceCommandBuffer = savedSource;
    if (isNestedSecondary) {
        MVRVB_LOG_DEBUG("ReplayCommandList complete: level=%s commands=%zu",
                        commandBufferLevelName(cb->level),
                        cb->commands.size());
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public entry point: replay deferred commands onto an existing MTLCommandBuffer
// ═══════════════════════════════════════════════════════════════════════════════

void replayCommandBufferOnMTL(MvCommandBuffer* cb, id<MTLCommandBuffer> mtlCB) {
    if (!cb || !mtlCB) return;

    MvDevice* device = nullptr;
    if (cb->pool) {
        auto* pool = reinterpret_cast<MvCommandPool*>(cb->pool);
        device = reinterpret_cast<MvDevice*>(pool->device);
    }

    ReplayState rs;
    rs.device = device;
    rs.sourceCommandBuffer = cb;
    rs.mtlCB = mtlCB;

    MVRVB_LOG_DEBUG("ReplayCommandBufferOnMTL: commands=%zu oneTime=%s simultaneous=%s renderPassContinue=%s",
                    cb->commands.size(),
                    cb->oneTimeSubmit ? "yes" : "no",
                    cb->simultaneousUse ? "yes" : "no",
                    cb->renderPassContinue ? "yes" : "no");

    replayCommandList(cb, rs);

    // End any active encoder left open
    rs.endActiveEncoder();
    MVRVB_LOG_DEBUG("ReplayCommandBufferOnMTL complete: commands=%zu", cb->commands.size());
}

} // namespace mvrvb

// ═══════════════════════════════════════════════════════════════════════════════
// Recording functions (extern "C" — wired into ICD dispatch table)
// ═══════════════════════════════════════════════════════════════════════════════

using namespace mvrvb;

extern "C" {

// ── Command buffer lifecycle ─────────────────────────────────────────────────

VkResult vkAllocateCommandBuffers(VkDevice device,
                                  const VkCommandBufferAllocateInfo* pAllocateInfo,
                                  VkCommandBuffer* pCommandBuffers) {
    if (!pAllocateInfo || !pCommandBuffers) return VK_ERROR_INITIALIZATION_FAILED;
    auto* pool = reinterpret_cast<MvCommandPool*>(pAllocateInfo->commandPool);
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; ++i) {
        auto* cb = pool->acquire(pAllocateInfo->level);
        pCommandBuffers[i] = reinterpret_cast<VkCommandBuffer>(cb);
    }
    return VK_SUCCESS;
}

void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                          uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    auto* pool = reinterpret_cast<MvCommandPool*>(commandPool);
    if (!pool || !pCommandBuffers) return;
    for (uint32_t i = 0; i < commandBufferCount; ++i) {
        auto* cb = toMv(pCommandBuffers[i]);
        if (cb) pool->release(cb);
    }
}

VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                              const VkCommandBufferBeginInfo* pBeginInfo) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    cb->reset();
    cb->state = CmdBufState::Recording;
    if (pBeginInfo) {
        cb->oneTimeSubmit    = (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) != 0;
        cb->simultaneousUse  = (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) != 0;
        cb->renderPassContinue = (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) != 0;
    }
    return VK_SUCCESS;
}

VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    cb->state = CmdBufState::Executable;
    return VK_SUCCESS;
}

VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
    cb->reset();
    return VK_SUCCESS;
}

// ── Command pool ─────────────────────────────────────────────────────────────

VkResult vkCreateCommandPool(VkDevice device,
                             const VkCommandPoolCreateInfo* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkCommandPool* pCommandPool) {
    if (!pCreateInfo || !pCommandPool) return VK_ERROR_INITIALIZATION_FAILED;
    auto* pool = new MvCommandPool();
    pool->device = reinterpret_cast<void*>(reinterpret_cast<MvDevice*>(device));
    pool->queueFamilyIndex = pCreateInfo->queueFamilyIndex;
    pool->flags = pCreateInfo->flags;
    *pCommandPool = reinterpret_cast<VkCommandPool>(pool);
    return VK_SUCCESS;
}

void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                          const VkAllocationCallbacks* pAllocator) {
    auto* pool = reinterpret_cast<MvCommandPool*>(commandPool);
    delete pool;
}

VkResult vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                            VkCommandPoolResetFlags flags) {
    auto* pool = reinterpret_cast<MvCommandPool*>(commandPool);
    if (pool) pool->resetAll();
    return VK_SUCCESS;
}

// ── Render pass recording ────────────────────────────────────────────────────

void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                          const VkRenderPassBeginInfo* pRenderPassBegin,
                          VkSubpassContents contents) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRenderPassBegin) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BeginRenderPass;
    cmd.beginRenderPass.renderPass = pRenderPassBegin->renderPass;
    cmd.beginRenderPass.framebuffer = pRenderPassBegin->framebuffer;
    cmd.beginRenderPass.renderArea = pRenderPassBegin->renderArea;
    cmd.beginRenderPass.clearValueCount =
        std::min(pRenderPassBegin->clearValueCount, kMaxColorAttachments + 1);
    for (uint32_t i = 0; i < cmd.beginRenderPass.clearValueCount; ++i) {
        cmd.beginRenderPass.clearValues[i] = pRenderPassBegin->pClearValues[i];
    }
    cb->record(std::move(cmd));
}

void vkCmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                           const VkRenderPassBeginInfo* pRenderPassBegin,
                           const VkSubpassBeginInfo* pSubpassBeginInfo) {
    vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin,
                         pSubpassBeginInfo ? pSubpassBeginInfo->contents : VK_SUBPASS_CONTENTS_INLINE);
}

void vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::NextSubpass;
    cb->record(std::move(cmd));
}

void vkCmdNextSubpass2(VkCommandBuffer commandBuffer,
                       const VkSubpassBeginInfo* pSubpassBeginInfo,
                       const VkSubpassEndInfo* pSubpassEndInfo) {
    vkCmdNextSubpass(commandBuffer, pSubpassBeginInfo ? pSubpassBeginInfo->contents : VK_SUBPASS_CONTENTS_INLINE);
}

void vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::EndRenderPass;
    cb->record(std::move(cmd));
}

void vkCmdEndRenderPass2(VkCommandBuffer commandBuffer, const VkSubpassEndInfo* pSubpassEndInfo) {
    vkCmdEndRenderPass(commandBuffer);
}

void vkCmdBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* pRenderingInfo) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRenderingInfo) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BeginRendering;
    auto& d = cmd.beginRendering;
    d.flags = pRenderingInfo->flags;
    d.renderArea = pRenderingInfo->renderArea;
    d.layerCount = pRenderingInfo->layerCount;
    d.viewMask = pRenderingInfo->viewMask;
    d.colorAttachmentCount = std::min(pRenderingInfo->colorAttachmentCount, kMaxColorAttachments);
    for (uint32_t i = 0; i < d.colorAttachmentCount; ++i) {
        const auto& src = pRenderingInfo->pColorAttachments[i];
        auto& dst = d.colorAttachments[i];
        dst.imageView    = src.imageView;
        dst.imageLayout  = src.imageLayout;
        dst.resolveMode  = src.resolveMode;
        dst.resolveImageView = src.resolveImageView;
        dst.loadOp       = src.loadOp;
        dst.storeOp      = src.storeOp;
        dst.clearValue   = src.clearValue;
    }
    d.hasDepth = (pRenderingInfo->pDepthAttachment != nullptr);
    if (d.hasDepth) {
        const auto& src = *pRenderingInfo->pDepthAttachment;
        d.depthAttachment.imageView   = src.imageView;
        d.depthAttachment.imageLayout = src.imageLayout;
        d.depthAttachment.loadOp      = src.loadOp;
        d.depthAttachment.storeOp     = src.storeOp;
        d.depthAttachment.clearValue  = src.clearValue;
    }
    d.hasStencil = (pRenderingInfo->pStencilAttachment != nullptr);
    if (d.hasStencil) {
        const auto& src = *pRenderingInfo->pStencilAttachment;
        d.stencilAttachment.imageView   = src.imageView;
        d.stencilAttachment.imageLayout = src.imageLayout;
        d.stencilAttachment.loadOp      = src.loadOp;
        d.stencilAttachment.storeOp     = src.storeOp;
        d.stencilAttachment.clearValue  = src.clearValue;
    }
    cb->record(std::move(cmd));
}

void vkCmdEndRendering(VkCommandBuffer commandBuffer) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::EndRendering;
    cb->record(std::move(cmd));
}

void vkCmdExecuteCommands(VkCommandBuffer commandBuffer,
                          uint32_t commandBufferCount,
                          const VkCommandBuffer* pCommandBuffers) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pCommandBuffers) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::ExecuteCommands;
    cmd.executeCommands.commandBufferCount = std::min(commandBufferCount, 4u);
    for (uint32_t i = 0; i < cmd.executeCommands.commandBufferCount; ++i) {
        cmd.executeCommands.commandBuffers[i] = pCommandBuffers[i];
    }
    cb->record(std::move(cmd));
}

// ── Pipeline / state binding ─────────────────────────────────────────────────

void vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                       VkPipelineBindPoint pipelineBindPoint,
                       VkPipeline pipeline) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BindPipeline;
    cmd.bindPipeline.bindPoint = pipelineBindPoint;
    cmd.bindPipeline.pipeline  = pipeline;
    cb->record(std::move(cmd));
}

void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                             VkPipelineBindPoint pipelineBindPoint,
                             VkPipelineLayout layout,
                             uint32_t firstSet,
                             uint32_t descriptorSetCount,
                             const VkDescriptorSet* pDescriptorSets,
                             uint32_t dynamicOffsetCount,
                             const uint32_t* pDynamicOffsets) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BindDescriptorSets;
    cmd.bindDescriptorSets.bindPoint = pipelineBindPoint;
    cmd.bindDescriptorSets.layout    = layout;
    cmd.bindDescriptorSets.firstSet  = firstSet;
    cmd.bindDescriptorSets.setCount  = std::min(descriptorSetCount, kMaxDescriptorSets);
    for (uint32_t i = 0; i < cmd.bindDescriptorSets.setCount; ++i) {
        cmd.bindDescriptorSets.sets[i] = pDescriptorSets[i];
    }
    cmd.bindDescriptorSets.dynamicOffsetCount = std::min(dynamicOffsetCount, kMaxDynamicOffsets);
    for (uint32_t i = 0; i < cmd.bindDescriptorSets.dynamicOffsetCount; ++i) {
        cmd.bindDescriptorSets.dynamicOffsets[i] = pDynamicOffsets[i];
    }
    cb->record(std::move(cmd));
}

void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                            uint32_t firstBinding, uint32_t bindingCount,
                            const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pBuffers || !pOffsets) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BindVertexBuffers;
    cmd.bindVertexBuffers.firstBinding = firstBinding;
    cmd.bindVertexBuffers.bindingCount = std::min(bindingCount, kMaxVertexBindings);
    cmd.bindVertexBuffers.hasSizesStrides = false;
    for (uint32_t i = 0; i < cmd.bindVertexBuffers.bindingCount; ++i) {
        cmd.bindVertexBuffers.buffers[i] = pBuffers[i];
        cmd.bindVertexBuffers.offsets[i] = pOffsets[i];
    }
    cb->record(std::move(cmd));
}

void vkCmdBindVertexBuffers2(VkCommandBuffer commandBuffer,
                             uint32_t firstBinding, uint32_t bindingCount,
                             const VkBuffer* pBuffers, const VkDeviceSize* pOffsets,
                             const VkDeviceSize* pSizes, const VkDeviceSize* pStrides) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pBuffers || !pOffsets) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BindVertexBuffers;
    cmd.bindVertexBuffers.firstBinding = firstBinding;
    cmd.bindVertexBuffers.bindingCount = std::min(bindingCount, kMaxVertexBindings);
    cmd.bindVertexBuffers.hasSizesStrides = (pSizes != nullptr || pStrides != nullptr);
    for (uint32_t i = 0; i < cmd.bindVertexBuffers.bindingCount; ++i) {
        cmd.bindVertexBuffers.buffers[i] = pBuffers[i];
        cmd.bindVertexBuffers.offsets[i] = pOffsets[i];
        cmd.bindVertexBuffers.sizes[i]   = pSizes   ? pSizes[i]   : VK_WHOLE_SIZE;
        cmd.bindVertexBuffers.strides[i] = pStrides  ? pStrides[i] : 0;
    }
    cb->record(std::move(cmd));
}

void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                          VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BindIndexBuffer;
    cmd.bindIndexBuffer.buffer = buffer;
    cmd.bindIndexBuffer.offset = offset;
    cmd.bindIndexBuffer.indexType = indexType;
    cb->record(std::move(cmd));
}

void vkCmdPushConstants(VkCommandBuffer commandBuffer,
                        VkPipelineLayout layout,
                        VkShaderStageFlags stageFlags,
                        uint32_t offset, uint32_t size, const void* pValues) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pValues) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::PushConstants;
    cmd.pushConstants.layout = layout;
    cmd.pushConstants.stageFlags = stageFlags;
    cmd.pushConstants.offset = offset;
    cmd.pushConstants.size = std::min(size, kMaxPushConstantBytes - offset);
    memcpy(cmd.pushConstants.data + offset, pValues, cmd.pushConstants.size);
    cb->record(std::move(cmd));
}

// ── Dynamic state ────────────────────────────────────────────────────────────

void vkCmdSetViewport(VkCommandBuffer commandBuffer,
                      uint32_t firstViewport, uint32_t viewportCount,
                      const VkViewport* pViewports) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pViewports) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetViewport;
    cmd.setViewport.firstViewport = firstViewport;
    cmd.setViewport.viewportCount = viewportCount;
    cmd.setViewport.viewports[0] = pViewports[0];
    cb->record(std::move(cmd));
}

void vkCmdSetScissor(VkCommandBuffer commandBuffer,
                     uint32_t firstScissor, uint32_t scissorCount,
                     const VkRect2D* pScissors) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pScissors) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetScissor;
    cmd.setScissor.firstScissor = firstScissor;
    cmd.setScissor.scissorCount = scissorCount;
    cmd.setScissor.scissors[0] = pScissors[0];
    cb->record(std::move(cmd));
}

void vkCmdSetDepthBias(VkCommandBuffer commandBuffer,
                       float depthBiasConstantFactor,
                       float depthBiasClamp,
                       float depthBiasSlopeFactor) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetDepthBias;
    cmd.setDepthBias.constantFactor = depthBiasConstantFactor;
    cmd.setDepthBias.clamp = depthBiasClamp;
    cmd.setDepthBias.slopeFactor = depthBiasSlopeFactor;
    cb->record(std::move(cmd));
}

void vkCmdSetBlendConstants(VkCommandBuffer commandBuffer, const float blendConstants[4]) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetBlendConstants;
    memcpy(cmd.setBlendConstants.constants, blendConstants, sizeof(float) * 4);
    cb->record(std::move(cmd));
}

void vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                VkStencilFaceFlags faceMask, uint32_t compareMask) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetStencilCompareMask;
    cmd.setStencilValue.faceMask = faceMask;
    cmd.setStencilValue.value = compareMask;
    cb->record(std::move(cmd));
}

void vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                              VkStencilFaceFlags faceMask, uint32_t writeMask) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetStencilWriteMask;
    cmd.setStencilValue.faceMask = faceMask;
    cmd.setStencilValue.value = writeMask;
    cb->record(std::move(cmd));
}

void vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                              VkStencilFaceFlags faceMask, uint32_t reference) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetStencilReference;
    cmd.setStencilValue.faceMask = faceMask;
    cmd.setStencilValue.value = reference;
    cb->record(std::move(cmd));
}

void vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetLineWidth;
    cb->record(std::move(cmd));
}

void vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds, float maxDepthBounds) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::SetDepthBounds;
    cb->record(std::move(cmd));
}

// ── Draw commands ────────────────────────────────────────────────────────────

void vkCmdDraw(VkCommandBuffer commandBuffer,
               uint32_t vertexCount, uint32_t instanceCount,
               uint32_t firstVertex, uint32_t firstInstance) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::Draw;
    cmd.draw.vertexCount   = vertexCount;
    cmd.draw.instanceCount = instanceCount;
    cmd.draw.firstVertex   = firstVertex;
    cmd.draw.firstInstance = firstInstance;
    cb->record(std::move(cmd));
}

void vkCmdDrawIndexed(VkCommandBuffer commandBuffer,
                      uint32_t indexCount, uint32_t instanceCount,
                      uint32_t firstIndex, int32_t vertexOffset,
                      uint32_t firstInstance) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DrawIndexed;
    cmd.drawIndexed.indexCount    = indexCount;
    cmd.drawIndexed.instanceCount = instanceCount;
    cmd.drawIndexed.firstIndex    = firstIndex;
    cmd.drawIndexed.vertexOffset  = vertexOffset;
    cmd.drawIndexed.firstInstance = firstInstance;
    cb->record(std::move(cmd));
}

void vkCmdDrawIndirect(VkCommandBuffer commandBuffer,
                       VkBuffer buffer, VkDeviceSize offset,
                       uint32_t drawCount, uint32_t stride) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DrawIndirect;
    cmd.drawIndirect.buffer    = buffer;
    cmd.drawIndirect.offset    = offset;
    cmd.drawIndirect.drawCount = drawCount;
    cmd.drawIndirect.stride    = stride;
    cb->record(std::move(cmd));
}

void vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                              VkBuffer buffer, VkDeviceSize offset,
                              uint32_t drawCount, uint32_t stride) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DrawIndexedIndirect;
    cmd.drawIndirect.buffer    = buffer;
    cmd.drawIndirect.offset    = offset;
    cmd.drawIndirect.drawCount = drawCount;
    cmd.drawIndirect.stride    = stride;
    cb->record(std::move(cmd));
}

void vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                            VkBuffer buffer, VkDeviceSize offset,
                            VkBuffer countBuffer, VkDeviceSize countOffset,
                            uint32_t maxDrawCount, uint32_t stride) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DrawIndirectCount;
    cmd.drawIndirectCount.buffer       = buffer;
    cmd.drawIndirectCount.offset       = offset;
    cmd.drawIndirectCount.countBuffer  = countBuffer;
    cmd.drawIndirectCount.countOffset  = countOffset;
    cmd.drawIndirectCount.maxDrawCount = maxDrawCount;
    cmd.drawIndirectCount.stride       = stride;
    cb->record(std::move(cmd));
}

void vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                                   VkBuffer buffer, VkDeviceSize offset,
                                   VkBuffer countBuffer, VkDeviceSize countOffset,
                                   uint32_t maxDrawCount, uint32_t stride) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DrawIndexedIndirectCount;
    cmd.drawIndirectCount.buffer       = buffer;
    cmd.drawIndirectCount.offset       = offset;
    cmd.drawIndirectCount.countBuffer  = countBuffer;
    cmd.drawIndirectCount.countOffset  = countOffset;
    cmd.drawIndirectCount.maxDrawCount = maxDrawCount;
    cmd.drawIndirectCount.stride       = stride;
    cb->record(std::move(cmd));
}

// ── Compute dispatch ─────────────────────────────────────────────────────────

void vkCmdDispatch(VkCommandBuffer commandBuffer,
                   uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::Dispatch;
    cmd.dispatch.x = groupCountX;
    cmd.dispatch.y = groupCountY;
    cmd.dispatch.z = groupCountZ;
    cb->record(std::move(cmd));
}

void vkCmdDispatchIndirect(VkCommandBuffer commandBuffer,
                           VkBuffer buffer, VkDeviceSize offset) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::DispatchIndirect;
    cmd.dispatchIndirect.buffer = buffer;
    cmd.dispatchIndirect.offset = offset;
    cb->record(std::move(cmd));
}

// ── Transfer recording ───────────────────────────────────────────────────────

void vkCmdCopyBuffer(VkCommandBuffer commandBuffer,
                     VkBuffer srcBuffer, VkBuffer dstBuffer,
                     uint32_t regionCount, const VkBufferCopy* pRegions) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::CopyBuffer;
    cmd.copyBuffer.srcBuffer = srcBuffer;
    cmd.copyBuffer.dstBuffer = dstBuffer;
    cmd.copyBuffer.regionCount = std::min(regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.copyBuffer.regionCount; ++i)
        cmd.copyBuffer.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdCopyBuffer2(VkCommandBuffer commandBuffer,
                      const VkCopyBufferInfo2* pCopyBufferInfo) {
    if (!pCopyBufferInfo) return;
    // Convert VkBufferCopy2 → VkBufferCopy (same fields)
    VkBufferCopy regions[kMaxCopyRegions];
    uint32_t count = std::min(pCopyBufferInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        regions[i].srcOffset = pCopyBufferInfo->pRegions[i].srcOffset;
        regions[i].dstOffset = pCopyBufferInfo->pRegions[i].dstOffset;
        regions[i].size      = pCopyBufferInfo->pRegions[i].size;
    }
    vkCmdCopyBuffer(commandBuffer, pCopyBufferInfo->srcBuffer,
                    pCopyBufferInfo->dstBuffer, count, regions);
}

void vkCmdCopyImage(VkCommandBuffer commandBuffer,
                    VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageCopy* pRegions) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::CopyImage;
    cmd.copyImage.srcImage   = srcImage;
    cmd.copyImage.srcLayout  = srcImageLayout;
    cmd.copyImage.dstImage   = dstImage;
    cmd.copyImage.dstLayout  = dstImageLayout;
    cmd.copyImage.regionCount = std::min(regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.copyImage.regionCount; ++i)
        cmd.copyImage.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdCopyImage2(VkCommandBuffer commandBuffer,
                     const VkCopyImageInfo2* pCopyImageInfo) {
    if (!pCopyImageInfo) return;
    VkImageCopy regions[kMaxCopyRegions];
    uint32_t count = std::min(pCopyImageInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = pCopyImageInfo->pRegions[i];
        regions[i].srcSubresource = src.srcSubresource;
        regions[i].srcOffset      = src.srcOffset;
        regions[i].dstSubresource = src.dstSubresource;
        regions[i].dstOffset      = src.dstOffset;
        regions[i].extent         = src.extent;
    }
    vkCmdCopyImage(commandBuffer, pCopyImageInfo->srcImage, pCopyImageInfo->srcImageLayout,
                   pCopyImageInfo->dstImage, pCopyImageInfo->dstImageLayout, count, regions);
}

void vkCmdBlitImage(VkCommandBuffer commandBuffer,
                    VkImage srcImage, VkImageLayout srcImageLayout,
                    VkImage dstImage, VkImageLayout dstImageLayout,
                    uint32_t regionCount, const VkImageBlit* pRegions,
                    VkFilter filter) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::BlitImage;
    cmd.blitImage.srcImage   = srcImage;
    cmd.blitImage.srcLayout  = srcImageLayout;
    cmd.blitImage.dstImage   = dstImage;
    cmd.blitImage.dstLayout  = dstImageLayout;
    cmd.blitImage.regionCount = std::min(regionCount, kMaxCopyRegions);
    cmd.blitImage.filter     = filter;
    for (uint32_t i = 0; i < cmd.blitImage.regionCount; ++i)
        cmd.blitImage.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdBlitImage2(VkCommandBuffer commandBuffer,
                     const VkBlitImageInfo2* pBlitImageInfo) {
    if (!pBlitImageInfo) return;
    VkImageBlit regions[kMaxCopyRegions];
    uint32_t count = std::min(pBlitImageInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = pBlitImageInfo->pRegions[i];
        regions[i].srcSubresource = src.srcSubresource;
        memcpy(regions[i].srcOffsets, src.srcOffsets, sizeof(VkOffset3D) * 2);
        regions[i].dstSubresource = src.dstSubresource;
        memcpy(regions[i].dstOffsets, src.dstOffsets, sizeof(VkOffset3D) * 2);
    }
    vkCmdBlitImage(commandBuffer, pBlitImageInfo->srcImage, pBlitImageInfo->srcImageLayout,
                   pBlitImageInfo->dstImage, pBlitImageInfo->dstImageLayout,
                   count, regions, pBlitImageInfo->filter);
}

void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                            VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout,
                            uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::CopyBufferToImage;
    cmd.copyBufferToImage.srcBuffer  = srcBuffer;
    cmd.copyBufferToImage.dstImage   = dstImage;
    cmd.copyBufferToImage.dstLayout  = dstImageLayout;
    cmd.copyBufferToImage.regionCount = std::min(regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.copyBufferToImage.regionCount; ++i)
        cmd.copyBufferToImage.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                             const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    if (!pCopyBufferToImageInfo) return;
    VkBufferImageCopy regions[kMaxCopyRegions];
    uint32_t count = std::min(pCopyBufferToImageInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = pCopyBufferToImageInfo->pRegions[i];
        regions[i].bufferOffset      = src.bufferOffset;
        regions[i].bufferRowLength   = src.bufferRowLength;
        regions[i].bufferImageHeight = src.bufferImageHeight;
        regions[i].imageSubresource  = src.imageSubresource;
        regions[i].imageOffset       = src.imageOffset;
        regions[i].imageExtent       = src.imageExtent;
    }
    vkCmdCopyBufferToImage(commandBuffer, pCopyBufferToImageInfo->srcBuffer,
                           pCopyBufferToImageInfo->dstImage, pCopyBufferToImageInfo->dstImageLayout,
                           count, regions);
}

void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                            VkImage srcImage, VkImageLayout srcImageLayout,
                            VkBuffer dstBuffer,
                            uint32_t regionCount, const VkBufferImageCopy* pRegions) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::CopyImageToBuffer;
    cmd.copyImageToBuffer.srcImage  = srcImage;
    cmd.copyImageToBuffer.srcLayout = srcImageLayout;
    cmd.copyImageToBuffer.dstBuffer = dstBuffer;
    cmd.copyImageToBuffer.regionCount = std::min(regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.copyImageToBuffer.regionCount; ++i)
        cmd.copyImageToBuffer.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                             const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    if (!pCopyImageToBufferInfo) return;
    VkBufferImageCopy regions[kMaxCopyRegions];
    uint32_t count = std::min(pCopyImageToBufferInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = pCopyImageToBufferInfo->pRegions[i];
        regions[i].bufferOffset      = src.bufferOffset;
        regions[i].bufferRowLength   = src.bufferRowLength;
        regions[i].bufferImageHeight = src.bufferImageHeight;
        regions[i].imageSubresource  = src.imageSubresource;
        regions[i].imageOffset       = src.imageOffset;
        regions[i].imageExtent       = src.imageExtent;
    }
    vkCmdCopyImageToBuffer(commandBuffer, pCopyImageToBufferInfo->srcImage,
                           pCopyImageToBufferInfo->srcImageLayout,
                           pCopyImageToBufferInfo->dstBuffer, count, regions);
}

void vkCmdUpdateBuffer(VkCommandBuffer commandBuffer,
                       VkBuffer dstBuffer, VkDeviceSize dstOffset,
                       VkDeviceSize dataSize, const void* pData) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pData || dataSize == 0) return;
    uint32_t clampedSize = static_cast<uint32_t>(std::min(dataSize, (VkDeviceSize)kMaxUpdateBufferBytes));
    DeferredCmd cmd{};
    cmd.tag = CmdTag::UpdateBuffer;
    cmd.updateBuffer.dstBuffer = dstBuffer;
    cmd.updateBuffer.dstOffset = dstOffset;
    cmd.updateBuffer.dataSize  = clampedSize;
    cmd.updateBuffer.dataBlobIndex = cb->storeInlineData(pData, clampedSize);
    cb->record(std::move(cmd));
}

void vkCmdFillBuffer(VkCommandBuffer commandBuffer,
                     VkBuffer dstBuffer, VkDeviceSize dstOffset,
                     VkDeviceSize size, uint32_t data) {
    auto* cb = toMv(commandBuffer);
    if (!cb) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::FillBuffer;
    cmd.fillBuffer.dstBuffer = dstBuffer;
    cmd.fillBuffer.dstOffset = dstOffset;
    cmd.fillBuffer.size      = size;
    cmd.fillBuffer.data      = data;
    cb->record(std::move(cmd));
}

void vkCmdClearColorImage(VkCommandBuffer commandBuffer,
                          VkImage image, VkImageLayout imageLayout,
                          const VkClearColorValue* pColor,
                          uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pColor || !pRanges) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::ClearColorImage;
    cmd.clearColorImage.image   = image;
    cmd.clearColorImage.layout  = imageLayout;
    cmd.clearColorImage.color   = *pColor;
    cmd.clearColorImage.rangeCount = std::min(rangeCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.clearColorImage.rangeCount; ++i)
        cmd.clearColorImage.ranges[i] = pRanges[i];
    cb->record(std::move(cmd));
}

void vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                                 VkImage image, VkImageLayout imageLayout,
                                 const VkClearDepthStencilValue* pDepthStencil,
                                 uint32_t rangeCount, const VkImageSubresourceRange* pRanges) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pDepthStencil || !pRanges) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::ClearDepthStencilImage;
    cmd.clearDepthStencilImage.image   = image;
    cmd.clearDepthStencilImage.layout  = imageLayout;
    cmd.clearDepthStencilImage.value   = *pDepthStencil;
    cmd.clearDepthStencilImage.rangeCount = std::min(rangeCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.clearDepthStencilImage.rangeCount; ++i)
        cmd.clearDepthStencilImage.ranges[i] = pRanges[i];
    cb->record(std::move(cmd));
}

void vkCmdClearAttachments(VkCommandBuffer commandBuffer,
                           uint32_t attachmentCount, const VkClearAttachment* pAttachments,
                           uint32_t rectCount, const VkClearRect* pRects) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pAttachments || !pRects) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::ClearAttachments;
    cmd.clearAttachments.attachmentCount = std::min(attachmentCount, kMaxClearAttachments);
    for (uint32_t i = 0; i < cmd.clearAttachments.attachmentCount; ++i)
        cmd.clearAttachments.attachments[i] = pAttachments[i];
    cmd.clearAttachments.rectCount = std::min(rectCount, kMaxClearRects);
    for (uint32_t i = 0; i < cmd.clearAttachments.rectCount; ++i)
        cmd.clearAttachments.rects[i] = pRects[i];
    cb->record(std::move(cmd));
}

void vkCmdResolveImage(VkCommandBuffer commandBuffer,
                       VkImage srcImage, VkImageLayout srcImageLayout,
                       VkImage dstImage, VkImageLayout dstImageLayout,
                       uint32_t regionCount, const VkImageResolve* pRegions) {
    auto* cb = toMv(commandBuffer);
    if (!cb || !pRegions) return;
    DeferredCmd cmd{};
    cmd.tag = CmdTag::ResolveImage;
    cmd.resolveImage.srcImage   = srcImage;
    cmd.resolveImage.srcLayout  = srcImageLayout;
    cmd.resolveImage.dstImage   = dstImage;
    cmd.resolveImage.dstLayout  = dstImageLayout;
    cmd.resolveImage.regionCount = std::min(regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < cmd.resolveImage.regionCount; ++i)
        cmd.resolveImage.regions[i] = pRegions[i];
    cb->record(std::move(cmd));
}

void vkCmdResolveImage2(VkCommandBuffer commandBuffer,
                        const VkResolveImageInfo2* pResolveImageInfo) {
    if (!pResolveImageInfo) return;
    VkImageResolve regions[kMaxCopyRegions];
    uint32_t count = std::min(pResolveImageInfo->regionCount, kMaxCopyRegions);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& src = pResolveImageInfo->pRegions[i];
        regions[i].srcSubresource = src.srcSubresource;
        regions[i].srcOffset      = src.srcOffset;
        regions[i].dstSubresource = src.dstSubresource;
        regions[i].dstOffset      = src.dstOffset;
        regions[i].extent         = src.extent;
    }
    vkCmdResolveImage(commandBuffer, pResolveImageInfo->srcImage, pResolveImageInfo->srcImageLayout,
                      pResolveImageInfo->dstImage, pResolveImageInfo->dstImageLayout, count, regions);
}

} // extern "C"
