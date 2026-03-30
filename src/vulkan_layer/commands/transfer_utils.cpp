#include "transfer_utils.h"

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

}  // namespace mvrvb
