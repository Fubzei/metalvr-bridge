/**
 * @file spirv_to_msl.cpp
 * @brief SPIR-V IR → MSL source code emitter — Phase 2 rewrite.
 *
 * Architecture:
 *   1. Collect all interface variables (inputs, outputs, uniforms, push consts).
 *   2. Build type declarations (structs for stage-in/out, UBO blocks).
 *   3. Emit resource declarations (buffers, textures, samplers).
 *   4. Walk the instruction-level IR (BasicBlock → Instruction) and emit MSL.
 *   5. Wrap with entry-point signature.
 *
 * Phase 2 changes from Phase 1:
 *   - Uses SpvFunction/BasicBlock/Instruction IR (not raw words)
 *   - Structured control flow from merge/continue annotations
 *   - Phi nodes → temporary variables
 *   - Proper depth texture handling (depth2d, sample_compare)
 *   - Integer arithmetic, comparison, bitwise, conversion opcodes
 *   - Fragment stage_in for varyings
 *   - Fixed matrix type names (MSL is colsxrows)
 *   - Fixed texture access mode string duplication
 *   - Better AccessChain → member name resolution
 */

#include "spirv_to_msl.h"
#include "../../common/logging.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <sstream>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace mvrvb::msl {

using namespace spirv;

// ── SPIR-V opcode constants (for instruction dispatch) ───────────────────────
// Must match the values in spirv_parser.cpp.
namespace SpvOp {
    // Arithmetic
    constexpr uint32_t SNegate = 126;
    constexpr uint32_t FNegate = 127;
    constexpr uint32_t IAdd = 128;
    constexpr uint32_t FAdd = 129;
    constexpr uint32_t ISub = 130;
    constexpr uint32_t FSub = 131;
    constexpr uint32_t IMul = 132;
    constexpr uint32_t FMul = 133;
    constexpr uint32_t UDiv = 134;
    constexpr uint32_t SDiv = 135;
    constexpr uint32_t FDiv = 136;
    constexpr uint32_t UMod = 137;
    constexpr uint32_t SRem = 138;
    constexpr uint32_t SMod = 139;
    constexpr uint32_t FRem = 140;
    constexpr uint32_t FMod = 141;
    constexpr uint32_t VectorTimesScalar = 142;
    constexpr uint32_t MatrixTimesScalar = 143;
    constexpr uint32_t VectorTimesMatrix = 144;
    constexpr uint32_t MatrixTimesVector = 145;
    constexpr uint32_t MatrixTimesMatrix = 146;
    constexpr uint32_t Dot = 148;
    // Comparison (float)
    constexpr uint32_t FOrdEqual = 180;
    constexpr uint32_t FOrdNotEqual = 182;
    constexpr uint32_t FOrdLessThan = 184;
    constexpr uint32_t FOrdGreaterThan = 186;
    constexpr uint32_t FOrdLessThanEqual = 188;
    constexpr uint32_t FOrdGreaterThanEqual = 190;
    constexpr uint32_t FUnordEqual = 181;
    constexpr uint32_t FUnordNotEqual = 183;
    constexpr uint32_t FUnordLessThan = 185;
    constexpr uint32_t FUnordGreaterThan = 187;
    constexpr uint32_t FUnordLessThanEqual = 189;
    constexpr uint32_t FUnordGreaterThanEqual = 191;
    // Comparison (integer)
    constexpr uint32_t IEqual = 170;
    constexpr uint32_t INotEqual = 171;
    constexpr uint32_t ULessThan = 172;
    constexpr uint32_t SLessThan = 174;
    constexpr uint32_t UGreaterThan = 176;
    constexpr uint32_t SGreaterThan = 178;
    constexpr uint32_t ULessThanEqual = 173;
    constexpr uint32_t SLessThanEqual = 175;
    constexpr uint32_t UGreaterThanEqual = 177;
    constexpr uint32_t SGreaterThanEqual = 179;
    // Logic
    constexpr uint32_t LogicalEqual = 164;
    constexpr uint32_t LogicalNotEqual = 165;
    constexpr uint32_t LogicalOr = 166;
    constexpr uint32_t LogicalAnd = 167;
    constexpr uint32_t LogicalNot = 168;
    constexpr uint32_t Select = 169;
    // Bitwise
    constexpr uint32_t ShiftRightLogical = 194;
    constexpr uint32_t ShiftRightArithmetic = 195;
    constexpr uint32_t ShiftLeftLogical = 196;
    constexpr uint32_t BitwiseOr = 197;
    constexpr uint32_t BitwiseXor = 198;
    constexpr uint32_t BitwiseAnd = 199;
    constexpr uint32_t Not = 200;
    // Conversion
    constexpr uint32_t ConvertFToU = 109;
    constexpr uint32_t ConvertFToS = 110;
    constexpr uint32_t ConvertSToF = 111;
    constexpr uint32_t ConvertUToF = 112;
    constexpr uint32_t UConvert = 113;
    constexpr uint32_t SConvert = 114;
    constexpr uint32_t FConvert = 115;
    constexpr uint32_t Bitcast = 124;
    // Composite / vector
    constexpr uint32_t VectorShuffle = 79;
    constexpr uint32_t CompositeConstruct = 80;
    constexpr uint32_t CompositeExtract = 81;
    constexpr uint32_t CompositeInsert = 82;
    constexpr uint32_t CopyObject = 83;
    constexpr uint32_t Transpose = 84;
    // Memory
    constexpr uint32_t Variable = 59;
    constexpr uint32_t Load = 61;
    constexpr uint32_t Store = 62;
    constexpr uint32_t AccessChain = 65;
    constexpr uint32_t InBoundsAccessChain = 66;
    // Control flow
    constexpr uint32_t Phi = 245;
    constexpr uint32_t LoopMerge = 246;
    constexpr uint32_t SelectionMerge = 247;
    constexpr uint32_t Label = 248;
    constexpr uint32_t Branch = 249;
    constexpr uint32_t BranchConditional = 250;
    constexpr uint32_t Switch = 251;
    constexpr uint32_t Kill = 252;
    constexpr uint32_t Return = 253;
    constexpr uint32_t ReturnValue = 254;
    constexpr uint32_t Unreachable = 255;
    // Function
    constexpr uint32_t Function = 54;
    constexpr uint32_t FunctionParameter = 55;
    constexpr uint32_t FunctionEnd = 56;
    constexpr uint32_t FunctionCall = 57;
    // Image
    constexpr uint32_t ImageSampleImplicitLod = 87;
    constexpr uint32_t ImageSampleExplicitLod = 88;
    constexpr uint32_t ImageSampleDrefImplicitLod = 89;
    constexpr uint32_t ImageSampleDrefExplicitLod = 90;
    constexpr uint32_t ImageFetch = 95;
    constexpr uint32_t ImageRead = 98;
    constexpr uint32_t ImageWrite = 99;
    constexpr uint32_t Image = 100;
    constexpr uint32_t ImageQuerySize = 104;
    constexpr uint32_t ImageQueryLod = 105;
    constexpr uint32_t ImageQueryLevels = 106;
    constexpr uint32_t ImageQuerySamples = 107;
    // Extended
    constexpr uint32_t ExtInst = 12;
    // Atomic
    constexpr uint32_t AtomicLoad = 227;
    constexpr uint32_t AtomicStore = 228;
    constexpr uint32_t AtomicExchange = 229;
    constexpr uint32_t AtomicCompareExchange = 230;
    constexpr uint32_t AtomicIAdd = 234;
    constexpr uint32_t AtomicISub = 235;
    constexpr uint32_t AtomicSMin = 236;
    constexpr uint32_t AtomicUMin = 237;
    constexpr uint32_t AtomicSMax = 238;
    constexpr uint32_t AtomicUMax = 239;
    constexpr uint32_t AtomicAnd = 240;
    constexpr uint32_t AtomicOr = 241;
    constexpr uint32_t AtomicXor = 242;
    // Barrier
    constexpr uint32_t ControlBarrier = 224;
    constexpr uint32_t MemoryBarrier = 225;
    // Derivative
    constexpr uint32_t DPdx = 207;
    constexpr uint32_t DPdy = 208;
    constexpr uint32_t Fwidth = 209;
    constexpr uint32_t DPdxFine = 210;
    constexpr uint32_t DPdyFine = 211;
    constexpr uint32_t FwidthFine = 212;
    constexpr uint32_t DPdxCoarse = 213;
    constexpr uint32_t DPdyCoarse = 214;
    constexpr uint32_t FwidthCoarse = 215;
    // Misc
    constexpr uint32_t Any = 154;
    constexpr uint32_t All = 155;
    constexpr uint32_t IsNan = 156;
    constexpr uint32_t IsInf = 157;
}

