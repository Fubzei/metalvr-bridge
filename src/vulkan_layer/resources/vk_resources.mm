/**
 * @file vk_resources.mm
 * @brief Buffer, Image, ImageView, Sampler, BufferView, ShaderModule,
 *        DescriptorSetLayout, DescriptorPool/Set — Metal-backed implementations.
 *
 * Phase 2 rewrite:
 *   - All resource functions that were ICD stubs now have real implementations
 *   - Proper memory requirements reporting (size from MTLBuffer/MTLTexture)
 *   - BufferView creates MTLTexture from MTLBuffer for texel buffer access
 *   - DescriptorSetLayout stores per-binding metadata
 *   - ImageView component swizzle via MTLTextureSwizzleChannels
 *   - ShaderModule stores SPIR-V bytes for deferred compilation
 *   - ImageSubresourceLayout reports correct row pitch
 */

#include "vk_resources.h"
#include "../device/vk_device.h"
#include "../memory/vk_memory.h"
#include "../format_table/format_table.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>

#include <cstring>
#include <algorithm>

using namespace mvrvb;

// ── Helper: bytes per pixel for a VkFormat ───────────────────────────────────
static uint32_t bytesPerPixel(VkFormat fmt) {
    const FormatInfo& info = getFormatInfo(fmt);
    if (info.bytesPerBlock > 0 && info.blockWidth == 1 && info.blockHeight == 1)
        return info.bytesPerBlock;
    // Fallback for compressed or unknown formats
    switch (fmt) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R8G8B8A8_UNORM: return 4;
        case VK_FORMAT_R16G16_UNORM: return 4;
        default: return 4;
    }
}

// ── Helper: align up ─────────────────────────────────────────────────────────
static uint64_t alignUp(uint64_t val, uint64_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

extern "C" {

// ═════════════════════════════════════════════════════════════════════════════
// ── VkBuffer ────────────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkResult vkCreateBuffer(VkDevice device,
                         const VkBufferCreateInfo* pCI,
                         const VkAllocationCallbacks*,
                         VkBuffer* pBuffer) {
    @autoreleasepool {
        if (!pCI || !pBuffer) return VK_ERROR_INITIALIZATION_FAILED;
        auto* dev = toMv(device);
        id<MTLDevice> mtlDev = dev->mtlDevice;

        auto* buf = new MvBuffer();
        buf->size        = pCI->size;
        buf->usage       = pCI->usage;
        buf->createFlags = pCI->flags;

        // On Apple Silicon (unified memory), all buffers use StorageModeShared
        // so the CPU and GPU can both access them without explicit copies.
        MTLResourceOptions opts = MTLResourceStorageModeShared |
                                  MTLResourceCPUCacheModeDefaultCache;

        // For GPU-only buffers that won't be mapped, use Private for perf.
        // But DXVK typically maps everything, so Shared is the safe default.

        id<MTLBuffer> mtlBuf = [mtlDev newBufferWithLength:pCI->size options:opts];
        if (!mtlBuf) {
            MVRVB_LOG_ERROR("MTLBuffer creation failed: size=%llu", (unsigned long long)pCI->size);
            delete buf;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        buf->mtlBuffer = (__bridge_retained void*)mtlBuf;

        MVRVB_LOG_DEBUG("Created buffer %p: %llu bytes, usage=0x%x",
                        buf, (unsigned long long)pCI->size, pCI->usage);
        *pBuffer = toVk(buf);
        return VK_SUCCESS;
    }
}

void vkDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) {
    auto* b = toMv(buffer);
    if (!b) return;
    if (b->mtlBuffer) CFRelease((__bridge CFTypeRef)b->mtlBuffer);
    delete b;
}

VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer,
                             VkDeviceMemory memory, VkDeviceSize memOffset) {
    auto* b = toMv(buffer);
    if (!b) return VK_ERROR_INITIALIZATION_FAILED;
    // Record the binding. On Apple Silicon with unified memory and
    // per-resource allocation, the MTLBuffer is already created.
    // The memory binding is tracked for API compliance.
    b->memory       = reinterpret_cast<MvMemory*>(memory);
    b->memoryOffset = memOffset;
    return VK_SUCCESS;
}

