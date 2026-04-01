/**
 * @file format_table.cpp
 * @brief Complete VkFormat ↔ MTLPixelFormat mapping table — Phase 2 rewrite.
 *
 * Changes from Phase 1:
 *   - O(1) flat-array lookup for core VkFormat values (0–186)
 *   - Reverse MTLPixelFormat → VkFormat table (flat array indexed by MTL enum)
 *   - Complete ASTC coverage (4×4 through 12×12, UNORM + sRGB)
 *   - A8B8G8R8_*_PACK32 variants mapped to RGBA8 equivalents
 *   - R32G32B32 formats mapped to Invalid with R32G32B32A32 fallbacks
 *   - Capability query functions for filtering, rendering, blending
 *   - D24_UNORM_S8_UINT always falls back to D32_SFLOAT_S8_UINT on Apple Silicon
 *   - Float3, Int3, UInt3 vertex format mappings
 *
 * Metal pixel format enum values are hard-coded numerically so this file
 * compiles in pure C++ translation units (no Metal.h dependency).
 *
 * Source references:
 *   vulkan_core.h   — VkFormat enum
 *   MTLPixelFormat   — Metal framework headers (macOS 14 / Metal 3)
 *   MoltenVK         — cross-reference for correctness
 */

#include "format_table.h"
#include <cstring>
#include <array>
#include <unordered_map>

