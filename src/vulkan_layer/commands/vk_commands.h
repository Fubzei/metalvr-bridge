#pragma once
/**
 * @file vk_commands.h
 * @brief Deferred-encoding command buffer system.
 *
 * Recording model:
 *   vkCmd* calls push tagged DeferredCmd structs into a flat vector.
 *   No Metal encoders are created during recording.
 *
 * Replay model (vkQueueSubmit):
 *   Iterate the command list.  Create/switch Metal encoders on the fly:
 *     BeginRenderPass  → close any active encoder, open MTLRenderCommandEncoder
 *     EndRenderPass    → close render encoder
 *     CopyBuffer etc.  → ensure blit encoder is active
 *     Dispatch         → ensure compute encoder is active
 *
 * Thread safety:
 *   One MvCommandBuffer per recording thread (Vulkan spec requirement).
 *   MvCommandPool is internally synchronized.
 */

#include <vulkan/vulkan.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace mvrvb {

// ── Forward declarations ──────────────────────────────────────────────────────
struct MvPipeline;
struct MvBuffer;
struct MvImage;
struct MvImageView;
struct MvSampler;
struct MvDescriptorSet;

// ── Encoder type (for replay state machine) ───────────────────────────────────
enum class EncoderType : uint8_t { None, Render, Compute, Blit };

// ═══════════════════════════════════════════════════════════════════════════════
// Deferred command tag + data
// ═══════════════════════════════════════════════════════════════════════════════

enum class CmdTag : uint8_t {
    // ── Render pass ───────────────────────────────────────────────────────
    BeginRenderPass,
    EndRenderPass,
    BeginRendering,     // VK_KHR_dynamic_rendering
    EndRendering,
    NextSubpass,
    // ── Pipeline / state binding ─────────────────────────────────────────
    BindPipeline,
    BindDescriptorSets,
    BindVertexBuffers,
    BindIndexBuffer,
    PushConstants,
    // ── Dynamic state ────────────────────────────────────────────────────
    SetViewport,
    SetScissor,
    SetDepthBias,
    SetBlendConstants,
    SetStencilCompareMask,
    SetStencilWriteMask,
    SetStencilReference,
    SetLineWidth,
    SetDepthBounds,
    // ── Draw ──────────────────────────────────────────────────────────────
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    DrawIndirectCount,
    DrawIndexedIndirectCount,
    // ── Compute ───────────────────────────────────────────────────────────
    Dispatch,
    DispatchIndirect,
    // ── Transfer ──────────────────────────────────────────────────────────
    CopyBuffer,
    CopyImage,
    CopyBufferToImage,
    CopyImageToBuffer,
    BlitImage,
    UpdateBuffer,
    FillBuffer,
    // ── Clear ─────────────────────────────────────────────────────────────
    ClearColorImage,
    ClearDepthStencilImage,
    ClearAttachments,
    ResolveImage,
    // ── Synchronization ──────────────────────────────────────────────────
    SetEvent,
    ResetEvent,
    PipelineBarrier,
    PipelineBarrier2,
    // ── Secondary ─────────────────────────────────────────────────────────
    ExecuteCommands,
};

// ── Max inline counts (covers 99%+ of DXVK usage) ────────────────────────────
static constexpr uint32_t kMaxVertexBindings   = 16;
static constexpr uint32_t kMaxDescriptorSets   = 8;
static constexpr uint32_t kMaxDynamicOffsets    = 32;
static constexpr uint32_t kMaxCopyRegions       = 8;
static constexpr uint32_t kMaxClearAttachments  = 8;
static constexpr uint32_t kMaxClearRects        = 4;
static constexpr uint32_t kMaxColorAttachments  = 8;
static constexpr uint32_t kMaxPushConstantBytes = 256;
static constexpr uint32_t kMaxUpdateBufferBytes = 65536;

