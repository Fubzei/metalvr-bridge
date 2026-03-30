#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace mvrvb {

struct TransferSliceResolution {
    uint32_t count{0};
    int32_t start{0};
    int32_t step{1};
};

uint32_t mipExtent(uint32_t baseExtent, uint32_t mipLevel);

uint32_t resolveRangeLevelCount(uint32_t mipLevels,
                                const VkImageSubresourceRange& range);

uint32_t resolveRangeLayerCount(VkImageType imageType,
                                uint32_t arrayLayers,
                                uint32_t depth,
                                const VkImageSubresourceRange& range,
                                uint32_t mipLevel);

bool resolveTransferSlices(VkImageType imageType,
                           const VkImageSubresourceLayers& subresource,
                           const VkOffset3D offsets[2],
                           TransferSliceResolution* out);

uint64_t resolvedBufferRangeSize(uint64_t bufferSize,
                                 VkDeviceSize dstOffset,
                                 VkDeviceSize size);

bool isRepeatedBytePattern(uint32_t pattern);

}  // namespace mvrvb
