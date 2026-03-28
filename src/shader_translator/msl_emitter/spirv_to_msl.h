#pragma once
/**
 * @file spirv_to_msl.h
 * @brief SPIR-V IR → Metal Shading Language (MSL) source code generator.
 *
 * Phase 2 rewrite — consumes the instruction-level IR from spirv_parser.h.
 *
 * Converts the SPIRVModule produced by spirv_parser into compilable MSL 2.4+
 * source code.  The emitter handles:
 *
 *   - Vertex, fragment, and compute shader stages
 *   - Uniform buffers (UBO) → constant address space buffers
 *   - Storage buffers (SSBO) → device address space buffers
 *   - Push constants → [[buffer(MVB_PUSH_CONST_SLOT)]]
 *   - Textures + samplers → combined into argument textures/samplers
 *   - Combined SampledImage → automatic sampler pairing
 *   - Shadow/depth textures → depth2d<float> with sample_compare
 *   - Geometry shaders → emit a warning + return stub (needs geometry_emulator)
 *   - Specialization constants → MTLFunctionConstant
 *   - Built-in variables (gl_Position, gl_FragDepth, etc.)
 *   - Stage-in / stage-out structs (vertex + fragment)
 *   - Structured control flow (if/else, loops) from merge annotations
 *   - Phi node → temporary variable resolution
 *   - GLSL.std.450 extended instruction set
 *
 * Binding layout conventions (must match the Vulkan layer):
 *   Buffer slot 0        — Push constants
 *   Buffer slots 1–15    — UBOs / SSBOs (descriptor set 0, binding 0–14)
 *   Buffer slots 16–29   — Descriptor set 1+
 *   Texture slots        — Sequential per descriptor set/binding
 *   Sampler slots        — Sequential per descriptor set/binding
 */

#include "../spirv/spirv_parser.h"
#include <string>

namespace mvrvb::msl {

// ── Metal binding slot constants ─────────────────────────────────────────────
static constexpr uint32_t kPushConstantBufferSlot = 0;
static constexpr uint32_t kFirstUBOSlot           = 1;
static constexpr uint32_t kMaxBufferSlots          = 31;
static constexpr uint32_t kMaxTextureSlots         = 128;
static constexpr uint32_t kMaxSamplerSlots         = 16;

// ── Translation options ───────────────────────────────────────────────────────
struct MSLOptions {
    uint32_t mslVersionMajor{2};
    uint32_t mslVersionMinor{4};

    /// If true, use Metal Argument Buffers for descriptor sets with >16 bindings.
    bool useArgumentBuffers{false};

    /// Flip vertex Y coordinate (Vulkan +Y down → Metal +Y up).
    bool flipVertexY{true};

    /// Remap viewport min/max depth from Vulkan [0,1] (default) — already matches Metal.
    bool remapDepth{false};

    /// Index of the entry point to translate (if module has multiple).
    uint32_t entryPointIndex{0};

    /// Override entry point name (empty → use original name).
    std::string entryPointName;

    /// Emit inline comments referencing SPIR-V IDs (useful for debugging).
    bool emitComments{true};

    /// Prefix all generated symbols with this string to avoid conflicts.
    std::string symbolPrefix{"_mv_"};
};

// ── Binding remapping (filled in by the emitter, used by the Vulkan layer) ───
struct BufferBinding {
    uint32_t set{};
    uint32_t binding{};
    uint32_t metalSlot{};
    bool     isUniform{true};  // false → device (SSBO)
    bool     isPushConst{false};
    std::string name;          // Variable name in MSL
    std::string typeName;      // Struct type name in MSL
};

struct TextureBinding {
    uint32_t set{};
    uint32_t binding{};
    uint32_t metalTextureSlot{};
    uint32_t metalSamplerSlot{UINT32_MAX}; // UINT32_MAX if no paired sampler
    bool     isSampledImage{true};
    bool     isStorageImage{false};
    bool     isDepthTexture{false};
    std::string name;
};

struct SamplerBinding {
    uint32_t set{};
    uint32_t binding{};
    uint32_t metalSamplerSlot{};
    std::string name;
};

struct MSLReflection {
    std::vector<BufferBinding>  buffers;
    std::vector<TextureBinding> textures;
    std::vector<SamplerBinding> samplers;
    uint32_t                    pushConstantsSize{0}; // bytes
    uint32_t                    numThreadgroupsX{1};
    uint32_t                    numThreadgroupsY{1};
    uint32_t                    numThreadgroupsZ{1};
};

// ── Translation result ────────────────────────────────────────────────────────
enum class TranslateError {
    None,
    NoEntryPoint,
    UnsupportedStage,    // e.g. Geometry (emulation path not available here)
    UnsupportedFeature,
    InternalError,
};

struct TranslateResult {
    TranslateError  error{TranslateError::None};
    std::string     errorMessage;
    std::string     mslSource;    // Compilable MSL source code
    MSLReflection   reflection;   // Binding map for the Vulkan layer
    spirv::ShaderStage stage{spirv::ShaderStage::Unknown};

    operator bool() const noexcept { return error == TranslateError::None; }
};

// ── Main entry point ──────────────────────────────────────────────────────────
/**
 * @brief Translate a parsed SPIR-V module to MSL.
 *
 * @param module   Parsed SPIR-V IR (from parseSPIRV / parseSPIRVBytes).
 * @param options  Translation configuration.
 * @return TranslateResult with MSL source and binding reflection data.
 */
TranslateResult translateToMSL(const spirv::SPIRVModule& module,
                                const MSLOptions&         options = {});

} // namespace mvrvb::msl