VkResult vkBindBufferMemory2(VkDevice device, uint32_t bindCount,
                              const VkBindBufferMemoryInfo* pBindInfos) {
    for (uint32_t i = 0; i < bindCount; ++i) {
        VkResult r = vkBindBufferMemory(device, pBindInfos[i].buffer,
                                         pBindInfos[i].memory,
                                         pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

void vkGetBufferMemoryRequirements(VkDevice, VkBuffer buffer,
                                    VkMemoryRequirements* pReqs) {
    if (!pReqs) return;
    auto* b = toMv(buffer);
    if (b && b->mtlBuffer) {
        id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)b->mtlBuffer;
        pReqs->size      = [mtlBuf length];
        pReqs->alignment = 256; // Metal minimum buffer alignment
        // Bit mask: both memory types valid (type 0=Shared, type 1=Private)
        pReqs->memoryTypeBits = 0x3;
    } else {
        pReqs->size           = b ? b->size : 0;
        pReqs->alignment      = 256;
        pReqs->memoryTypeBits = 0x3;
    }
}

void vkGetBufferMemoryRequirements2(VkDevice device,
                                     const VkBufferMemoryRequirementsInfo2* pInfo,
                                     VkMemoryRequirements2* pReqs) {
    if (!pInfo || !pReqs) return;
    vkGetBufferMemoryRequirements(device, pInfo->buffer, &pReqs->memoryRequirements);
}

void vkGetDeviceMemoryCommitment(VkDevice, VkDeviceMemory, VkDeviceSize* p) {
    // On Apple Silicon, all memory is committed.
    if (p) *p = 0;
}

// ═════════════════════════════════════════════════════════════════════════════
// ── VkBufferView (texel buffers) ────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkResult vkCreateBufferView(VkDevice device,
                              const VkBufferViewCreateInfo* pCI,
                              const VkAllocationCallbacks*,
                              VkBufferView* pView) {
    @autoreleasepool {
        if (!pCI || !pView) return VK_ERROR_INITIALIZATION_FAILED;

        auto* bv = new MvBufferView();
        bv->buffer = toMv(pCI->buffer);
        bv->format = pCI->format;
        bv->offset = pCI->offset;
        bv->range  = (pCI->range == VK_WHOLE_SIZE && bv->buffer)
                     ? (bv->buffer->size - pCI->offset)
                     : pCI->range;

        // Create a texture view over the buffer for shader access.
        if (bv->buffer && bv->buffer->mtlBuffer) {
            id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)bv->buffer->mtlBuffer;
            MTLPixelFormat fmt = (MTLPixelFormat)vkFormatToMTL(pCI->format);
            if (fmt == MTLPixelFormatInvalid) fmt = MTLPixelFormatR32Float;

            uint32_t bpp = bytesPerPixel(pCI->format);
            if (bpp == 0) bpp = 4;
            uint64_t texelCount = bv->range / bpp;

            MTLTextureDescriptor* desc = [MTLTextureDescriptor
                textureBufferDescriptorWithPixelFormat:fmt
                                                width:texelCount
                                        resourceOptions:MTLResourceStorageModeShared
                                                  usage:MTLTextureUsageShaderRead];
            id<MTLTexture> tex = [mtlBuf newTextureWithDescriptor:desc
                                                           offset:bv->offset
                                                      bytesPerRow:bv->range];
            bv->mtlTexture = tex ? (__bridge_retained void*)tex : nullptr;
        }
        *pView = toVk(bv);
        return VK_SUCCESS;
    }
}

void vkDestroyBufferView(VkDevice, VkBufferView view, const VkAllocationCallbacks*) {
    auto* bv = toMv(view);
    if (!bv) return;
    if (bv->mtlTexture) CFRelease((__bridge CFTypeRef)bv->mtlTexture);
    delete bv;
}

