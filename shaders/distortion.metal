/**
 * @file distortion.metal
 * @brief Lens distortion correction for VR HMDs.
 *
 * Takes the rendered eye texture and applies barrel/pincushion distortion
 * correction along with chromatic aberration correction per colour channel.
 *
 * The distortion mesh is generated on the CPU (see distortion_mesh.mm) and
 * supplied as a vertex buffer.  Each vertex carries three UV coordinates —
 * one per colour channel — to correct chromatic aberration.
 *
 * Coordinate conventions:
 *   - Vertex position: normalised device coordinates [-1, 1]
 *   - UVs: [0, 1] with (0,0) at bottom-left (Metal convention)
 */

#include <metal_stdlib>
using namespace metal;

// ── Vertex shader ─────────────────────────────────────────────────────────────

struct DistortionVertex {
    float2 position  [[attribute(0)]];  ///< NDC position
    float2 uvRed     [[attribute(1)]];  ///< UV for red channel
    float2 uvGreen   [[attribute(2)]];  ///< UV for green channel
    float2 uvBlue    [[attribute(3)]];  ///< UV for blue channel
};

struct DistortionVaryings {
    float4 position  [[position]];
    float2 uvRed;
    float2 uvGreen;
    float2 uvBlue;
    float  vignette;
};

struct DistortionUniforms {
    float  vignetteInner;  ///< Inner radius of vignette fade  (0.85 default)
    float  vignetteOuter;  ///< Outer radius of vignette cutoff (1.0 default)
    float2 uvScale;        ///< Per-eye UV scale (accounts for render target aspect)
    float2 uvBias;         ///< Per-eye UV bias  (left=0, right=0.5 for side-by-side)
};

vertex DistortionVaryings distortion_vertex(
    DistortionVertex       in       [[stage_in]],
    constant DistortionUniforms& u  [[buffer(0)]],
    uint                   vid      [[vertex_id]])
{
    DistortionVaryings out;
    out.position  = float4(in.position, 0.0f, 1.0f);
    out.uvRed     = in.uvRed   * u.uvScale + u.uvBias;
    out.uvGreen   = in.uvGreen * u.uvScale + u.uvBias;
    out.uvBlue    = in.uvBlue  * u.uvScale + u.uvBias;

    // Vignette: fade to black near the edge of the lens
    float r       = length(in.position);
    out.vignette  = 1.0f - smoothstep(u.vignetteInner, u.vignetteOuter, r);

    return out;
}

// ── Fragment shader ───────────────────────────────────────────────────────────

fragment float4 distortion_fragment(
    DistortionVaryings   in   [[stage_in]],
    texture2d<float>     eye  [[texture(0)]],
    sampler              smp  [[sampler(0)]])
{
    // Clamp UVs so out-of-bounds samples return black (edge padding)
    float2 uvR = clamp(in.uvRed,   float2(0.0f), float2(1.0f));
    float2 uvG = clamp(in.uvGreen, float2(0.0f), float2(1.0f));
    float2 uvB = clamp(in.uvBlue,  float2(0.0f), float2(1.0f));

    // Sample each channel at its corrected UV
    float r = eye.sample(smp, uvR).r;
    float g = eye.sample(smp, uvG).g;
    float b = eye.sample(smp, uvB).b;

    // Detect out-of-bounds (any UV outside [0,1] is masked to black)
    float maskR = all(uvR == in.uvRed)   ? 1.0f : 0.0f;
    float maskG = all(uvG == in.uvGreen) ? 1.0f : 0.0f;
    float maskB = all(uvB == in.uvBlue)  ? 1.0f : 0.0f;

    float4 colour = float4(r * maskR, g * maskG, b * maskB, 1.0f);
    colour.rgb *= in.vignette;

    return colour;
}
