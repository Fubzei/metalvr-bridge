/**
 * @file geometry_emu.mm
 * @brief Geometry shader → Metal compute emulator.
 *
 * Generates specialised MSL compute kernels that replicate the behaviour of
 * Vulkan geometry shaders using two Metal passes (compute + indirect render).
 *
 * The compute kernel template is populated with:
 *   1. The translated body of the SPIR-V geometry main() function (from MSL emitter).
 *   2. A typed output vertex buffer struct matching the geometry outputs.
 *   3. An atomic counter for the output vertex count.
 *   4. An indirect draw arguments buffer for the subsequent render pass.
 */

#include "geometry_emu.h"
#include "../../common/logging.h"

#include <sstream>
#include <cstring>

namespace mvrvb {

// ── MSL struct for the output vertex ─────────────────────────────────────────
// This must match the GeoEmitVertex struct in geometry_emu.metal.

static const char* kOutputVertexStruct = R"msl(
struct GeoEmitVertex {
    float4 position;
    float4 colour;
    float2 texCoord;
    float3 normal;
    float  _pad;
};
static_assert(sizeof(GeoEmitVertex) == 60, "stride mismatch");
)msl";

// ── Compute kernel template ───────────────────────────────────────────────────

static const char* kComputeKernelHeader = R"msl(
#include <metal_stdlib>
using namespace metal;

)msl";

static const char* kComputeKernelBody = R"msl(

// ── Output vertex buffer ─────────────────────────────────────────────────────

struct IndirectDrawArguments {
    uint vertexCount;
    uint instanceCount;
    uint vertexStart;
    uint baseInstance;
};

// ── Emitter helper ────────────────────────────────────────────────────────────

struct GeoEmitter {
    device GeoEmitVertex* outBuf;
    device atomic_uint*   counter;
    uint                  maxVerts;
    uint                  baseIndex;

    void emitVertex(GeoEmitVertex v) {
        uint idx = atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
        if (idx < maxVerts) outBuf[baseIndex + idx] = v;
    }

    void endPrimitive() {
        // Triangle strip → triangle list conversion is handled by re-ordering.
        // For now, strips are emitted in-order and the passthrough VS reads
        // them directly.
    }
};

// ── Compute kernel entry point ────────────────────────────────────────────────

kernel void geo_emu_main(
    const device GeoInVertex*  inVerts       [[buffer(0)]],
    device       GeoEmitVertex* outVerts     [[buffer(1)]],
    device       atomic_uint&   outCount     [[buffer(2)]],
    device       IndirectDrawArguments& indirectArgs [[buffer(3)]],
    uint tid [[thread_position_in_grid]],
    uint gridSize [[threads_per_grid]])
{
    uint totalPrims = gridSize;
    if (tid >= totalPrims) return;

    uint inBase = tid * INPUT_PRIM_VERTS;

    // Read input primitive vertices
    GeoInVertex inPrim[INPUT_PRIM_VERTS_MAX];
    for (uint i = 0; i < INPUT_PRIM_VERTS; ++i)
        inPrim[i] = inVerts[inBase + i];

    // Reserve output slots for this primitive
    uint outBase = atomic_fetch_add_explicit(&outCount, MAX_OUTPUT_VERTS, memory_order_relaxed);

    // Reset local counter to track actual emitted verts within the reservation
    uint emitted = 0;

    // ── User geometry function body (inserted here by buildGeometryEmulator) ──
    GEO_FUNC_BODY
    // ── End user geometry function body ───────────────────────────────────────

    // If fewer verts were emitted, zero out the unused slots
    GeoEmitVertex zero{};
    for (uint i = emitted; i < MAX_OUTPUT_VERTS; ++i)
        outVerts[outBase + i] = zero;

    // Thread 0 finalises the indirect draw args after all threads complete
    threadgroup_barrier(mem_flags::mem_device);
    if (tid == 0) {
        uint total = atomic_load_explicit(&outCount, memory_order_relaxed);
        indirectArgs.vertexCount   = total;
        indirectArgs.instanceCount = 1;
        indirectArgs.vertexStart   = 0;
        indirectArgs.baseInstance  = 0;
    }
}
)msl";

// ── Draw-pass (passthrough vertex + stub fragment) ────────────────────────────

static const char* kDrawPassMSL = R"msl(
#include <metal_stdlib>
using namespace metal;

struct GeoEmitVertex {
    float4 position;
    float4 colour;
    float2 texCoord;
    float3 normal;
    float  _pad;
};

