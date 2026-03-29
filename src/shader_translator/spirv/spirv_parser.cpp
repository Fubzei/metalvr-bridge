/**
 * @file spirv_parser.cpp
 * @brief SPIR-V binary parser — Phase 2 rewrite.
 *
 * Parses SPIR-V into the SPIRVModule IR consumed by spirv_to_msl.cpp.
 * Phase 2 changes from Phase 1:
 *   - Instruction-level IR for function bodies (BasicBlock / Instruction structs)
 *   - OpExtInstImport tracking for GLSL.std.450
 *   - OpConstantComposite / OpSpecConstantTrue / OpSpecConstantFalse
 *   - OpTypeFunction support
 *   - Image depth flag parsing (critical for shadow textures)
 *   - Additional decoration flags (Flat, NoPerspective, Invariant, etc.)
 *   - OpMemoryModel, OpSelectionMerge, OpLoopMerge, OpPhi handling
 *
 * Reference: https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
 */

#include "spirv_parser.h"
#include "../../common/logging.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace mvrvb::spirv {

// ═══════════════════════════════════════════════════════════════════════════════
//  SPIR-V opcode constants
// ═══════════════════════════════════════════════════════════════════════════════
enum SpvOp : uint32_t {
    OpNop                       = 0,
    OpSource                    = 3,
    OpSourceExtension           = 4,
    OpName                      = 5,
    OpMemberName                = 6,
    OpString                    = 7,
    OpLine                      = 8,
    OpNoLine                    = 317,
    OpExtension                 = 10,
    OpExtInstImport             = 11,
    OpExtInst                   = 12,
    OpMemoryModel               = 14,
    OpEntryPoint                = 15,
    OpExecutionMode             = 16,
    OpCapability                = 17,
    OpTypeVoid                  = 19,
    OpTypeBool                  = 20,
    OpTypeInt                   = 21,
    OpTypeFloat                 = 22,
    OpTypeVector                = 23,
    OpTypeMatrix                = 24,
    OpTypeImage                 = 25,
    OpTypeSampler               = 26,
    OpTypeSampledImage          = 27,
    OpTypeArray                 = 28,
    OpTypeRuntimeArray          = 29,
    OpTypeStruct                = 30,
    OpTypeOpaque                = 31,
    OpTypePointer               = 32,
    OpTypeFunction              = 33,
    OpTypeForwardPointer        = 39,
    OpConstantTrue              = 41,
    OpConstantFalse             = 42,
    OpConstant                  = 43,
    OpConstantComposite         = 44,
    OpConstantNull              = 46,
    OpSpecConstantTrue          = 48,
    OpSpecConstantFalse         = 49,
    OpSpecConstant              = 50,
    OpSpecConstantComposite     = 51,
    OpSpecConstantOp            = 52,
    OpFunction                  = 54,
    OpFunctionParameter         = 55,
    OpFunctionEnd               = 56,
    OpFunctionCall              = 57,
    OpVariable                  = 59,
    OpLoad                      = 61,
    OpStore                     = 62,
    OpAccessChain               = 65,
    OpInBoundsAccessChain       = 66,
    OpDecorate                  = 71,
    OpMemberDecorate            = 72,
    OpDecorationGroup           = 73,
    OpGroupDecorate             = 74,
    OpVectorShuffle             = 79,
    OpCompositeConstruct        = 80,
    OpCompositeExtract          = 81,
    OpCompositeInsert           = 82,
    OpCopyObject                = 83,
    OpTranspose                 = 84,
    OpConvertFToU               = 109,
    OpConvertFToS               = 110,
    OpConvertSToF               = 111,
    OpConvertUToF               = 112,
    OpUConvert                  = 113,
    OpSConvert                  = 114,
    OpFConvert                  = 115,
    OpBitcast                   = 124,
    OpSNegate                   = 126,
    OpFNegate                   = 127,
    OpIAdd                      = 128,
    OpFAdd                      = 129,
    OpISub                      = 130,
    OpFSub                      = 131,
    OpIMul                      = 132,
    OpFMul                      = 133,
    OpUDiv                      = 134,
    OpSDiv                      = 135,
    OpFDiv                      = 136,
    OpUMod                      = 137,
    OpSRem                      = 138,
    OpSMod                      = 139,
    OpFRem                      = 140,
    OpFMod                      = 141,
    OpVectorTimesScalar         = 142,
    OpMatrixTimesScalar         = 143,
    OpVectorTimesMatrix         = 144,
    OpMatrixTimesVector         = 145,
    OpMatrixTimesMatrix         = 146,
    OpOuterProduct              = 147,
    OpDot                       = 148,
    OpIAddCarry                 = 149,
    OpISubBorrow                = 150,
    OpBitwiseOr                 = 197,
    OpBitwiseXor                = 198,
    OpBitwiseAnd                = 199,
    OpNot                       = 200,
    OpShiftRightLogical         = 194,
    OpShiftRightArithmetic      = 195,
    OpShiftLeftLogical          = 196,
    OpLogicalEqual              = 164,
    OpLogicalNotEqual           = 165,
    OpLogicalOr                 = 166,
    OpLogicalAnd                = 167,
    OpLogicalNot                = 168,
    OpSelect                    = 169,
    OpIEqual                    = 170,
    OpINotEqual                 = 171,
    OpUGreaterThan              = 172,
    OpSGreaterThan              = 173,
    OpUGreaterThanEqual         = 174,
    OpSGreaterThanEqual         = 175,
    OpULessThan                 = 176,
    OpSLessThan                 = 177,
    OpULessThanEqual            = 178,
    OpSLessThanEqual            = 179,
    OpFOrdEqual                 = 180,
    OpFUnordEqual               = 181,
    OpFOrdNotEqual              = 182,
    OpFUnordNotEqual            = 183,
    OpFOrdLessThan              = 184,
    OpFUnordLessThan            = 185,
    OpFOrdGreaterThan           = 186,
    OpFUnordGreaterThan         = 187,
    OpFOrdLessThanEqual         = 188,
    OpFUnordLessThanEqual       = 189,
    OpFOrdGreaterThanEqual      = 190,
    OpFUnordGreaterThanEqual    = 191,
    OpIsNan                     = 156,
    OpIsInf                     = 157,
    OpDPdx                      = 207,
    OpDPdy                      = 208,
    OpFwidth                    = 209,
    OpDPdxFine                  = 210,
    OpDPdyFine                  = 211,
    OpFwidthFine                = 212,
    OpDPdxCoarse                = 213,
    OpDPdyCoarse                = 214,
    OpFwidthCoarse              = 215,
    OpPhi                       = 245,
    OpLoopMerge                 = 246,
    OpSelectionMerge            = 247,
    OpLabel                     = 248,
    OpBranch                    = 249,
    OpBranchConditional         = 250,
    OpSwitch                    = 251,
    OpKill                      = 252,
    OpReturn                    = 253,
    OpReturnValue               = 254,
    OpUnreachable               = 255,
    OpControlBarrier            = 224,
    OpMemoryBarrier             = 225,
    OpAtomicLoad                = 227,
    OpAtomicStore               = 228,
    OpAtomicExchange            = 229,
    OpAtomicCompareExchange     = 230,
    OpAtomicIIncrement          = 232,
    OpAtomicIDecrement          = 233,
    OpAtomicIAdd                = 234,
    OpAtomicISub                = 235,
    OpAtomicSMin                = 236,
    OpAtomicUMin                = 237,
    OpAtomicSMax                = 238,
    OpAtomicUMax                = 239,
    OpAtomicAnd                 = 240,
    OpAtomicOr                  = 241,
    OpAtomicXor                 = 242,
    OpImageSampleImplicitLod    = 87,
    OpImageSampleExplicitLod    = 88,
    OpImageSampleDrefImplicitLod= 89,
    OpImageSampleDrefExplicitLod= 90,
    OpImageSampleProjImplicitLod= 91,
    OpImageSampleProjExplicitLod= 92,
    OpImageFetch                = 95,
    OpImageGather               = 96,
    OpImageDrefGather           = 97,
    OpImageRead                 = 98,
    OpImageWrite                = 99,
    OpImage                     = 100,
    OpImageQueryFormat          = 101,
    OpImageQueryOrder           = 102,
    OpImageQuerySizeLod         = 103,
    OpImageQuerySize            = 104,
    OpImageQueryLod             = 105,
    OpImageQueryLevels          = 106,
    OpImageQuerySamples         = 107,
    OpSampledImage              = 86,
    OpImageTexelPointer         = 60,
    OpDecorateString            = 5632,
    OpExecutionModeId           = 331,
    OpCopyMemory                = 63,
    OpArrayLength               = 68,
    OpEmitVertex                = 218,
    OpEndPrimitive              = 219,
    OpEmitStreamVertex          = 220,
    OpEndStreamPrimitive        = 221,
    OpBitFieldSExtract          = 201,
    OpBitFieldUExtract          = 202,
    OpBitFieldInsert            = 203,
    OpBitReverse                = 204,
    OpBitCount                  = 205,
    OpAny                       = 154,
    OpAll                       = 155,
    OpGroupNonUniformElect      = 333,
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Execution mode constants
// ═══════════════════════════════════════════════════════════════════════════════
enum SpvExecMode : uint32_t {
    ExecModeInvocations          = 0,
    ExecModeSpacingEqual         = 1,
    ExecModeSpacingFractEven     = 2,
    ExecModeSpacingFractOdd      = 3,
    ExecModeVertexOrderCw        = 4,
    ExecModeVertexOrderCcw       = 5,
    ExecModePixelCenterInteger   = 6,
    ExecModeOriginUpperLeft      = 7,
    ExecModeOriginLowerLeft      = 8,
    ExecModeEarlyFragTests       = 9,
    ExecModePointMode            = 10,
    ExecModeXfb                  = 11,
    ExecModeDepthReplacing       = 12,
    ExecModeDepthGreater         = 14,
    ExecModeDepthLess            = 15,
    ExecModeDepthUnchanged       = 16,
    ExecModeLocalSize            = 17,
    ExecModeOutputVertices       = 26,
    ExecModeOutputPoints         = 27,
    ExecModeOutputLineStrip      = 28,
    ExecModeOutputTriangleStrip  = 29,
    ExecModeInputPoints          = 30,
    ExecModeInputLines           = 31,
    ExecModeInputLinesAdjacency  = 32,
    ExecModeTriangles            = 33,
    ExecModeInputTrianglesAdj    = 34,
    ExecModeQuads                = 35,
    ExecModeIsolines             = 36,
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Decoration constants
// ═══════════════════════════════════════════════════════════════════════════════
enum SpvDecorationCode : uint32_t {
    DecorationRelaxedPrecision      = 0,
    DecorationSpecId                = 1,
    DecorationBlock                 = 2,
    DecorationBufferBlock           = 3,
    DecorationRowMajor              = 4,
    DecorationColMajor              = 5,
    DecorationArrayStride           = 6,
    DecorationMatrixStride          = 7,
    DecorationGLSLShared            = 8,
    DecorationBuiltIn               = 11,
    DecorationNoPerspective         = 13,
    DecorationFlat                  = 14,
    DecorationPatch                 = 15,
    DecorationCentroid              = 16,
    DecorationSample                = 17,
    DecorationInvariant             = 18,
    DecorationRestrict              = 19,
    DecorationAliased               = 20,
    DecorationVolatile              = 21,
    DecorationConstant              = 22,
    DecorationCoherent              = 23,
    DecorationNonWritable           = 24,
    DecorationNonReadable           = 25,
    DecorationUniform               = 26,
    DecorationSaturatedConversion   = 28,
    DecorationLocation              = 30,
    DecorationComponent             = 31,
    DecorationIndex                 = 32,
    DecorationBinding               = 33,
    DecorationDescriptorSet         = 34,
    DecorationOffset                = 35,
    DecorationInputAttachmentIndex  = 43,
};

// ═══════════════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════════════

static ShaderStage execModelToStage(uint32_t model) noexcept {
    switch (model) {
        case 0: return ShaderStage::Vertex;
        case 1: return ShaderStage::TessControl;
        case 2: return ShaderStage::TessEval;
        case 3: return ShaderStage::Geometry;
        case 4: return ShaderStage::Fragment;
        case 5: return ShaderStage::Compute;
        case 5313: return ShaderStage::RayGeneration;
        case 5314: return ShaderStage::AnyHit;
        case 5315: return ShaderStage::ClosestHit;
        case 5316: return ShaderStage::Miss;
        case 5317: return ShaderStage::Intersection;
        case 5318: return ShaderStage::Callable;
        default:   return ShaderStage::Unknown;
    }
}

/// Does this opcode produce a result ID (word layout: ... resultType resultId ...)?
/// Returns true for instructions with form: resultType resultId ...
static bool opcodeHasResultType(uint32_t opcode) {
    // Most arithmetic, logical, image, conversion, composite, load, access chain,
    // function call, phi, select, and ext inst instructions have a result type.
    // We use a conservative approach: if the instruction has both result type and
    // result ID, they appear as the first two operand words after the opcode word.
    switch (opcode) {
    case OpTypeVoid: case OpTypeBool: case OpTypeInt: case OpTypeFloat:
    case OpTypeVector: case OpTypeMatrix: case OpTypeImage: case OpTypeSampler:
    case OpTypeSampledImage: case OpTypeArray: case OpTypeRuntimeArray:
    case OpTypeStruct: case OpTypePointer: case OpTypeFunction:
    case OpTypeOpaque: case OpTypeForwardPointer:
        return false; // Type declarations: result ID only (no result type)
    case OpFunction:
        return true;  // result type + result id + control + function type
    case OpLabel:
        return false; // result ID only
    case OpBranch: case OpBranchConditional: case OpSwitch:
    case OpReturn: case OpReturnValue: case OpKill: case OpUnreachable:
    case OpStore: case OpLoopMerge: case OpSelectionMerge:
    case OpMemoryBarrier: case OpControlBarrier:
    case OpEmitVertex: case OpEndPrimitive:
    case OpImageWrite: case OpCopyMemory: case OpNoLine: case OpLine:
    case OpNop: case OpAtomicStore:
        return false; // No result
    default:
        return true;  // Most instructions: result type + result id + operands
    }
}

static bool opcodeHasResultIdOnly(uint32_t opcode) {
    switch (opcode) {
    case OpLabel: case OpTypeVoid: case OpTypeBool: case OpTypeInt:
    case OpTypeFloat: case OpTypeVector: case OpTypeMatrix: case OpTypeImage:
    case OpTypeSampler: case OpTypeSampledImage: case OpTypeArray:
    case OpTypeRuntimeArray: case OpTypeStruct: case OpTypePointer:
    case OpTypeFunction: case OpTypeOpaque:
        return true;
    default:
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Function body parser
// ═══════════════════════════════════════════════════════════════════════════════

/// Parse a sequence of SPIR-V words that comprise a function body
/// (from OpFunction through OpFunctionEnd) into basic blocks with decoded instructions.
static SpvFunction parseFunctionBody(const uint32_t* words, size_t wordCount,
                                      std::function<uint32_t(size_t)> w) {
    SpvFunction func;
    func.rawWords.assign(words, words + wordCount);

    // First instruction must be OpFunction.
    // OpFunction: resultType resultId functionControl functionType
    if (wordCount < 5) return func;
    uint32_t inst0    = w(0);
    uint32_t wordLen0 = inst0 >> 16;
    func.returnTypeId  = w(1);
    func.id            = w(2);
    func.controlMask   = w(3);
    func.functionTypeId= w(4);

    size_t pc = wordLen0;

    // Collect function parameters (OpFunctionParameter)
    while (pc < wordCount) {
        uint32_t inst = w(pc);
        uint32_t op   = inst & 0xFFFF;
        uint32_t len  = inst >> 16;
        if (len == 0 || pc + len > wordCount) break;
        if (op != OpFunctionParameter) break;
        // OpFunctionParameter: resultType resultId
        func.parameterIds.push_back(w(pc + 2));
        pc += len;
    }

    // Parse basic blocks: OpLabel starts each block, ends at next OpLabel or OpFunctionEnd.
    BasicBlock* currentBlock = nullptr;

    while (pc < wordCount) {
        uint32_t inst = w(pc);
        uint32_t op   = inst & 0xFFFF;
        uint32_t len  = inst >> 16;
        if (len == 0 || pc + len > wordCount) break;
        if (op == OpFunctionEnd) break;

        if (op == OpLabel) {
            func.blocks.emplace_back();
            currentBlock = &func.blocks.back();
            currentBlock->labelId = w(pc + 1);
            pc += len;
            continue;
        }

        if (!currentBlock) {
            // Should not happen in well-formed SPIR-V, but skip gracefully.
            pc += len;
            continue;
        }

        // Decode instruction into our IR.
        Instruction decoded;
        decoded.opcode = op;

        // Handle merge instructions (these annotate the current block).
        if (op == OpSelectionMerge) {
            currentBlock->mergeBlock = w(pc + 1);
            pc += len;
            continue;
        }
        if (op == OpLoopMerge) {
            currentBlock->mergeBlock    = w(pc + 1);
            currentBlock->continueBlock = w(pc + 2);
            currentBlock->isLoopHeader  = true;
            pc += len;
            continue;
        }

        // Determine result type and result ID based on opcode class.
        size_t operandStart = 1; // Skip the instruction word itself.

        if (opcodeHasResultType(op)) {
            // Layout: opcode | resultType | resultId | operands...
            if (len >= 3) {
                decoded.typeId   = w(pc + 1);
                decoded.resultId = w(pc + 2);
                operandStart = 3;
            }
        } else if (opcodeHasResultIdOnly(op)) {
            // Layout: opcode | resultId | operands...
            if (len >= 2) {
                decoded.resultId = w(pc + 1);
                operandStart = 2;
            }
        } else {
            // No result: operands start at word 1
            operandStart = 1;
        }

        // Collect remaining words as operands.
        for (size_t i = operandStart; i < len; ++i) {
            decoded.operands.push_back(w(pc + i));
        }

        // Special handling for control flow instructions.
        switch (op) {
        case OpBranch:
            decoded.branchTargets.push_back(w(pc + 1));
            break;
        case OpBranchConditional:
            // condition, trueLabel, falseLabel [, weights...]
            if (len >= 4) {
                decoded.branchTargets.push_back(w(pc + 2)); // true
                decoded.branchTargets.push_back(w(pc + 3)); // false
            }
            break;
        case OpSwitch:
            // selector, default, [literal, label]...
            if (len >= 3) {
                decoded.branchTargets.push_back(w(pc + 2)); // default
                for (size_t i = 3; i + 1 < len; i += 2) {
                    decoded.branchTargets.push_back(w(pc + i + 1)); // case labels
                }
            }
            break;
        case OpPhi:
            // resultType, resultId, [value, parent]...
            for (size_t i = 0; i + 1 < decoded.operands.size(); i += 2) {
                decoded.phiPairs.push_back({decoded.operands[i], decoded.operands[i + 1]});
            }
            break;
        default:
            break;
        }

        currentBlock->instructions.push_back(std::move(decoded));
        pc += len;
    }

    return func;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Main parser
// ═══════════════════════════════════════════════════════════════════════════════

ParseResult parseSPIRV(const uint32_t* words, size_t wordCount) {
    ParseResult result;
    SPIRVModule& mod = result.module;

    if (wordCount < 5) {
        result.error = ParseError::TruncatedHeader;
        result.errorMessage = "SPIR-V header requires at least 5 words";
        return result;
    }

    // ── Validate header ─────────────────────────────────────────────────────
    static constexpr uint32_t kMagic         = 0x07230203;
    static constexpr uint32_t kMagicReversed = 0x03022307;

    uint32_t magic = words[0];
    bool swapEndian = false;

    if (magic == kMagic) {
        // Native endian — good.
    } else if (magic == kMagicReversed) {
        swapEndian = true;
        MVRVB_LOG_WARN("SPIR-V module is big-endian; byte-swapping.");
    } else {
        result.error = ParseError::InvalidMagic;
        result.errorMessage = "Not a SPIR-V module (bad magic number)";
        return result;
    }

    // Byte-swap helper.
    auto w = [&](size_t i) -> uint32_t {
        uint32_t v = words[i];
        if (!swapEndian) return v;
        return ((v & 0xFF) << 24) | (((v >> 8) & 0xFF) << 16) |
               (((v >> 16) & 0xFF) << 8) | ((v >> 24) & 0xFF);
    };

    uint32_t version = w(1);
    mod.versionMajor = (version >> 16) & 0xFF;
    mod.versionMinor = (version >> 8)  & 0xFF;
    mod.generatorMagic = w(2);
    mod.bound          = w(3);
    // w(4) is the schema, always 0.

    MVRVB_LOG_DEBUG("SPIR-V v%u.%u, generator=0x%08x, bound=%u",
                    mod.versionMajor, mod.versionMinor, mod.generatorMagic, mod.bound);

    if (mod.versionMajor > 1 || (mod.versionMajor == 1 && mod.versionMinor > 6)) {
        result.error = ParseError::UnsupportedVersion;
        result.errorMessage = "SPIR-V version > 1.6 not supported";
        return result;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Pre-pass: collect all decorations
    // ═════════════════════════════════════════════════════════════════════════
    std::unordered_map<SpvId, std::vector<SpvDecoration>> decorMap;
    struct MemberDeco { uint32_t memberIdx; SpvDecoration deco; };
    std::unordered_map<SpvId, std::vector<MemberDeco>> memberDecoMap;

    size_t pc = 5;
    while (pc < wordCount) {
        uint32_t inst    = w(pc);
        uint32_t opcode  = inst & 0xFFFF;
        uint32_t wordLen = inst >> 16;
        if (wordLen == 0 || pc + wordLen > wordCount) break;

        if (opcode == OpDecorate) {
            SpvId target = w(pc + 1);
            SpvDecoration d;
            d.decoration = w(pc + 2);
            if (wordLen >= 4) d.value = w(pc + 3);
            decorMap[target].push_back(d);
        } else if (opcode == OpMemberDecorate) {
            SpvId structId  = w(pc + 1);
            uint32_t member = w(pc + 2);
            SpvDecoration d;
            d.decoration = w(pc + 3);
            if (wordLen >= 5) d.value = w(pc + 4);
            memberDecoMap[structId].push_back({member, d});
        }
        pc += wordLen;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Main pass: process all instructions
    // ═════════════════════════════════════════════════════════════════════════
    pc = 5;
    while (pc < wordCount) {
        uint32_t inst    = w(pc);
        uint32_t opcode  = inst & 0xFFFF;
        uint32_t wordLen = inst >> 16;

        if (wordLen == 0 || pc + wordLen > wordCount) {
            result.error = ParseError::MalformedInstruction;
            result.errorMessage = "Instruction word count out of range at word " +
                                  std::to_string(pc);
            return result;
        }

        switch (opcode) {

        // ── Capabilities & extensions ────────────────────────────────────────
        case OpCapability:
            mod.capabilities.push_back(w(pc + 1));
            break;

        case OpExtension: {
            const char* raw = reinterpret_cast<const char*>(&words[pc + 1]);
            mod.extensions.emplace_back(raw, strnlen(raw, (wordLen - 1) * 4));
            break;
        }

        case OpExtInstImport: {
            ExtInstSet ext;
            ext.id = w(pc + 1);
            const char* raw = reinterpret_cast<const char*>(&words[pc + 2]);
            ext.name = std::string(raw, strnlen(raw, (wordLen - 2) * 4));
            mod.extInstSets.push_back(std::move(ext));
            break;
        }

        case OpMemoryModel:
            mod.addressingModel = w(pc + 1);
            mod.memoryModel     = w(pc + 2);
            break;

        // ── Names ────────────────────────────────────────────────────────────
        case OpName: {
            SpvId id = w(pc + 1);
            const char* s = reinterpret_cast<const char*>(&words[pc + 2]);
            mod.names[id] = std::string(s, strnlen(s, (wordLen - 2) * 4));
            break;
        }

        case OpMemberName: {
            SpvId structId = w(pc + 1);
            uint32_t member = w(pc + 2);
            const char* s = reinterpret_cast<const char*>(&words[pc + 3]);
            auto& blk = mod.blocks[structId];
            if (blk.members.size() <= member) blk.members.resize(member + 1);
            blk.members[member].name = std::string(s, strnlen(s, (wordLen - 3) * 4));
            break;
        }

        // ── Entry point ──────────────────────────────────────────────────────
        case OpEntryPoint: {
            EntryPoint ep;
            ep.stage = execModelToStage(w(pc + 1));
            ep.functionId = w(pc + 2);
            const char* rawName = reinterpret_cast<const char*>(&words[pc + 3]);
            ep.name = std::string(rawName, strnlen(rawName, (wordLen - 3) * 4));
            // Interface variables follow the name string.
            size_t strWords = (ep.name.size() + 4) / 4;
            for (uint32_t iv = 3 + (uint32_t)strWords; iv < wordLen; ++iv) {
                ep.interfaceVars.push_back(w(pc + iv));
            }
            mod.entryPoints.push_back(std::move(ep));
            break;
        }

        // ── Execution modes ──────────────────────────────────────────────────
        case OpExecutionMode:
        case OpExecutionModeId: {
            SpvId funcId = w(pc + 1);
            uint32_t mode = w(pc + 2);
            for (auto& ep : mod.entryPoints) {
                if (ep.functionId != funcId) continue;
                switch (mode) {
                    case ExecModeOriginUpperLeft:      ep.originUpperLeft = true; break;
                    case ExecModeDepthReplacing:       ep.depthReplacing = true; break;
                    case ExecModeEarlyFragTests:       ep.earlyFragTests = true; break;
                    case ExecModeLocalSize:
                        if (wordLen >= 6) {
                            ep.localSizeX = w(pc + 3);
                            ep.localSizeY = w(pc + 4);
                            ep.localSizeZ = w(pc + 5);
                        }
                        break;
                    case ExecModeInvocations:
                        if (wordLen >= 4) ep.invocations = w(pc + 3);
                        break;
                    case ExecModeOutputVertices:
                        if (wordLen >= 4) ep.outputVertices = w(pc + 3);
                        break;
                    case ExecModeOutputPoints:        ep.outputPrimitive = 0; break;
                    case ExecModeOutputLineStrip:     ep.outputPrimitive = 1; break;
                    case ExecModeOutputTriangleStrip: ep.outputPrimitive = 2; break;
                    case ExecModeInputPoints:         ep.inputPrimitive = 0; break;
                    case ExecModeInputLines:          ep.inputPrimitive = 1; break;
                    case ExecModeInputLinesAdjacency: ep.inputPrimitive = 2; break;
                    case ExecModeTriangles:           ep.inputPrimitive = 3; break;
                    default: break;
                }
            }
            break;
        }

        // ── Type declarations ────────────────────────────────────────────────
        case OpTypeVoid: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Void;
            mod.types[t.id] = t; break;
        }
        case OpTypeBool: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Bool; t.bitWidth = 1;
            mod.types[t.id] = t; break;
        }
        case OpTypeInt: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Int;
            t.bitWidth = w(pc + 2);
            t.isSigned = (w(pc + 3) != 0);
            if (!t.isSigned) t.base = BaseType::UInt;
            mod.types[t.id] = t; break;
        }
        case OpTypeFloat: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Float;
            t.bitWidth = w(pc + 2);
            if (t.bitWidth == 16) t.base = BaseType::Half;
            mod.types[t.id] = t; break;
        }
        case OpTypeVector: {
            SpvType t; t.id = w(pc + 1);
            t.elementTypeId = w(pc + 2);
            uint32_t count = w(pc + 3);
            if (auto it = mod.types.find(t.elementTypeId); it != mod.types.end()) {
                t.base     = it->second.base;
                t.bitWidth = it->second.bitWidth;
                t.isSigned = it->second.isSigned;
            }
            t.dim = {1, static_cast<uint8_t>(count)};
            mod.types[t.id] = t; break;
        }
        case OpTypeMatrix: {
            SpvType t; t.id = w(pc + 1);
            t.elementTypeId = w(pc + 2);
            uint32_t cols = w(pc + 3);
            if (auto it = mod.types.find(t.elementTypeId); it != mod.types.end()) {
                t.base     = it->second.base;
                t.bitWidth = it->second.bitWidth;
                t.isSigned = it->second.isSigned;
                t.dim = {it->second.dim.cols, static_cast<uint8_t>(cols)};
            }
            mod.types[t.id] = t; break;
        }
        case OpTypeArray: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Array;
            t.elementTypeId = w(pc + 2);
            SpvId lenId = w(pc + 3);
            if (auto it = mod.constants.find(lenId); it != mod.constants.end()) {
                t.arrayLength = static_cast<uint32_t>(it->second.value);
            }
            mod.types[t.id] = t; break;
        }
        case OpTypeRuntimeArray: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::RuntimeArray;
            t.elementTypeId = w(pc + 2);
            t.arrayLength = 0;
            mod.types[t.id] = t; break;
        }
        case OpTypeStruct: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Struct;
            SpvBlock& blk = mod.blocks[t.id];
            blk.typeId = t.id;
            // Ensure members vector is large enough (may already have names from OpMemberName).
            size_t memberCount = wordLen - 2;
            if (blk.members.size() < memberCount) blk.members.resize(memberCount);
            for (uint32_t i = 2; i < wordLen; ++i) {
                blk.members[i - 2].typeId = w(pc + i);
            }
            // Apply member decorations from pre-pass.
            if (auto it = memberDecoMap.find(t.id); it != memberDecoMap.end()) {
                for (auto& md : it->second) {
                    if (md.memberIdx >= blk.members.size()) continue;
                    auto& mem = blk.members[md.memberIdx];
                    mem.decorations.push_back(md.deco);
                    switch (md.deco.decoration) {
                        case DecorationOffset:       mem.offset = md.deco.value; break;
                        case DecorationMatrixStride:  mem.matrixStride = md.deco.value; break;
                        case DecorationArrayStride:   mem.arrayStride = md.deco.value; break;
                        case DecorationColMajor:      mem.colMajor = true; break;
                        case DecorationRowMajor:      mem.colMajor = false; break;
                        case DecorationNonWritable:   mem.isNonWritable = true; break;
                        case DecorationNonReadable:   mem.isNonReadable = true; break;
                        default: break;
                    }
                }
            }
            // Apply struct-level decorations.
            if (auto it = decorMap.find(t.id); it != decorMap.end()) {
                for (auto& d : it->second) {
                    if (d.decoration == DecorationBlock)       blk.isBlock = true;
                    if (d.decoration == DecorationBufferBlock) blk.isBufferBlock = true;
                }
            }
            mod.types[t.id] = t; break;
        }
        case OpTypePointer: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Pointer;
            t.isPointer = true;
            t.storageClass = w(pc + 2);
            t.elementTypeId = w(pc + 3);
            mod.types[t.id] = t; break;
        }
        case OpTypeFunction: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Function;
            t.returnTypeId = w(pc + 2);
            for (uint32_t i = 3; i < wordLen; ++i) {
                t.paramTypeIds.push_back(w(pc + i));
            }
            mod.types[t.id] = t; break;
        }
        case OpTypeSampler: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Sampler;
            mod.types[t.id] = t; break;
        }
        case OpTypeSampledImage: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::SampledImage;
            t.elementTypeId = w(pc + 2); // The Image type it wraps.
            mod.types[t.id] = t; break;
        }
        case OpTypeImage: {
            SpvType t; t.id = w(pc + 1); t.base = BaseType::Image;
            t.elementTypeId = w(pc + 2); // Sampled type (float, int, etc.)
            t.imageDim      = w(pc + 3);
            // Word 4: Depth (0=not depth, 1=depth, 2=unknown — treat 1 and 2 as depth)
            if (wordLen >= 5) {
                uint32_t depthFlag = w(pc + 4);
                t.imageIsDepth = (depthFlag != 0);
            }
            if (wordLen >= 6) t.imageIsArray   = (w(pc + 5) != 0);
            if (wordLen >= 7) t.imageIsMS      = (w(pc + 6) != 0);
            if (wordLen >= 8) t.imageIsSampled = (w(pc + 7) == 1);
            if (wordLen >= 9) t.imageFormat    = w(pc + 8);
            mod.types[t.id] = t; break;
        }

        // ── Constants ────────────────────────────────────────────────────────
        case OpConstant:
        case OpSpecConstant: {
            SpvConstant c;
            c.typeId = w(pc + 1); c.id = w(pc + 2);
            c.value = w(pc + 3);
            if (wordLen >= 5) c.value |= ((uint64_t)w(pc + 4) << 32);
            c.isSpec = (opcode == OpSpecConstant);
            if (c.isSpec) {
                if (auto it = decorMap.find(c.id); it != decorMap.end()) {
                    for (auto& d : it->second) {
                        if (d.decoration == DecorationSpecId) c.specId = d.value;
                    }
                }
            }
            mod.constants[c.id] = c; break;
        }
        case OpConstantTrue:
        case OpSpecConstantTrue: {
            SpvConstant c; c.typeId = w(pc + 1); c.id = w(pc + 2);
            c.value = 1; c.isBoolTrue = true;
            c.isSpec = (opcode == OpSpecConstantTrue);
            if (c.isSpec) {
                if (auto it = decorMap.find(c.id); it != decorMap.end()) {
                    for (auto& d : it->second) {
                        if (d.decoration == DecorationSpecId) c.specId = d.value;
                    }
                }
            }
            mod.constants[c.id] = c; break;
        }
        case OpConstantFalse:
        case OpSpecConstantFalse: {
            SpvConstant c; c.typeId = w(pc + 1); c.id = w(pc + 2);
            c.value = 0;
            c.isSpec = (opcode == OpSpecConstantFalse);
            if (c.isSpec) {
                if (auto it = decorMap.find(c.id); it != decorMap.end()) {
                    for (auto& d : it->second) {
                        if (d.decoration == DecorationSpecId) c.specId = d.value;
                    }
                }
            }
            mod.constants[c.id] = c; break;
        }
        case OpConstantComposite:
        case OpSpecConstantComposite: {
            SpvConstant c; c.typeId = w(pc + 1); c.id = w(pc + 2);
            c.isComposite = true;
            c.isSpec = (opcode == OpSpecConstantComposite);
            for (uint32_t i = 3; i < wordLen; ++i) {
                c.constituents.push_back(w(pc + i));
            }
            mod.constants[c.id] = c; break;
        }
        case OpConstantNull: {
            SpvConstant c; c.typeId = w(pc + 1); c.id = w(pc + 2);
            c.value = 0;
            mod.constants[c.id] = c; break;
        }

        // ── Variables ────────────────────────────────────────────────────────
        case OpVariable: {
            SpvVariable v;
            v.typeId = w(pc + 1); v.id = w(pc + 2);
            v.storageClass = static_cast<StorageClass>(w(pc + 3));
            // Attach name.
            if (auto it = mod.names.find(v.id); it != mod.names.end())
                v.name = it->second;
            // Attach decorations from pre-pass.
            if (auto it = decorMap.find(v.id); it != decorMap.end()) {
                v.decorations = it->second;
                for (auto& d : it->second) {
                    switch (d.decoration) {
                        case DecorationBinding:            v.binding  = d.value; break;
                        case DecorationDescriptorSet:      v.set      = d.value; break;
                        case DecorationLocation:           v.location = d.value; break;
                        case DecorationComponent:          v.component = d.value; break;
                        case DecorationInputAttachmentIndex: v.inputAttachmentIndex = d.value; break;
                        case DecorationBuiltIn:            v.isBuiltin = true; v.builtinKind = d.value; break;
                        case DecorationFlat:               v.isFlat = true; break;
                        case DecorationNoPerspective:      v.isNoPerspective = true; break;
                        case DecorationNonWritable:        v.isNonWritable = true; break;
                        case DecorationNonReadable:        v.isNonReadable = true; break;
                        case DecorationInvariant:          v.isInvariant = true; break;
                        case DecorationCentroid:           v.isCentroid = true; break;
                        case DecorationSample:             v.isSample = true; break;
                        case DecorationPatch:              v.isPatch = true; break;
                        default: break;
                    }
                }
            }
            // Tag struct blocks with Block/BufferBlock from their type's decorations.
            if (auto tit = mod.types.find(v.typeId); tit != mod.types.end()) {
                SpvId elemId = tit->second.isPointer ? tit->second.elementTypeId : v.typeId;
                if (auto bit = mod.blocks.find(elemId); bit != mod.blocks.end()) {
                    if (mod.names.count(elemId) && bit->second.name.empty()) {
                        bit->second.name = mod.names[elemId];
                    }
                }
            }
            mod.variables[v.id] = v; break;
        }

        // ── Function bodies ──────────────────────────────────────────────────
        case OpFunction: {
            // Capture all words from OpFunction through OpFunctionEnd.
            size_t startPc = pc;
            size_t endPc   = pc + wordLen;
            while (endPc < wordCount) {
                uint32_t fi  = w(endPc);
                uint32_t fop = fi & 0xFFFF;
                uint32_t flen = fi >> 16;
                if (flen == 0) break;
                endPc += flen;
                if (fop == OpFunctionEnd) break;
            }
            size_t bodyWordCount = endPc - startPc;

            // Build a word-reader lambda for the function body that applies endian swap.
            auto bodyW = [&](size_t i) -> uint32_t { return w(startPc + i); };

            SpvFunction func = parseFunctionBody(&words[startPc], bodyWordCount, bodyW);

            // Attach name if available.
            if (mod.names.count(func.id)) {
                // Name is stored in the names map.
            }

            mod.functions[func.id] = std::move(func);

            // Advance past OpFunctionEnd.
            pc = endPc;
            continue; // Skip the default pc += wordLen.
        }

        default:
            // Unknown / unhandled opcode — skip silently.
            break;
        }

        pc += wordLen;
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  Post-processing
    // ═════════════════════════════════════════════════════════════════════════

    // Resolve names for blocks and types.
    for (auto& [id, blk] : mod.blocks) {
        if (blk.name.empty() && mod.names.count(id)) {
            blk.name = mod.names[id];
        }
    }
    for (auto& [id, typ] : mod.types) {
        if (typ.name.empty() && mod.names.count(id)) {
            typ.name = mod.names[id];
        }
    }

    if (mod.entryPoints.empty()) {
        MVRVB_LOG_WARN("SPIR-V module has no entry points");
    }

    MVRVB_LOG_DEBUG("Parsed SPIR-V: %zu types, %zu variables, %zu functions, "
                    "%zu constants, %zu ext inst sets",
                    mod.types.size(), mod.variables.size(), mod.functions.size(),
                    mod.constants.size(), mod.extInstSets.size());

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Byte-aligned entry point
// ═══════════════════════════════════════════════════════════════════════════════

ParseResult parseSPIRVBytes(const uint8_t* bytes, size_t byteCount) {
    if (byteCount % 4 != 0) {
        ParseResult r;
        r.error = ParseError::MalformedInstruction;
        r.errorMessage = "SPIR-V byte count not aligned to 4 bytes";
        return r;
    }
    return parseSPIRV(reinterpret_cast<const uint32_t*>(bytes), byteCount / 4);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Utility
// ═══════════════════════════════════════════════════════════════════════════════

const char* shaderStageName(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:        return "Vertex";
        case ShaderStage::Fragment:      return "Fragment";
        case ShaderStage::Compute:       return "Compute";
        case ShaderStage::TessControl:   return "TessellationControl";
        case ShaderStage::TessEval:      return "TessellationEvaluation";
        case ShaderStage::Geometry:      return "Geometry";
        case ShaderStage::RayGeneration: return "RayGeneration";
        case ShaderStage::AnyHit:        return "AnyHit";
        case ShaderStage::ClosestHit:    return "ClosestHit";
        case ShaderStage::Miss:          return "Miss";
        case ShaderStage::Intersection:  return "Intersection";
        case ShaderStage::Callable:      return "Callable";
        default:                         return "Unknown";
    }
}

} // namespace mvrvb::spirv
