/**
 * @file composite.metal
 * @brief VR compositor — layer blending and final frame assembly.
 *
 * Composites multiple OpenVR overlay layers on top of the scene eye textures,
 * then blits the result to the final output surface.
 *
 * Layer types supported:
 *   - Scene (eye projection, warped by timewarp)
 *   - Overlay (quad in world-space, alpha-blended)
 *   - Dashboard overlay (HUD, always on top)
 *
 * The compositor runs after timewarp on a dedicated high-priority Metal queue.
 */

#include <metal_stdlib>
using namespace metal;

// ── Scene layer composite ─────────────────────────────────────────────────────

struct CompositeUniforms {
    float alpha;        ///< Global opacity for this layer (usually 1.0)
    float colorScale;   ///< HDR tone-mapping scale
    float2 uvOffset;    ///< Per-layer UV offset (for split-screen side-by-side)
    float2 uvScale;     ///< Per-layer UV scale
};

struct QuadVertex {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
};

struct QuadVaryings {
    float4 position [[position]];
    float2 uv;
};

// ── Full-screen blit (scene eye after timewarp) ───────────────────────────────

vertex QuadVaryings composite_blit_vertex(uint vid [[vertex_id]])
{
    // Full-screen triangle
    float2 pos = float2((vid == 2) ? 3.0f : -1.0f,
                        (vid == 1) ? 3.0f : -1.0f);
    float2 uv  = float2((vid == 2) ? 2.0f :  0.0f,
                        (vid == 1) ? 2.0f :  0.0f);
    QuadVaryings out;
    out.position = float4(pos, 0.0f, 1.0f);
    out.uv       = uv;
    return out;
}

fragment float4 composite_blit_fragment(
    QuadVaryings           in   [[stage_in]],
    texture2d<float>       src  [[texture(0)]],
    sampler                smp  [[sampler(0)]],
    constant CompositeUniforms& u [[buffer(0)]])
{
    float2 uv = in.uv * u.uvScale + u.uvOffset;
    float4 c  = src.sample(smp, uv);
    c.rgb     *= u.colorScale;
    c.a       *= u.alpha;
    return c;
}

// ── Overlay layer composite (quad in clip space) ──────────────────────────────

struct OverlayUniforms {
    float4x4 mvpMatrix;   ///< Model-view-projection for the overlay quad
    float    alpha;       ///< Layer opacity
    float    premultiplied; ///< 1.0 if source alpha is pre-multiplied
    float2   _pad;
};

vertex QuadVaryings overlay_vertex(
    QuadVertex              in  [[stage_in]],
    constant OverlayUniforms& u [[buffer(0)]])
{
    QuadVaryings out;
    out.position = u.mvpMatrix * float4(in.position, 0.0f, 1.0f);
    out.uv       = in.uv;
    return out;
}

fragment float4 overlay_fragment(
    QuadVaryings            in   [[stage_in]],
    texture2d<float>        tex  [[texture(0)]],
    sampler                 smp  [[sampler(0)]],
    constant OverlayUniforms& u  [[buffer(0)]])
{
    float4 c = tex.sample(smp, in.uv);
    if (u.premultiplied < 0.5f) {
        // Convert straight alpha to pre-multiplied for correct blending
        c.rgb *= c.a;
    }
    c.a *= u.alpha;
    return c;
}

// ── Hidden area mesh (stencil fill optimisation) ──────────────────────────────
// Draws an opaque black mask over the hidden/unusable lens area to
// save fragment shading on pixels that will never be seen.

vertex float4 hidden_area_vertex(
    uint vid [[vertex_id]],
    const device float2* verts [[buffer(0)]])
{
    return float4(verts[vid], 0.0f, 1.0f);
}

fragment float4 hidden_area_fragment()
{
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

// ── sRGB gamma conversion (when rendering to linear buffer) ──────────────────

fragment float4 gamma_correct_fragment(
    QuadVaryings      in   [[stage_in]],
    texture2d<float>  src  [[texture(0)]],
    sampler           smp  [[sampler(0)]])
{
    float4 c = src.sample(smp, in.uv);
    // Apply sRGB encoding: linear → display
    float3 lo = c.rgb * 12.92f;
    float3 hi = 1.055f * pow(c.rgb, float3(1.0f / 2.4f)) - 0.055f;
    c.rgb = select(lo, hi, c.rgb > float3(0.0031308f));
    return c;
}

// ── Compute: resolve MSAA eye texture ─────────────────────────────────────────

kernel void msaa_resolve(
    texture2d_ms<float, access::read> src [[texture(0)]],
    texture2d<float, access::write>   dst [[texture(1)]],
    uint2                             gid [[thread_position_in_grid]])
{
    uint2 dims = uint2(dst.get_width(), dst.get_height());
    if (any(gid >= dims)) return;

    uint  n   = src.get_num_samples();
    float4 acc = float4(0.0f);
    for (uint s = 0; s < n; ++s)
        acc += src.read(gid, s);
    dst.write(acc / float(n), gid);
}