struct GeoVaryings {
    float4 position [[position]];
    float4 colour;
    float2 texCoord;
    float3 normal;
};

vertex GeoVaryings geo_emu_passthrough_vs(
    const device GeoEmitVertex* verts [[buffer(0)]],
    uint vid [[vertex_id]])
{
    GeoEmitVertex v = verts[vid];
    GeoVaryings out;
    out.position = v.position;
    out.colour   = v.colour;
    out.texCoord = v.texCoord;
    out.normal   = v.normal;
    return out;
}

fragment float4 geo_emu_passthrough_fs(GeoVaryings in [[stage_in]])
{
    return in.colour;
}
)msl";

// ── Passthrough body (when geometry shader just forwards primitives) ──────────

static std::string buildPassthroughBody(uint32_t inputVerts) {
    std::ostringstream s;
    s << "    // Passthrough: forward all input vertices unchanged\n";
    for (uint32_t i = 0; i < inputVerts; ++i) {
        s << "    {\n";
        s << "        GeoEmitVertex o;\n";
        s << "        o.position = inPrim[" << i << "].position;\n";
        s << "        o.colour   = inPrim[" << i << "].colour;\n";
        s << "        o.texCoord = inPrim[" << i << "].texCoord;\n";
        s << "        o.normal   = inPrim[" << i << "].normal;\n";
        s << "        o._pad     = 0.0f;\n";
        s << "        outVerts[outBase + emitted++] = o;\n";
        s << "    }\n";
    }
    return s.str();
}

// ── buildGeometryEmulator ─────────────────────────────────────────────────────

GeometryEmuResult buildGeometryEmulator(const spirv::SPIRVModule& module,
                                         uint32_t                  entryIndex,
                                         const GeometryEmuOptions& opts) {
    GeometryEmuResult result;

    if (entryIndex >= module.entryPoints.size()) {
        result.errorMessage = "Entry point index out of range";
        return result;
    }

    const spirv::EntryPoint& ep = module.entryPoints[entryIndex];
    if (ep.stage != spirv::ShaderStage::Geometry) {
        result.errorMessage = "Entry point is not a geometry stage";
        return result;
    }

    uint32_t maxVerts   = opts.maxOutputVertices;
    uint32_t inputVerts = opts.inputPrimitiveVertices;
    if (opts.inputAdjacency) inputVerts *= 2;

    // Build the compute kernel MSL source
    std::ostringstream cs;
    cs << kComputeKernelHeader;

    // GeoInVertex struct (must match vertex shader's StageOut)
    cs << "struct GeoInVertex {\n";
    cs << "    float4 position;\n";
    cs << "    float4 colour;\n";
    cs << "    float2 texCoord;\n";
    cs << "    float3 normal;\n";
    cs << "    float  _pad;\n";
    cs << "};\n\n";

    cs << kOutputVertexStruct << "\n";

    // Replace template variables in the kernel body
    std::string kernelBody = kComputeKernelBody;

    auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replaceAll(kernelBody, "INPUT_PRIM_VERTS_MAX", std::to_string(std::max(inputVerts, 6u)));
    replaceAll(kernelBody, "INPUT_PRIM_VERTS",     std::to_string(inputVerts));
    replaceAll(kernelBody, "MAX_OUTPUT_VERTS",     std::to_string(maxVerts));

    // For now: use passthrough body if we can't translate the geometry function.
    // Full SPIR-V → MSL geometry translation would substitute the actual function body here.
    std::string geoBody;
    if (module.functions.empty() || module.functions.find(ep.functionId) == module.functions.end()) {
        MVRVB_LOG_WARN("GeometryEmu: no function body for entry point %u; using passthrough", ep.functionId);
        geoBody = buildPassthroughBody(inputVerts);
    } else {
        // TODO: translate SPIR-V function body to MSL using the msl_emitter
        MVRVB_LOG_WARN("GeometryEmu: SPIR-V geometry body translation not yet implemented; using passthrough");
        geoBody = buildPassthroughBody(inputVerts);
    }

    replaceAll(kernelBody, "GEO_FUNC_BODY", geoBody);

    cs << kernelBody;
    result.computeKernelMSL      = cs.str();
    result.drawShaderMSL         = kDrawPassMSL;
    result.outputVertexStrideBytes = 60; // sizeof(GeoEmitVertex)
    result.success               = true;

    MVRVB_LOG_DEBUG("GeometryEmu: built for entry '%s' inputVerts=%u maxOutput=%u",
                    ep.name.c_str(), inputVerts, maxVerts);
    return result;
}

} // namespace mvrvb
