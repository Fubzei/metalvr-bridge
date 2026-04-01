#include "transfer_utils.h"

#include "../format_table/format_table.h"

#include <algorithm>

namespace mvrvb {

uint32_t mipExtent(uint32_t baseExtent, uint32_t mipLevel) {
    if (mipLevel >= 32u) return 1u;
    return std::max(1u, baseExtent >> mipLevel);
}

uint32_t resolveRangeLevelCount(uint32_t mipLevels,
                                const VkImageSubresourceRange& range) {
    if (range.levelCount == VK_REMAINING_MIP_LEVELS) {
        return mipLevels > range.baseMipLevel
            ? mipLevels - range.baseMipLevel
            : 0u;
    }
    return range.levelCount;
}

uint32_t resolveRangeLayerCount(VkImageType imageType,
                                uint32_t arrayLayers,
                                uint32_t depth,
                                const VkImageSubresourceRange& range,
                                uint32_t mipLevel) {
    if (imageType == VK_IMAGE_TYPE_3D) {
        return mipExtent(depth, mipLevel);
    }
    if (range.layerCount == VK_REMAINING_ARRAY_LAYERS) {
        return arrayLayers > range.baseArrayLayer
            ? arrayLayers - range.baseArrayLayer
            : 0u;
    }
    return range.layerCount;
}

bool resolveTransferSlices(VkImageType imageType,
                           const VkImageSubresourceLayers& subresource,
                           const VkOffset3D offsets[2],
                           TransferSliceResolution* out) {
    if (!offsets || !out) return false;

    if (imageType == VK_IMAGE_TYPE_3D) {
        const int64_t delta = static_cast<int64_t>(offsets[1].z) -
                              static_cast<int64_t>(offsets[0].z);
        out->count = static_cast<uint32_t>(delta >= 0 ? delta : -delta);
        out->start = offsets[0].z;
        out->step = delta >= 0 ? 1 : -1;
    } else {
        out->count = subresource.layerCount;
        out->start = static_cast<int32_t>(subresource.baseArrayLayer);
        out->step = 1;
    }

    return out->count > 0;
}

uint64_t resolvedBufferRangeSize(uint64_t bufferSize,
                                 VkDeviceSize dstOffset,
                                 VkDeviceSize size) {
    if (dstOffset >= bufferSize) return 0;

    const uint64_t remaining = bufferSize - dstOffset;
    if (size == VK_WHOLE_SIZE) return remaining;
    return std::min<uint64_t>(size, remaining);
}

bool isRepeatedBytePattern(uint32_t pattern) {
    const uint8_t byteValue = static_cast<uint8_t>(pattern & 0xFF);
    return pattern == (uint32_t(byteValue) * 0x01010101u);
}

TransferPipelineKind blitPipelineKindForFormat(const FormatInfo& info) {
    if (info.isUInt) return TransferPipelineKind::BlitUInt;
    if (info.isSInt) return TransferPipelineKind::BlitSInt;
    return TransferPipelineKind::BlitFloat;
}

TransferPipelineKind resolvePipelineKindForFormat(const FormatInfo& info) {
    if (info.isUInt) return TransferPipelineKind::ResolveUInt;
    if (info.isSInt) return TransferPipelineKind::ResolveSInt;
    return TransferPipelineKind::ResolveFloat;
}

bool isIntegralFormat(const FormatInfo& info) {
    return info.isUInt || info.isSInt;
}

bool areTransferColorClassesCompatible(const FormatInfo& srcInfo,
                                       const FormatInfo& dstInfo) {
    if (srcInfo.isUInt) return dstInfo.isUInt;
    if (srcInfo.isSInt) return dstInfo.isSInt;
    return !dstInfo.isUInt && !dstInfo.isSInt;
}

bool buildTransferRegionGeometry(const VkOffset3D srcOffsets[2],
                                 const VkOffset3D dstOffsets[2],
                                 uint32_t srcWidth,
                                 uint32_t srcHeight,
                                 uint32_t sampleCount,
                                 TransferRegionUniformData* uniforms,
                                 TransferViewport* viewport,
                                 TransferScissorRect* scissor) {
    if (!srcOffsets || !dstOffsets || !uniforms || !viewport || !scissor) {
        return false;
    }

    const int64_t dstDeltaX = static_cast<int64_t>(dstOffsets[1].x) -
                              static_cast<int64_t>(dstOffsets[0].x);
    const int64_t dstDeltaY = static_cast<int64_t>(dstOffsets[1].y) -
                              static_cast<int64_t>(dstOffsets[0].y);
    const uint32_t dstWidth = static_cast<uint32_t>(dstDeltaX >= 0 ? dstDeltaX : -dstDeltaX);
    const uint32_t dstHeight = static_cast<uint32_t>(dstDeltaY >= 0 ? dstDeltaY : -dstDeltaY);
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

    scissor->x = static_cast<uint32_t>(std::max(0, dstMinX));
    scissor->y = static_cast<uint32_t>(std::max(0, dstMinY));
    scissor->width = dstWidth;
    scissor->height = dstHeight;
    return true;
}

}  // namespace mvrvb