// ── Built-in ID → MSL attribute ──────────────────────────────────────────────
static const char* builtinMSLAttr(uint32_t builtIn) noexcept {
    switch (builtIn) {
        case 0:  return "[[position]]";          // Position
        case 1:  return "[[point_size]]";        // PointSize
        case 3:  return "[[clip_distance]]";     // ClipDistance
        case 9:  return "[[depth(any)]]";        // FragDepth
        case 15: return "[[vertex_id]]";         // VertexIndex
        case 16: return "[[instance_id]]";       // InstanceIndex
        case 17: return "[[base_vertex]]";       // BaseVertex
        case 18: return "[[base_instance]]";     // BaseInstance
        case 25: return "[[front_facing]]";      // FrontFacing
        case 26: return "[[primitive_id]]";      // PrimitiveId
        case 28: return "[[sample_id]]";         // SampleId
        case 29: return "[[sample_mask]]";       // SampleMask
        case 31: return "[[position]]";          // FragCoord → position in fragment
        case 34: return "[[threads_per_grid]]";  // NumWorkgroups (legacy)
        case 41: return "[[thread_groups_per_grid]]"; // NumWorkgroups
        case 42: return "[[thread_position_in_threadgroup]]"; // LocalInvocationId
        case 43: return "[[threadgroup_position_in_grid]]";  // WorkgroupId
        case 44: return "[[thread_position_in_grid]]";       // GlobalInvocationId
        case 45: return "[[thread_index_in_threadgroup]]";   // LocalInvocationIndex
        default: return "/* unknown_builtin */";
    }
}

// ── Code emitter helper ──────────────────────────────────────────────────────
class CodeWriter {
public:
    void indent()   noexcept { m_indent += "    "; }
    void dedent()   noexcept { if (m_indent.size() >= 4) m_indent.resize(m_indent.size()-4); }
    void line(const std::string& s) { m_ss << m_indent << s << "\n"; }
    void raw(const std::string& s)  { m_ss << s; }
    void blank()                    { m_ss << "\n"; }
    std::string str() const         { return m_ss.str(); }
private:
    std::ostringstream m_ss;
    std::string        m_indent;
};

// ── Type name emission ───────────────────────────────────────────────────────
// MSL type names from SPIR-V type IR.

static std::string mslScalarType(const SpvType& t) {
    switch (t.base) {
        case BaseType::Bool:   return "bool";
        case BaseType::Int:    return (t.bitWidth == 16) ? "short"  : "int";
        case BaseType::UInt:   return (t.bitWidth == 16) ? "ushort" : "uint";
        case BaseType::Float:
            if (t.bitWidth == 16) return "half";
            if (t.bitWidth == 64) return "float"; // MSL has no double; promote to float
            return "float";
        case BaseType::Half:   return "half";
        case BaseType::Void:   return "void";
        default:               return "/* unknown_scalar */";
    }
}

/// Emit an MSL type name for a SpvType.
/// For vectors: float4, int3, etc.
/// For matrices: float4x4 (MSL convention: columnsXrows, but metal float4x4 = 4 cols of float4).
///   SPIR-V: OpTypeMatrix colType colCount → dim.rows=vecSize, dim.cols=colCount
///   MSL: float{cols}x{rows} where cols=number of column vectors, rows=components per column
static std::string mslTypeName(const SpvType& t, const SPIRVModule& mod) {
    switch (t.base) {
        case BaseType::Bool:
        case BaseType::Int:
        case BaseType::UInt:
        case BaseType::Float:
        case BaseType::Half: {
            std::string base = mslScalarType(t);
            if (t.dim.cols > 1 && t.dim.rows > 1) {
                // Matrix: float{cols}x{rows} in MSL
                return base + std::to_string(t.dim.cols) + "x" + std::to_string(t.dim.rows);
            }
            if (t.dim.cols > 1) {
                // Vector: float{cols}
                return base + std::to_string(t.dim.cols);
            }
            return base;
        }
        case BaseType::Void: return "void";
        case BaseType::Struct: {
            auto bit = mod.blocks.find(t.id);
            if (bit != mod.blocks.end() && !bit->second.name.empty())
                return bit->second.name;
            if (!t.name.empty()) return t.name;
            return "Struct_" + std::to_string(t.id);
        }
        case BaseType::Array: {
            auto it = mod.types.find(t.elementTypeId);
            if (it != mod.types.end()) {
                std::string elem = mslTypeName(it->second, mod);
                if (t.arrayLength > 0)
                    return "array<" + elem + ", " + std::to_string(t.arrayLength) + ">";
                return elem + "* /* runtime_array */";
            }
            return "array_" + std::to_string(t.id);
        }
        case BaseType::RuntimeArray: {
            auto it = mod.types.find(t.elementTypeId);
            if (it != mod.types.end()) {
                return mslTypeName(it->second, mod) + "* /* runtime_array */";
            }
            return "void* /* runtime_array */";
        }
        case BaseType::Image:
        case BaseType::SampledImage: {
            // Determine access mode
            std::string access = t.imageIsSampled ? "sample" : "read_write";
            // Determine element type
            std::string elemType = "float";
            if (t.elementTypeId) {
                auto it = mod.types.find(t.elementTypeId);
                if (it != mod.types.end()) {
                    if (it->second.base == BaseType::Int)  elemType = "int";
                    if (it->second.base == BaseType::UInt) elemType = "uint";
                }
            }
            // Depth texture → depth2d<float>
            if (t.imageIsDepth) {
                switch (t.imageDim) {
                    case 1: return t.imageIsArray
                        ? "depth2d_array<float, access::" + access + ">"
                        : "depth2d<float, access::" + access + ">";
                    case 3: return "depthcube<float, access::" + access + ">";
                    default: return "depth2d<float, access::" + access + ">";
                }
            }
            // Regular texture
            switch (t.imageDim) {
                case 0: return "texture1d<" + elemType + ", access::" + access + ">";
                case 1: return t.imageIsArray
                    ? "texture2d_array<" + elemType + ", access::" + access + ">"
                    : (t.imageIsMS
                        ? "texture2d_ms<" + elemType + ", access::" + access + ">"
                        : "texture2d<" + elemType + ", access::" + access + ">");
                case 2: return "texture3d<" + elemType + ", access::" + access + ">";
                case 3: return t.imageIsArray
                    ? "texturecube_array<" + elemType + ", access::" + access + ">"
                    : "texturecube<" + elemType + ", access::" + access + ">";
                case 5: return "texture_buffer<" + elemType + ", access::" + access + ">";
                default: return "texture2d<" + elemType + ">";
            }
        }
        case BaseType::Sampler: return "sampler";
        case BaseType::Pointer: {
            auto it = mod.types.find(t.elementTypeId);
            if (it != mod.types.end()) {
                return mslTypeName(it->second, mod);
            }
            return "void*";
        }
        default: return "/* unknown_type:" + std::to_string(static_cast<int>(t.base)) + " */";
    }
}

/// Get MSL type name for a SPIR-V type ID.
static std::string mslTypeNameById(SpvId typeId, const SPIRVModule& mod) {
    auto it = mod.types.find(typeId);
    if (it != mod.types.end()) return mslTypeName(it->second, mod);
    return "/* type_" + std::to_string(typeId) + " */";
}

// ── Interface variable collector ─────────────────────────────────────────────
struct InterfaceVar {
    const SpvVariable* var{};
    const SpvType*     pointedType{};  // The type the pointer points to
};

static const SpvType* resolveElemType(const SpvVariable& v, const SPIRVModule& mod) {
    auto it = mod.types.find(v.typeId);
    if (it == mod.types.end()) return nullptr;
    const SpvType* ptrType = &it->second;
    if (ptrType->isPointer || ptrType->base == BaseType::Pointer) {
        auto jt = mod.types.find(ptrType->elementTypeId);
        return jt != mod.types.end() ? &jt->second : ptrType;
    }
    return ptrType;
}

static const SpvType* unwrapArrayType(const SpvType* type, const SPIRVModule& mod) {
    const SpvType* current = type;
    while (current && current->base == BaseType::Array) {
        auto it = mod.types.find(current->elementTypeId);
        current = (it != mod.types.end()) ? &it->second : nullptr;
    }
    return current;
}

static uint32_t descriptorArrayCount(const InterfaceVar& var, const SPIRVModule& mod) {
    uint32_t count = 1;
    const SpvType* current = var.pointedType;

    while (current && current->base == BaseType::Array) {
        count *= std::max(1u, current->arrayLength);
        auto it = mod.types.find(current->elementTypeId);
        current = (it != mod.types.end()) ? &it->second : nullptr;
    }

    return count;
}

