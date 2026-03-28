#pragma once
/**
 * @file spirv_parser.h
 * @brief SPIR-V binary module parser — Phase 2 rewrite.
 *
 * Parses SPIR-V binary into a structured IR consumed directly by the MSL emitter.
 * Preserves enough semantic information to generate idiomatic MSL without the
 * emitter needing to re-parse raw SPIR-V.
 *
 * Phase 2 changes:
 *   - Instruction-level IR for function bodies (Instruction struct with typed operands)
 *   - Proper composite constant handling
 *   - Extended instruction set tracking (GLSL.std.450)
 *   - Image depth flag parsing for shadow texture detection
 *   - OpTypeFunction support
 *   - Phi node tracking for SSA resolution
 *
 * SPIR-V magic: 0x07230203
 * Supported versions: 1.0 – 1.6
 */

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mvrvb::spirv {

// ── SPIR-V ID type ──────────────────────────────────────────────────────────
using SpvId = uint32_t;

// ── Shader stage ────────────────────────────────────────────────────────────
enum class ShaderStage : uint8_t {
    Vertex,
    Fragment,
    Compute,
    TessControl,
    TessEval,
    Geometry,       // Must be emulated via compute on Metal
    RayGeneration,
    AnyHit,
    ClosestHit,
    Miss,
    Intersection,
    Callable,
    Unknown,
};

// ── Type system ─────────────────────────────────────────────────────────────

enum class BaseType : uint8_t {
    Void, Bool, Int, UInt, Float, Half,
    Struct, Array, RuntimeArray,
    Image, Sampler, SampledImage, AccelStruct,
    Pointer, Function,
};

struct TypeDim {
    uint8_t rows{1};   // For matrices: number of rows in each column vector
    uint8_t cols{1};   // Vectors: component count. Matrices: column count.
};

struct SpvType {
    SpvId      id{};
    BaseType   base{BaseType::Void};
    uint32_t   bitWidth{32};   // For scalar types (Int, UInt, Float, Half)
    bool       isSigned{true};
    TypeDim    dim{};           // Vector: cols=N, rows=1. Matrix: rows=M, cols=N.
    SpvId      elementTypeId{}; // Element/pointed-to/sampled type
    uint32_t   arrayLength{0};  // 0 → runtime array
    bool       isPointer{false};
    uint32_t   storageClass{};  // SpvStorageClass (only for Pointer types)
    // Image type details (only for BaseType::Image)
    uint32_t   imageDim{};      // SpvDim: 0=1D, 1=2D, 2=3D, 3=Cube, 4=Rect, 5=Buffer, 6=SubpassData
    bool       imageIsDepth{false};   // depth texture (for sample_compare)
    bool       imageIsArray{false};
    bool       imageIsMS{false};      // multisampled
    bool       imageIsSampled{true};  // 1=sampled, 0=storage, 2=both
    uint32_t   imageFormat{};         // SpvImageFormat
    // Function type details
    SpvId      returnTypeId{};
    std::vector<SpvId> paramTypeIds;  // Parameter types (for Function type)
    std::string name;           // From OpName if available
};

// ── Variable / constant ─────────────────────────────────────────────────────

enum class StorageClass : uint32_t {
    UniformConstant = 0,
    Input           = 1,
    Uniform         = 2,
    Output          = 3,
    Workgroup       = 4,
    CrossWorkgroup  = 5,
    Private         = 6,
    Function        = 7,
    Generic         = 8,
    PushConstant    = 9,
    AtomicCounter   = 10,
    Image           = 11,
    StorageBuffer   = 12,
    PhysicalStorageBuffer = 5349185,
    CallableDataKHR = 5328,
    IncomingCallableDataKHR = 5329,
    RayPayloadKHR   = 5338,
    HitAttributeKHR = 5339,
    IncomingRayPayloadKHR = 5342,
    ShaderRecordBufferKHR = 5343,
};

struct SpvDecoration {
    uint32_t decoration{};  // SpvDecoration enum value
    uint32_t value{0};      // Associated value (e.g. binding=3, location=2)
};

struct SpvVariable {
    SpvId               id{};
    SpvId               typeId{};        // Pointer type
    StorageClass        storageClass{StorageClass::Function};
    std::string         name;
    std::vector<SpvDecoration> decorations;
    uint32_t            binding{UINT32_MAX};
    uint32_t            set{UINT32_MAX};
    uint32_t            location{UINT32_MAX};
    uint32_t            component{UINT32_MAX};
    uint32_t            inputAttachmentIndex{UINT32_MAX};
    bool                isBuiltin{false};
    uint32_t            builtinKind{};     // SpvBuiltIn
    bool                isFlat{false};
    bool                isNoPerspective{false};
    bool                isNonWritable{false};
    bool                isNonReadable{false};
    bool                isInvariant{false};
    bool                isCentroid{false};
    bool                isSample{false};
    bool                isPatch{false};
};

struct SpvConstant {
    SpvId    id{};
    SpvId    typeId{};
    uint64_t value{0};              // Raw bits (int/float interpreted by type)
    bool     isSpec{false};         // Specialization constant
    uint32_t specId{UINT32_MAX};    // SpecId decoration
    std::vector<SpvId> constituents; // For composite constants
    bool     isComposite{false};
    bool     isBoolTrue{false};     // OpConstantTrue / OpSpecConstantTrue
};

// ── Block / struct member ───────────────────────────────────────────────────

struct MemberInfo {
    uint32_t    offset{};
    uint32_t    matrixStride{};
    uint32_t    arrayStride{};
    bool        colMajor{true};
    bool        isNonWritable{false};
    bool        isNonReadable{false};
    std::string name;
    SpvId       typeId{};
    std::vector<SpvDecoration> decorations;
};

struct SpvBlock {
    SpvId                    typeId{};
    std::string              name;
    std::vector<MemberInfo>  members;
    bool                     isBufferBlock{false}; // SSBO (BufferBlock) vs UBO (Block)
    bool                     isBlock{false};       // Has Block decoration (UBO)
};

// ── Entry point ─────────────────────────────────────────────────────────────

struct EntryPoint {
    SpvId                    functionId{};
    std::string              name;
    ShaderStage              stage{ShaderStage::Unknown};
    std::vector<SpvId>       interfaceVars; // Input/output variable IDs
    // Execution mode annotations
    bool                     originUpperLeft{false};
    bool                     depthReplacing{false};
    bool                     earlyFragTests{false};
    uint32_t                 localSizeX{1}, localSizeY{1}, localSizeZ{1};
    uint32_t                 outputVertices{0};  // Geometry / tessellation
    uint32_t                 outputPrimitive{0}; // Geometry: 0=point,1=line,2=tri
    uint32_t                 inputPrimitive{0};  // Geometry input topology
    uint32_t                 invocations{1};     // Geometry shader invocations
    float                    depthRangeMin{0.f};
    float                    depthRangeMax{1.f};
};

// ── Instruction-level IR ────────────────────────────────────────────────────
// Each instruction inside a function body is decoded into this struct.
// The MSL emitter walks these to generate expressions.

struct Instruction {
    uint32_t                opcode{};       // SpvOp
    SpvId                   resultId{};     // 0 if instruction has no result
    SpvId                   typeId{};       // 0 if instruction has no result type
    std::vector<uint32_t>   operands;       // All remaining operands (IDs and literals)
    // For OpExtInst: operands[0] = ext set ID, operands[1] = ext opcode,
    //               operands[2..] = arguments

    // For control flow:
    SpvId                   labelId{};      // For OpLabel
    std::vector<SpvId>      branchTargets;  // For OpBranch/OpBranchConditional/OpSwitch
    // For OpPhi:
    struct PhiPair { SpvId valueId; SpvId blockId; };
    std::vector<PhiPair>    phiPairs;
};

struct BasicBlock {
    SpvId                       labelId{};
    std::vector<Instruction>    instructions;
    // Merge and continue targets (from OpSelectionMerge / OpLoopMerge)
    SpvId                       mergeBlock{};
    SpvId                       continueBlock{};
    bool                        isLoopHeader{false};
};

struct SpvFunction {
    SpvId                       id{};
    SpvId                       returnTypeId{};
    SpvId                       functionTypeId{};
    uint32_t                    controlMask{};  // SpvFunctionControl
    std::vector<SpvId>          parameterIds;
    std::vector<BasicBlock>     blocks;         // Ordered list of basic blocks
    // Also keep raw words for potential fallback / debugging
    std::vector<uint32_t>       rawWords;
};

// ── Extended instruction set tracking ───────────────────────────────────────

struct ExtInstSet {
    SpvId       id{};
    std::string name;   // "GLSL.std.450", "NonSemantic.DebugInfo", etc.
};

// ── IR module ───────────────────────────────────────────────────────────────

struct SPIRVModule {
    uint32_t   versionMajor{1};
    uint32_t   versionMinor{0};
    uint32_t   generatorMagic{0};
    uint32_t   bound{0};

    std::vector<EntryPoint>  entryPoints;
    std::unordered_map<SpvId, SpvType>      types;
    std::unordered_map<SpvId, SpvVariable>  variables;
    std::unordered_map<SpvId, SpvConstant>  constants;
    std::unordered_map<SpvId, SpvBlock>     blocks;
    std::unordered_map<SpvId, std::string>  names;
    // Fully decoded function bodies with basic block structure
    std::unordered_map<SpvId, SpvFunction>  functions;
    // Extended instruction sets
    std::vector<ExtInstSet>                 extInstSets;
    // Capabilities declared
    std::vector<uint32_t> capabilities;
    // Extensions
    std::vector<std::string> extensions;
    // Addressing model and memory model
    uint32_t addressingModel{0};
    uint32_t memoryModel{0};

    // ── Convenience lookups ──────────────────────────────────────────────

    /// Resolve a pointer type to the type it points to.
    const SpvType* resolvePointedType(SpvId ptrTypeId) const {
        auto it = types.find(ptrTypeId);
        if (it == types.end() || !it->second.isPointer) return nullptr;
        auto jt = types.find(it->second.elementTypeId);
        return jt != types.end() ? &jt->second : nullptr;
    }

    /// Get the entry point for a specific shader stage (first match).
    const EntryPoint* getEntryPoint(ShaderStage stage) const {
        for (auto& ep : entryPoints) {
            if (ep.stage == stage) return &ep;
        }
        return entryPoints.empty() ? nullptr : &entryPoints[0];
    }

    /// Check if a capability is declared.
    bool hasCapability(uint32_t cap) const {
        for (auto c : capabilities) { if (c == cap) return true; }
        return false;
    }

    /// Check if "GLSL.std.450" extended instruction set is imported.
    SpvId glslStd450SetId() const {
        for (auto& ext : extInstSets) {
            if (ext.name == "GLSL.std.450") return ext.id;
        }
        return 0;
    }
};

// ── Parser result ───────────────────────────────────────────────────────────

enum class ParseError {
    None,
    InvalidMagic,
    TruncatedHeader,
    UnsupportedVersion,
    MalformedInstruction,
    UnknownOpcode,
    MultipleEntryPoints, // Warn only — not fatal
};

struct ParseResult {
    ParseError      error{ParseError::None};
    std::string     errorMessage;
    SPIRVModule     module;
    operator bool() const noexcept { return error == ParseError::None; }
};

/**
 * @brief Parse a SPIR-V binary module from 32-bit words.
 *
 * @param words     Pointer to the 32-bit word array.
 * @param wordCount Number of 32-bit words (NOT bytes).
 * @return ParseResult — check .error before using .module.
 */
ParseResult parseSPIRV(const uint32_t* words, size_t wordCount);

/**
 * @brief Parse SPIR-V from raw bytes (handles endianness automatically).
 */
ParseResult parseSPIRVBytes(const uint8_t* bytes, size_t byteCount);

/// Convert a ShaderStage to a human-readable string.
const char* shaderStageName(ShaderStage stage) noexcept;

} // namespace mvrvb::spirv
