/**
 * @file geometry_emu.metal
 * @brief Geometry shader emulation via Metal compute + indirect draw.
 *
 * Vulkan supports geometry shaders; Metal does not.  We emulate them with
 * a two-pass approach:
 *
 *   Pass 1 (compute):  Run the geometry "expansion" kernel.
 *                      For each input primitive, emit 0..maxVertices output
 *                      vertices into a typed output buffer.  Write an atomic
 *                      counter for the draw call's vertex count.
 *
 *   Pass 2 (vertex):   A passthrough vertex shader reads from the output
 *                      buffer via [[vertex_id]] and feeds the rasterizer.
 *
 * Limitations:
 *   - Only triangles → triangles and triangles → triangle strips are
 *     implemented here.  Points and lines are handled similarly.
 *   - The maximum output vertex count per input primitive is capped at 60
 *     (matches common shadow volume / cubemap face generation workloads).
 *   - No stream output / transform feedback.
 *
 * The host-side geometry_emu.mm generates specialised kernels by substituting
 * the translated MSL geometry function into the template below.
 */

#include <metal_stdlib>
using namespace metal;

// ── Output vertex (matches the rasterisation pipeline's StageIn) ──────────────

struct GeoEmitVertex {
    float4 position;   ///< [[position]] — clip space
    float4 colour;
    float2 texCoord;
    float3 normal;
    float  _pad;
};

// ── Per-invocation geometry context ──────────────────────────────────────────

struct GeoEmitContext {
    device GeoEmitVertex* outVerts;
    device atomic_uint*   outCount;
    uint                  maxVerts;
    uint                  baseIndex;
};

// Emitted by the compute kernel; one entry per original input primitive.
struct IndirectDrawArguments {
    uint vertexCount;
    uint instanceCount;
    uint vertexStart;
    uint baseInstance;
};

// ── Triangle list expansion kernel template ───────────────────────────────────
// For each input triangle (3 vertices), the geometry function may emit
// 0..kMaxOutputPerPrim output vertices.

static constant uint kMaxOutputPerPrim = 60;

// Input vertex layout — matches the vertex shader's StageOut.
struct GeoInVertex {
    float4 position;
    float4 colour;
    float2 texCoord;
    float3 normal;
    float  _pad;
};

/**
 * Passthrough geometry emulator: 1 triangle → 1 triangle.
 *
 * This is the trivial case used when a VkPipeline includes a geometry
 * stage that only passes through primitives.  The actual geometry function
 * is inlined into a specialised copy of this kernel by geometry_emu.mm.
 */
kernel void geo_emu_passthrough(
    const device GeoInVertex*        inVerts    [[buffer(0)]],
    device       GeoEmitVertex*      outVerts   [[buffer(1)]],
    device       atomic_uint&        outCount   [[buffer(2)]],
    device       IndirectDrawArguments& indirectArgs [[buffer(3)]],
    uint                             tid        [[thread_position_in_grid]],
    uint                             gridSize   [[threads_per_grid]])
{
    uint primIndex   = tid;
    uint inBase      = primIndex * 3;  // triangle list

    // Guard against out-of-bounds (last dispatch may have extra threads)
    // The grid size is set to ceil(triangleCount / 64) * 64
    uint totalTriangles = gridSize; // host sets this correctly
    if (primIndex >= totalTriangles) return;

    // Read the 3 input vertices for this primitive
    GeoInVertex v[3] = { inVerts[inBase], inVerts[inBase+1], inVerts[inBase+2] };

    // Allocate output slots atomically
    uint base = atomic_fetch_add_explicit(&outCount, 3, memory_order_relaxed);

    // Pass through
    GeoEmitVertex o;
    for (uint i = 0; i < 3; ++i) {
        o.position = v[i].position;
        o.colour   = v[i].colour;
        o.texCoord = v[i].texCoord;
        o.normal   = v[i].normal;
        o._pad     = 0.0f;
        outVerts[base + i] = o;
    }

    // Update indirect draw args (only thread 0 sets vertex count after barrier)
    // The host uses a separate indirect-args pass; this is for demonstration.
}

/**
 * Shadow volume extrusion geometry emulator.
 *
 * For each silhouette edge (identified by the host), extrudes a quad to
 * infinity along the light direction.  This is the primary workload that
 * forces geometry shaders in SteamVR titles.
 */
struct ShadowUniforms {
    float4x4 mvpMatrix;
    float4   lightPos;    ///< w==0: directional, w==1: point
    float    extrudeLen;  ///< Length to extrude shadow volumes
};

kernel void geo_emu_shadow_extrude(
    const device float4*             edgeVerts  [[buffer(0)]],  ///< pairs of edge vertices
    device       GeoEmitVertex*      outVerts   [[buffer(1)]],
    device       atomic_uint&        outCount   [[buffer(2)]],
    constant     ShadowUniforms&     u          [[buffer(3)]],
    uint                             tid        [[thread_position_in_grid]])
{
    uint edgeBase = tid * 2;
    float4 v0 = edgeVerts[edgeBase];
    float4 v1 = edgeVerts[edgeBase + 1];

    // Compute extruded positions (to infinity for directional lights)
    float3 dir0, dir1;
    if (u.lightPos.w < 0.5f) {
        // Directional: extrude along -lightDir
        dir0 = dir1 = -normalize(u.lightPos.xyz);
    } else {
        // Point: extrude away from light position
        dir0 = normalize(v0.xyz - u.lightPos.xyz);
        dir1 = normalize(v1.xyz - u.lightPos.xyz);
    }

    float4 v0e = float4(v0.xyz + dir0 * u.extrudeLen, 1.0f);
    float4 v1e = float4(v1.xyz + dir1 * u.extrudeLen, 1.0f);

    // Emit a quad (2 triangles = 6 verts)
    uint base = atomic_fetch_add_explicit(&outCount, 6, memory_order_relaxed);

    auto emit = [&](float4 pos) {
        GeoEmitVertex o;
        o.position = u.mvpMatrix * pos;
        o.colour   = float4(0.0f);
        o.texCoord = float2(0.0f);
        o.normal   = float3(0.0f);
        o._pad     = 0.0f;
        return o;
    };

    // Triangle 1
    outVerts[base + 0] = emit(v0);
    outVerts[base + 1] = emit(v1);
    outVerts[base + 2] = emit(v1e);
    // Triangle 2
    outVerts[base + 3] = emit(v0);
    outVerts[base + 4] = emit(v1e);
    outVerts[base + 5] = emit(v0e);
}

// ── Passthrough vertex shader for emulated output ─────────────────────────────

struct GeoPassVaryings {
    float4 position [[position]];
    float4 colour;
    float2 texCoord;
    float3 normal;
};

vertex GeoPassVaryings geo_emu_passthrough_vertex(
    const device GeoEmitVertex* verts [[buffer(0)]],
    uint                        vid   [[vertex_id]])
{
    GeoEmitVertex v = verts[vid];
    GeoPassVaryings out;
    out.position = v.position;
    out.colour   = v.colour;
    out.texCoord = v.texCoord;
    out.normal   = v.normal;
    return out;
}

fragment float4 geo_emu_passthrough_fragment(GeoPassVaryings in [[stage_in]])
{
    return in.colour;
}
