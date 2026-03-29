#pragma once
/**
 * @file format_table.h
 * @brief Bidirectional VkFormat ↔ MTLPixelFormat mapping table.
 *
 * Phase 2 upgrade: O(1) flat-array lookup for core formats, hash map fallback
 * for extensions, complete coverage of Vulkan 1.3 core formats, capability
 * query functions, and vertex/index format mapping.
 *
 * Also provides:
 *   - FormatInfo          — per-format metadata (block size, depth/stencil, etc.)
 *   - isFormatSupported() — runtime check against current Metal device
 *   - isFormatFilterable()— can be used with linear filtering
 *   - isFormatRenderable()— can be used as a color render target
 *   - isFormatBlendable() — can be used with hardware blending
 *   - getFallbackFormat() — returns closest supported Metal format
 */

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstddef>

#ifdef __OBJC__
#import <Metal/Metal.h>
#else
// Pure C++ translation units avoid a hard Metal.framework dependency.
using MTLPixelFormat  = uint64_t; // NSUInteger on Apple
using MTLVertexFormat = uint64_t;
using MTLIndexType    = uint32_t;
#endif

// Opaque Metal device handle for capability queries.
// Callers that include Metal.h can pass id<MTLDevice> directly;
// callers in pure-C++ translation units pass nullptr (skips device checks).
#ifdef __OBJC__
@protocol MTLDevice;
using MVBMetalDevice = id<MTLDevice>;
#else
using MVBMetalDevice = void*;
#endif

namespace mvrvb {

// ── Per-format descriptor ────────────────────────────────────────────────────

/// Metadata for a single VkFormat.  Stored in a flat array for O(1) access.
struct FormatInfo {
    VkFormat        vkFormat;          ///< The VkFormat this entry describes
    MTLPixelFormat  mtlFormat;         ///< Corresponding MTLPixelFormat (0 = Invalid)
    VkFormat        fallbackFormat;    ///< Closest supported VkFormat if mtlFormat is Invalid
    uint8_t         blockWidth;        ///< Texels per block width  (1 for uncompressed)
    uint8_t         blockHeight;       ///< Texels per block height (1 for uncompressed)
    uint8_t         bytesPerBlock;     ///< Bytes per pixel (uncompressed) or per block (BCn/ASTC)
    uint8_t         componentCount;    ///< Number of channels
    bool            isDepth;
    bool            isStencil;
    bool            isCompressed;
    bool            isSRGB;
    bool            isFloat;           ///< 16-bit or 32-bit float channel
    bool            isUNorm;
    bool            isSNorm;
    bool            isUInt;
    bool            isSInt;
    bool            isFilterable;      ///< Supports linear texture filtering
    bool            isRenderable;      ///< Can be used as a color render target
    bool            isBlendable;       ///< Supports hardware blending
    const char*     name;              ///< Human-readable, e.g. "R8G8B8A8_UNORM"
};

// ── Core lookup functions (O(1) for core formats) ────────────────────────────

/// Translate VkFormat → MTLPixelFormat.  Returns MTLPixelFormatInvalid (0)
/// if no mapping exists.  O(1) for VkFormat < 256, hash lookup otherwise.
MTLPixelFormat vkFormatToMTL(VkFormat vkFmt) noexcept;

/// Translate MTLPixelFormat → VkFormat.  Returns VK_FORMAT_UNDEFINED (0)
/// if no mapping exists.  Uses a separate reverse-lookup table.
VkFormat mtlFormatToVK(MTLPixelFormat mtlFmt) noexcept;

/// Return full metadata for a VkFormat.  Returns a zeroed-out entry with
/// name="UNKNOWN" for unrecognized formats.
const FormatInfo& getFormatInfo(VkFormat vkFmt) noexcept;

/// Return a supported fallback for formats Metal cannot represent exactly.
/// e.g. VK_FORMAT_R8G8B8_UNORM → VK_FORMAT_R8G8B8A8_UNORM (no 24-bit in Metal).
/// Returns VK_FORMAT_UNDEFINED if no fallback is needed (format maps directly).
VkFormat getFallbackFormat(VkFormat vkFmt) noexcept;

// ── Capability queries ───────────────────────────────────────────────────────

/// Is the format supported at all on this device?
/// If device is nullptr, returns true for all formats that have an MTL mapping.
/// On Apple Silicon, D24_UNORM_S8_UINT is NOT supported and automatically
/// remaps to D32_SFLOAT_S8_UINT.
bool isFormatSupported(VkFormat vkFmt, MVBMetalDevice device = nullptr) noexcept;

/// Can this format be sampled with a linear (non-nearest) filter?
/// Integer and compressed formats generally cannot.
bool isFormatFilterable(VkFormat vkFmt) noexcept;

/// Can this format be used as a color render target?
bool isFormatRenderable(VkFormat vkFmt) noexcept;

/// Can this format be used with hardware blending?
bool isFormatBlendable(VkFormat vkFmt) noexcept;

// ── Depth / stencil helpers ──────────────────────────────────────────────────

inline bool formatHasDepth(VkFormat f) noexcept   { return getFormatInfo(f).isDepth;   }
inline bool formatHasStencil(VkFormat f) noexcept { return getFormatInfo(f).isStencil; }

/// Returns a depth-only view MTLPixelFormat for combined depth/stencil formats.
MTLPixelFormat depthOnlyView(VkFormat vkFmt) noexcept;

/// Returns a stencil-only view MTLPixelFormat for combined depth/stencil formats.
MTLPixelFormat stencilOnlyView(VkFormat vkFmt) noexcept;

// ── Vertex format mapping ────────────────────────────────────────────────────

/// Translate a VkFormat used as a vertex attribute to an MTLVertexFormat.
/// Returns MTLVertexFormatInvalid (0) for unsupported formats.
MTLVertexFormat vkFormatToMTLVertex(VkFormat vkFmt) noexcept;

// ── Index type ───────────────────────────────────────────────────────────────

/// VK_INDEX_TYPE_UINT16 (0) → MTLIndexTypeUInt16 (0)
/// VK_INDEX_TYPE_UINT32 (1) → MTLIndexTypeUInt32 (1)
MTLIndexType vkIndexTypeToMTL(VkIndexType vkType) noexcept;

// ── Convenience: bytes-per-pixel or bytes-per-block ──────────────────────────

inline uint32_t formatBytesPerPixel(VkFormat f) noexcept {
    return getFormatInfo(f).bytesPerBlock; // Same for uncompressed (block = 1×1)
}

// ── Phase 2 aliases (match the mvb:: namespace names in the spec) ────────────
// These allow callers using the Phase-2 interface names to compile without
// changes, while keeping backward compat with the Phase-1 mvrvb:: names.

inline MTLPixelFormat  vk_to_mtl(VkFormat f) noexcept          { return vkFormatToMTL(f); }
inline VkFormat        mtl_to_vk(MTLPixelFormat f) noexcept    { return mtlFormatToVK(f); }
inline const FormatInfo* get_format_info(VkFormat f) noexcept  { return &getFormatInfo(f); }
inline MTLVertexFormat vk_to_mtl_vertex(VkFormat f) noexcept   { return vkFormatToMTLVertex(f); }
inline bool is_format_supported(VkFormat f, MVBMetalDevice d = nullptr) noexcept {
    return isFormatSupported(f, d);
}
inline bool is_format_filterable(VkFormat f) noexcept { return isFormatFilterable(f); }
inline bool is_format_renderable(VkFormat f) noexcept { return isFormatRenderable(f); }
inline bool is_format_blendable(VkFormat f) noexcept  { return isFormatBlendable(f);  }

} // namespace mvrvb