// ── Render pass attachment for BeginRendering ─────────────────────────────────
struct RenderingAttachment {
    VkImageView      imageView{VK_NULL_HANDLE};
    VkImageLayout    imageLayout{};
    VkResolveModeFlagBits resolveMode{VK_RESOLVE_MODE_NONE};
    VkImageView      resolveImageView{VK_NULL_HANDLE};
    VkAttachmentLoadOp  loadOp{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
    VkAttachmentStoreOp storeOp{VK_ATTACHMENT_STORE_OP_DONT_CARE};
    VkClearValue     clearValue{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// DeferredCmd — tagged union of all command payloads
// ═══════════════════════════════════════════════════════════════════════════════

struct DeferredCmd {
    CmdTag tag;

    union {
        // ── BeginRenderPass ──────────────────────────────────────────────
        struct {
            VkRenderPass  renderPass;
            VkFramebuffer framebuffer;
            VkRect2D      renderArea;
            uint32_t      clearValueCount;
            VkClearValue  clearValues[kMaxColorAttachments + 1]; // +1 for depth
        } beginRenderPass;

        // ── BeginRendering (VK_KHR_dynamic_rendering) ────────────────────
        struct {
            VkRenderingFlags flags;
            VkRect2D         renderArea;
            uint32_t         layerCount;
            uint32_t         viewMask;
            uint32_t         colorAttachmentCount;
            RenderingAttachment colorAttachments[kMaxColorAttachments];
            RenderingAttachment depthAttachment;
            RenderingAttachment stencilAttachment;
            bool hasDepth;
            bool hasStencil;
        } beginRendering;

        // ── BindPipeline ──────────────────────────────────────────────────
        struct {
            VkPipelineBindPoint bindPoint;
            VkPipeline          pipeline;
        } bindPipeline;

        // ── BindDescriptorSets ────────────────────────────────────────────
        struct {
            VkPipelineBindPoint bindPoint;
            VkPipelineLayout    layout;
            uint32_t            firstSet;
            uint32_t            setCount;
            VkDescriptorSet     sets[kMaxDescriptorSets];
            uint32_t            dynamicOffsetCount;
            uint32_t            dynamicOffsets[kMaxDynamicOffsets];
        } bindDescriptorSets;

        // ── BindVertexBuffers ─────────────────────────────────────────────
        struct {
            uint32_t      firstBinding;
            uint32_t      bindingCount;
            VkBuffer      buffers[kMaxVertexBindings];
            VkDeviceSize  offsets[kMaxVertexBindings];
            VkDeviceSize  sizes[kMaxVertexBindings];    // from BindVertexBuffers2
            VkDeviceSize  strides[kMaxVertexBindings];  // from BindVertexBuffers2
            bool          hasSizesStrides;
        } bindVertexBuffers;

        // ── BindIndexBuffer ───────────────────────────────────────────────
        struct {
            VkBuffer     buffer;
            VkDeviceSize offset;
            VkIndexType  indexType;
        } bindIndexBuffer;

        // ── PushConstants ─────────────────────────────────────────────────
        struct {
            VkPipelineLayout   layout;
            VkShaderStageFlags stageFlags;
            uint32_t           offset;
            uint32_t           size;
            uint8_t            data[kMaxPushConstantBytes];
        } pushConstants;

        // ── SetViewport ───────────────────────────────────────────────────
        struct {
            uint32_t   firstViewport;
            uint32_t   viewportCount;
            VkViewport viewports[1]; // only first viewport used on Metal
        } setViewport;

        // ── SetScissor ────────────────────────────────────────────────────
        struct {
            uint32_t firstScissor;
            uint32_t scissorCount;
            VkRect2D scissors[1];
        } setScissor;

        // ── SetDepthBias ──────────────────────────────────────────────────
        struct {
            float constantFactor;
            float clamp;
            float slopeFactor;
        } setDepthBias;

        // ── SetBlendConstants ─────────────────────────────────────────────
        struct { float constants[4]; } setBlendConstants;

        // ── SetStencilCompareMask / WriteMask / Reference ─────────────────
        struct {
            VkStencilFaceFlags faceMask;
            uint32_t           value;
        } setStencilValue; // reused for compare mask, write mask, reference

        // ── Draw ──────────────────────────────────────────────────────────
        struct {
            uint32_t vertexCount;
            uint32_t instanceCount;
            uint32_t firstVertex;
            uint32_t firstInstance;
        } draw;

        // ── DrawIndexed ───────────────────────────────────────────────────
        struct {
            uint32_t indexCount;
            uint32_t instanceCount;
            uint32_t firstIndex;
            int32_t  vertexOffset;
            uint32_t firstInstance;
        } drawIndexed;

        // ── DrawIndirect / DrawIndexedIndirect ────────────────────────────
        struct {
            VkBuffer     buffer;
            VkDeviceSize offset;
            uint32_t     drawCount;
            uint32_t     stride;
        } drawIndirect;

        // ── DrawIndirectCount / DrawIndexedIndirectCount ──────────────────
        struct {
            VkBuffer     buffer;
            VkDeviceSize offset;
            VkBuffer     countBuffer;
            VkDeviceSize countOffset;
            uint32_t     maxDrawCount;
            uint32_t     stride;
        } drawIndirectCount;

        // ── Dispatch ──────────────────────────────────────────────────────
        struct { uint32_t x, y, z; } dispatch;

        // ── DispatchIndirect ──────────────────────────────────────────────
        struct {
            VkBuffer     buffer;
            VkDeviceSize offset;
        } dispatchIndirect;

        // ── CopyBuffer ────────────────────────────────────────────────────
        struct {
            VkBuffer     srcBuffer;
            VkBuffer     dstBuffer;
            uint32_t     regionCount;
            VkBufferCopy regions[kMaxCopyRegions];
        } copyBuffer;

        // ── CopyImage ─────────────────────────────────────────────────────
        struct {
            VkImage       srcImage;
            VkImageLayout srcLayout;
            VkImage       dstImage;
            VkImageLayout dstLayout;
            uint32_t      regionCount;
            VkImageCopy   regions[kMaxCopyRegions];
        } copyImage;

        // ── CopyBufferToImage ─────────────────────────────────────────────
        struct {
            VkBuffer        srcBuffer;
            VkImage         dstImage;
            VkImageLayout   dstLayout;
            uint32_t        regionCount;
            VkBufferImageCopy regions[kMaxCopyRegions];
        } copyBufferToImage;

        // ── CopyImageToBuffer ─────────────────────────────────────────────
        struct {
            VkImage         srcImage;
            VkImageLayout   srcLayout;
            VkBuffer        dstBuffer;
            uint32_t        regionCount;
            VkBufferImageCopy regions[kMaxCopyRegions];
        } copyImageToBuffer;

        // ── BlitImage ─────────────────────────────────────────────────────
        struct {
            VkImage       srcImage;
            VkImageLayout srcLayout;
            VkImage       dstImage;
            VkImageLayout dstLayout;
            uint32_t      regionCount;
            VkImageBlit   regions[kMaxCopyRegions];
            VkFilter      filter;
        } blitImage;

        // ── UpdateBuffer ──────────────────────────────────────────────────
        struct {
            VkBuffer     dstBuffer;
            VkDeviceSize dstOffset;
            uint32_t     dataSize;
            uint32_t     dataBlobIndex;
        } updateBuffer;

        // ── FillBuffer ────────────────────────────────────────────────────
        struct {
            VkBuffer     dstBuffer;
            VkDeviceSize dstOffset;
            VkDeviceSize size;
            uint32_t     data;
        } fillBuffer;

        // ── ClearColorImage ───────────────────────────────────────────────
        struct {
            VkImage            image;
            VkImageLayout      layout;
            VkClearColorValue  color;
            uint32_t           rangeCount;
            VkImageSubresourceRange ranges[kMaxCopyRegions];
        } clearColorImage;

        // ── ClearDepthStencilImage ────────────────────────────────────────
        struct {
            VkImage                   image;
            VkImageLayout             layout;
            VkClearDepthStencilValue  value;
            uint32_t                  rangeCount;
            VkImageSubresourceRange   ranges[kMaxCopyRegions];
        } clearDepthStencilImage;

        // ── ClearAttachments ──────────────────────────────────────────────
        struct {
            uint32_t         attachmentCount;
            VkClearAttachment attachments[kMaxClearAttachments];
            uint32_t         rectCount;
            VkClearRect      rects[kMaxClearRects];
        } clearAttachments;

        // ── ResolveImage ──────────────────────────────────────────────────
        struct {
            VkImage        srcImage;
            VkImageLayout  srcLayout;
            VkImage        dstImage;
            VkImageLayout  dstLayout;
            uint32_t       regionCount;
            VkImageResolve regions[kMaxCopyRegions];
        } resolveImage;

        struct { VkEvent event; } setEvent;
        struct { VkEvent event; } resetEvent;

        // ── ExecuteCommands ───────────────────────────────────────────────
        struct {
            uint32_t        commandBufferCount;
            VkCommandBuffer commandBuffers[4];
        } executeCommands;

        // SetLineWidth / SetDepthBounds
        struct { float value; } setFloat;
        struct { float min; float max; } setDepthBounds;
    };

    // ── Convenience constructors ──────────────────────────────────────────
    DeferredCmd() : tag(CmdTag::PipelineBarrier) { std::memset(this, 0, sizeof(*this)); }
    explicit DeferredCmd(CmdTag t) : tag(t) {}
};

// ═══════════════════════════════════════════════════════════════════════════════
// VkRenderPass — stores attachment metadata for replay
// ═══════════════════════════════════════════════════════════════════════════════

struct AttachmentDesc {
    VkFormat            format{};
    uint32_t            samples{1};
    VkAttachmentLoadOp  loadOp{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
    VkAttachmentStoreOp storeOp{VK_ATTACHMENT_STORE_OP_DONT_CARE};
    VkAttachmentLoadOp  stencilLoadOp{VK_ATTACHMENT_LOAD_OP_DONT_CARE};
    VkAttachmentStoreOp stencilStoreOp{VK_ATTACHMENT_STORE_OP_DONT_CARE};
    VkImageLayout       initialLayout{};
    VkImageLayout       finalLayout{};
};

struct SubpassDesc {
    std::vector<uint32_t> colorAttachments;     // indices into MvRenderPass::attachments
    int32_t               depthAttachment{-1};  // -1 = none
};

struct MvRenderPass {
    std::vector<AttachmentDesc> attachments;
    std::vector<SubpassDesc>    subpasses;
    uint32_t colorAttachmentCount{0};
    bool     hasDepth{false};
    bool     hasStencil{false};
};

// ═══════════════════════════════════════════════════════════════════════════════
// VkFramebuffer — stores image view handles for replay
// ═══════════════════════════════════════════════════════════════════════════════

struct MvFramebuffer {
    std::vector<VkImageView> imageViews;  // raw handles, resolved to MTLTexture at replay
    uint32_t     width{0};
    uint32_t     height{0};
    uint32_t     layers{1};
    MvRenderPass* renderPass{nullptr};
};

// ═══════════════════════════════════════════════════════════════════════════════
// MvCommandBuffer — the deferred command list
// ═══════════════════════════════════════════════════════════════════════════════

enum class CmdBufState : uint8_t { Initial, Recording, Executable, Pending, Invalid };

struct MvCommandBuffer {
    // ── Deferred command list (the core of this design) ───────────────────
    std::vector<DeferredCmd> commands;
    std::vector<std::vector<uint8_t>> inlineDataBlobs;

    CmdBufState state{CmdBufState::Initial};
    void*       pool{nullptr};     ///< MvCommandPool* back-reference
    bool        oneTimeSubmit{false};
    bool        simultaneousUse{false};
    bool        renderPassContinue{false};
    VkCommandBufferLevel level{VK_COMMAND_BUFFER_LEVEL_PRIMARY};

    void record(DeferredCmd&& cmd) { commands.push_back(std::move(cmd)); }
    uint32_t storeInlineData(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        inlineDataBlobs.emplace_back(bytes, bytes + size);
        return static_cast<uint32_t>(inlineDataBlobs.size() - 1);
    }
    const std::vector<uint8_t>* inlineData(uint32_t index) const {
        if (index >= inlineDataBlobs.size()) return nullptr;
        return &inlineDataBlobs[index];
    }
    void reset() {
        commands.clear();
        inlineDataBlobs.clear();
        state = CmdBufState::Initial;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// MvCommandPool
// ═══════════════════════════════════════════════════════════════════════════════

struct MvCommandPool {
    void*    device{nullptr};  ///< MvDevice*
    uint32_t queueFamilyIndex{0};
    VkCommandPoolCreateFlags flags{};
    std::mutex mutex;
    std::vector<MvCommandBuffer*> allocated;
    std::vector<MvCommandBuffer*> freeList;

    MvCommandBuffer* acquire(VkCommandBufferLevel level);
    void release(MvCommandBuffer* cb);
    void resetAll();
    ~MvCommandPool();
};

// ═══════════════════════════════════════════════════════════════════════════════
// Handle casts
// ═══════════════════════════════════════════════════════════════════════════════

inline MvCommandBuffer* toMv(VkCommandBuffer h) { return reinterpret_cast<MvCommandBuffer*>(h); }
inline MvCommandPool*   toMv(VkCommandPool   h) { return reinterpret_cast<MvCommandPool*>(h);   }
inline MvRenderPass*    toMv(VkRenderPass    h) { return reinterpret_cast<MvRenderPass*>(h);    }
inline MvFramebuffer*   toMv(VkFramebuffer   h) { return reinterpret_cast<MvFramebuffer*>(h);   }

inline VkCommandBuffer  toVk(MvCommandBuffer* p) { return reinterpret_cast<VkCommandBuffer>(p); }
inline VkCommandPool    toVk(MvCommandPool*   p) { return reinterpret_cast<VkCommandPool>(p);   }
inline VkRenderPass     toVk(MvRenderPass*    p) { return reinterpret_cast<VkRenderPass>(p);    }
inline VkFramebuffer    toVk(MvFramebuffer*   p) { return reinterpret_cast<VkFramebuffer>(p);   }

} // namespace mvrvb