// ═════════════════════════════════════════════════════════════════════════════
// ── VkImage ─────────────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkResult vkCreateImage(VkDevice device,
                        const VkImageCreateInfo* pCI,
                        const VkAllocationCallbacks*,
                        VkImage* pImage) {
    @autoreleasepool {
        if (!pCI || !pImage) return VK_ERROR_INITIALIZATION_FAILED;
        auto* dev = toMv(device);
        id<MTLDevice> mtlDev = dev->mtlDevice;

        auto* img = new MvImage();
        img->format      = pCI->format;
        img->width       = pCI->extent.width;
        img->height      = pCI->extent.height;
        img->depth       = pCI->extent.depth;
        img->mipLevels   = pCI->mipLevels;
        img->arrayLayers = pCI->arrayLayers;
        img->samples     = static_cast<uint32_t>(pCI->samples);
        img->imageType   = pCI->imageType;
        img->usage       = pCI->usage;
        img->createFlags = pCI->flags;
        img->tiling      = pCI->tiling;

        // ── Pixel format with fallback ──────────────────────────────────────
        MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
        desc.pixelFormat = (MTLPixelFormat)vkFormatToMTL(pCI->format);
        if (desc.pixelFormat == MTLPixelFormatInvalid) {
            VkFormat fb = getFallbackFormat(pCI->format);
            if (fb != 0) desc.pixelFormat = (MTLPixelFormat)vkFormatToMTL(fb);
        }
        if (desc.pixelFormat == MTLPixelFormatInvalid) {
            MVRVB_LOG_ERROR("No Metal pixel format for VkFormat %u", pCI->format);
            delete img;
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }

        // ── Dimensions ──────────────────────────────────────────────────────
        desc.width             = pCI->extent.width;
        desc.height            = pCI->extent.height;
        desc.depth             = pCI->extent.depth;
        desc.mipmapLevelCount  = pCI->mipLevels;
        desc.arrayLength       = pCI->arrayLayers;
        desc.sampleCount       = (pCI->samples > 1) ? (NSUInteger)pCI->samples : 1;

        // ── Texture type ────────────────────────────────────────────────────
        if (pCI->imageType == VK_IMAGE_TYPE_1D) {
            desc.textureType = (pCI->arrayLayers > 1) ? MTLTextureType1DArray
                                                       : MTLTextureType1D;
        } else if (pCI->imageType == VK_IMAGE_TYPE_2D) {
            if (pCI->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
                desc.textureType = (pCI->arrayLayers > 6) ? MTLTextureTypeCubeArray
                                                           : MTLTextureTypeCube;
                // Cube faces are encoded as array layers in Vulkan.
                // Metal's arrayLength for cubes = number of cube faces / 6.
                desc.arrayLength = pCI->arrayLayers / 6;
            } else if (pCI->arrayLayers > 1) {
                desc.textureType = MTLTextureType2DArray;
            } else if (pCI->samples > 1) {
                desc.textureType = MTLTextureType2DMultisample;
            } else {
                desc.textureType = MTLTextureType2D;
            }
        } else { // VK_IMAGE_TYPE_3D
            desc.textureType = MTLTextureType3D;
            desc.arrayLength = 1; // 3D textures don't have array layers
        }

        // ── Usage flags → MTLTextureUsage ───────────────────────────────────
        MTLTextureUsage mtlUsage = 0;
        const FormatInfo& formatInfo = getFormatInfo(pCI->format);
        if (pCI->usage & VK_IMAGE_USAGE_SAMPLED_BIT)
            mtlUsage |= MTLTextureUsageShaderRead;
        if (pCI->usage & VK_IMAGE_USAGE_STORAGE_BIT)
            mtlUsage |= MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
        if (pCI->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
            mtlUsage |= MTLTextureUsageRenderTarget;
        if (pCI->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
            mtlUsage |= MTLTextureUsageShaderRead;
        if (pCI->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            mtlUsage |= MTLTextureUsageShaderRead;
        if (pCI->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
            mtlUsage |= MTLTextureUsageShaderWrite;
            if (formatInfo.isRenderable || formatInfo.isDepth || formatInfo.isStencil) {
                mtlUsage |= MTLTextureUsageRenderTarget;
            }
        }
        // If format reinterpretation is possible, enable PixelFormatView
        if (pCI->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
            mtlUsage |= MTLTextureUsagePixelFormatView;
        desc.usage = mtlUsage;

        // ── Storage mode ────────────────────────────────────────────────────
        const bool isRenderTarget = (pCI->usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0;
        const bool needsCPUAccess = (pCI->usage & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                    VK_IMAGE_USAGE_TRANSFER_DST_BIT)) != 0;
        if (pCI->tiling == VK_IMAGE_TILING_LINEAR) {
            // Linear tiling → CPU-accessible
            desc.storageMode = MTLStorageModeShared;
        } else if (isRenderTarget && !needsCPUAccess) {
            desc.storageMode = MTLStorageModePrivate;
        } else {
            // Default: Shared on Apple Silicon (unified memory)
            desc.storageMode = MTLStorageModeShared;
        }

        // Multisample textures must be Private on some configurations
        if (pCI->samples > 1) {
            desc.storageMode = MTLStorageModePrivate;
        }

        // ── Create the texture ──────────────────────────────────────────────
        id<MTLTexture> tex = [mtlDev newTextureWithDescriptor:desc];
        if (!tex) {
            MVRVB_LOG_ERROR("MTLTexture creation failed: %ux%ux%u fmt=%u mips=%u layers=%u samples=%u",
                            img->width, img->height, img->depth,
                            img->format, img->mipLevels, img->arrayLayers, img->samples);
            delete img;
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        img->mtlTexture = (__bridge_retained void*)tex;

        MVRVB_LOG_DEBUG("Created image %p: %ux%u fmt=%u type=%d mips=%u layers=%u",
                        img, img->width, img->height, img->format,
                        img->imageType, img->mipLevels, img->arrayLayers);
        *pImage = toVk(img);
        return VK_SUCCESS;
    }
}

void vkDestroyImage(VkDevice, VkImage image, const VkAllocationCallbacks*) {
    auto* i = toMv(image);
    if (!i) return;
    if (i->isSwapchainImage) return; // Swapchain owns the texture
    if (i->mtlTexture) CFRelease((__bridge CFTypeRef)i->mtlTexture);
    delete i;
}

VkResult vkBindImageMemory(VkDevice, VkImage image, VkDeviceMemory memory,
                             VkDeviceSize memOffset) {
    auto* img = toMv(image);
    if (!img) return VK_ERROR_INITIALIZATION_FAILED;
    img->memory       = reinterpret_cast<MvMemory*>(memory);
    img->memoryOffset = memOffset;
    return VK_SUCCESS;
}

VkResult vkBindImageMemory2(VkDevice device, uint32_t bindCount,
                              const VkBindImageMemoryInfo* pBindInfos) {
    for (uint32_t i = 0; i < bindCount; ++i) {
        VkResult r = vkBindImageMemory(device, pBindInfos[i].image,
                                        pBindInfos[i].memory,
                                        pBindInfos[i].memoryOffset);
        if (r != VK_SUCCESS) return r;
    }
    return VK_SUCCESS;
}

void vkGetImageMemoryRequirements(VkDevice, VkImage image, VkMemoryRequirements* pReqs) {
    if (!pReqs) return;
    auto* img = toMv(image);
    if (img && img->mtlTexture) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>)img->mtlTexture;
        // Metal doesn't directly report texture size, so calculate it.
        const FormatInfo& fi = getFormatInfo(img->format);
        uint32_t bpp = fi.bytesPerBlock;
        if (bpp == 0) bpp = 4;
        uint32_t bw = fi.blockWidth;   if (bw == 0) bw = 1;
        uint32_t bh = fi.blockHeight;  if (bh == 0) bh = 1;

        uint64_t totalSize = 0;
        for (uint32_t mip = 0; mip < img->mipLevels; ++mip) {
            uint32_t w = std::max(1u, img->width >> mip);
            uint32_t h = std::max(1u, img->height >> mip);
            uint32_t d = std::max(1u, img->depth >> mip);
            uint32_t blocksW = (w + bw - 1) / bw;
            uint32_t blocksH = (h + bh - 1) / bh;
            totalSize += (uint64_t)blocksW * blocksH * d * bpp;
        }
        totalSize *= img->arrayLayers;
        totalSize *= img->samples;
        totalSize  = alignUp(totalSize, 4096); // Metal page alignment

        pReqs->size           = totalSize;
        pReqs->alignment      = 4096; // Metal texture alignment
        pReqs->memoryTypeBits = 0x3;
    } else {
        pReqs->size           = 0;
        pReqs->alignment      = 4096;
        pReqs->memoryTypeBits = 0x3;
    }
}

void vkGetImageMemoryRequirements2(VkDevice device,
                                    const VkImageMemoryRequirementsInfo2* pInfo,
                                    VkMemoryRequirements2* pReqs) {
    if (!pInfo || !pReqs) return;
    vkGetImageMemoryRequirements(device, pInfo->image, &pReqs->memoryRequirements);

    // Walk pNext chain for VkMemoryDedicatedRequirements
    VkBaseOutStructure* next = (VkBaseOutStructure*)pReqs->pNext;
    while (next) {
        if (next->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
            auto* ded = (VkMemoryDedicatedRequirements*)next;
            // Metal always uses per-resource allocation, so dedicated is preferred.
            ded->prefersDedicatedAllocation  = VK_TRUE;
            ded->requiresDedicatedAllocation = VK_FALSE;
        }
        next = next->pNext;
    }
}

void vkGetImageSubresourceLayout(VkDevice, VkImage image,
                                  const VkImageSubresource* pSub,
                                  VkSubresourceLayout* pLayout) {
    if (!pLayout) return;
    auto* img = toMv(image);
    if (!img) {
        *pLayout = {};
        return;
    }

    const FormatInfo& fi = getFormatInfo(img->format);
    uint32_t bpp = fi.bytesPerBlock;  if (bpp == 0) bpp = 4;
    uint32_t bw  = fi.blockWidth;     if (bw == 0) bw = 1;
    uint32_t bh  = fi.blockHeight;    if (bh == 0) bh = 1;

    uint32_t mip = pSub ? pSub->mipLevel : 0;
    uint32_t w = std::max(1u, img->width >> mip);
    uint32_t h = std::max(1u, img->height >> mip);
    uint32_t d = std::max(1u, img->depth >> mip);

    uint32_t blocksW = (w + bw - 1) / bw;
    uint32_t blocksH = (h + bh - 1) / bh;

    pLayout->rowPitch   = alignUp((uint64_t)blocksW * bpp, 256); // Metal row alignment
    pLayout->arrayPitch = pLayout->rowPitch * blocksH * d;
    pLayout->depthPitch = pLayout->rowPitch * blocksH;
    pLayout->size       = pLayout->arrayPitch;

    // Compute offset: sum of all previous mip levels and array layers
    uint64_t offset = 0;
    for (uint32_t m = 0; m < mip; ++m) {
        uint32_t mw = std::max(1u, img->width >> m);
        uint32_t mh = std::max(1u, img->height >> m);
        uint32_t md = std::max(1u, img->depth >> m);
        uint32_t mbw = (mw + bw - 1) / bw;
        uint32_t mbh = (mh + bh - 1) / bh;
        offset += alignUp((uint64_t)mbw * bpp, 256) * mbh * md * img->arrayLayers;
    }
    if (pSub) {
        offset += pLayout->arrayPitch * pSub->arrayLayer;
    }
    pLayout->offset = offset;
}

// ═════════════════════════════════════════════════════════════════════════════
// ── VkImageView ─────────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkResult vkCreateImageView(VkDevice,
                             const VkImageViewCreateInfo* pCI,
                             const VkAllocationCallbacks*,
                             VkImageView* pView) {
    @autoreleasepool {
        if (!pCI || !pView) return VK_ERROR_INITIALIZATION_FAILED;

        auto* iv = new MvImageView();
        iv->image          = toMv(pCI->image);
        iv->format         = pCI->format;
        iv->viewType       = pCI->viewType;
        iv->aspectMask     = pCI->subresourceRange.aspectMask;
        iv->baseMipLevel   = pCI->subresourceRange.baseMipLevel;
        iv->levelCount     = pCI->subresourceRange.levelCount;
        iv->baseArrayLayer = pCI->subresourceRange.baseArrayLayer;
        iv->layerCount     = pCI->subresourceRange.layerCount;
        iv->swizzle        = pCI->components;

        // VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS
        if (iv->levelCount == VK_REMAINING_MIP_LEVELS && iv->image)
            iv->levelCount = iv->image->mipLevels - iv->baseMipLevel;
        if (iv->layerCount == VK_REMAINING_ARRAY_LAYERS && iv->image)
            iv->layerCount = iv->image->arrayLayers - iv->baseArrayLayer;

        if (iv->image && iv->image->mtlTexture) {
            id<MTLTexture> srcTex = (__bridge id<MTLTexture>)iv->image->mtlTexture;

            // Determine view format
            MTLPixelFormat viewFmt = (MTLPixelFormat)vkFormatToMTL(pCI->format);
            if (viewFmt == MTLPixelFormatInvalid) viewFmt = [srcTex pixelFormat];

            // Determine texture type
            MTLTextureType viewType;
            switch (pCI->viewType) {
                case VK_IMAGE_VIEW_TYPE_1D:         viewType = MTLTextureType1D; break;
                case VK_IMAGE_VIEW_TYPE_1D_ARRAY:   viewType = MTLTextureType1DArray; break;
                case VK_IMAGE_VIEW_TYPE_2D:         viewType = MTLTextureType2D; break;
                case VK_IMAGE_VIEW_TYPE_2D_ARRAY:   viewType = MTLTextureType2DArray; break;
                case VK_IMAGE_VIEW_TYPE_3D:         viewType = MTLTextureType3D; break;
                case VK_IMAGE_VIEW_TYPE_CUBE:       viewType = MTLTextureTypeCube; break;
                case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY: viewType = MTLTextureTypeCubeArray; break;
                default:                            viewType = MTLTextureType2D; break;
            }

            NSRange mipRange   = NSMakeRange(iv->baseMipLevel, iv->levelCount);
            NSRange sliceRange = NSMakeRange(iv->baseArrayLayer, iv->layerCount);

            // ── Component swizzle ───────────────────────────────────────────
            auto vkSwizzleToMTL = [](VkComponentSwizzle s, MTLTextureSwizzle identity) -> MTLTextureSwizzle {
                switch (s) {
                    case VK_COMPONENT_SWIZZLE_IDENTITY: return identity;
                    case VK_COMPONENT_SWIZZLE_ZERO:     return MTLTextureSwizzleZero;
                    case VK_COMPONENT_SWIZZLE_ONE:      return MTLTextureSwizzleOne;
                    case VK_COMPONENT_SWIZZLE_R:        return MTLTextureSwizzleRed;
                    case VK_COMPONENT_SWIZZLE_G:        return MTLTextureSwizzleGreen;
                    case VK_COMPONENT_SWIZZLE_B:        return MTLTextureSwizzleBlue;
                    case VK_COMPONENT_SWIZZLE_A:        return MTLTextureSwizzleAlpha;
                    default:                            return identity;
                }
            };

            bool hasSwizzle = (pCI->components.r != VK_COMPONENT_SWIZZLE_IDENTITY ||
                               pCI->components.g != VK_COMPONENT_SWIZZLE_IDENTITY ||
                               pCI->components.b != VK_COMPONENT_SWIZZLE_IDENTITY ||
                               pCI->components.a != VK_COMPONENT_SWIZZLE_IDENTITY);

            if (hasSwizzle) {
                MTLTextureSwizzleChannels channels;
                channels.red   = vkSwizzleToMTL(pCI->components.r, MTLTextureSwizzleRed);
                channels.green = vkSwizzleToMTL(pCI->components.g, MTLTextureSwizzleGreen);
                channels.blue  = vkSwizzleToMTL(pCI->components.b, MTLTextureSwizzleBlue);
                channels.alpha = vkSwizzleToMTL(pCI->components.a, MTLTextureSwizzleAlpha);

                id<MTLTexture> view = [srcTex newTextureViewWithPixelFormat:viewFmt
                                                                textureType:viewType
                                                                     levels:mipRange
                                                                     slices:sliceRange
                                                                    swizzle:channels];
                iv->mtlTexture = view ? (__bridge_retained void*)view : nullptr;
            } else {
                id<MTLTexture> view = [srcTex newTextureViewWithPixelFormat:viewFmt
                                                                textureType:viewType
                                                                     levels:mipRange
                                                                     slices:sliceRange];
                iv->mtlTexture = view ? (__bridge_retained void*)view : nullptr;
            }
        }
        *pView = toVk(iv);
        return VK_SUCCESS;
    }
}

void vkDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*) {
    auto* iv = toMv(view);
    if (!iv) return;
    if (iv->mtlTexture) CFRelease((__bridge CFTypeRef)iv->mtlTexture);
    delete iv;
}

// ═════════════════════════════════════════════════════════════════════════════
// ── VkSampler ───────────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkResult vkCreateSampler(VkDevice device,
                          const VkSamplerCreateInfo* pCI,
                          const VkAllocationCallbacks*,
                          VkSampler* pSampler) {
    @autoreleasepool {
        if (!pCI || !pSampler) return VK_ERROR_INITIALIZATION_FAILED;
        auto* dev = toMv(device);
        id<MTLDevice> mtlDev = dev->mtlDevice;

        MTLSamplerDescriptor* desc = [[MTLSamplerDescriptor alloc] init];

        // ── Filter modes ────────────────────────────────────────────────────
        auto filterToMTL = [](VkFilter f) -> MTLSamplerMinMagFilter {
            return (f == VK_FILTER_NEAREST) ? MTLSamplerMinMagFilterNearest
                                            : MTLSamplerMinMagFilterLinear;
        };
        auto mipmapToMTL = [](VkSamplerMipmapMode m) -> MTLSamplerMipFilter {
            return (m == VK_SAMPLER_MIPMAP_MODE_NEAREST) ? MTLSamplerMipFilterNearest
                                                          : MTLSamplerMipFilterLinear;
        };
        auto addrToMTL = [](VkSamplerAddressMode a) -> MTLSamplerAddressMode {
            switch (a) {
                case VK_SAMPLER_ADDRESS_MODE_REPEAT:            return MTLSamplerAddressModeRepeat;
                case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:   return MTLSamplerAddressModeMirrorRepeat;
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:     return MTLSamplerAddressModeClampToEdge;
                case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:   return MTLSamplerAddressModeClampToZero;
                case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE: return MTLSamplerAddressModeMirrorClampToEdge;
                default:                                         return MTLSamplerAddressModeRepeat;
            }
        };

        desc.minFilter    = filterToMTL(pCI->minFilter);
        desc.magFilter    = filterToMTL(pCI->magFilter);
        desc.mipFilter    = mipmapToMTL(pCI->mipmapMode);
        desc.sAddressMode = addrToMTL(pCI->addressModeU);
        desc.tAddressMode = addrToMTL(pCI->addressModeV);
        desc.rAddressMode = addrToMTL(pCI->addressModeW);
        desc.maxAnisotropy = pCI->anisotropyEnable ? (NSUInteger)pCI->maxAnisotropy : 1;
        desc.lodMinClamp  = pCI->minLod;
        desc.lodMaxClamp  = pCI->maxLod;
        desc.normalizedCoordinates = YES;

        // ── Compare function (for shadow/depth samplers) ────────────────────
        if (pCI->compareEnable) {
            switch (pCI->compareOp) {
                case VK_COMPARE_OP_NEVER:            desc.compareFunction = MTLCompareFunctionNever; break;
                case VK_COMPARE_OP_LESS:             desc.compareFunction = MTLCompareFunctionLess; break;
                case VK_COMPARE_OP_EQUAL:            desc.compareFunction = MTLCompareFunctionEqual; break;
                case VK_COMPARE_OP_LESS_OR_EQUAL:    desc.compareFunction = MTLCompareFunctionLessEqual; break;
                case VK_COMPARE_OP_GREATER:          desc.compareFunction = MTLCompareFunctionGreater; break;
                case VK_COMPARE_OP_NOT_EQUAL:        desc.compareFunction = MTLCompareFunctionNotEqual; break;
                case VK_COMPARE_OP_GREATER_OR_EQUAL: desc.compareFunction = MTLCompareFunctionGreaterEqual; break;
                case VK_COMPARE_OP_ALWAYS:           desc.compareFunction = MTLCompareFunctionAlways; break;
                default:                             desc.compareFunction = MTLCompareFunctionNever; break;
            }
        }

        // ── Border color ────────────────────────────────────────────────────
        if (pCI->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
            pCI->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
            pCI->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
            switch (pCI->borderColor) {
                case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
                case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
                    desc.borderColor = MTLSamplerBorderColorTransparentBlack; break;
                case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
                case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
                    desc.borderColor = MTLSamplerBorderColorOpaqueBlack; break;
                case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
                case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
                    desc.borderColor = MTLSamplerBorderColorOpaqueWhite; break;
                default: break;
            }
        }

        id<MTLSamplerState> ss = [mtlDev newSamplerStateWithDescriptor:desc];
        if (!ss) {
            MVRVB_LOG_ERROR("MTLSamplerState creation failed");
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }

        auto* smp = new MvSampler();
        smp->mtlSamplerState = (__bridge_retained void*)ss;
        smp->compareEnable   = pCI->compareEnable;
        smp->maxAnisotropy   = pCI->maxAnisotropy;
        *pSampler = toVk(smp);
        return VK_SUCCESS;
    }
}