// ── GLSL.std.450 opcode → MSL function name ─────────────────────────────────
static const char* glslStd450Name(uint32_t extOp) {
    switch (extOp) {
        case 1:  return "round";
        case 2:  return "round"; // RoundEven
        case 3:  return "trunc";
        case 4:  return "abs";   // FAbs
        case 5:  return "abs";   // SAbs
        case 6:  return "sign";  // FSign
        case 7:  return "sign";  // SSign
        case 8:  return "floor";
        case 9:  return "ceil";
        case 10: return "fract";
        case 13: return "sin";
        case 14: return "cos";
        case 15: return "tan";
        case 16: return "asin";
        case 17: return "acos";
        case 18: return "atan";
        case 19: return "sinh";
        case 20: return "cosh";
        case 21: return "tanh";
        case 22: return "asinh";
        case 23: return "acosh";
        case 24: return "atanh";
        case 25: return "atan2";
        case 26: return "pow";
        case 27: return "exp";
        case 28: return "log";
        case 29: return "exp2";
        case 30: return "log2";
        case 31: return "sqrt";
        case 32: return "rsqrt";
        case 33: return "determinant"; // Determinant
        case 34: return "/* matrix_inverse */"; // MatrixInverse — not built-in in MSL
        case 37: return "fmin";  // FMin
        case 38: return "min";   // UMin
        case 39: return "min";   // SMin
        case 40: return "fmax";  // FMax
        case 41: return "max";   // UMax
        case 42: return "max";   // SMax
        case 43: return "clamp"; // FClamp
        case 44: return "clamp"; // UClamp
        case 45: return "clamp"; // SClamp
        case 46: return "mix";
        case 48: return "step";
        case 49: return "smoothstep";
        case 50: return "fma";
        case 66: return "length";
        case 67: return "distance";
        case 68: return "cross";
        case 69: return "normalize";
        case 70: return "faceforward";
        case 71: return "reflect";
        case 72: return "refract";
        default: return nullptr;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ── Main translator ─────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

TranslateResult translateToMSL(const SPIRVModule& module, const MSLOptions& options) {
    TranslateResult result;

    if (module.entryPoints.empty()) {
        result.error = TranslateError::NoEntryPoint;
        result.errorMessage = "No entry points found in SPIR-V module";
        return result;
    }

    uint32_t epIdx = std::min(options.entryPointIndex,
                              static_cast<uint32_t>(module.entryPoints.size() - 1));
    const EntryPoint& ep = module.entryPoints[epIdx];
    result.stage = ep.stage;

    MVRVB_LOG_DEBUG("Translating %s shader '%s' to MSL %u.%u",
                    shaderStageName(ep.stage), ep.name.c_str(),
                    options.mslVersionMajor, options.mslVersionMinor);

    if (ep.stage == ShaderStage::Geometry) {
        result.error = TranslateError::UnsupportedStage;
        result.errorMessage = "Geometry shaders must go through the geometry emulator";
        MVRVB_LOG_WARN("Geometry shader detected — route through geometry_emulator");
        return result;
    }

    CodeWriter cw;

    // ── Header ──────────────────────────────────────────────────────────────
    cw.line("// MetalVR Bridge — Auto-generated MSL");
    cw.line("// Source: SPIR-V v" + std::to_string(module.versionMajor) + "." +
            std::to_string(module.versionMinor));
    cw.line("// Stage: " + std::string(shaderStageName(ep.stage)));
    cw.line("// Entry: " + ep.name);
    cw.blank();
    cw.line("#include <metal_stdlib>");
    cw.line("#include <simd/simd.h>");
    cw.line("using namespace metal;");
    cw.blank();

    // ── Collect interface variables ──────────────────────────────────────────
    std::vector<InterfaceVar> inputs, outputs, uniformBuffers, storageBuffers,
                              textures, samplers, pushConsts;

    // Build a set of entry-point interface variable IDs for fast lookup
    std::unordered_set<SpvId> epVarIds(ep.interfaceVars.begin(), ep.interfaceVars.end());

    auto classifyVar = [&](const SpvVariable& v) {
        const SpvType* elemType = resolveElemType(v, module);
        switch (v.storageClass) {
            case StorageClass::Input:           inputs.push_back({&v, elemType}); break;
            case StorageClass::Output:          outputs.push_back({&v, elemType}); break;
            case StorageClass::Uniform:         uniformBuffers.push_back({&v, elemType}); break;
            case StorageClass::StorageBuffer:   storageBuffers.push_back({&v, elemType}); break;
            case StorageClass::PushConstant:    pushConsts.push_back({&v, elemType}); break;
            case StorageClass::UniformConstant:
                if (elemType) {
                    if (elemType->base == BaseType::Image ||
                        elemType->base == BaseType::SampledImage) {
                        textures.push_back({&v, elemType});
                    } else if (elemType->base == BaseType::Sampler) {
                        samplers.push_back({&v, elemType});
                    } else {
                        uniformBuffers.push_back({&v, elemType});
                    }
                }
                break;
            default: break;
        }
    };

    // First: entry point interface vars
    for (SpvId varId : ep.interfaceVars) {
        auto it = module.variables.find(varId);
        if (it != module.variables.end()) classifyVar(it->second);
    }
    // Second: non-interface globals (UBOs/SSBOs in Vulkan 1.0 may not be in interface list)
    for (auto& [id, v] : module.variables) {
        if (epVarIds.count(id)) continue;
        if (v.storageClass == StorageClass::Uniform ||
            v.storageClass == StorageClass::StorageBuffer ||
            v.storageClass == StorageClass::PushConstant ||
            v.storageClass == StorageClass::UniformConstant) {
            classifyVar(v);
        }
    }

    // ── Emit specialization constants ────────────────────────────────────────
    bool hasSpecConst = false;
    for (auto& [id, c] : module.constants) {
        if (!c.isSpec) continue;
        if (!hasSpecConst) {
            cw.line("// ── Specialization constants ──");
            hasSpecConst = true;
        }
        std::string typeName = mslTypeNameById(c.typeId, module);
        std::string constName = module.names.count(id)
            ? module.names.at(id)
            : ("spec_" + std::to_string(id));
        cw.line("constant " + typeName + " " + constName + " [[function_constant(" +
                std::to_string(c.specId == UINT32_MAX ? id : c.specId) + ")]];");
    }
    if (hasSpecConst) cw.blank();

    // ── Emit UBO / push constant struct declarations ─────────────────────────
    std::set<SpvId> emittedStructs;

    // Recursive struct emitter — handles nested structs
    std::function<void(SpvId)> emitStruct = [&](SpvId typeId) {
        if (emittedStructs.count(typeId)) return;
        emittedStructs.insert(typeId);

        auto bit = module.blocks.find(typeId);
        if (bit == module.blocks.end()) return;
        const SpvBlock& blk = bit->second;

        // Emit nested structs first
        for (auto& mem : blk.members) {
            auto mt = module.types.find(mem.typeId);
            if (mt != module.types.end() && mt->second.base == BaseType::Struct) {
                emitStruct(mem.typeId);
            }
        }

        std::string sname = blk.name.empty() ? ("Block_" + std::to_string(typeId)) : blk.name;
        cw.line("struct " + sname + " {");
        cw.indent();
        for (size_t mi = 0; mi < blk.members.size(); ++mi) {
            const MemberInfo& mem = blk.members[mi];
            std::string mTypeName = mslTypeNameById(mem.typeId, module);
            std::string mName = mem.name.empty() ? ("m" + std::to_string(mi)) : mem.name;
            std::string comment;
            if (options.emitComments && mem.offset > 0) {
                comment = " /* offset=" + std::to_string(mem.offset) + " */";
            }
            cw.line(mTypeName + " " + mName + ";" + comment);
        }
        cw.dedent();
        cw.line("};");
        cw.blank();
    };

    for (auto& ub : uniformBuffers) {
        if (ub.pointedType && ub.pointedType->base == BaseType::Struct)
            emitStruct(ub.pointedType->id);
    }
    for (auto& sb : storageBuffers) {
        if (sb.pointedType && sb.pointedType->base == BaseType::Struct)
            emitStruct(sb.pointedType->id);
    }
    for (auto& pc : pushConsts) {
        if (pc.pointedType && pc.pointedType->base == BaseType::Struct)
            emitStruct(pc.pointedType->id);
    }

    // ── Helper: valid variable name ──────────────────────────────────────────
    auto validVarName = [](const SpvVariable* v, const char* prefix, uint32_t idx) -> std::string {
        if (!v->name.empty()) return v->name;
        return std::string(prefix) + std::to_string(idx);
    };

    // ── Separate built-in from user-defined interface vars ───────────────────
    std::vector<InterfaceVar> nonBuiltinInputs, nonBuiltinOutputs;
    std::vector<InterfaceVar> builtinInputs, builtinOutputs;
    for (auto& iv : inputs) {
        if (iv.var->isBuiltin) builtinInputs.push_back(iv);
        else                   nonBuiltinInputs.push_back(iv);
    }
    for (auto& iv : outputs) {
        if (iv.var->isBuiltin) builtinOutputs.push_back(iv);
        else                   nonBuiltinOutputs.push_back(iv);
    }

    // ── Emit stage-in struct ─────────────────────────────────────────────────
    // Vertex: stage_in has vertex attributes ([[attribute(N)]])
    // Fragment: stage_in has varyings from vertex ([[user(locnN)]])
    bool emitStageIn = !nonBuiltinInputs.empty();

    if (emitStageIn) {
        cw.line("struct StageIn {");
        cw.indent();
        for (size_t i = 0; i < nonBuiltinInputs.size(); ++i) {
            auto& iv = nonBuiltinInputs[i];
            std::string tname = iv.pointedType ? mslTypeName(*iv.pointedType, module) : "float4";
            std::string vname = validVarName(iv.var, "in_", i);
            uint32_t loc = iv.var->location;
            if (ep.stage == ShaderStage::Vertex) {
                cw.line(tname + " " + vname + " [[attribute(" + std::to_string(loc) + ")]];");
            } else if (ep.stage == ShaderStage::Fragment) {
                std::string interp;
                if (iv.var->isFlat) interp = " [[flat]]";
                else if (iv.var->isNoPerspective) interp = " [[center_no_perspective]]";
                cw.line(tname + " " + vname + " [[user(locn" + std::to_string(loc) + ")]]" +
                        interp + ";");
            } else {
                cw.line(tname + " " + vname + ";");
            }
        }
        cw.dedent();
        cw.line("};");
        cw.blank();
    }

    // ── Emit stage-out struct ────────────────────────────────────────────────
    bool emitStageOut = !nonBuiltinOutputs.empty() ||
                        ep.stage == ShaderStage::Vertex ||
                        ep.depthReplacing;

    if (emitStageOut) {
        cw.line("struct StageOut {");
        cw.indent();
        // Vertex: always emit [[position]]
        if (ep.stage == ShaderStage::Vertex) {
            cw.line("float4 position [[position]];");
        }
        for (size_t i = 0; i < nonBuiltinOutputs.size(); ++i) {
            auto& iv = nonBuiltinOutputs[i];
            std::string tname = iv.pointedType ? mslTypeName(*iv.pointedType, module) : "float4";
            std::string vname = validVarName(iv.var, "out_", i);
            uint32_t loc = iv.var->location;
            if (ep.stage == ShaderStage::Vertex && loc != UINT32_MAX) {
                cw.line(tname + " " + vname + " [[user(locn" + std::to_string(loc) + ")]];");
            } else if (ep.stage == ShaderStage::Fragment && loc != UINT32_MAX) {
                cw.line(tname + " " + vname + " [[color(" + std::to_string(loc) + ")]];");
            } else {
                cw.line(tname + " " + vname + ";");
            }
        }
        // Fragment: depth output
        if (ep.stage == ShaderStage::Fragment && ep.depthReplacing) {
            cw.line("float depth [[depth(any)]];");
        }
        cw.dedent();
        cw.line("};");
        cw.blank();
    }

    // ── Assign Metal buffer/texture/sampler slots ────────────────────────────
    uint32_t bufferSlot  = kFirstUBOSlot;
    uint32_t textureSlot = 0;
    uint32_t samplerSlot = 0;

    // Push constants → slot 0
    for (auto& pc : pushConsts) {
        BufferBinding bb;
        bb.metalSlot  = kPushConstantBufferSlot;
        bb.isPushConst = true;
        bb.name = pc.var->name.empty() ? "pushConst" : pc.var->name;
        bb.typeName = pc.pointedType ? mslTypeName(*pc.pointedType, module) : "PushConstants";
        result.reflection.buffers.push_back(bb);
    }
    // UBOs
    for (auto& ub : uniformBuffers) {
        BufferBinding bb;
        bb.set = ub.var->set; bb.binding = ub.var->binding;
        bb.metalSlot = bufferSlot; bb.isUniform = true;
        bufferSlot += descriptorArrayCount(ub, module);
        bb.name = ub.var->name.empty() ? ("ubo_" + std::to_string(bb.metalSlot)) : ub.var->name;
        bb.typeName = ub.pointedType ? mslTypeName(*ub.pointedType, module) : "UBO";
        result.reflection.buffers.push_back(bb);
    }
    // SSBOs
    for (auto& sb : storageBuffers) {
        BufferBinding bb;
        bb.set = sb.var->set; bb.binding = sb.var->binding;
        bb.metalSlot = bufferSlot; bb.isUniform = false;
        bufferSlot += descriptorArrayCount(sb, module);
        bb.name = sb.var->name.empty() ? ("ssbo_" + std::to_string(bb.metalSlot)) : sb.var->name;
        bb.typeName = sb.pointedType ? mslTypeName(*sb.pointedType, module) : "SSBO";
        result.reflection.buffers.push_back(bb);
    }
    // Textures + samplers
    // Build a map: variable ID → sampler slot for pairing combined image/samplers
    std::unordered_map<SpvId, uint32_t> varToSamplerSlot;
    for (auto& sp : samplers) {
        SamplerBinding sb;
        sb.set = sp.var->set;
        sb.binding = sp.var->binding;
        sb.metalSamplerSlot = samplerSlot;
        sb.name = sp.var->name.empty() ? ("smp_" + std::to_string(sb.metalSamplerSlot)) : sp.var->name;
        result.reflection.samplers.push_back(sb);

        varToSamplerSlot[sp.var->id] = samplerSlot;
        samplerSlot += descriptorArrayCount(sp, module);
    }
    for (auto& tx : textures) {
        const SpvType* textureType = unwrapArrayType(tx.pointedType, module);
        TextureBinding tb;
        tb.set = tx.var->set; tb.binding = tx.var->binding;
        tb.metalTextureSlot = textureSlot;
        textureSlot += descriptorArrayCount(tx, module);
        tb.isSampledImage = (textureType && textureType->base == BaseType::SampledImage);
        tb.isStorageImage  = (textureType && !textureType->imageIsSampled);
        tb.isDepthTexture  = (textureType && textureType->imageIsDepth);
        tb.name = tx.var->name.empty() ? ("tex_" + std::to_string(tb.metalTextureSlot)) : tx.var->name;
        // For sampled images: auto-pair with a sampler at the matching binding
        if (tb.isSampledImage && !samplers.empty()) {
            // Try to find a sampler with the same set/binding pair
            bool found = false;
            for (auto& sp : samplers) {
                if (sp.var->set == tx.var->set && sp.var->binding == tx.var->binding) {
                    tb.metalSamplerSlot = varToSamplerSlot[sp.var->id];
                    found = true;
                    break;
                }
            }
            if (!found && !samplers.empty()) {
                // Fallback: use the first available sampler
                tb.metalSamplerSlot = 0;
            }
        }
        result.reflection.textures.push_back(tb);
    }

    // ── Compute threadgroup size ─────────────────────────────────────────────
    if (ep.stage == ShaderStage::Compute) {
        result.reflection.numThreadgroupsX = ep.localSizeX;
        result.reflection.numThreadgroupsY = ep.localSizeY;
        result.reflection.numThreadgroupsZ = ep.localSizeZ;
    }

    // ── Entry point signature ────────────────────────────────────────────────
    std::string epAttr, epReturn, epName;
    epName = options.entryPointName.empty() ? ep.name : options.entryPointName;

    switch (ep.stage) {
        case ShaderStage::Vertex:
            epAttr   = "vertex";
            epReturn = emitStageOut ? "StageOut" : "float4";
            break;
        case ShaderStage::Fragment:
            epAttr   = "fragment";
            epReturn = emitStageOut ? "StageOut" : "void";
            break;
        case ShaderStage::Compute:
            epAttr   = "kernel";
            epReturn = "void";
            break;
        default:
            epAttr   = "/* unknown_stage */";
            epReturn = "void";
            break;
    }

    // ── Build parameter list ─────────────────────────────────────────────────
    std::vector<std::string> params;

    if (emitStageIn)
        params.push_back("StageIn stageIn [[stage_in]]");

    // Built-in inputs as parameters
    for (auto& iv : builtinInputs) {
        std::string attr = builtinMSLAttr(iv.var->builtinKind);
        std::string tname = iv.pointedType ? mslTypeName(*iv.pointedType, module) : "uint";
        std::string vname = iv.var->name.empty()
            ? ("builtin_" + std::to_string(iv.var->id)) : iv.var->name;
        params.push_back(tname + " " + vname + " " + attr);
    }

    // Compute built-ins
    if (ep.stage == ShaderStage::Compute) {
        params.push_back("uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]]");
        params.push_back("uint3 gl_WorkGroupID [[threadgroup_position_in_grid]]");
        params.push_back("uint3 gl_GlobalInvocationID [[thread_position_in_grid]]");
        params.push_back("uint  gl_LocalInvocationIndex [[thread_index_in_threadgroup]]");
    }

    // Buffers: push constants
    for (auto& bb : result.reflection.buffers) {
        if (!bb.isPushConst) continue;
        params.push_back("constant " + bb.typeName + "& " + bb.name +
                         " [[buffer(" + std::to_string(bb.metalSlot) + ")]]");
    }
    // Buffers: UBOs
    for (auto& bb : result.reflection.buffers) {
        if (bb.isPushConst || !bb.isUniform) continue;
        params.push_back("constant " + bb.typeName + "& " + bb.name +
                         " [[buffer(" + std::to_string(bb.metalSlot) + ")]]");
    }
    // Buffers: SSBOs
    for (auto& bb : result.reflection.buffers) {
        if (bb.isUniform || bb.isPushConst) continue;
        params.push_back("device " + bb.typeName + "* " + bb.name +
                         " [[buffer(" + std::to_string(bb.metalSlot) + ")]]");
    }
    // Textures
    for (auto& tb : result.reflection.textures) {
        auto tvar = std::find_if(textures.begin(), textures.end(),
            [&](const InterfaceVar& iv) { return iv.var->name == tb.name || iv.var->id; });
        // Find the InterfaceVar for this texture to get the type
        const SpvType* texType = nullptr;
        for (auto& tv : textures) {
            if (tv.var->name == tb.name) { texType = tv.pointedType; break; }
        }
        std::string tname = texType ? mslTypeName(*texType, module) : "texture2d<float>";
        params.push_back(tname + " " + tb.name +
                         " [[texture(" + std::to_string(tb.metalTextureSlot) + ")]]");
    }
    // Samplers
    for (auto& sp : samplers) {
        std::string tname = sp.pointedType ? mslTypeName(*sp.pointedType, module) : "sampler";
        std::string vname = sp.var->name.empty()
            ? ("smp_" + std::to_string(varToSamplerSlot[sp.var->id]))
            : sp.var->name;
        params.push_back(tname + " " + vname +
                         " [[sampler(" + std::to_string(varToSamplerSlot[sp.var->id]) + ")]]");
    }

    // ── Emit function signature ──────────────────────────────────────────────
    cw.raw(epAttr + " " + epReturn + " " + epName + "(");
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) cw.raw(",\n    ");
        cw.raw(params[i]);
    }
    cw.raw(")\n");
    cw.line("{");
    cw.indent();

    // ── Function body emission ───────────────────────────────────────────────
    // ID → MSL expression name mapping
    std::unordered_map<SpvId, std::string> idExpr;
    std::unordered_map<SpvId, std::string> idTypeName;

    // Seed interface variable names
    for (size_t i = 0; i < nonBuiltinInputs.size(); ++i) {
        auto& iv = nonBuiltinInputs[i];
        std::string vname = validVarName(iv.var, "in_", i);
        idExpr[iv.var->id] = "stageIn." + vname;
    }
    for (size_t i = 0; i < builtinInputs.size(); ++i) {
        auto& iv = builtinInputs[i];
        std::string vname = iv.var->name.empty()
            ? ("builtin_" + std::to_string(iv.var->id)) : iv.var->name;
        idExpr[iv.var->id] = vname;
    }
    for (size_t i = 0; i < nonBuiltinOutputs.size(); ++i) {
        auto& iv = nonBuiltinOutputs[i];
        std::string vname = validVarName(iv.var, "out_", i);
        idExpr[iv.var->id] = "stageOut." + vname;
    }
    // Built-in outputs (gl_Position → stageOut.position)
    for (auto& iv : builtinOutputs) {
        if (iv.var->builtinKind == 0) { // Position
            idExpr[iv.var->id] = "stageOut.position";
        } else if (iv.var->builtinKind == 9) { // FragDepth
            idExpr[iv.var->id] = "stageOut.depth";
        } else if (iv.var->builtinKind == 1) { // PointSize
            idExpr[iv.var->id] = "stageOut.pointSize";
        }
    }
    // UBO/SSBO/push constant variables
    for (auto& bb : result.reflection.buffers) {
        // Find the original variable and map its ID
        for (auto& ub : uniformBuffers) {
            if (ub.var->name == bb.name || (!bb.name.empty() && ub.var->name == bb.name)) {
                idExpr[ub.var->id] = bb.name;
            }
        }
        for (auto& sb : storageBuffers) {
            if (sb.var->name == bb.name) idExpr[sb.var->id] = bb.name;
        }
        for (auto& pc : pushConsts) {
            idExpr[pc.var->id] = bb.name;
        }
    }
    // Texture and sampler variables
    for (auto& tb : result.reflection.textures) {
        for (auto& tv : textures) {
            if (tv.var->name == tb.name) { idExpr[tv.var->id] = tb.name; break; }
        }
    }
    for (auto& sp : samplers) {
        std::string vname = sp.var->name.empty()
            ? ("smp_" + std::to_string(varToSamplerSlot[sp.var->id]))
            : sp.var->name;
        idExpr[sp.var->id] = vname;
    }
    // Seed constants
    for (auto& [id, c] : module.constants) {
        if (c.isComposite) continue; // Handled via CompositeConstruct
        auto ct = module.types.find(c.typeId);
        if (ct == module.types.end()) continue;
        const SpvType& t = ct->second;
        if (c.isBoolTrue) {
            idExpr[id] = "true";
        } else if (t.base == BaseType::Bool) {
            idExpr[id] = (c.value != 0) ? "true" : "false";
        } else if (t.base == BaseType::Float || t.base == BaseType::Half) {
            // Reinterpret the raw bits as float
            if (t.bitWidth == 32) {
                uint32_t bits = static_cast<uint32_t>(c.value);
                float fval;
                std::memcpy(&fval, &bits, sizeof(float));
                std::ostringstream oss;
                oss << fval;
                std::string s = oss.str();
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
                    s += ".0";
                idExpr[id] = s;
            } else if (t.bitWidth == 16) {
                // half constant — just emit as float literal for now
                idExpr[id] = "half(" + std::to_string(static_cast<uint16_t>(c.value)) + ")";
            } else {
                idExpr[id] = std::to_string(c.value);
            }
        } else if (t.base == BaseType::Int) {
            int32_t sval = static_cast<int32_t>(c.value);
            idExpr[id] = std::to_string(sval);
        } else if (t.base == BaseType::UInt) {
            idExpr[id] = std::to_string(static_cast<uint32_t>(c.value)) + "u";
        } else {
            idExpr[id] = std::to_string(c.value);
        }
    }

    // Helper: get expression for an ID
    auto expr = [&](SpvId id) -> std::string {
        auto it = idExpr.find(id);
        if (it != idExpr.end()) return it->second;
        auto nit = module.names.find(id);
        if (nit != module.names.end()) return nit->second;
        return "v" + std::to_string(id);
    };

    // Helper: declare a temporary variable for an instruction result
    auto declTemp = [&](SpvId typeId, SpvId resultId) -> std::string {
        std::string tname = mslTypeNameById(typeId, module);
        std::string vname = "v" + std::to_string(resultId);
        if (module.names.count(resultId)) vname = module.names.at(resultId);
        idExpr[resultId] = vname;
        idTypeName[resultId] = tname;
        return vname;
    };

    // Helper: emit a binary op
    auto emitBinOp = [&](const Instruction& inst, const char* op) {
        if (inst.operands.size() < 2) return;
        std::string vname = declTemp(inst.typeId, inst.resultId);
        std::string tname = idTypeName[inst.resultId];
        cw.line(tname + " " + vname + " = " +
                expr(inst.operands[0]) + " " + op + " " + expr(inst.operands[1]) + ";");
    };

    // Helper: emit a unary MSL function call
    auto emitUnaryFunc = [&](const Instruction& inst, const char* func) {
        if (inst.operands.empty()) return;
        std::string vname = declTemp(inst.typeId, inst.resultId);
        std::string tname = idTypeName[inst.resultId];
        cw.line(tname + " " + vname + " = " +
                std::string(func) + "(" + expr(inst.operands[0]) + ");");
    };

    // Helper: emit a unary prefix op
    auto emitUnaryOp = [&](const Instruction& inst, const char* op) {
        if (inst.operands.empty()) return;
        std::string vname = declTemp(inst.typeId, inst.resultId);
        std::string tname = idTypeName[inst.resultId];
        cw.line(tname + " " + vname + " = " +
                std::string(op) + expr(inst.operands[0]) + ";");
    };

    // Declare output variable
    if (emitStageOut) {
        cw.line("StageOut stageOut = {};");
    }

    // ── Find the entry point function ────────────────────────────────────────
    auto funcIt = module.functions.find(ep.functionId);
    if (funcIt == module.functions.end()) {
        // No function body — emit a stub
        if (emitStageOut) cw.line("return stageOut;");
        else              cw.line("return;");
        cw.dedent();
        cw.line("}");
        result.mslSource = cw.str();
        return result;
    }

    const SpvFunction& func = funcIt->second;

    // ── Pre-declare phi temporaries ──────────────────────────────────────────
    std::unordered_set<SpvId> phiResults;
    for (auto& block : func.blocks) {
        for (auto& inst : block.instructions) {
            if (inst.opcode == SpvOp::Phi && inst.resultId) {
                phiResults.insert(inst.resultId);
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + ";  // phi");
            }
        }
    }

    // ── Walk basic blocks ────────────────────────────────────────────────────
    // We emit structured control flow using the merge/continue annotations.
    // For now, we use a linearized approach with labeled blocks and structured
    // if/else from SelectionMerge + BranchConditional.

    // Build a map from label → block index for navigation
    std::unordered_map<SpvId, size_t> labelToBlockIdx;
    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
        labelToBlockIdx[func.blocks[bi].labelId] = bi;
    }

    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
        const BasicBlock& block = func.blocks[bi];

        // Skip the first block label (entry block) — we're already inside the function
        if (bi > 0) {
            if (options.emitComments)
                cw.line("// block_" + std::to_string(block.labelId) + ":");
        }

        for (auto& inst : block.instructions) {
            // Skip labels, merges (handled structurally)
            if (inst.opcode == SpvOp::Label ||
                inst.opcode == SpvOp::SelectionMerge ||
                inst.opcode == SpvOp::LoopMerge)
                continue;

            switch (inst.opcode) {

            // ── Memory ──────────────────────────────────────────────────────
            case SpvOp::Variable: {
                // Function-scope variable
                if (inst.resultId && inst.typeId) {
                    const SpvType* pt = nullptr;
                    auto tit = module.types.find(inst.typeId);
                    if (tit != module.types.end()) {
                        auto eit = module.types.find(tit->second.elementTypeId);
                        if (eit != module.types.end()) pt = &eit->second;
                    }
                    std::string tname = pt ? mslTypeName(*pt, module) : "float4";
                    std::string vname = declTemp(inst.typeId, inst.resultId);
                    // Override with actual type name (not pointer type)
                    idTypeName[inst.resultId] = tname;
                    cw.line(tname + " " + vname + " = {};");
                }
                break;
            }

            case SpvOp::Load: {
                if (inst.operands.empty()) break;
                SpvId ptrId = inst.operands[0];
                std::string tname = mslTypeNameById(inst.typeId, module);
                std::string vname = declTemp(inst.typeId, inst.resultId);
                idTypeName[inst.resultId] = tname;
                cw.line(tname + " " + vname + " = " + expr(ptrId) + ";");
                break;
            }

            case SpvOp::Store: {
                if (inst.operands.size() < 2) break;
                SpvId ptrId = inst.operands[0], valId = inst.operands[1];
                cw.line(expr(ptrId) + " = " + expr(valId) + ";");
                break;
            }

            case SpvOp::AccessChain:
            case SpvOp::InBoundsAccessChain: {
                if (inst.operands.empty()) break;
                SpvId baseId = inst.operands[0];
                std::string accessExpr = expr(baseId);

                // Walk the index chain, resolving struct member names
                // Get the type of the base variable (dereference pointer)
                SpvId curTypeId = 0;
                {
                    // Find base variable's type
                    auto vit = module.variables.find(baseId);
                    if (vit != module.variables.end()) {
                        auto tit = module.types.find(vit->second.typeId);
                        if (tit != module.types.end() && tit->second.isPointer)
                            curTypeId = tit->second.elementTypeId;
                    }
                }

                for (size_t oi = 1; oi < inst.operands.size(); ++oi) {
                    SpvId idxId = inst.operands[oi];
                    // Try to resolve as constant index
                    auto cit = module.constants.find(idxId);
                    if (cit != module.constants.end()) {
                        uint32_t idx = static_cast<uint32_t>(cit->second.value);
                        // Check if current type is a struct → use member name
                        auto bit2 = module.blocks.find(curTypeId);
                        if (bit2 != module.blocks.end() && idx < bit2->second.members.size()) {
                            const MemberInfo& mem = bit2->second.members[idx];
                            std::string mname = mem.name.empty()
                                ? ("m" + std::to_string(idx)) : mem.name;
                            accessExpr += "." + mname;
                            curTypeId = mem.typeId;
                        } else {
                            // Array index
                            accessExpr += "[" + std::to_string(idx) + "]";
                            auto ait = module.types.find(curTypeId);
                            if (ait != module.types.end())
                                curTypeId = ait->second.elementTypeId;
                        }
                    } else {
                        // Dynamic index
                        accessExpr += "[" + expr(idxId) + "]";
                        auto ait = module.types.find(curTypeId);
                        if (ait != module.types.end())
                            curTypeId = ait->second.elementTypeId;
                    }
                }
                idExpr[inst.resultId] = accessExpr;
                break;
            }

            case SpvOp::CopyObject: {
                if (inst.operands.empty()) break;
                idExpr[inst.resultId] = expr(inst.operands[0]);
                break;
            }

            // ── Arithmetic ──────────────────────────────────────────────────
            case SpvOp::FAdd: case SpvOp::IAdd: emitBinOp(inst, "+"); break;
            case SpvOp::FSub: case SpvOp::ISub: emitBinOp(inst, "-"); break;
            case SpvOp::FMul: case SpvOp::IMul: emitBinOp(inst, "*"); break;
            case SpvOp::FDiv: case SpvOp::UDiv: case SpvOp::SDiv: emitBinOp(inst, "/"); break;
            case SpvOp::UMod: case SpvOp::SRem: case SpvOp::SMod: emitBinOp(inst, "%"); break;
            case SpvOp::FMod:
            case SpvOp::FRem: {
                // Float modulo → fmod() in MSL (% is integer-only)
                if (inst.operands.size() < 2) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = fmod(" +
                        expr(inst.operands[0]) + ", " + expr(inst.operands[1]) + ");");
                break;
            }
            case SpvOp::FNegate: emitUnaryOp(inst, "-"); break;
            case SpvOp::SNegate: emitUnaryOp(inst, "-"); break;

            case SpvOp::VectorTimesScalar:
            case SpvOp::MatrixTimesScalar:
            case SpvOp::MatrixTimesVector:
            case SpvOp::VectorTimesMatrix:
            case SpvOp::MatrixTimesMatrix:
                emitBinOp(inst, "*");
                break;

            case SpvOp::Dot: {
                if (inst.operands.size() < 2) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = dot(" +
                        expr(inst.operands[0]) + ", " + expr(inst.operands[1]) + ");");
                break;
            }

            case SpvOp::Transpose: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = transpose(" + expr(inst.operands[0]) + ");");
                break;
            }

            // ── Comparison (float) ──────────────────────────────────────────
            case SpvOp::FOrdEqual:     case SpvOp::FUnordEqual:     emitBinOp(inst, "=="); break;
            case SpvOp::FOrdNotEqual:  case SpvOp::FUnordNotEqual:  emitBinOp(inst, "!="); break;
            case SpvOp::FOrdLessThan:  case SpvOp::FUnordLessThan:  emitBinOp(inst, "<");  break;
            case SpvOp::FOrdGreaterThan: case SpvOp::FUnordGreaterThan: emitBinOp(inst, ">"); break;
            case SpvOp::FOrdLessThanEqual: case SpvOp::FUnordLessThanEqual: emitBinOp(inst, "<="); break;
            case SpvOp::FOrdGreaterThanEqual: case SpvOp::FUnordGreaterThanEqual: emitBinOp(inst, ">="); break;

            // ── Comparison (integer) ────────────────────────────────────────
            case SpvOp::IEqual:          emitBinOp(inst, "=="); break;
            case SpvOp::INotEqual:       emitBinOp(inst, "!="); break;
            case SpvOp::ULessThan:       case SpvOp::SLessThan:       emitBinOp(inst, "<");  break;
            case SpvOp::UGreaterThan:    case SpvOp::SGreaterThan:    emitBinOp(inst, ">");  break;
            case SpvOp::ULessThanEqual:  case SpvOp::SLessThanEqual:  emitBinOp(inst, "<="); break;
            case SpvOp::UGreaterThanEqual: case SpvOp::SGreaterThanEqual: emitBinOp(inst, ">="); break;

            // ── Logic ───────────────────────────────────────────────────────
            case SpvOp::LogicalAnd:      emitBinOp(inst, "&&"); break;
            case SpvOp::LogicalOr:       emitBinOp(inst, "||"); break;
            case SpvOp::LogicalEqual:    emitBinOp(inst, "=="); break;
            case SpvOp::LogicalNotEqual: emitBinOp(inst, "!="); break;
            case SpvOp::LogicalNot:      emitUnaryOp(inst, "!"); break;

            case SpvOp::Select: {
                if (inst.operands.size() < 3) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " +
                        expr(inst.operands[0]) + " ? " +
                        expr(inst.operands[1]) + " : " + expr(inst.operands[2]) + ";");
                break;
            }

            // ── Bitwise ────────────────────────────────────────────────────
            case SpvOp::BitwiseAnd:            emitBinOp(inst, "&");  break;
            case SpvOp::BitwiseOr:             emitBinOp(inst, "|");  break;
            case SpvOp::BitwiseXor:            emitBinOp(inst, "^");  break;
            case SpvOp::ShiftLeftLogical:      emitBinOp(inst, "<<"); break;
            case SpvOp::ShiftRightLogical:     emitBinOp(inst, ">>"); break;
            case SpvOp::ShiftRightArithmetic:  emitBinOp(inst, ">>"); break;
            case SpvOp::Not:                   emitUnaryOp(inst, "~"); break;

            // ── Conversion ──────────────────────────────────────────────────
            case SpvOp::ConvertFToU:
            case SpvOp::ConvertFToS:
            case SpvOp::ConvertSToF:
            case SpvOp::ConvertUToF:
            case SpvOp::UConvert:
            case SpvOp::SConvert:
            case SpvOp::FConvert: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " + tname + "(" + expr(inst.operands[0]) + ");");
                break;
            }
            case SpvOp::Bitcast: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = as_type<" + tname + ">(" +
                        expr(inst.operands[0]) + ");");
                break;
            }

            // ── Composite ───────────────────────────────────────────────────
            case SpvOp::CompositeConstruct: {
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                std::string args;
                for (size_t oi = 0; oi < inst.operands.size(); ++oi) {
                    if (oi > 0) args += ", ";
                    args += expr(inst.operands[oi]);
                }
                cw.line(tname + " " + vname + " = " + tname + "(" + args + ");");
                break;
            }

            case SpvOp::CompositeExtract: {
                if (inst.operands.size() < 2) break;
                SpvId srcId = inst.operands[0];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                // Single index
                uint32_t idx = inst.operands[1];
                static const char* swiz[] = {"x","y","z","w"};
                std::string accessor;
                if (idx < 4) accessor = std::string(".") + swiz[idx];
                else         accessor = "[" + std::to_string(idx) + "]";
                // Multiple indices (nested access)
                for (size_t oi = 2; oi < inst.operands.size(); ++oi) {
                    uint32_t ni = inst.operands[oi];
                    if (ni < 4) accessor += std::string(".") + swiz[ni];
                    else        accessor += "[" + std::to_string(ni) + "]";
                }
                cw.line(tname + " " + vname + " = " + expr(srcId) + accessor + ";");
                break;
            }

            case SpvOp::CompositeInsert: {
                if (inst.operands.size() < 3) break;
                SpvId valId = inst.operands[0];
                SpvId srcId = inst.operands[1];
                uint32_t idx = inst.operands[2];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " + expr(srcId) + ";");
                static const char* swiz[] = {"x","y","z","w"};
                std::string accessor;
                if (idx < 4) accessor = std::string(".") + swiz[idx];
                else         accessor = "[" + std::to_string(idx) + "]";
                cw.line(vname + accessor + " = " + expr(valId) + ";");
                break;
            }

            case SpvOp::VectorShuffle: {
                if (inst.operands.size() < 2) break;
                SpvId vec1 = inst.operands[0], vec2 = inst.operands[1];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                // Determine vector size of input
                // Build component list
                static const char* swiz[] = {"x","y","z","w"};
                std::string args;
                for (size_t oi = 2; oi < inst.operands.size(); ++oi) {
                    if (oi > 2) args += ", ";
                    uint32_t comp = inst.operands[oi];
                    if (comp == 0xFFFFFFFF) {
                        args += "0"; // Undefined component
                    } else if (comp < 4) {
                        args += expr(vec1) + "." + swiz[comp];
                    } else {
                        args += expr(vec2) + "." + swiz[comp - 4];
                    }
                }
                cw.line(tname + " " + vname + " = " + tname + "(" + args + ");");
                break;
            }

            // ── Function call ───────────────────────────────────────────────
            case SpvOp::FunctionCall: {
                if (inst.operands.empty()) break;
                SpvId funcId = inst.operands[0];
                std::string fname = module.names.count(funcId)
                    ? module.names.at(funcId) : ("func_" + std::to_string(funcId));
                std::string args;
                for (size_t oi = 1; oi < inst.operands.size(); ++oi) {
                    if (oi > 1) args += ", ";
                    args += expr(inst.operands[oi]);
                }
                if (inst.resultId) {
                    std::string vname = declTemp(inst.typeId, inst.resultId);
                    std::string tname = idTypeName[inst.resultId];
                    cw.line(tname + " " + vname + " = " + fname + "(" + args + ");");
                } else {
                    cw.line(fname + "(" + args + ");");
                }
                break;
            }

            // ── Image sampling ──────────────────────────────────────────────
            case SpvOp::ImageSampleImplicitLod: {
                if (inst.operands.size() < 2) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                // Find the paired sampler
                std::string samplerName = "smp_0";
                if (!samplers.empty()) {
                    samplerName = samplers[0].var->name.empty()
                        ? "smp_0" : samplers[0].var->name;
                }
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".sample(" + samplerName + ", " + expr(coordId) + ");");
                break;
            }

            case SpvOp::ImageSampleExplicitLod: {
                if (inst.operands.size() < 2) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                std::string samplerName = "smp_0";
                if (!samplers.empty()) {
                    samplerName = samplers[0].var->name.empty()
                        ? "smp_0" : samplers[0].var->name;
                }
                // Check for Lod image operand (bit 1 in operand mask)
                std::string lodArg;
                if (inst.operands.size() >= 4) {
                    uint32_t mask = inst.operands[2];
                    if (mask & 0x2) { // Lod
                        lodArg = ", level(" + expr(inst.operands[3]) + ")";
                    }
                }
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".sample(" + samplerName + ", " + expr(coordId) + lodArg + ");");
                break;
            }

            case SpvOp::ImageSampleDrefImplicitLod: {
                // Shadow texture comparison sampling
                if (inst.operands.size() < 3) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1], drefId = inst.operands[2];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                std::string samplerName = "smp_0";
                if (!samplers.empty()) {
                    samplerName = samplers[0].var->name.empty()
                        ? "smp_0" : samplers[0].var->name;
                }
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".sample_compare(" + samplerName + ", " +
                        expr(coordId) + ", " + expr(drefId) + ");");
                break;
            }

            case SpvOp::ImageSampleDrefExplicitLod: {
                if (inst.operands.size() < 3) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1], drefId = inst.operands[2];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                std::string samplerName = "smp_0";
                if (!samplers.empty()) {
                    samplerName = samplers[0].var->name.empty()
                        ? "smp_0" : samplers[0].var->name;
                }
                std::string lodArg;
                if (inst.operands.size() >= 5) {
                    uint32_t mask = inst.operands[3];
                    if (mask & 0x2) lodArg = ", level(" + expr(inst.operands[4]) + ")";
                }
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".sample_compare(" + samplerName + ", " +
                        expr(coordId) + ", " + expr(drefId) + lodArg + ");");
                break;
            }

            case SpvOp::ImageFetch: {
                if (inst.operands.size() < 2) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                std::string lodArg;
                if (inst.operands.size() >= 4) {
                    uint32_t mask = inst.operands[2];
                    if (mask & 0x2) lodArg = ", " + expr(inst.operands[3]);
                }
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".read(uint2(" + expr(coordId) + ")" + lodArg + ");");
                break;
            }

            case SpvOp::ImageRead: {
                if (inst.operands.size() < 2) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1];
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " +
                        expr(imgId) + ".read(uint2(" + expr(coordId) + "));");
                break;
            }

            case SpvOp::ImageWrite: {
                if (inst.operands.size() < 3) break;
                SpvId imgId = inst.operands[0], coordId = inst.operands[1], dataId = inst.operands[2];
                cw.line(expr(imgId) + ".write(" + expr(dataId) +
                        ", uint2(" + expr(coordId) + "));");
                break;
            }

            case SpvOp::Image: {
                // OpImage extracts the image from a sampled image — in MSL they're separate
                if (inst.operands.empty()) break;
                idExpr[inst.resultId] = expr(inst.operands[0]);
                break;
            }

            case SpvOp::ImageQuerySize: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " + tname + "(" +
                        expr(inst.operands[0]) + ".get_width(), " +
                        expr(inst.operands[0]) + ".get_height());");
                break;
            }

            case SpvOp::ImageQueryLevels: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " +
                        expr(inst.operands[0]) + ".get_num_mip_levels();");
                break;
            }

            case SpvOp::ImageQuerySamples: {
                if (inst.operands.empty()) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = " +
                        expr(inst.operands[0]) + ".get_num_samples();");
                break;
            }

            // ── Extended instructions (GLSL.std.450) ────────────────────────
            case SpvOp::ExtInst: {
                if (inst.operands.size() < 2) break;
                // operands[0] = ext set ID, operands[1] = ext opcode, operands[2..] = args
                uint32_t extOp = inst.operands[1];
                const char* fname = glslStd450Name(extOp);

                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];

                if (fname) {
                    std::string args;
                    for (size_t oi = 2; oi < inst.operands.size(); ++oi) {
                        if (oi > 2) args += ", ";
                        args += expr(inst.operands[oi]);
                    }
                    cw.line(tname + " " + vname + " = " +
                            std::string(fname) + "(" + args + ");");
                } else {
                    cw.line(tname + " " + vname + " = " + tname +
                            "(0); // unhandled GLSL.std.450 op=" + std::to_string(extOp));
                }
                break;
            }

            // ── Derivatives (fragment only) ─────────────────────────────────
            case SpvOp::DPdx:      emitUnaryFunc(inst, "dfdx");  break;
            case SpvOp::DPdy:      emitUnaryFunc(inst, "dfdy");  break;
            case SpvOp::Fwidth:    emitUnaryFunc(inst, "fwidth"); break;
            case SpvOp::DPdxFine:  emitUnaryFunc(inst, "dfdx");  break;
            case SpvOp::DPdyFine:  emitUnaryFunc(inst, "dfdy");  break;
            case SpvOp::FwidthFine:  emitUnaryFunc(inst, "fwidth"); break;
            case SpvOp::DPdxCoarse:  emitUnaryFunc(inst, "dfdx");  break;
            case SpvOp::DPdyCoarse:  emitUnaryFunc(inst, "dfdy");  break;
            case SpvOp::FwidthCoarse: emitUnaryFunc(inst, "fwidth"); break;

            // ── Misc ────────────────────────────────────────────────────────
            case SpvOp::Any:   emitUnaryFunc(inst, "any");   break;
            case SpvOp::All:   emitUnaryFunc(inst, "all");   break;
            case SpvOp::IsNan: emitUnaryFunc(inst, "isnan"); break;
            case SpvOp::IsInf: emitUnaryFunc(inst, "isinf"); break;

            // ── Atomic operations ───────────────────────────────────────────
            case SpvOp::AtomicIAdd: {
                if (inst.operands.size() < 4) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = atomic_fetch_add_explicit(" +
                        "&" + expr(inst.operands[0]) + ", " + expr(inst.operands[3]) +
                        ", memory_order_relaxed);");
                break;
            }
            case SpvOp::AtomicISub: {
                if (inst.operands.size() < 4) break;
                std::string vname = declTemp(inst.typeId, inst.resultId);
                std::string tname = idTypeName[inst.resultId];
                cw.line(tname + " " + vname + " = atomic_fetch_sub_explicit(" +
                        "&" + expr(inst.operands[0]) + ", " + expr(inst.operands[3]) +
                        ", memory_order_relaxed);");
                break;
            }

            // ── Barriers ────────────────────────────────────────────────────
            case SpvOp::ControlBarrier: {
                cw.line("threadgroup_barrier(mem_flags::mem_threadgroup);");
                break;
            }
            case SpvOp::MemoryBarrier: {
                cw.line("threadgroup_barrier(mem_flags::mem_device);");
                break;
            }

            // ── Phi nodes ───────────────────────────────────────────────────
            case SpvOp::Phi: {
                // Phi temporaries are pre-declared; assignments happen at branch sites.
                // For linearized emission, assign from the first pair as a fallback.
                if (!inst.phiPairs.empty() && inst.resultId) {
                    cw.line(expr(inst.resultId) + " = " + expr(inst.phiPairs[0].valueId) + ";");
                }
                break;
            }

            // ── Control flow ────────────────────────────────────────────────
            case SpvOp::Branch:
                // Unconditional branch — handled by block ordering
                break;

            case SpvOp::BranchConditional: {
                if (inst.operands.size() < 3) break;
                SpvId condId = inst.operands[0];
                SpvId trueLabel = inst.operands[1];
                SpvId falseLabel = inst.operands[2];

                // Check if the parent block has a selection merge → emit if/else
                if (block.mergeBlock != 0 && !block.isLoopHeader) {
                    cw.line("if (" + expr(condId) + ") {");
                    cw.indent();
                    // Emit true block inline
                    auto tit = labelToBlockIdx.find(trueLabel);
                    if (tit != labelToBlockIdx.end() && tit->second > bi) {
                        // We'll let the normal block iteration handle it
                        // Just emit a comment for now
                        cw.line("// → block_" + std::to_string(trueLabel));
                    }
                    cw.dedent();
                    cw.line("} else {");
                    cw.indent();
                    cw.line("// → block_" + std::to_string(falseLabel));
                    cw.dedent();
                    cw.line("}");
                } else if (block.isLoopHeader) {
                    // Loop: while (condition)
                    cw.line("while (" + expr(condId) + ") {");
                    cw.indent();
                    cw.line("// loop body → block_" + std::to_string(trueLabel));
                    cw.dedent();
                    cw.line("}");
                } else {
                    cw.line("if (" + expr(condId) + ") {");
                    cw.indent();
                    cw.line("// → block_" + std::to_string(trueLabel));
                    cw.dedent();
                    cw.line("}");
                }
                break;
            }

            case SpvOp::Switch: {
                if (inst.operands.empty()) break;
                cw.line("switch (" + expr(inst.operands[0]) + ") {");
                cw.indent();
                // operands[1] = default label, then pairs of (literal, label)
                if (inst.operands.size() > 1) {
                    for (size_t oi = 2; oi + 1 < inst.operands.size(); oi += 2) {
                        cw.line("case " + std::to_string(inst.operands[oi]) + ":");
                        cw.indent();
                        cw.line("break; // → block_" + std::to_string(inst.operands[oi+1]));
                        cw.dedent();
                    }
                    cw.line("default:");
                    cw.indent();
                    cw.line("break; // → block_" + std::to_string(inst.operands[1]));
                    cw.dedent();
                }
                cw.dedent();
                cw.line("}");
                break;
            }

            case SpvOp::Return: {
                if (emitStageOut) {
                    // Flip Y for Vulkan → Metal coordinate system
                    if (ep.stage == ShaderStage::Vertex && options.flipVertexY) {
                        cw.line("stageOut.position.y = -stageOut.position.y;");
                    }
                    cw.line("return stageOut;");
                } else {
                    cw.line("return;");
                }
                break;
            }

            case SpvOp::ReturnValue: {
                if (inst.operands.empty()) break;
                cw.line("return " + expr(inst.operands[0]) + ";");
                break;
            }

            case SpvOp::Kill: {
                cw.line("discard_fragment();");
                break;
            }

            case SpvOp::Unreachable:
                break;

            // ── Skip structural ops ─────────────────────────────────────────
            case SpvOp::Function:
            case SpvOp::FunctionParameter:
            case SpvOp::FunctionEnd:
                break;

            default:
                if (options.emitComments) {
                    cw.line("// unhandled op " + std::to_string(inst.opcode));
                }
                break;
            }
        }
    }

    // ── Ensure function has a return ─────────────────────────────────────────
    // If the last instruction wasn't a return, add one
    bool lastWasReturn = false;
    if (!func.blocks.empty()) {
        auto& lastBlock = func.blocks.back();
        if (!lastBlock.instructions.empty()) {
            uint32_t lastOp = lastBlock.instructions.back().opcode;
            lastWasReturn = (lastOp == SpvOp::Return || lastOp == SpvOp::ReturnValue ||
                             lastOp == SpvOp::Kill || lastOp == SpvOp::Unreachable);
        }
    }
    if (!lastWasReturn) {
        if (emitStageOut) {
            if (ep.stage == ShaderStage::Vertex && options.flipVertexY) {
                cw.line("stageOut.position.y = -stageOut.position.y;");
            }
            cw.line("return stageOut;");
        }
    }

    cw.dedent();
    cw.line("}");

    result.mslSource = cw.str();
    MVRVB_LOG_INFO("Generated %zu bytes of MSL for '%s'",
                   result.mslSource.size(), ep.name.c_str());
    return result;
}

} // namespace mvrvb::msl
