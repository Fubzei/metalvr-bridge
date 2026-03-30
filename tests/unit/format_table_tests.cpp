#include <gtest/gtest.h>

#include "format_table/format_table.h"

namespace mvrvb {
namespace {

TEST(FormatTable, MapsCommonColorFormatsRoundTrip) {
    const MTLPixelFormat mtlFormat = vkFormatToMTL(VK_FORMAT_R8G8B8A8_UNORM);
    const FormatInfo& srgbInfo = getFormatInfo(VK_FORMAT_R8G8B8A8_SRGB);

    EXPECT_NE(mtlFormat, 0u);
    EXPECT_EQ(mtlFormatToVK(mtlFormat), VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_STREQ(srgbInfo.name, "R8G8B8A8_SRGB");
    EXPECT_TRUE(srgbInfo.isSRGB);
    EXPECT_TRUE(isFormatFilterable(VK_FORMAT_R8G8B8A8_SRGB));
    EXPECT_TRUE(isFormatRenderable(VK_FORMAT_R8G8B8A8_SRGB));
    EXPECT_TRUE(isFormatBlendable(VK_FORMAT_R8G8B8A8_SRGB));
}

TEST(FormatTable, ProvidesFallbacksForFormatsMetalCannotRepresentExactly) {
    EXPECT_EQ(vkFormatToMTL(VK_FORMAT_R8G8B8_UNORM), 0u);
    EXPECT_EQ(getFallbackFormat(VK_FORMAT_R8G8B8_UNORM), VK_FORMAT_R8G8B8A8_UNORM);
    EXPECT_FALSE(isFormatSupported(VK_FORMAT_R8G8B8_UNORM));

    EXPECT_EQ(getFallbackFormat(VK_FORMAT_D24_UNORM_S8_UINT), VK_FORMAT_D32_SFLOAT_S8_UINT);
    EXPECT_TRUE(formatHasDepth(VK_FORMAT_D24_UNORM_S8_UINT));
    EXPECT_TRUE(formatHasStencil(VK_FORMAT_D24_UNORM_S8_UINT));
    EXPECT_NE(depthOnlyView(VK_FORMAT_D24_UNORM_S8_UINT), 0u);
    EXPECT_NE(stencilOnlyView(VK_FORMAT_D24_UNORM_S8_UINT), 0u);
}

TEST(FormatTable, SupportsThreeComponentVertexFormatsAndPackedAliases) {
    EXPECT_NE(vkFormatToMTLVertex(VK_FORMAT_R32G32B32_SFLOAT), 0u);
    EXPECT_NE(vkFormatToMTLVertex(VK_FORMAT_R32G32B32_UINT), 0u);
    EXPECT_NE(vkFormatToMTLVertex(VK_FORMAT_R32G32B32_SINT), 0u);
    EXPECT_NE(vkFormatToMTLVertex(VK_FORMAT_A8B8G8R8_UNORM_PACK32), 0u);
}

TEST(FormatTable, ReturnsSafeDefaultsForUnknownFormatsAndIndexTypes) {
    const auto unknownFormat = static_cast<VkFormat>(9999);
    const FormatInfo& info = getFormatInfo(unknownFormat);

    EXPECT_EQ(vkFormatToMTL(unknownFormat), 0u);
    EXPECT_EQ(getFallbackFormat(unknownFormat), VK_FORMAT_UNDEFINED);
    EXPECT_EQ(info.vkFormat, VK_FORMAT_UNDEFINED);
    EXPECT_STREQ(info.name, "UNKNOWN");

    EXPECT_EQ(vkIndexTypeToMTL(VK_INDEX_TYPE_UINT16), static_cast<MTLIndexType>(0u));
    EXPECT_EQ(vkIndexTypeToMTL(VK_INDEX_TYPE_UINT32), static_cast<MTLIndexType>(1u));
}

}  // namespace
}  // namespace mvrvb