namespace mvrvb {

// ═══════════════════════════════════════════════════════════════════════════════
//  MTLPixelFormat numeric constants (from Metal.framework, macOS 14 / Metal 3)
// ═══════════════════════════════════════════════════════════════════════════════
namespace MTL {
    static constexpr MTLPixelFormat Invalid              = 0;
    // 8-bit
    static constexpr MTLPixelFormat A8Unorm              = 1;
    static constexpr MTLPixelFormat R8Unorm              = 10;
    static constexpr MTLPixelFormat R8Unorm_sRGB         = 11;
    static constexpr MTLPixelFormat R8Snorm              = 12;
    static constexpr MTLPixelFormat R8Uint               = 13;
    static constexpr MTLPixelFormat R8Sint               = 14;
    // 16-bit
    static constexpr MTLPixelFormat R16Unorm             = 20;
    static constexpr MTLPixelFormat R16Snorm             = 22;
    static constexpr MTLPixelFormat R16Uint              = 23;
    static constexpr MTLPixelFormat R16Sint              = 24;
    static constexpr MTLPixelFormat R16Float             = 25;
    static constexpr MTLPixelFormat RG8Unorm             = 30;
    static constexpr MTLPixelFormat RG8Unorm_sRGB        = 31;
    static constexpr MTLPixelFormat RG8Snorm             = 32;
    static constexpr MTLPixelFormat RG8Uint              = 33;
    static constexpr MTLPixelFormat RG8Sint              = 34;
    // Packed 16-bit
    static constexpr MTLPixelFormat B5G6R5Unorm          = 40;
    static constexpr MTLPixelFormat A1BGR5Unorm          = 41;
    static constexpr MTLPixelFormat ABGR4Unorm           = 42;
    static constexpr MTLPixelFormat BGR5A1Unorm          = 43;
    // 32-bit single-channel
    static constexpr MTLPixelFormat R32Uint              = 53;
    static constexpr MTLPixelFormat R32Sint              = 54;
    static constexpr MTLPixelFormat R32Float             = 55;
    // 32-bit dual-channel
    static constexpr MTLPixelFormat RG16Unorm            = 60;
    static constexpr MTLPixelFormat RG16Snorm            = 62;
    static constexpr MTLPixelFormat RG16Uint             = 63;
    static constexpr MTLPixelFormat RG16Sint             = 64;
    static constexpr MTLPixelFormat RG16Float            = 65;
    // 32-bit RGBA
    static constexpr MTLPixelFormat RGBA8Unorm           = 70;
    static constexpr MTLPixelFormat RGBA8Unorm_sRGB      = 71;
    static constexpr MTLPixelFormat RGBA8Snorm           = 72;
    static constexpr MTLPixelFormat RGBA8Uint            = 73;
    static constexpr MTLPixelFormat RGBA8Sint            = 74;
    static constexpr MTLPixelFormat BGRA8Unorm           = 80;
    static constexpr MTLPixelFormat BGRA8Unorm_sRGB      = 81;
    // Packed 32-bit
    static constexpr MTLPixelFormat RGB10A2Unorm         = 90;
    static constexpr MTLPixelFormat RGB10A2Uint          = 91;
    static constexpr MTLPixelFormat RG11B10Float         = 92;
    static constexpr MTLPixelFormat RGB9E5Float          = 93;
    static constexpr MTLPixelFormat BGR10A2Unorm         = 94;
    // 64-bit
    static constexpr MTLPixelFormat RG32Uint             = 103;
    static constexpr MTLPixelFormat RG32Sint             = 104;
    static constexpr MTLPixelFormat RG32Float            = 105;
    static constexpr MTLPixelFormat RGBA16Unorm          = 110;
    static constexpr MTLPixelFormat RGBA16Snorm          = 112;
    static constexpr MTLPixelFormat RGBA16Uint           = 113;
    static constexpr MTLPixelFormat RGBA16Sint           = 114;
    static constexpr MTLPixelFormat RGBA16Float          = 115;
    // 128-bit
    static constexpr MTLPixelFormat RGBA32Uint           = 123;
    static constexpr MTLPixelFormat RGBA32Sint           = 124;
    static constexpr MTLPixelFormat RGBA32Float          = 125;
    // BC compressed (macOS — supported on both Intel and Apple Silicon)
    static constexpr MTLPixelFormat BC1_RGBA             = 130;
    static constexpr MTLPixelFormat BC1_RGBA_sRGB        = 131;
    static constexpr MTLPixelFormat BC2_RGBA             = 132;
    static constexpr MTLPixelFormat BC2_RGBA_sRGB        = 133;
    static constexpr MTLPixelFormat BC3_RGBA             = 134;
    static constexpr MTLPixelFormat BC3_RGBA_sRGB        = 135;
    static constexpr MTLPixelFormat BC4_RUnorm           = 140;
    static constexpr MTLPixelFormat BC4_RSnorm           = 141;
    static constexpr MTLPixelFormat BC5_RGUnorm          = 142;
    static constexpr MTLPixelFormat BC5_RGSnorm          = 143;
    static constexpr MTLPixelFormat BC6H_RGBFloat        = 150;
    static constexpr MTLPixelFormat BC6H_RGBUfloat       = 151;
    static constexpr MTLPixelFormat BC7_RGBAUnorm        = 152;
    static constexpr MTLPixelFormat BC7_RGBAUnorm_sRGB   = 153;
    // ETC2 / EAC (Apple Silicon — not available on Intel Macs)
    static constexpr MTLPixelFormat EAC_R11Unorm         = 170;
    static constexpr MTLPixelFormat EAC_R11Snorm         = 172;
    static constexpr MTLPixelFormat EAC_RG11Unorm        = 174;
    static constexpr MTLPixelFormat EAC_RG11Snorm        = 176;
    static constexpr MTLPixelFormat EAC_RGBA8            = 178;
    static constexpr MTLPixelFormat EAC_RGBA8_sRGB       = 179;
    static constexpr MTLPixelFormat ETC2_RGB8            = 180;
    static constexpr MTLPixelFormat ETC2_RGB8_sRGB       = 181;
    static constexpr MTLPixelFormat ETC2_RGB8A1          = 182;
    static constexpr MTLPixelFormat ETC2_RGB8A1_sRGB     = 183;
    // ASTC LDR (Apple Silicon natively)
    static constexpr MTLPixelFormat ASTC_4x4_sRGB        = 186;
    static constexpr MTLPixelFormat ASTC_5x4_sRGB        = 187;
    static constexpr MTLPixelFormat ASTC_5x5_sRGB        = 188;
    static constexpr MTLPixelFormat ASTC_6x5_sRGB        = 189;
    static constexpr MTLPixelFormat ASTC_6x6_sRGB        = 190;
    static constexpr MTLPixelFormat ASTC_8x5_sRGB        = 192;
    static constexpr MTLPixelFormat ASTC_8x6_sRGB        = 193;
    static constexpr MTLPixelFormat ASTC_8x8_sRGB        = 194;
    static constexpr MTLPixelFormat ASTC_10x5_sRGB       = 195;
    static constexpr MTLPixelFormat ASTC_10x6_sRGB       = 196;
    static constexpr MTLPixelFormat ASTC_10x8_sRGB       = 197;
    static constexpr MTLPixelFormat ASTC_10x10_sRGB      = 198;
    static constexpr MTLPixelFormat ASTC_12x10_sRGB      = 199;
    static constexpr MTLPixelFormat ASTC_12x12_sRGB      = 200;
    static constexpr MTLPixelFormat ASTC_4x4_LDR         = 204;
    static constexpr MTLPixelFormat ASTC_5x4_LDR         = 205;
    static constexpr MTLPixelFormat ASTC_5x5_LDR         = 206;
    static constexpr MTLPixelFormat ASTC_6x5_LDR         = 207;
    static constexpr MTLPixelFormat ASTC_6x6_LDR         = 208;
    static constexpr MTLPixelFormat ASTC_8x5_LDR         = 210;
    static constexpr MTLPixelFormat ASTC_8x6_LDR         = 211;
    static constexpr MTLPixelFormat ASTC_8x8_LDR         = 212;
    static constexpr MTLPixelFormat ASTC_10x5_LDR        = 213;
    static constexpr MTLPixelFormat ASTC_10x6_LDR        = 214;
    static constexpr MTLPixelFormat ASTC_10x8_LDR        = 215;
    static constexpr MTLPixelFormat ASTC_10x10_LDR       = 216;
    static constexpr MTLPixelFormat ASTC_12x10_LDR       = 217;
    static constexpr MTLPixelFormat ASTC_12x12_LDR       = 218;
    // Depth / stencil
    static constexpr MTLPixelFormat Depth16Unorm           = 250;
    static constexpr MTLPixelFormat Depth32Float           = 252;
    static constexpr MTLPixelFormat Stencil8               = 253;
    static constexpr MTLPixelFormat Depth24Unorm_Stencil8  = 255; // macOS Intel only
    static constexpr MTLPixelFormat Depth32Float_Stencil8  = 260;
    static constexpr MTLPixelFormat X32_Stencil8           = 261;
    static constexpr MTLPixelFormat X24_Stencil8           = 262;
} // namespace MTL

// ═══════════════════════════════════════════════════════════════════════════════
//  MTLVertexFormat numeric constants
// ═══════════════════════════════════════════════════════════════════════════════
namespace MTLV {
    static constexpr MTLVertexFormat Invalid              = 0;
    static constexpr MTLVertexFormat UChar2               = 45;
    static constexpr MTLVertexFormat UChar3               = 47; // Metal 2.1+
    static constexpr MTLVertexFormat UChar4               = 46;
    static constexpr MTLVertexFormat Char2                = 48;
    static constexpr MTLVertexFormat Char3                = 50; // Metal 2.1+
    static constexpr MTLVertexFormat Char4                = 49;
    static constexpr MTLVertexFormat UChar2Normalized     = 51;
    static constexpr MTLVertexFormat UChar3Normalized     = 53; // Metal 2.1+
    static constexpr MTLVertexFormat UChar4Normalized     = 52;
    static constexpr MTLVertexFormat Char2Normalized      = 54;
    static constexpr MTLVertexFormat Char3Normalized      = 56; // Metal 2.1+
    static constexpr MTLVertexFormat Char4Normalized      = 55;
    static constexpr MTLVertexFormat UShort2              = 57;
    static constexpr MTLVertexFormat UShort3              = 59; // Metal 2.1+
    static constexpr MTLVertexFormat UShort4              = 58;
    static constexpr MTLVertexFormat Short2               = 60;
    static constexpr MTLVertexFormat Short3               = 62; // Metal 2.1+
    static constexpr MTLVertexFormat Short4               = 61;
    static constexpr MTLVertexFormat UShort2Normalized    = 63;
    static constexpr MTLVertexFormat UShort3Normalized    = 65; // Metal 2.1+
    static constexpr MTLVertexFormat UShort4Normalized    = 64;
    static constexpr MTLVertexFormat Short2Normalized     = 66;
    static constexpr MTLVertexFormat Short3Normalized     = 68; // Metal 2.1+
    static constexpr MTLVertexFormat Short4Normalized     = 67;
    static constexpr MTLVertexFormat Half2                = 69;
    static constexpr MTLVertexFormat Half3                = 71; // Metal 2.1+ — NOT the same as Float!
    static constexpr MTLVertexFormat Half4                = 70;
    static constexpr MTLVertexFormat Float                = 28; // MTLVertexFormatFloat = 28
    static constexpr MTLVertexFormat Float2               = 29;
    static constexpr MTLVertexFormat Float3               = 30;
    static constexpr MTLVertexFormat Float4               = 31;
    static constexpr MTLVertexFormat Int                  = 32;
    static constexpr MTLVertexFormat Int2                 = 33;
    static constexpr MTLVertexFormat Int3                 = 34;
    static constexpr MTLVertexFormat Int4                 = 35;
    static constexpr MTLVertexFormat UInt                 = 36;
    static constexpr MTLVertexFormat UInt2                = 37;
    static constexpr MTLVertexFormat UInt3                = 38;
    static constexpr MTLVertexFormat UInt4                = 39;
    static constexpr MTLVertexFormat UInt1010102Normalized = 90;
    static constexpr MTLVertexFormat UChar                = 95;
    static constexpr MTLVertexFormat Char                 = 96;
    static constexpr MTLVertexFormat UCharNormalized      = 97;
    static constexpr MTLVertexFormat CharNormalized       = 98;
    static constexpr MTLVertexFormat UShort               = 99;
    static constexpr MTLVertexFormat Short                = 100;
    static constexpr MTLVertexFormat UShortNormalized     = 101;
    static constexpr MTLVertexFormat ShortNormalized      = 102;
    static constexpr MTLVertexFormat Half                 = 103;
} // namespace MTLV

// ═══════════════════════════════════════════════════════════════════════════════
//  VkFormat enum values (matching vulkan_core.h from Vulkan 1.3)
// ═══════════════════════════════════════════════════════════════════════════════
namespace VK {
    static constexpr VkFormat UNDEFINED                       = static_cast<VkFormat>(0);
    static constexpr VkFormat R4G4_UNORM_PACK8                = static_cast<VkFormat>(1);
    static constexpr VkFormat R4G4B4A4_UNORM_PACK16           = static_cast<VkFormat>(2);
    static constexpr VkFormat B4G4R4A4_UNORM_PACK16           = static_cast<VkFormat>(3);
    static constexpr VkFormat R5G6B5_UNORM_PACK16             = static_cast<VkFormat>(4);
    static constexpr VkFormat B5G6R5_UNORM_PACK16             = static_cast<VkFormat>(5);
    static constexpr VkFormat R5G5B5A1_UNORM_PACK16           = static_cast<VkFormat>(6);
    static constexpr VkFormat B5G5R5A1_UNORM_PACK16           = static_cast<VkFormat>(7);
    static constexpr VkFormat A1R5G5B5_UNORM_PACK16           = static_cast<VkFormat>(8);
    static constexpr VkFormat R8_UNORM                        = static_cast<VkFormat>(9);
    static constexpr VkFormat R8_SNORM                        = static_cast<VkFormat>(10);
    static constexpr VkFormat R8_USCALED                      = static_cast<VkFormat>(11);
    static constexpr VkFormat R8_SSCALED                      = static_cast<VkFormat>(12);
    static constexpr VkFormat R8_UINT                         = static_cast<VkFormat>(13);
    static constexpr VkFormat R8_SINT                         = static_cast<VkFormat>(14);
    static constexpr VkFormat R8_SRGB                         = static_cast<VkFormat>(15);
    static constexpr VkFormat R8G8_UNORM                      = static_cast<VkFormat>(16);
    static constexpr VkFormat R8G8_SNORM                      = static_cast<VkFormat>(17);
    static constexpr VkFormat R8G8_USCALED                    = static_cast<VkFormat>(18);
    static constexpr VkFormat R8G8_SSCALED                    = static_cast<VkFormat>(19);
    static constexpr VkFormat R8G8_UINT                       = static_cast<VkFormat>(20);
    static constexpr VkFormat R8G8_SINT                       = static_cast<VkFormat>(21);
    static constexpr VkFormat R8G8_SRGB                       = static_cast<VkFormat>(22);
    static constexpr VkFormat R8G8B8_UNORM                    = static_cast<VkFormat>(23);
    static constexpr VkFormat R8G8B8_SNORM                    = static_cast<VkFormat>(24);
    static constexpr VkFormat R8G8B8_USCALED                  = static_cast<VkFormat>(25);
    static constexpr VkFormat R8G8B8_SSCALED                  = static_cast<VkFormat>(26);
    static constexpr VkFormat R8G8B8_UINT                     = static_cast<VkFormat>(27);
    static constexpr VkFormat R8G8B8_SINT                     = static_cast<VkFormat>(28);
    static constexpr VkFormat R8G8B8_SRGB                     = static_cast<VkFormat>(29);
    static constexpr VkFormat B8G8R8_UNORM                    = static_cast<VkFormat>(30);
    static constexpr VkFormat B8G8R8_SNORM                    = static_cast<VkFormat>(31);
    static constexpr VkFormat B8G8R8_USCALED                  = static_cast<VkFormat>(32);
    static constexpr VkFormat B8G8R8_SSCALED                  = static_cast<VkFormat>(33);
    static constexpr VkFormat B8G8R8_UINT                     = static_cast<VkFormat>(34);
    static constexpr VkFormat B8G8R8_SINT                     = static_cast<VkFormat>(35);
    static constexpr VkFormat B8G8R8_SRGB                     = static_cast<VkFormat>(36);
    static constexpr VkFormat R8G8B8A8_UNORM                  = static_cast<VkFormat>(37);
    static constexpr VkFormat R8G8B8A8_SNORM                  = static_cast<VkFormat>(38);
    static constexpr VkFormat R8G8B8A8_USCALED                = static_cast<VkFormat>(39);
    static constexpr VkFormat R8G8B8A8_SSCALED                = static_cast<VkFormat>(40);
    static constexpr VkFormat R8G8B8A8_UINT                   = static_cast<VkFormat>(41);
    static constexpr VkFormat R8G8B8A8_SINT                   = static_cast<VkFormat>(42);
    static constexpr VkFormat R8G8B8A8_SRGB                   = static_cast<VkFormat>(43);
    static constexpr VkFormat B8G8R8A8_UNORM                  = static_cast<VkFormat>(44);
    static constexpr VkFormat B8G8R8A8_SNORM                  = static_cast<VkFormat>(45);
    static constexpr VkFormat B8G8R8A8_USCALED                = static_cast<VkFormat>(46);
    static constexpr VkFormat B8G8R8A8_SSCALED                = static_cast<VkFormat>(47);
    static constexpr VkFormat B8G8R8A8_UINT                   = static_cast<VkFormat>(48);
    static constexpr VkFormat B8G8R8A8_SINT                   = static_cast<VkFormat>(49);
    static constexpr VkFormat B8G8R8A8_SRGB                   = static_cast<VkFormat>(50);
    static constexpr VkFormat A8B8G8R8_UNORM_PACK32           = static_cast<VkFormat>(51);
    static constexpr VkFormat A8B8G8R8_SNORM_PACK32           = static_cast<VkFormat>(52);
    static constexpr VkFormat A8B8G8R8_USCALED_PACK32         = static_cast<VkFormat>(53);
    static constexpr VkFormat A8B8G8R8_SSCALED_PACK32         = static_cast<VkFormat>(54);
    static constexpr VkFormat A8B8G8R8_UINT_PACK32            = static_cast<VkFormat>(55);
    static constexpr VkFormat A8B8G8R8_SINT_PACK32            = static_cast<VkFormat>(56);
    static constexpr VkFormat A8B8G8R8_SRGB_PACK32            = static_cast<VkFormat>(57);
    static constexpr VkFormat A2R10G10B10_UNORM_PACK32        = static_cast<VkFormat>(58);
    static constexpr VkFormat A2R10G10B10_SNORM_PACK32        = static_cast<VkFormat>(59);
    static constexpr VkFormat A2R10G10B10_UINT_PACK32         = static_cast<VkFormat>(60);
    static constexpr VkFormat A2R10G10B10_SINT_PACK32         = static_cast<VkFormat>(61);
    static constexpr VkFormat A2B10G10R10_UNORM_PACK32        = static_cast<VkFormat>(64);
    static constexpr VkFormat A2B10G10R10_SNORM_PACK32        = static_cast<VkFormat>(65);
    static constexpr VkFormat A2B10G10R10_UINT_PACK32         = static_cast<VkFormat>(66);
    static constexpr VkFormat A2B10G10R10_SINT_PACK32         = static_cast<VkFormat>(67);
    static constexpr VkFormat R16_UNORM                       = static_cast<VkFormat>(70);
    static constexpr VkFormat R16_SNORM                       = static_cast<VkFormat>(71);
    static constexpr VkFormat R16_USCALED                     = static_cast<VkFormat>(72);
    static constexpr VkFormat R16_SSCALED                     = static_cast<VkFormat>(73);
    static constexpr VkFormat R16_UINT                        = static_cast<VkFormat>(74);
    static constexpr VkFormat R16_SINT                        = static_cast<VkFormat>(75);
    static constexpr VkFormat R16_SFLOAT                      = static_cast<VkFormat>(76);
    static constexpr VkFormat R16G16_UNORM                    = static_cast<VkFormat>(77);
    static constexpr VkFormat R16G16_SNORM                    = static_cast<VkFormat>(78);
    static constexpr VkFormat R16G16_USCALED                  = static_cast<VkFormat>(79);
    static constexpr VkFormat R16G16_SSCALED                  = static_cast<VkFormat>(80);
    static constexpr VkFormat R16G16_UINT                     = static_cast<VkFormat>(81);
    static constexpr VkFormat R16G16_SINT                     = static_cast<VkFormat>(82);
    static constexpr VkFormat R16G16_SFLOAT                   = static_cast<VkFormat>(83);
    static constexpr VkFormat R16G16B16_UNORM                 = static_cast<VkFormat>(84);
    static constexpr VkFormat R16G16B16_SNORM                 = static_cast<VkFormat>(85);
    static constexpr VkFormat R16G16B16_USCALED               = static_cast<VkFormat>(86);
    static constexpr VkFormat R16G16B16_SSCALED               = static_cast<VkFormat>(87);
    static constexpr VkFormat R16G16B16_UINT                  = static_cast<VkFormat>(88);
    static constexpr VkFormat R16G16B16_SINT                  = static_cast<VkFormat>(89);
    static constexpr VkFormat R16G16B16_SFLOAT                = static_cast<VkFormat>(90);
    static constexpr VkFormat R16G16B16A16_UNORM              = static_cast<VkFormat>(91);
    static constexpr VkFormat R16G16B16A16_SNORM              = static_cast<VkFormat>(92);
    static constexpr VkFormat R16G16B16A16_USCALED            = static_cast<VkFormat>(93);
    static constexpr VkFormat R16G16B16A16_SSCALED            = static_cast<VkFormat>(94);
    static constexpr VkFormat R16G16B16A16_UINT               = static_cast<VkFormat>(95);
    static constexpr VkFormat R16G16B16A16_SINT               = static_cast<VkFormat>(96);
    static constexpr VkFormat R16G16B16A16_SFLOAT             = static_cast<VkFormat>(97);
    static constexpr VkFormat R32_UINT                        = static_cast<VkFormat>(98);
    static constexpr VkFormat R32_SINT                        = static_cast<VkFormat>(99);
    static constexpr VkFormat R32_SFLOAT                      = static_cast<VkFormat>(100);
    static constexpr VkFormat R32G32_UINT                     = static_cast<VkFormat>(101);
    static constexpr VkFormat R32G32_SINT                     = static_cast<VkFormat>(102);
    static constexpr VkFormat R32G32_SFLOAT                   = static_cast<VkFormat>(103);
    static constexpr VkFormat R32G32B32_UINT                  = static_cast<VkFormat>(104);
    static constexpr VkFormat R32G32B32_SINT                  = static_cast<VkFormat>(105);
    static constexpr VkFormat R32G32B32_SFLOAT                = static_cast<VkFormat>(106);
    static constexpr VkFormat R32G32B32A32_UINT               = static_cast<VkFormat>(107);
    static constexpr VkFormat R32G32B32A32_SINT               = static_cast<VkFormat>(108);
    static constexpr VkFormat R32G32B32A32_SFLOAT             = static_cast<VkFormat>(109);
    static constexpr VkFormat R64_UINT                        = static_cast<VkFormat>(110);
    static constexpr VkFormat R64_SINT                        = static_cast<VkFormat>(111);
    static constexpr VkFormat R64_SFLOAT                      = static_cast<VkFormat>(112);
    static constexpr VkFormat R64G64_UINT                     = static_cast<VkFormat>(113);
    static constexpr VkFormat R64G64_SINT                     = static_cast<VkFormat>(114);
    static constexpr VkFormat R64G64_SFLOAT                   = static_cast<VkFormat>(115);
    static constexpr VkFormat R64G64B64_UINT                  = static_cast<VkFormat>(116);
    static constexpr VkFormat R64G64B64_SINT                  = static_cast<VkFormat>(117);
    static constexpr VkFormat R64G64B64_SFLOAT                = static_cast<VkFormat>(118);
    static constexpr VkFormat R64G64B64A64_UINT               = static_cast<VkFormat>(119);
    static constexpr VkFormat R64G64B64A64_SINT               = static_cast<VkFormat>(120);
    static constexpr VkFormat R64G64B64A64_SFLOAT             = static_cast<VkFormat>(121);
    static constexpr VkFormat B10G11R11_UFLOAT_PACK32         = static_cast<VkFormat>(122);
    static constexpr VkFormat E5B9G9R9_UFLOAT_PACK32          = static_cast<VkFormat>(123);
    static constexpr VkFormat D16_UNORM                       = static_cast<VkFormat>(124);
    static constexpr VkFormat X8_D24_UNORM_PACK32             = static_cast<VkFormat>(125);
    static constexpr VkFormat D32_SFLOAT                      = static_cast<VkFormat>(126);
    static constexpr VkFormat S8_UINT                         = static_cast<VkFormat>(127);
    static constexpr VkFormat D16_UNORM_S8_UINT               = static_cast<VkFormat>(128);
    static constexpr VkFormat D24_UNORM_S8_UINT               = static_cast<VkFormat>(129);
    static constexpr VkFormat D32_SFLOAT_S8_UINT              = static_cast<VkFormat>(130);
    static constexpr VkFormat BC1_RGB_UNORM_BLOCK             = static_cast<VkFormat>(131);
    static constexpr VkFormat BC1_RGB_SRGB_BLOCK              = static_cast<VkFormat>(132);
    static constexpr VkFormat BC1_RGBA_UNORM_BLOCK            = static_cast<VkFormat>(133);
    static constexpr VkFormat BC1_RGBA_SRGB_BLOCK             = static_cast<VkFormat>(134);
    static constexpr VkFormat BC2_UNORM_BLOCK                 = static_cast<VkFormat>(135);
    static constexpr VkFormat BC2_SRGB_BLOCK                  = static_cast<VkFormat>(136);
    static constexpr VkFormat BC3_UNORM_BLOCK                 = static_cast<VkFormat>(137);
    static constexpr VkFormat BC3_SRGB_BLOCK                  = static_cast<VkFormat>(138);
    static constexpr VkFormat BC4_UNORM_BLOCK                 = static_cast<VkFormat>(139);
    static constexpr VkFormat BC4_SNORM_BLOCK                 = static_cast<VkFormat>(140);
    static constexpr VkFormat BC5_UNORM_BLOCK                 = static_cast<VkFormat>(141);
    static constexpr VkFormat BC5_SNORM_BLOCK                 = static_cast<VkFormat>(142);
    static constexpr VkFormat BC6H_UFLOAT_BLOCK               = static_cast<VkFormat>(143);
    static constexpr VkFormat BC6H_SFLOAT_BLOCK               = static_cast<VkFormat>(144);
    static constexpr VkFormat BC7_UNORM_BLOCK                 = static_cast<VkFormat>(145);
    static constexpr VkFormat BC7_SRGB_BLOCK                  = static_cast<VkFormat>(146);
    static constexpr VkFormat ETC2_R8G8B8_UNORM_BLOCK         = static_cast<VkFormat>(147);
    static constexpr VkFormat ETC2_R8G8B8_SRGB_BLOCK          = static_cast<VkFormat>(148);
    static constexpr VkFormat ETC2_R8G8B8A1_UNORM_BLOCK       = static_cast<VkFormat>(149);
    static constexpr VkFormat ETC2_R8G8B8A1_SRGB_BLOCK        = static_cast<VkFormat>(150);
    static constexpr VkFormat ETC2_R8G8B8A8_UNORM_BLOCK       = static_cast<VkFormat>(151);
    static constexpr VkFormat ETC2_R8G8B8A8_SRGB_BLOCK        = static_cast<VkFormat>(152);
    static constexpr VkFormat EAC_R11_UNORM_BLOCK             = static_cast<VkFormat>(153);
    static constexpr VkFormat EAC_R11_SNORM_BLOCK             = static_cast<VkFormat>(154);
    static constexpr VkFormat EAC_R11G11_UNORM_BLOCK          = static_cast<VkFormat>(155);
    static constexpr VkFormat EAC_R11G11_SNORM_BLOCK          = static_cast<VkFormat>(156);
    static constexpr VkFormat ASTC_4x4_UNORM_BLOCK            = static_cast<VkFormat>(157);
    static constexpr VkFormat ASTC_4x4_SRGB_BLOCK             = static_cast<VkFormat>(158);
    static constexpr VkFormat ASTC_5x4_UNORM_BLOCK            = static_cast<VkFormat>(159);
    static constexpr VkFormat ASTC_5x4_SRGB_BLOCK             = static_cast<VkFormat>(160);
    static constexpr VkFormat ASTC_5x5_UNORM_BLOCK            = static_cast<VkFormat>(161);
    static constexpr VkFormat ASTC_5x5_SRGB_BLOCK             = static_cast<VkFormat>(162);
    static constexpr VkFormat ASTC_6x5_UNORM_BLOCK            = static_cast<VkFormat>(163);
    static constexpr VkFormat ASTC_6x5_SRGB_BLOCK             = static_cast<VkFormat>(164);
    static constexpr VkFormat ASTC_6x6_UNORM_BLOCK            = static_cast<VkFormat>(165);
    static constexpr VkFormat ASTC_6x6_SRGB_BLOCK             = static_cast<VkFormat>(166);
    static constexpr VkFormat ASTC_8x5_UNORM_BLOCK            = static_cast<VkFormat>(169);
    static constexpr VkFormat ASTC_8x5_SRGB_BLOCK             = static_cast<VkFormat>(170);
    static constexpr VkFormat ASTC_8x6_UNORM_BLOCK            = static_cast<VkFormat>(171);
    static constexpr VkFormat ASTC_8x6_SRGB_BLOCK             = static_cast<VkFormat>(172);
    static constexpr VkFormat ASTC_8x8_UNORM_BLOCK            = static_cast<VkFormat>(173);
    static constexpr VkFormat ASTC_8x8_SRGB_BLOCK             = static_cast<VkFormat>(174);
    static constexpr VkFormat ASTC_10x5_UNORM_BLOCK           = static_cast<VkFormat>(175);
    static constexpr VkFormat ASTC_10x5_SRGB_BLOCK            = static_cast<VkFormat>(176);
    static constexpr VkFormat ASTC_10x6_UNORM_BLOCK           = static_cast<VkFormat>(177);
    static constexpr VkFormat ASTC_10x6_SRGB_BLOCK            = static_cast<VkFormat>(178);
    static constexpr VkFormat ASTC_10x8_UNORM_BLOCK           = static_cast<VkFormat>(179);
    static constexpr VkFormat ASTC_10x8_SRGB_BLOCK            = static_cast<VkFormat>(180);
    static constexpr VkFormat ASTC_10x10_UNORM_BLOCK          = static_cast<VkFormat>(181);
    static constexpr VkFormat ASTC_10x10_SRGB_BLOCK           = static_cast<VkFormat>(182);
    static constexpr VkFormat ASTC_12x10_UNORM_BLOCK          = static_cast<VkFormat>(183);
    static constexpr VkFormat ASTC_12x10_SRGB_BLOCK           = static_cast<VkFormat>(184);
    static constexpr VkFormat ASTC_12x12_UNORM_BLOCK          = static_cast<VkFormat>(185);
    static constexpr VkFormat ASTC_12x12_SRGB_BLOCK           = static_cast<VkFormat>(186);
} // namespace VK

// ═══════════════════════════════════════════════════════════════════════════════
//  FormatInfo flat array — O(1) lookup by VkFormat enum value
// ═══════════════════════════════════════════════════════════════════════════════

// Maximum VkFormat value we handle via flat array.
// ASTC_12x12_SRGB_BLOCK = 186, so 187 entries covers everything.
static constexpr size_t kFormatTableSize = 187;

// Sentinel entry for unmapped formats.
static const FormatInfo kUnknown = {
    static_cast<VkFormat>(0), 0, static_cast<VkFormat>(0), 0, 0, 0, 0,
    false, false, false, false, false, false, false, false, false,
    false, false, false,
    "UNKNOWN"
};

// Helper macro to define a FormatInfo entry concisely.
// Args: vk, mtl, fb, bw, bh, bpb, cc, d, s, cmp, srgb, flt, un, sn, ui, si, filt, rend, blend, name
#define FMT(vk, mtl, fb, bw, bh, bpb, cc, d, s, cmp, srgb, flt, un, sn, ui, si, filt, rend, bld, nm) \
    FormatInfo{static_cast<VkFormat>(vk), (mtl), static_cast<VkFormat>(fb), (uint8_t)(bw), (uint8_t)(bh), (uint8_t)(bpb), (uint8_t)(cc), \
               (d), (s), (cmp), (srgb), (flt), (un), (sn), (ui), (si), (filt), (rend), (bld), (nm)}

// Build the flat array.  Index = VkFormat enum value.
// Entries that don't correspond to a real VkFormat are set to kUnknown.
static FormatInfo buildEntry(VkFormat) { return kUnknown; }

// The actual table, populated at startup.
static std::array<FormatInfo, kFormatTableSize> sFormatTable = []() {
    std::array<FormatInfo, kFormatTableSize> t;
    t.fill(kUnknown);

    // Shorthand for entries that Metal can't represent (no direct mapping).
    // They get mtlFormat=0 (Invalid) and a fallback format.
    auto noMtl = [](VkFormat vk, VkFormat fb, uint8_t bw, uint8_t bh, uint8_t bpb,
                    uint8_t cc, bool d, bool s, bool cmp, bool srgb, bool flt,
                    bool un, bool sn, bool ui, bool si, const char* nm) -> FormatInfo {
        return {static_cast<VkFormat>(vk), MTL::Invalid, static_cast<VkFormat>(fb), bw, bh, bpb, cc, d, s, cmp, srgb, flt, un, sn, ui, si,
                false, false, false, nm};
    };

    // ─── 8-bit ───────────────────────────────────────────────────────────
    //                              vk                mtl                  fb        bw bh bpb cc d s c sr fl un sn ui si filt rend bld  name
    t[VK::R8_UNORM]     = FMT(VK::R8_UNORM,     MTL::R8Unorm,        VK::UNDEFINED, 1,1,1,1, 0,0,0,0,0,1,0,0,0, 1,1,1, "R8_UNORM");
    t[VK::R8_SNORM]     = FMT(VK::R8_SNORM,     MTL::R8Snorm,        VK::UNDEFINED, 1,1,1,1, 0,0,0,0,0,0,1,0,0, 1,0,0, "R8_SNORM");
    t[VK::R8_UINT]      = FMT(VK::R8_UINT,      MTL::R8Uint,         VK::UNDEFINED, 1,1,1,1, 0,0,0,0,0,0,0,1,0, 0,1,0, "R8_UINT");
    t[VK::R8_SINT]      = FMT(VK::R8_SINT,      MTL::R8Sint,         VK::UNDEFINED, 1,1,1,1, 0,0,0,0,0,0,0,0,1, 0,1,0, "R8_SINT");
    t[VK::R8_SRGB]      = FMT(VK::R8_SRGB,      MTL::R8Unorm_sRGB,   VK::UNDEFINED, 1,1,1,1, 0,0,0,1,0,1,0,0,0, 1,1,1, "R8_SRGB");
    // Scaled formats: no Metal equivalent, fall back to UNORM/SNORM
    t[VK::R8_USCALED]   = noMtl(VK::R8_USCALED,  VK::R8_UNORM, 1,1,1,1, 0,0,0,0,0,1,0,0,0, "R8_USCALED");
    t[VK::R8_SSCALED]   = noMtl(VK::R8_SSCALED,  VK::R8_SNORM, 1,1,1,1, 0,0,0,0,0,0,1,0,0, "R8_SSCALED");

    // ─── RG8 ─────────────────────────────────────────────────────────────
    t[VK::R8G8_UNORM]   = FMT(VK::R8G8_UNORM,   MTL::RG8Unorm,       VK::UNDEFINED, 1,1,2,2, 0,0,0,0,0,1,0,0,0, 1,1,1, "R8G8_UNORM");
    t[VK::R8G8_SNORM]   = FMT(VK::R8G8_SNORM,   MTL::RG8Snorm,       VK::UNDEFINED, 1,1,2,2, 0,0,0,0,0,0,1,0,0, 1,0,0, "R8G8_SNORM");
    t[VK::R8G8_UINT]    = FMT(VK::R8G8_UINT,    MTL::RG8Uint,        VK::UNDEFINED, 1,1,2,2, 0,0,0,0,0,0,0,1,0, 0,1,0, "R8G8_UINT");
    t[VK::R8G8_SINT]    = FMT(VK::R8G8_SINT,    MTL::RG8Sint,        VK::UNDEFINED, 1,1,2,2, 0,0,0,0,0,0,0,0,1, 0,1,0, "R8G8_SINT");
    t[VK::R8G8_SRGB]    = FMT(VK::R8G8_SRGB,    MTL::RG8Unorm_sRGB,  VK::UNDEFINED, 1,1,2,2, 0,0,0,1,0,1,0,0,0, 1,1,1, "R8G8_SRGB");
    t[VK::R8G8_USCALED] = noMtl(VK::R8G8_USCALED, VK::R8G8_UNORM, 1,1,2,2, 0,0,0,0,0,1,0,0,0, "R8G8_USCALED");
    t[VK::R8G8_SSCALED] = noMtl(VK::R8G8_SSCALED, VK::R8G8_SNORM, 1,1,2,2, 0,0,0,0,0,0,1,0,0, "R8G8_SSCALED");

    // ─── RGB8 — Metal has no 24-bit formats, use RGBA8 fallback ─────────
    t[VK::R8G8B8_UNORM]   = noMtl(VK::R8G8B8_UNORM,   VK::R8G8B8A8_UNORM,  1,1,3,3, 0,0,0,0,0,1,0,0,0, "R8G8B8_UNORM");
    t[VK::R8G8B8_SNORM]   = noMtl(VK::R8G8B8_SNORM,   VK::R8G8B8A8_SNORM,  1,1,3,3, 0,0,0,0,0,0,1,0,0, "R8G8B8_SNORM");
    t[VK::R8G8B8_UINT]    = noMtl(VK::R8G8B8_UINT,    VK::R8G8B8A8_UINT,   1,1,3,3, 0,0,0,0,0,0,0,1,0, "R8G8B8_UINT");
    t[VK::R8G8B8_SINT]    = noMtl(VK::R8G8B8_SINT,    VK::R8G8B8A8_SINT,   1,1,3,3, 0,0,0,0,0,0,0,0,1, "R8G8B8_SINT");
    t[VK::R8G8B8_SRGB]    = noMtl(VK::R8G8B8_SRGB,    VK::R8G8B8A8_SRGB,   1,1,3,3, 0,0,0,1,0,1,0,0,0, "R8G8B8_SRGB");
    t[VK::R8G8B8_USCALED] = noMtl(VK::R8G8B8_USCALED, VK::R8G8B8A8_UNORM,  1,1,3,3, 0,0,0,0,0,1,0,0,0, "R8G8B8_USCALED");
    t[VK::R8G8B8_SSCALED] = noMtl(VK::R8G8B8_SSCALED, VK::R8G8B8A8_SNORM,  1,1,3,3, 0,0,0,0,0,0,1,0,0, "R8G8B8_SSCALED");

    // ─── BGR8 — same situation, no 24-bit Metal format ──────────────────
    t[VK::B8G8R8_UNORM]   = noMtl(VK::B8G8R8_UNORM,   VK::B8G8R8A8_UNORM,  1,1,3,3, 0,0,0,0,0,1,0,0,0, "B8G8R8_UNORM");
    t[VK::B8G8R8_SNORM]   = noMtl(VK::B8G8R8_SNORM,   VK::B8G8R8A8_SNORM,  1,1,3,3, 0,0,0,0,0,0,1,0,0, "B8G8R8_SNORM");
    t[VK::B8G8R8_UINT]    = noMtl(VK::B8G8R8_UINT,    VK::B8G8R8A8_UINT,   1,1,3,3, 0,0,0,0,0,0,0,1,0, "B8G8R8_UINT");
    t[VK::B8G8R8_SINT]    = noMtl(VK::B8G8R8_SINT,    VK::B8G8R8A8_SINT,   1,1,3,3, 0,0,0,0,0,0,0,0,1, "B8G8R8_SINT");
    t[VK::B8G8R8_SRGB]    = noMtl(VK::B8G8R8_SRGB,    VK::B8G8R8A8_SRGB,   1,1,3,3, 0,0,0,1,0,1,0,0,0, "B8G8R8_SRGB");
    t[VK::B8G8R8_USCALED] = noMtl(VK::B8G8R8_USCALED, VK::B8G8R8A8_UNORM,  1,1,3,3, 0,0,0,0,0,1,0,0,0, "B8G8R8_USCALED");
    t[VK::B8G8R8_SSCALED] = noMtl(VK::B8G8R8_SSCALED, VK::B8G8R8A8_SNORM,  1,1,3,3, 0,0,0,0,0,0,1,0,0, "B8G8R8_SSCALED");

    // ─── RGBA8 ───────────────────────────────────────────────────────────
    t[VK::R8G8B8A8_UNORM]   = FMT(VK::R8G8B8A8_UNORM,   MTL::RGBA8Unorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "R8G8B8A8_UNORM");
    t[VK::R8G8B8A8_SNORM]   = FMT(VK::R8G8B8A8_SNORM,   MTL::RGBA8Snorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,1,0,0, 1,1,1, "R8G8B8A8_SNORM");
    t[VK::R8G8B8A8_UINT]    = FMT(VK::R8G8B8A8_UINT,    MTL::RGBA8Uint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "R8G8B8A8_UINT");
    t[VK::R8G8B8A8_SINT]    = FMT(VK::R8G8B8A8_SINT,    MTL::RGBA8Sint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,0,1, 0,1,0, "R8G8B8A8_SINT");
    t[VK::R8G8B8A8_SRGB]    = FMT(VK::R8G8B8A8_SRGB,    MTL::RGBA8Unorm_sRGB, VK::UNDEFINED, 1,1,4,4, 0,0,0,1,0,1,0,0,0, 1,1,1, "R8G8B8A8_SRGB");
    t[VK::R8G8B8A8_USCALED] = noMtl(VK::R8G8B8A8_USCALED, VK::R8G8B8A8_UNORM, 1,1,4,4, 0,0,0,0,0,1,0,0,0, "R8G8B8A8_USCALED");
    t[VK::R8G8B8A8_SSCALED] = noMtl(VK::R8G8B8A8_SSCALED, VK::R8G8B8A8_SNORM, 1,1,4,4, 0,0,0,0,0,0,1,0,0, "R8G8B8A8_SSCALED");

    // ─── BGRA8 — the most common swapchain format ───────────────────────
    t[VK::B8G8R8A8_UNORM]   = FMT(VK::B8G8R8A8_UNORM,   MTL::BGRA8Unorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "B8G8R8A8_UNORM");
    t[VK::B8G8R8A8_SNORM]   = FMT(VK::B8G8R8A8_SNORM,   MTL::RGBA8Snorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,1,0,0, 1,1,1, "B8G8R8A8_SNORM");
    t[VK::B8G8R8A8_UINT]    = FMT(VK::B8G8R8A8_UINT,    MTL::RGBA8Uint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "B8G8R8A8_UINT");
    t[VK::B8G8R8A8_SINT]    = FMT(VK::B8G8R8A8_SINT,    MTL::RGBA8Sint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,0,1, 0,1,0, "B8G8R8A8_SINT");
    t[VK::B8G8R8A8_SRGB]    = FMT(VK::B8G8R8A8_SRGB,    MTL::BGRA8Unorm_sRGB, VK::UNDEFINED, 1,1,4,4, 0,0,0,1,0,1,0,0,0, 1,1,1, "B8G8R8A8_SRGB");
    t[VK::B8G8R8A8_USCALED] = noMtl(VK::B8G8R8A8_USCALED, VK::B8G8R8A8_UNORM, 1,1,4,4, 0,0,0,0,0,1,0,0,0, "B8G8R8A8_USCALED");
    t[VK::B8G8R8A8_SSCALED] = noMtl(VK::B8G8R8A8_SSCALED, VK::B8G8R8A8_SNORM, 1,1,4,4, 0,0,0,0,0,0,1,0,0, "B8G8R8A8_SSCALED");

    // ─── A8B8G8R8 PACK32 — identical memory layout to RGBA8 on little-endian
    t[VK::A8B8G8R8_UNORM_PACK32]   = FMT(VK::A8B8G8R8_UNORM_PACK32,   MTL::RGBA8Unorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "A8B8G8R8_UNORM_PACK32");
    t[VK::A8B8G8R8_SNORM_PACK32]   = FMT(VK::A8B8G8R8_SNORM_PACK32,   MTL::RGBA8Snorm,      VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,1,0,0, 1,1,1, "A8B8G8R8_SNORM_PACK32");
    t[VK::A8B8G8R8_UINT_PACK32]    = FMT(VK::A8B8G8R8_UINT_PACK32,    MTL::RGBA8Uint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "A8B8G8R8_UINT_PACK32");
    t[VK::A8B8G8R8_SINT_PACK32]    = FMT(VK::A8B8G8R8_SINT_PACK32,    MTL::RGBA8Sint,       VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,0,1, 0,1,0, "A8B8G8R8_SINT_PACK32");
    t[VK::A8B8G8R8_SRGB_PACK32]    = FMT(VK::A8B8G8R8_SRGB_PACK32,   MTL::RGBA8Unorm_sRGB, VK::UNDEFINED, 1,1,4,4, 0,0,0,1,0,1,0,0,0, 1,1,1, "A8B8G8R8_SRGB_PACK32");
    t[VK::A8B8G8R8_USCALED_PACK32] = noMtl(VK::A8B8G8R8_USCALED_PACK32, VK::R8G8B8A8_UNORM, 1,1,4,4, 0,0,0,0,0,1,0,0,0, "A8B8G8R8_USCALED_PACK32");
    t[VK::A8B8G8R8_SSCALED_PACK32] = noMtl(VK::A8B8G8R8_SSCALED_PACK32, VK::R8G8B8A8_SNORM, 1,1,4,4, 0,0,0,0,0,0,1,0,0, "A8B8G8R8_SSCALED_PACK32");

    // ─── Packed 16-bit ───────────────────────────────────────────────────
    t[VK::R4G4_UNORM_PACK8]        = noMtl(VK::R4G4_UNORM_PACK8,        VK::R8G8_UNORM,         1,1,1,2, 0,0,0,0,0,1,0,0,0, "R4G4_UNORM_PACK8");
    t[VK::R4G4B4A4_UNORM_PACK16]   = FMT(VK::R4G4B4A4_UNORM_PACK16,   MTL::ABGR4Unorm,         VK::UNDEFINED, 1,1,2,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "R4G4B4A4_UNORM_PACK16");
    t[VK::B4G4R4A4_UNORM_PACK16]   = FMT(VK::B4G4R4A4_UNORM_PACK16,   MTL::ABGR4Unorm,         VK::UNDEFINED, 1,1,2,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "B4G4R4A4_UNORM_PACK16");
    t[VK::R5G6B5_UNORM_PACK16]     = FMT(VK::R5G6B5_UNORM_PACK16,     MTL::B5G6R5Unorm,        VK::UNDEFINED, 1,1,2,3, 0,0,0,0,0,1,0,0,0, 1,1,1, "R5G6B5_UNORM_PACK16");
    t[VK::B5G6R5_UNORM_PACK16]     = FMT(VK::B5G6R5_UNORM_PACK16,     MTL::B5G6R5Unorm,        VK::UNDEFINED, 1,1,2,3, 0,0,0,0,0,1,0,0,0, 1,1,1, "B5G6R5_UNORM_PACK16");
    t[VK::R5G5B5A1_UNORM_PACK16]   = FMT(VK::R5G5B5A1_UNORM_PACK16,   MTL::A1BGR5Unorm,        VK::UNDEFINED, 1,1,2,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "R5G5B5A1_UNORM_PACK16");
    t[VK::B5G5R5A1_UNORM_PACK16]   = FMT(VK::B5G5R5A1_UNORM_PACK16,   MTL::BGR5A1Unorm,        VK::UNDEFINED, 1,1,2,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "B5G5R5A1_UNORM_PACK16");
    t[VK::A1R5G5B5_UNORM_PACK16]   = FMT(VK::A1R5G5B5_UNORM_PACK16,   MTL::A1BGR5Unorm,        VK::UNDEFINED, 1,1,2,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "A1R5G5B5_UNORM_PACK16");

    // ─── 10/11-bit packed ────────────────────────────────────────────────
    t[VK::A2R10G10B10_UNORM_PACK32] = FMT(VK::A2R10G10B10_UNORM_PACK32, MTL::BGR10A2Unorm,  VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "A2R10G10B10_UNORM_PACK32");
    t[VK::A2R10G10B10_UINT_PACK32]  = FMT(VK::A2R10G10B10_UINT_PACK32,  MTL::BGR10A2Unorm,  VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "A2R10G10B10_UINT_PACK32");
    t[VK::A2B10G10R10_UNORM_PACK32] = FMT(VK::A2B10G10R10_UNORM_PACK32, MTL::RGB10A2Unorm,  VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "A2B10G10R10_UNORM_PACK32");
    t[VK::A2B10G10R10_UINT_PACK32]  = FMT(VK::A2B10G10R10_UINT_PACK32,  MTL::RGB10A2Uint,   VK::UNDEFINED, 1,1,4,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "A2B10G10R10_UINT_PACK32");
    // SNORM/SINT variants of 10-bit: no Metal equivalent
    t[VK::A2R10G10B10_SNORM_PACK32] = noMtl(VK::A2R10G10B10_SNORM_PACK32, VK::A2R10G10B10_UNORM_PACK32, 1,1,4,4, 0,0,0,0,0,0,1,0,0, "A2R10G10B10_SNORM_PACK32");
    t[VK::A2R10G10B10_SINT_PACK32]  = noMtl(VK::A2R10G10B10_SINT_PACK32,  VK::A2R10G10B10_UINT_PACK32,  1,1,4,4, 0,0,0,0,0,0,0,0,1, "A2R10G10B10_SINT_PACK32");
    t[VK::A2B10G10R10_SNORM_PACK32] = noMtl(VK::A2B10G10R10_SNORM_PACK32, VK::A2B10G10R10_UNORM_PACK32, 1,1,4,4, 0,0,0,0,0,0,1,0,0, "A2B10G10R10_SNORM_PACK32");
    t[VK::A2B10G10R10_SINT_PACK32]  = noMtl(VK::A2B10G10R10_SINT_PACK32,  VK::A2B10G10R10_UINT_PACK32,  1,1,4,4, 0,0,0,0,0,0,0,0,1, "A2B10G10R10_SINT_PACK32");

    // ─── R16 ─────────────────────────────────────────────────────────────
    t[VK::R16_UNORM]   = FMT(VK::R16_UNORM,   MTL::R16Unorm,  VK::UNDEFINED, 1,1,2,1, 0,0,0,0,0,1,0,0,0, 1,1,1, "R16_UNORM");
    t[VK::R16_SNORM]   = FMT(VK::R16_SNORM,   MTL::R16Snorm,  VK::UNDEFINED, 1,1,2,1, 0,0,0,0,0,0,1,0,0, 1,0,0, "R16_SNORM");
    t[VK::R16_UINT]    = FMT(VK::R16_UINT,    MTL::R16Uint,   VK::UNDEFINED, 1,1,2,1, 0,0,0,0,0,0,0,1,0, 0,1,0, "R16_UINT");
    t[VK::R16_SINT]    = FMT(VK::R16_SINT,    MTL::R16Sint,   VK::UNDEFINED, 1,1,2,1, 0,0,0,0,0,0,0,0,1, 0,1,0, "R16_SINT");
    t[VK::R16_SFLOAT]  = FMT(VK::R16_SFLOAT,  MTL::R16Float,  VK::UNDEFINED, 1,1,2,1, 0,0,0,0,1,0,0,0,0, 1,1,1, "R16_SFLOAT");
    t[VK::R16_USCALED] = noMtl(VK::R16_USCALED, VK::R16_UNORM, 1,1,2,1, 0,0,0,0,0,1,0,0,0, "R16_USCALED");
    t[VK::R16_SSCALED] = noMtl(VK::R16_SSCALED, VK::R16_SNORM, 1,1,2,1, 0,0,0,0,0,0,1,0,0, "R16_SSCALED");

    // ─── RG16 ────────────────────────────────────────────────────────────
    t[VK::R16G16_UNORM]   = FMT(VK::R16G16_UNORM,   MTL::RG16Unorm,  VK::UNDEFINED, 1,1,4,2, 0,0,0,0,0,1,0,0,0, 1,1,1, "R16G16_UNORM");
    t[VK::R16G16_SNORM]   = FMT(VK::R16G16_SNORM,   MTL::RG16Snorm,  VK::UNDEFINED, 1,1,4,2, 0,0,0,0,0,0,1,0,0, 1,0,0, "R16G16_SNORM");
    t[VK::R16G16_UINT]    = FMT(VK::R16G16_UINT,    MTL::RG16Uint,   VK::UNDEFINED, 1,1,4,2, 0,0,0,0,0,0,0,1,0, 0,1,0, "R16G16_UINT");
    t[VK::R16G16_SINT]    = FMT(VK::R16G16_SINT,    MTL::RG16Sint,   VK::UNDEFINED, 1,1,4,2, 0,0,0,0,0,0,0,0,1, 0,1,0, "R16G16_SINT");
    t[VK::R16G16_SFLOAT]  = FMT(VK::R16G16_SFLOAT,  MTL::RG16Float,  VK::UNDEFINED, 1,1,4,2, 0,0,0,0,1,0,0,0,0, 1,1,1, "R16G16_SFLOAT");
    t[VK::R16G16_USCALED] = noMtl(VK::R16G16_USCALED, VK::R16G16_UNORM, 1,1,4,2, 0,0,0,0,0,1,0,0,0, "R16G16_USCALED");
    t[VK::R16G16_SSCALED] = noMtl(VK::R16G16_SSCALED, VK::R16G16_SNORM, 1,1,4,2, 0,0,0,0,0,0,1,0,0, "R16G16_SSCALED");

    // ─── RGB16 — no Metal 3-component 16-bit, use RGBA16 fallback ───────
    t[VK::R16G16B16_UNORM]   = noMtl(VK::R16G16B16_UNORM,   VK::R16G16B16A16_UNORM,  1,1,6,3, 0,0,0,0,0,1,0,0,0, "R16G16B16_UNORM");
    t[VK::R16G16B16_SNORM]   = noMtl(VK::R16G16B16_SNORM,   VK::R16G16B16A16_SNORM,  1,1,6,3, 0,0,0,0,0,0,1,0,0, "R16G16B16_SNORM");
    t[VK::R16G16B16_UINT]    = noMtl(VK::R16G16B16_UINT,    VK::R16G16B16A16_UINT,   1,1,6,3, 0,0,0,0,0,0,0,1,0, "R16G16B16_UINT");
    t[VK::R16G16B16_SINT]    = noMtl(VK::R16G16B16_SINT,    VK::R16G16B16A16_SINT,   1,1,6,3, 0,0,0,0,0,0,0,0,1, "R16G16B16_SINT");
    t[VK::R16G16B16_SFLOAT]  = noMtl(VK::R16G16B16_SFLOAT,  VK::R16G16B16A16_SFLOAT, 1,1,6,3, 0,0,0,0,1,0,0,0,0, "R16G16B16_SFLOAT");
    t[VK::R16G16B16_USCALED] = noMtl(VK::R16G16B16_USCALED, VK::R16G16B16A16_UNORM,  1,1,6,3, 0,0,0,0,0,1,0,0,0, "R16G16B16_USCALED");
    t[VK::R16G16B16_SSCALED] = noMtl(VK::R16G16B16_SSCALED, VK::R16G16B16A16_SNORM,  1,1,6,3, 0,0,0,0,0,0,1,0,0, "R16G16B16_SSCALED");

    // ─── RGBA16 ──────────────────────────────────────────────────────────
    t[VK::R16G16B16A16_UNORM]   = FMT(VK::R16G16B16A16_UNORM,   MTL::RGBA16Unorm,  VK::UNDEFINED, 1,1,8,4, 0,0,0,0,0,1,0,0,0, 1,1,1, "R16G16B16A16_UNORM");
    t[VK::R16G16B16A16_SNORM]   = FMT(VK::R16G16B16A16_SNORM,   MTL::RGBA16Snorm,  VK::UNDEFINED, 1,1,8,4, 0,0,0,0,0,0,1,0,0, 1,1,1, "R16G16B16A16_SNORM");
    t[VK::R16G16B16A16_UINT]    = FMT(VK::R16G16B16A16_UINT,    MTL::RGBA16Uint,   VK::UNDEFINED, 1,1,8,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "R16G16B16A16_UINT");
    t[VK::R16G16B16A16_SINT]    = FMT(VK::R16G16B16A16_SINT,    MTL::RGBA16Sint,   VK::UNDEFINED, 1,1,8,4, 0,0,0,0,0,0,0,0,1, 0,1,0, "R16G16B16A16_SINT");
    t[VK::R16G16B16A16_SFLOAT]  = FMT(VK::R16G16B16A16_SFLOAT,  MTL::RGBA16Float,  VK::UNDEFINED, 1,1,8,4, 0,0,0,0,1,0,0,0,0, 1,1,1, "R16G16B16A16_SFLOAT");
    t[VK::R16G16B16A16_USCALED] = noMtl(VK::R16G16B16A16_USCALED, VK::R16G16B16A16_UNORM, 1,1,8,4, 0,0,0,0,0,1,0,0,0, "R16G16B16A16_USCALED");
    t[VK::R16G16B16A16_SSCALED] = noMtl(VK::R16G16B16A16_SSCALED, VK::R16G16B16A16_SNORM, 1,1,8,4, 0,0,0,0,0,0,1,0,0, "R16G16B16A16_SSCALED");

    // ─── R32 ─────────────────────────────────────────────────────────────
    t[VK::R32_UINT]    = FMT(VK::R32_UINT,    MTL::R32Uint,   VK::UNDEFINED, 1,1,4,1, 0,0,0,0,0,0,0,1,0, 0,1,0, "R32_UINT");
    t[VK::R32_SINT]    = FMT(VK::R32_SINT,    MTL::R32Sint,   VK::UNDEFINED, 1,1,4,1, 0,0,0,0,0,0,0,0,1, 0,1,0, "R32_SINT");
    t[VK::R32_SFLOAT]  = FMT(VK::R32_SFLOAT,  MTL::R32Float,  VK::UNDEFINED, 1,1,4,1, 0,0,0,0,1,0,0,0,0, 1,1,1, "R32_SFLOAT");

    // ─── RG32 ────────────────────────────────────────────────────────────
    t[VK::R32G32_UINT]    = FMT(VK::R32G32_UINT,    MTL::RG32Uint,   VK::UNDEFINED, 1,1,8,2, 0,0,0,0,0,0,0,1,0, 0,1,0, "R32G32_UINT");
    t[VK::R32G32_SINT]    = FMT(VK::R32G32_SINT,    MTL::RG32Sint,   VK::UNDEFINED, 1,1,8,2, 0,0,0,0,0,0,0,0,1, 0,1,0, "R32G32_SINT");
    t[VK::R32G32_SFLOAT]  = FMT(VK::R32G32_SFLOAT,  MTL::RG32Float,  VK::UNDEFINED, 1,1,8,2, 0,0,0,0,1,0,0,0,0, 1,1,0, "R32G32_SFLOAT");

    // ─── RGB32 — Metal has NO 3-component 32-bit texture format ─────────
    // Fall back to RGBA32.  Vertex format Float3/Int3/UInt3 IS supported.
    t[VK::R32G32B32_UINT]    = noMtl(VK::R32G32B32_UINT,    VK::R32G32B32A32_UINT,   1,1,12,3, 0,0,0,0,0,0,0,1,0, "R32G32B32_UINT");
    t[VK::R32G32B32_SINT]    = noMtl(VK::R32G32B32_SINT,    VK::R32G32B32A32_SINT,   1,1,12,3, 0,0,0,0,0,0,0,0,1, "R32G32B32_SINT");
    t[VK::R32G32B32_SFLOAT]  = noMtl(VK::R32G32B32_SFLOAT,  VK::R32G32B32A32_SFLOAT, 1,1,12,3, 0,0,0,0,1,0,0,0,0, "R32G32B32_SFLOAT");

    // ─── RGBA32 ──────────────────────────────────────────────────────────
    t[VK::R32G32B32A32_UINT]    = FMT(VK::R32G32B32A32_UINT,    MTL::RGBA32Uint,   VK::UNDEFINED, 1,1,16,4, 0,0,0,0,0,0,0,1,0, 0,1,0, "R32G32B32A32_UINT");
    t[VK::R32G32B32A32_SINT]    = FMT(VK::R32G32B32A32_SINT,    MTL::RGBA32Sint,   VK::UNDEFINED, 1,1,16,4, 0,0,0,0,0,0,0,0,1, 0,1,0, "R32G32B32A32_SINT");
    t[VK::R32G32B32A32_SFLOAT]  = FMT(VK::R32G32B32A32_SFLOAT,  MTL::RGBA32Float,  VK::UNDEFINED, 1,1,16,4, 0,0,0,0,1,0,0,0,0, 1,1,0, "R32G32B32A32_SFLOAT");

    // ─── Special packed 32-bit ───────────────────────────────────────────
    t[VK::B10G11R11_UFLOAT_PACK32] = FMT(VK::B10G11R11_UFLOAT_PACK32, MTL::RG11B10Float, VK::UNDEFINED, 1,1,4,3, 0,0,0,0,1,0,0,0,0, 1,1,1, "B10G11R11_UFLOAT_PACK32");
    t[VK::E5B9G9R9_UFLOAT_PACK32]  = FMT(VK::E5B9G9R9_UFLOAT_PACK32,  MTL::RGB9E5Float,  VK::UNDEFINED, 1,1,4,3, 0,0,0,0,1,0,0,0,0, 1,0,0, "E5B9G9R9_UFLOAT_PACK32");

    // ─── Depth / stencil ─────────────────────────────────────────────────
    t[VK::D16_UNORM]           = FMT(VK::D16_UNORM,           MTL::Depth16Unorm,          VK::UNDEFINED,           1,1,2,1, 1,0,0,0,0,1,0,0,0, 1,1,0, "D16_UNORM");
    t[VK::D32_SFLOAT]          = FMT(VK::D32_SFLOAT,          MTL::Depth32Float,          VK::UNDEFINED,           1,1,4,1, 1,0,0,0,1,0,0,0,0, 1,1,0, "D32_SFLOAT");
    t[VK::S8_UINT]             = FMT(VK::S8_UINT,             MTL::Stencil8,              VK::UNDEFINED,           1,1,1,1, 0,1,0,0,0,0,0,1,0, 0,0,0, "S8_UINT");
    // X8_D24: macOS Intel only.  Map to D32F on Apple Silicon (fallback handles it).
    t[VK::X8_D24_UNORM_PACK32] = FMT(VK::X8_D24_UNORM_PACK32, MTL::Depth24Unorm_Stencil8, VK::D32_SFLOAT,         1,1,4,1, 1,0,0,0,0,1,0,0,0, 1,1,0, "X8_D24_UNORM_PACK32");
    // D24_UNORM_S8_UINT: maps to Depth24Unorm_Stencil8 on Intel, but that format
    // does NOT exist on Apple Silicon.  We set the primary mapping to
    // Depth32Float_Stencil8 (always works) and note the precision difference.
    // NOTE: D24→D32F means depth values gain precision (32-bit float > 24-bit unorm).
    // This is a widening conversion and should not cause visual artifacts.
    t[VK::D24_UNORM_S8_UINT]  = FMT(VK::D24_UNORM_S8_UINT,   MTL::Depth32Float_Stencil8, VK::D32_SFLOAT_S8_UINT, 1,1,4,2, 1,1,0,0,0,1,0,0,0, 1,1,0, "D24_UNORM_S8_UINT");
    t[VK::D16_UNORM_S8_UINT]  = FMT(VK::D16_UNORM_S8_UINT,   MTL::Depth32Float_Stencil8, VK::D32_SFLOAT_S8_UINT, 1,1,3,2, 1,1,0,0,0,1,0,0,0, 1,1,0, "D16_UNORM_S8_UINT");
    t[VK::D32_SFLOAT_S8_UINT] = FMT(VK::D32_SFLOAT_S8_UINT,  MTL::Depth32Float_Stencil8, VK::UNDEFINED,          1,1,5,2, 1,1,0,0,1,0,0,0,0, 1,1,0, "D32_SFLOAT_S8_UINT");

    // ─── BC compressed (macOS, supported on both Intel and Apple Silicon) ─
    t[VK::BC1_RGB_UNORM_BLOCK]   = FMT(VK::BC1_RGB_UNORM_BLOCK,   MTL::BC1_RGBA,           VK::UNDEFINED, 4,4,8, 3, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC1_RGB_UNORM_BLOCK");
    t[VK::BC1_RGB_SRGB_BLOCK]    = FMT(VK::BC1_RGB_SRGB_BLOCK,    MTL::BC1_RGBA_sRGB,      VK::UNDEFINED, 4,4,8, 3, 0,0,1,1,0,1,0,0,0, 1,0,0, "BC1_RGB_SRGB_BLOCK");
    t[VK::BC1_RGBA_UNORM_BLOCK]  = FMT(VK::BC1_RGBA_UNORM_BLOCK,  MTL::BC1_RGBA,           VK::UNDEFINED, 4,4,8, 4, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC1_RGBA_UNORM_BLOCK");
    t[VK::BC1_RGBA_SRGB_BLOCK]   = FMT(VK::BC1_RGBA_SRGB_BLOCK,   MTL::BC1_RGBA_sRGB,      VK::UNDEFINED, 4,4,8, 4, 0,0,1,1,0,1,0,0,0, 1,0,0, "BC1_RGBA_SRGB_BLOCK");
    t[VK::BC2_UNORM_BLOCK]       = FMT(VK::BC2_UNORM_BLOCK,       MTL::BC2_RGBA,           VK::UNDEFINED, 4,4,16,4, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC2_UNORM_BLOCK");
    t[VK::BC2_SRGB_BLOCK]        = FMT(VK::BC2_SRGB_BLOCK,        MTL::BC2_RGBA_sRGB,      VK::UNDEFINED, 4,4,16,4, 0,0,1,1,0,1,0,0,0, 1,0,0, "BC2_SRGB_BLOCK");
    t[VK::BC3_UNORM_BLOCK]       = FMT(VK::BC3_UNORM_BLOCK,       MTL::BC3_RGBA,           VK::UNDEFINED, 4,4,16,4, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC3_UNORM_BLOCK");
    t[VK::BC3_SRGB_BLOCK]        = FMT(VK::BC3_SRGB_BLOCK,        MTL::BC3_RGBA_sRGB,      VK::UNDEFINED, 4,4,16,4, 0,0,1,1,0,1,0,0,0, 1,0,0, "BC3_SRGB_BLOCK");
    t[VK::BC4_UNORM_BLOCK]       = FMT(VK::BC4_UNORM_BLOCK,       MTL::BC4_RUnorm,         VK::UNDEFINED, 4,4,8, 1, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC4_UNORM_BLOCK");
    t[VK::BC4_SNORM_BLOCK]       = FMT(VK::BC4_SNORM_BLOCK,       MTL::BC4_RSnorm,         VK::UNDEFINED, 4,4,8, 1, 0,0,1,0,0,0,1,0,0, 1,0,0, "BC4_SNORM_BLOCK");
    t[VK::BC5_UNORM_BLOCK]       = FMT(VK::BC5_UNORM_BLOCK,       MTL::BC5_RGUnorm,        VK::UNDEFINED, 4,4,16,2, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC5_UNORM_BLOCK");
    t[VK::BC5_SNORM_BLOCK]       = FMT(VK::BC5_SNORM_BLOCK,       MTL::BC5_RGSnorm,        VK::UNDEFINED, 4,4,16,2, 0,0,1,0,0,0,1,0,0, 1,0,0, "BC5_SNORM_BLOCK");
    t[VK::BC6H_UFLOAT_BLOCK]     = FMT(VK::BC6H_UFLOAT_BLOCK,     MTL::BC6H_RGBUfloat,     VK::UNDEFINED, 4,4,16,3, 0,0,1,0,1,0,0,0,0, 1,0,0, "BC6H_UFLOAT_BLOCK");
    t[VK::BC6H_SFLOAT_BLOCK]     = FMT(VK::BC6H_SFLOAT_BLOCK,     MTL::BC6H_RGBFloat,      VK::UNDEFINED, 4,4,16,3, 0,0,1,0,1,0,0,0,0, 1,0,0, "BC6H_SFLOAT_BLOCK");
    t[VK::BC7_UNORM_BLOCK]       = FMT(VK::BC7_UNORM_BLOCK,       MTL::BC7_RGBAUnorm,      VK::UNDEFINED, 4,4,16,4, 0,0,1,0,0,1,0,0,0, 1,0,0, "BC7_UNORM_BLOCK");
    t[VK::BC7_SRGB_BLOCK]        = FMT(VK::BC7_SRGB_BLOCK,        MTL::BC7_RGBAUnorm_sRGB, VK::UNDEFINED, 4,4,16,4, 0,0,1,1,0,1,0,0,0, 1,0,0, "BC7_SRGB_BLOCK");

    // ─── ETC2 / EAC (Apple Silicon only) ─────────────────────────────────
    t[VK::ETC2_R8G8B8_UNORM_BLOCK]   = FMT(VK::ETC2_R8G8B8_UNORM_BLOCK,   MTL::ETC2_RGB8,        VK::UNDEFINED, 4,4,8, 3, 0,0,1,0,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8_UNORM_BLOCK");
    t[VK::ETC2_R8G8B8_SRGB_BLOCK]    = FMT(VK::ETC2_R8G8B8_SRGB_BLOCK,    MTL::ETC2_RGB8_sRGB,   VK::UNDEFINED, 4,4,8, 3, 0,0,1,1,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8_SRGB_BLOCK");
    t[VK::ETC2_R8G8B8A1_UNORM_BLOCK] = FMT(VK::ETC2_R8G8B8A1_UNORM_BLOCK, MTL::ETC2_RGB8A1,      VK::UNDEFINED, 4,4,8, 4, 0,0,1,0,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8A1_UNORM_BLOCK");
    t[VK::ETC2_R8G8B8A1_SRGB_BLOCK]  = FMT(VK::ETC2_R8G8B8A1_SRGB_BLOCK,  MTL::ETC2_RGB8A1_sRGB, VK::UNDEFINED, 4,4,8, 4, 0,0,1,1,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8A1_SRGB_BLOCK");
    t[VK::ETC2_R8G8B8A8_UNORM_BLOCK] = FMT(VK::ETC2_R8G8B8A8_UNORM_BLOCK, MTL::EAC_RGBA8,        VK::UNDEFINED, 4,4,16,4, 0,0,1,0,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8A8_UNORM_BLOCK");
    t[VK::ETC2_R8G8B8A8_SRGB_BLOCK]  = FMT(VK::ETC2_R8G8B8A8_SRGB_BLOCK,  MTL::EAC_RGBA8_sRGB,   VK::UNDEFINED, 4,4,16,4, 0,0,1,1,0,1,0,0,0, 1,0,0, "ETC2_R8G8B8A8_SRGB_BLOCK");
    t[VK::EAC_R11_UNORM_BLOCK]       = FMT(VK::EAC_R11_UNORM_BLOCK,       MTL::EAC_R11Unorm,     VK::UNDEFINED, 4,4,8, 1, 0,0,1,0,0,1,0,0,0, 1,0,0, "EAC_R11_UNORM_BLOCK");
    t[VK::EAC_R11_SNORM_BLOCK]       = FMT(VK::EAC_R11_SNORM_BLOCK,       MTL::EAC_R11Snorm,     VK::UNDEFINED, 4,4,8, 1, 0,0,1,0,0,0,1,0,0, 1,0,0, "EAC_R11_SNORM_BLOCK");
    t[VK::EAC_R11G11_UNORM_BLOCK]    = FMT(VK::EAC_R11G11_UNORM_BLOCK,    MTL::EAC_RG11Unorm,    VK::UNDEFINED, 4,4,16,2, 0,0,1,0,0,1,0,0,0, 1,0,0, "EAC_R11G11_UNORM_BLOCK");
    t[VK::EAC_R11G11_SNORM_BLOCK]    = FMT(VK::EAC_R11G11_SNORM_BLOCK,    MTL::EAC_RG11Snorm,    VK::UNDEFINED, 4,4,16,2, 0,0,1,0,0,0,1,0,0, 1,0,0, "EAC_R11G11_SNORM_BLOCK");

    // ─── ASTC — complete 4×4 through 12×12 (Apple Silicon) ──────────────
    // All ASTC blocks are 16 bytes regardless of block size.
    #define ASTC_ENTRY(bw, bh, vk_u, vk_s, mtl_u, mtl_s) \
        t[(vk_u)] = FMT((vk_u), (mtl_u), VK::UNDEFINED, (bw),(bh),16,4, 0,0,1,0,0,1,0,0,0, 1,0,0, "ASTC_" #bw "x" #bh "_UNORM_BLOCK"); \
        t[(vk_s)] = FMT((vk_s), (mtl_s), VK::UNDEFINED, (bw),(bh),16,4, 0,0,1,1,0,1,0,0,0, 1,0,0, "ASTC_" #bw "x" #bh "_SRGB_BLOCK");

    ASTC_ENTRY(4, 4,   VK::ASTC_4x4_UNORM_BLOCK,   VK::ASTC_4x4_SRGB_BLOCK,   MTL::ASTC_4x4_LDR,   MTL::ASTC_4x4_sRGB)
    ASTC_ENTRY(5, 4,   VK::ASTC_5x4_UNORM_BLOCK,   VK::ASTC_5x4_SRGB_BLOCK,   MTL::ASTC_5x4_LDR,   MTL::ASTC_5x4_sRGB)
    ASTC_ENTRY(5, 5,   VK::ASTC_5x5_UNORM_BLOCK,   VK::ASTC_5x5_SRGB_BLOCK,   MTL::ASTC_5x5_LDR,   MTL::ASTC_5x5_sRGB)
    ASTC_ENTRY(6, 5,   VK::ASTC_6x5_UNORM_BLOCK,   VK::ASTC_6x5_SRGB_BLOCK,   MTL::ASTC_6x5_LDR,   MTL::ASTC_6x5_sRGB)
    ASTC_ENTRY(6, 6,   VK::ASTC_6x6_UNORM_BLOCK,   VK::ASTC_6x6_SRGB_BLOCK,   MTL::ASTC_6x6_LDR,   MTL::ASTC_6x6_sRGB)
    ASTC_ENTRY(8, 5,   VK::ASTC_8x5_UNORM_BLOCK,   VK::ASTC_8x5_SRGB_BLOCK,   MTL::ASTC_8x5_LDR,   MTL::ASTC_8x5_sRGB)
    ASTC_ENTRY(8, 6,   VK::ASTC_8x6_UNORM_BLOCK,   VK::ASTC_8x6_SRGB_BLOCK,   MTL::ASTC_8x6_LDR,   MTL::ASTC_8x6_sRGB)
    ASTC_ENTRY(8, 8,   VK::ASTC_8x8_UNORM_BLOCK,   VK::ASTC_8x8_SRGB_BLOCK,   MTL::ASTC_8x8_LDR,   MTL::ASTC_8x8_sRGB)
    ASTC_ENTRY(10, 5,  VK::ASTC_10x5_UNORM_BLOCK,  VK::ASTC_10x5_SRGB_BLOCK,  MTL::ASTC_10x5_LDR,  MTL::ASTC_10x5_sRGB)
    ASTC_ENTRY(10, 6,  VK::ASTC_10x6_UNORM_BLOCK,  VK::ASTC_10x6_SRGB_BLOCK,  MTL::ASTC_10x6_LDR,  MTL::ASTC_10x6_sRGB)
    ASTC_ENTRY(10, 8,  VK::ASTC_10x8_UNORM_BLOCK,  VK::ASTC_10x8_SRGB_BLOCK,  MTL::ASTC_10x8_LDR,  MTL::ASTC_10x8_sRGB)
    ASTC_ENTRY(10, 10, VK::ASTC_10x10_UNORM_BLOCK, VK::ASTC_10x10_SRGB_BLOCK, MTL::ASTC_10x10_LDR, MTL::ASTC_10x10_sRGB)
    ASTC_ENTRY(12, 10, VK::ASTC_12x10_UNORM_BLOCK, VK::ASTC_12x10_SRGB_BLOCK, MTL::ASTC_12x10_LDR, MTL::ASTC_12x10_sRGB)
    ASTC_ENTRY(12, 12, VK::ASTC_12x12_UNORM_BLOCK, VK::ASTC_12x12_SRGB_BLOCK, MTL::ASTC_12x12_LDR, MTL::ASTC_12x12_sRGB)
    #undef ASTC_ENTRY

    return t;
}();

#undef FMT

// ═══════════════════════════════════════════════════════════════════════════════
//  Reverse table: MTLPixelFormat → VkFormat
// ═══════════════════════════════════════════════════════════════════════════════
// Metal pixel format values go up to ~262 (X24_Stencil8).
// We use a flat array of size 263 for O(1) reverse lookup.
static constexpr size_t kReverseTableSize = 263;

static std::array<VkFormat, kReverseTableSize> sReverseTable = []() {
    std::array<VkFormat, kReverseTableSize> r;
    r.fill(VK::UNDEFINED);
    // Walk the forward table and populate reverse entries.
    // First-write wins for the canonical reverse mapping.
    for (size_t i = 0; i < kFormatTableSize; ++i) {
        auto mtl = sFormatTable[i].mtlFormat;
        if (mtl != MTL::Invalid && mtl < kReverseTableSize && r[mtl] == VK::UNDEFINED) {
            r[mtl] = sFormatTable[i].vkFormat;
        }
    }
    return r;
}();

// ═══════════════════════════════════════════════════════════════════════════════
//  Public API implementations
// ═══════════════════════════════════════════════════════════════════════════════

MTLPixelFormat vkFormatToMTL(VkFormat vkFmt) noexcept {
    if (vkFmt < kFormatTableSize) {
        return sFormatTable[vkFmt].mtlFormat;
    }
    // Extension formats would go through a hash map here.
    // For now, no extension formats are mapped.
    return MTL::Invalid;
}

VkFormat mtlFormatToVK(MTLPixelFormat mtlFmt) noexcept {
    if (mtlFmt < kReverseTableSize) {
        return sReverseTable[mtlFmt];
    }
    return VK::UNDEFINED;
}

const FormatInfo& getFormatInfo(VkFormat vkFmt) noexcept {
    if (vkFmt < kFormatTableSize) {
        return sFormatTable[vkFmt];
    }
    return kUnknown;
}

VkFormat getFallbackFormat(VkFormat vkFmt) noexcept {
    if (vkFmt < kFormatTableSize) {
        return sFormatTable[vkFmt].fallbackFormat;
    }
    return VK::UNDEFINED;
}

bool isFormatSupported(VkFormat vkFmt, MVBMetalDevice /*device*/) noexcept {
    if (vkFmt >= kFormatTableSize) return false;
    const auto& info = sFormatTable[vkFmt];
    // Format is supported if it has a valid Metal mapping.
    // TODO: When device is non-null, check device.supportsFamily for
    //       ETC2/ASTC (Apple family only) vs BC (macOS family).
    //       For now, report supported if mapped.
    return info.mtlFormat != MTL::Invalid;
}

bool isFormatFilterable(VkFormat vkFmt) noexcept {
    if (vkFmt >= kFormatTableSize) return false;
    return sFormatTable[vkFmt].isFilterable;
}

bool isFormatRenderable(VkFormat vkFmt) noexcept {
    if (vkFmt >= kFormatTableSize) return false;
    return sFormatTable[vkFmt].isRenderable;
}

bool isFormatBlendable(VkFormat vkFmt) noexcept {
    if (vkFmt >= kFormatTableSize) return false;
    return sFormatTable[vkFmt].isBlendable;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Depth / stencil view helpers
// ═══════════════════════════════════════════════════════════════════════════════

MTLPixelFormat depthOnlyView(VkFormat vkFmt) noexcept {
    switch (vkFmt) {
        case VK::D16_UNORM:            return MTL::Depth16Unorm;
        case VK::X8_D24_UNORM_PACK32:  return MTL::Depth32Float;  // Apple Silicon remap
        case VK::D24_UNORM_S8_UINT:    return MTL::Depth32Float;  // depth-only view of D32F_S8
        case VK::D32_SFLOAT:           return MTL::Depth32Float;
        case VK::D32_SFLOAT_S8_UINT:   return MTL::Depth32Float;  // depth-only view
        case VK::D16_UNORM_S8_UINT:    return MTL::Depth32Float;  // promoted to D32F_S8
        default:                       return MTL::Invalid;
    }
}

MTLPixelFormat stencilOnlyView(VkFormat vkFmt) noexcept {
    switch (vkFmt) {
        case VK::S8_UINT:              return MTL::Stencil8;
        case VK::D24_UNORM_S8_UINT:    return MTL::X32_Stencil8;  // maps to D32F_S8 backing
        case VK::D32_SFLOAT_S8_UINT:   return MTL::X32_Stencil8;
        case VK::D16_UNORM_S8_UINT:    return MTL::X32_Stencil8;  // promoted to D32F_S8
        default:                       return MTL::Invalid;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Vertex format mapping
// ═══════════════════════════════════════════════════════════════════════════════

MTLVertexFormat vkFormatToMTLVertex(VkFormat vkFmt) noexcept {
    switch (vkFmt) {
        // ── 8-bit UNORM ──
        case VK::R8_UNORM:                return MTLV::UCharNormalized;
        case VK::R8G8_UNORM:              return MTLV::UChar2Normalized;
        case VK::R8G8B8_UNORM:            return MTLV::UChar3Normalized;
        case VK::R8G8B8A8_UNORM:          return MTLV::UChar4Normalized;
        // ── 8-bit SNORM ──
        case VK::R8_SNORM:                return MTLV::CharNormalized;
        case VK::R8G8_SNORM:              return MTLV::Char2Normalized;
        case VK::R8G8B8_SNORM:            return MTLV::Char3Normalized;
        case VK::R8G8B8A8_SNORM:          return MTLV::Char4Normalized;
        // ── 8-bit UINT ──
        case VK::R8_UINT:                 return MTLV::UChar;
        case VK::R8G8_UINT:               return MTLV::UChar2;
        case VK::R8G8B8_UINT:             return MTLV::UChar3;
        case VK::R8G8B8A8_UINT:           return MTLV::UChar4;
        // ── 8-bit SINT ──
        case VK::R8_SINT:                 return MTLV::Char;
        case VK::R8G8_SINT:               return MTLV::Char2;
        case VK::R8G8B8_SINT:             return MTLV::Char3;
        case VK::R8G8B8A8_SINT:           return MTLV::Char4;
        // ── 16-bit UNORM ──
        case VK::R16_UNORM:               return MTLV::UShortNormalized;
        case VK::R16G16_UNORM:            return MTLV::UShort2Normalized;
        case VK::R16G16B16_UNORM:         return MTLV::UShort3Normalized;
        case VK::R16G16B16A16_UNORM:      return MTLV::UShort4Normalized;
        // ── 16-bit SNORM ──
        case VK::R16_SNORM:               return MTLV::ShortNormalized;
        case VK::R16G16_SNORM:            return MTLV::Short2Normalized;
        case VK::R16G16B16_SNORM:         return MTLV::Short3Normalized;
        case VK::R16G16B16A16_SNORM:      return MTLV::Short4Normalized;
        // ── 16-bit UINT ──
        case VK::R16_UINT:                return MTLV::UShort;
        case VK::R16G16_UINT:             return MTLV::UShort2;
        case VK::R16G16B16_UINT:          return MTLV::UShort3;
        case VK::R16G16B16A16_UINT:       return MTLV::UShort4;
        // ── 16-bit SINT ──
        case VK::R16_SINT:                return MTLV::Short;
        case VK::R16G16_SINT:             return MTLV::Short2;
        case VK::R16G16B16_SINT:          return MTLV::Short3;
        case VK::R16G16B16A16_SINT:       return MTLV::Short4;
        // ── 16-bit FLOAT (half) ──
        case VK::R16_SFLOAT:              return MTLV::Half;
        case VK::R16G16_SFLOAT:           return MTLV::Half2;
        case VK::R16G16B16_SFLOAT:        return MTLV::Half3;
        case VK::R16G16B16A16_SFLOAT:     return MTLV::Half4;
        // ── 32-bit FLOAT ──
        case VK::R32_SFLOAT:              return MTLV::Float;
        case VK::R32G32_SFLOAT:           return MTLV::Float2;
        case VK::R32G32B32_SFLOAT:        return MTLV::Float3;  // Supported as vertex!
        case VK::R32G32B32A32_SFLOAT:     return MTLV::Float4;
        // ── 32-bit UINT ──
        case VK::R32_UINT:                return MTLV::UInt;
        case VK::R32G32_UINT:             return MTLV::UInt2;
        case VK::R32G32B32_UINT:          return MTLV::UInt3;   // Supported as vertex!
        case VK::R32G32B32A32_UINT:       return MTLV::UInt4;
        // ── 32-bit SINT ──
        case VK::R32_SINT:                return MTLV::Int;
        case VK::R32G32_SINT:             return MTLV::Int2;
        case VK::R32G32B32_SINT:          return MTLV::Int3;    // Supported as vertex!
        case VK::R32G32B32A32_SINT:       return MTLV::Int4;
        // ── 10-bit packed ──
        case VK::A2B10G10R10_UNORM_PACK32: return MTLV::UInt1010102Normalized;
        // ── A8B8G8R8 pack variants (same layout as RGBA8) ──
        case VK::A8B8G8R8_UNORM_PACK32:   return MTLV::UChar4Normalized;
        case VK::A8B8G8R8_SNORM_PACK32:   return MTLV::Char4Normalized;
        case VK::A8B8G8R8_UINT_PACK32:    return MTLV::UChar4;
        case VK::A8B8G8R8_SINT_PACK32:    return MTLV::Char4;
        default:                          return MTLV::Invalid;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Index type mapping
// ═══════════════════════════════════════════════════════════════════════════════

MTLIndexType vkIndexTypeToMTL(VkIndexType vkType) noexcept {
    // VK_INDEX_TYPE_UINT16 = 0 → MTLIndexTypeUInt16 = 0
    // VK_INDEX_TYPE_UINT32 = 1 → MTLIndexTypeUInt32 = 1
    // Values match directly.
    return static_cast<MTLIndexType>(vkType);
}

} // namespace mvrvb