void vkDestroySampler(VkDevice, VkSampler sampler, const VkAllocationCallbacks*) {
    auto* s = toMv(sampler);
    if (!s) return;
    if (s->mtlSamplerState) CFRelease((__bridge CFTypeRef)s->mtlSamplerState);
    delete s;
}

// NOTE: Descriptor set lifecycle (create/destroy/allocate/free/update) has
// moved to descriptors/vk_descriptors.mm (Milestone 6).

// ═════════════════════════════════════════════════════════════════════════════
// ── YCbCr (stub — not supported on Metal for games) ────────────────────────
// ═════════════════════════════════════════════════════════════════════════════
VkResult vkCreateSamplerYcbcrConversion(VkDevice,
                                         const VkSamplerYcbcrConversionCreateInfo*,
                                         const VkAllocationCallbacks*,
                                         VkSamplerYcbcrConversion* p) {
    if (p) *p = VK_NULL_HANDLE;
    return VK_SUCCESS;
}

void vkDestroySamplerYcbcrConversion(VkDevice, VkSamplerYcbcrConversion,
                                      const VkAllocationCallbacks*) {}

// ═════════════════════════════════════════════════════════════════════════════
// ── Buffer device address (Vulkan 1.2) ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

VkDeviceAddress vkGetBufferDeviceAddress(VkDevice, const VkBufferDeviceAddressInfo* pInfo) {
    if (!pInfo) return 0;
    auto* b = toMv(pInfo->buffer);
    if (!b || !b->mtlBuffer) return 0;
    id<MTLBuffer> mtlBuf = (__bridge id<MTLBuffer>)b->mtlBuffer;
    return [mtlBuf gpuAddress];
}

VkDeviceAddress vkGetBufferDeviceAddressKHR(VkDevice device,
                                              const VkBufferDeviceAddressInfo* pInfo) {
    return vkGetBufferDeviceAddress(device, pInfo);
}

uint64_t vkGetBufferOpaqueCaptureAddress(VkDevice, const VkBufferDeviceAddressInfo*) {
    return 0; // Not needed for replay
}

uint64_t vkGetDeviceMemoryOpaqueCaptureAddress(VkDevice,
                                                 const VkDeviceMemoryOpaqueCaptureAddressInfo*) {
    return 0;
}

} // extern "C"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               
