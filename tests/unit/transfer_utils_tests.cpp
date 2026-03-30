#include <gtest/gtest.h>

#include "commands/transfer_utils.h"

namespace mvrvb {
namespace {

TEST(TransferUtils, MipExtentShrinksAndClampsToOne) {
    EXPECT_EQ(mipExtent(64u, 0u), 64u);
    EXPECT_EQ(mipExtent(64u, 3u), 8u);
    EXPECT_EQ(mipExtent(3u, 4u), 1u);
    EXPECT_EQ(mipExtent(3u, 32u), 1u);
}

TEST(TransferUtils, ResolvesRemainingMipLevelsFromBaseLevel) {
    VkImageSubresourceRange range{};
    range.baseMipLevel = 2u;
    range.levelCount = VK_REMAINING_MIP_LEVELS;

    EXPECT_EQ(resolveRangeLevelCount(6u, range), 4u);

    range.baseMipLevel = 6u;
    EXPECT_EQ(resolveRangeLevelCount(6u, range), 0u);
}

TEST(TransferUtils, ResolvesRemainingArrayLayersForArrayImages) {
    VkImageSubresourceRange range{};
    range.baseArrayLayer = 3u;
    range.layerCount = VK_REMAINING_ARRAY_LAYERS;

    EXPECT_EQ(resolveRangeLayerCount(VK_IMAGE_TYPE_2D, 8u, 1u, range, 0u), 5u);

    range.baseArrayLayer = 8u;
    EXPECT_EQ(resolveRangeLayerCount(VK_IMAGE_TYPE_2D, 8u, 1u, range, 0u), 0u);
}

TEST(TransferUtils, Resolves3DLayerCountFromMipDepth) {
    VkImageSubresourceRange range{};
    range.layerCount = 99u;

    EXPECT_EQ(resolveRangeLayerCount(VK_IMAGE_TYPE_3D, 1u, 16u, range, 0u), 16u);
    EXPECT_EQ(resolveRangeLayerCount(VK_IMAGE_TYPE_3D, 1u, 16u, range, 2u), 4u);
    EXPECT_EQ(resolveRangeLayerCount(VK_IMAGE_TYPE_3D, 1u, 3u, range, 4u), 1u);
}

TEST(TransferUtils, ResolvesArrayTransferSlicesFromLayerInfo) {
    VkImageSubresourceLayers subresource{};
    subresource.baseArrayLayer = 5u;
    subresource.layerCount = 3u;
    const VkOffset3D offsets[2]{{0, 0, 0}, {8, 8, 1}};

    TransferSliceResolution resolution{};
    ASSERT_TRUE(resolveTransferSlices(
        VK_IMAGE_TYPE_2D, subresource, offsets, &resolution));

    EXPECT_EQ(resolution.count, 3u);
    EXPECT_EQ(resolution.start, 5);
    EXPECT_EQ(resolution.step, 1);
}

TEST(TransferUtils, ResolvesForwardAndReverse3DSlices) {
    VkImageSubresourceLayers subresource{};
    subresource.layerCount = 1u;

    const VkOffset3D forward[2]{{0, 0, 2}, {8, 8, 6}};
    TransferSliceResolution forwardResolution{};
    ASSERT_TRUE(resolveTransferSlices(
        VK_IMAGE_TYPE_3D, subresource, forward, &forwardResolution));
    EXPECT_EQ(forwardResolution.count, 4u);
    EXPECT_EQ(forwardResolution.start, 2);
    EXPECT_EQ(forwardResolution.step, 1);

    const VkOffset3D reverse[2]{{0, 0, 6}, {8, 8, 2}};
    TransferSliceResolution reverseResolution{};
    ASSERT_TRUE(resolveTransferSlices(
        VK_IMAGE_TYPE_3D, subresource, reverse, &reverseResolution));
    EXPECT_EQ(reverseResolution.count, 4u);
    EXPECT_EQ(reverseResolution.start, 6);
    EXPECT_EQ(reverseResolution.step, -1);
}

TEST(TransferUtils, RejectsZeroSliceTransfers) {
    VkImageSubresourceLayers subresource{};
    subresource.layerCount = 0u;
    const VkOffset3D offsets[2]{{0, 0, 0}, {4, 4, 0}};

    TransferSliceResolution resolution{};
    EXPECT_FALSE(resolveTransferSlices(
        VK_IMAGE_TYPE_2D, subresource, offsets, &resolution));
}

TEST(TransferUtils, ResolvesWholeAndClampedBufferRanges) {
    EXPECT_EQ(resolvedBufferRangeSize(1024u, 128u, VK_WHOLE_SIZE), 896u);
    EXPECT_EQ(resolvedBufferRangeSize(1024u, 128u, 512u), 512u);
    EXPECT_EQ(resolvedBufferRangeSize(1024u, 900u, 512u), 124u);
    EXPECT_EQ(resolvedBufferRangeSize(1024u, 1024u, 1u), 0u);
}

TEST(TransferUtils, DetectsRepeatedBytePatterns) {
    EXPECT_TRUE(isRepeatedBytePattern(0x00000000u));
    EXPECT_TRUE(isRepeatedBytePattern(0xFFFFFFFFu));
    EXPECT_TRUE(isRepeatedBytePattern(0x7A7A7A7Au));
    EXPECT_FALSE(isRepeatedBytePattern(0xAABBCCDDu));
}

TEST(TransferUtils, BuildsTransferRegionGeometryForPositiveDestinationExtents) {
    const VkOffset3D srcOffsets[2]{{4, 8, 0}, {20, 24, 1}};
    const VkOffset3D dstOffsets[2]{{10, 12, 0}, {26, 28, 1}};

    TransferRegionUniformData uniforms{};
    TransferViewport viewport{};
    TransferScissorRect scissor{};
    ASSERT_TRUE(buildTransferRegionGeometry(
        srcOffsets, dstOffsets, 128u, 64u, 4u, &uniforms, &viewport, &scissor));

    EXPECT_FLOAT_EQ(uniforms.dstOrigin[0], 10.0f);
    EXPECT_FLOAT_EQ(uniforms.dstOrigin[1], 12.0f);
    EXPECT_FLOAT_EQ(uniforms.dstExtent[0], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.dstExtent[1], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.srcOrigin[0], 4.0f);
    EXPECT_FLOAT_EQ(uniforms.srcOrigin[1], 8.0f);
    EXPECT_FLOAT_EQ(uniforms.srcExtent[0], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.srcExtent[1], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.srcTextureSize[0], 128.0f);
    EXPECT_FLOAT_EQ(uniforms.srcTextureSize[1], 64.0f);
    EXPECT_EQ(uniforms.sampleCount, 4u);

    EXPECT_DOUBLE_EQ(viewport.originX, 10.0);
    EXPECT_DOUBLE_EQ(viewport.originY, 12.0);
    EXPECT_DOUBLE_EQ(viewport.width, 16.0);
    EXPECT_DOUBLE_EQ(viewport.height, 16.0);
    EXPECT_DOUBLE_EQ(viewport.znear, 0.0);
    EXPECT_DOUBLE_EQ(viewport.zfar, 1.0);

    EXPECT_EQ(scissor.x, 10u);
    EXPECT_EQ(scissor.y, 12u);
    EXPECT_EQ(scissor.width, 16u);
    EXPECT_EQ(scissor.height, 16u);
}

TEST(TransferUtils, BuildsTransferRegionGeometryForReversedSourceAndNegativeDestination) {
    const VkOffset3D srcOffsets[2]{{24, 20, 0}, {8, 4, 1}};
    const VkOffset3D dstOffsets[2]{{12, 6, 0}, {-4, -10, 1}};

    TransferRegionUniformData uniforms{};
    TransferViewport viewport{};
    TransferScissorRect scissor{};
    ASSERT_TRUE(buildTransferRegionGeometry(
        srcOffsets, dstOffsets, 64u, 32u, 1u, &uniforms, &viewport, &scissor));

    EXPECT_FLOAT_EQ(uniforms.dstOrigin[0], -4.0f);
    EXPECT_FLOAT_EQ(uniforms.dstOrigin[1], -10.0f);
    EXPECT_FLOAT_EQ(uniforms.dstExtent[0], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.dstExtent[1], 16.0f);
    EXPECT_FLOAT_EQ(uniforms.srcOrigin[0], 24.0f);
    EXPECT_FLOAT_EQ(uniforms.srcOrigin[1], 20.0f);
    EXPECT_FLOAT_EQ(uniforms.srcExtent[0], -16.0f);
    EXPECT_FLOAT_EQ(uniforms.srcExtent[1], -16.0f);

    EXPECT_DOUBLE_EQ(viewport.originX, -4.0);
    EXPECT_DOUBLE_EQ(viewport.originY, -10.0);
    EXPECT_DOUBLE_EQ(viewport.width, 16.0);
    EXPECT_DOUBLE_EQ(viewport.height, 16.0);

    EXPECT_EQ(scissor.x, 0u);
    EXPECT_EQ(scissor.y, 0u);
    EXPECT_EQ(scissor.width, 16u);
    EXPECT_EQ(scissor.height, 16u);
}

TEST(TransferUtils, RejectsZeroSizedTransferRegionGeometry) {
    const VkOffset3D srcOffsets[2]{{0, 0, 0}, {4, 4, 1}};
    const VkOffset3D dstOffsets[2]{{3, 7, 0}, {3, 9, 1}};

    TransferRegionUniformData uniforms{};
    TransferViewport viewport{};
    TransferScissorRect scissor{};
    EXPECT_FALSE(buildTransferRegionGeometry(
        srcOffsets, dstOffsets, 16u, 16u, 1u, &uniforms, &viewport, &scissor));
}

}  // namespace
}  // namespace mvrvb
