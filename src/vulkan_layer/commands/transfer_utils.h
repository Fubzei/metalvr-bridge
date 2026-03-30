#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace mvrvb {

struct TransferSliceResolution {
    uint32_t count{0};
    int32_t start{0};
    int32_t step{1};
};

struct TransferRegionUniformData {
    float dstOrigin[2]{};
    float dstExtent[2]{};
    float srcOrigin[2]{};
    float srcExtent[2]{};
    float srcTextureSize[2]{};
    uint32_t sampleCount{1};
};

struct TransferViewport {
    double originX{0.0};
    double originY{0.0};
    double width{0.0};
    double height{0.0};
    double znear{0.0};
    double zfar{1.0};
};

struct TransferScissorRect {
    uint32_t x{0};
    uint32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
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

bool buildTransferRegionGeometry(const VkOffset3D srcOffsets[2],
                                 const VkOffset3D dstOffsets[2],
                                 uint32_t srcWidth,
                                 uint32_t srcHeight,
                                 uint32_t sampleCount,
                                 TransferRegionUniformData* uniforms,
                                 TransferViewport* viewport,
                                 TransferScissorRect* scissor);

}  // namespace mvrvb
