/**
 * @file timewarp.metal
 * @brief Asynchronous Timewarp (ATW) reprojection kernel.
 *
 * Timewarp reduces perceived latency by rotating the rendered eye image
 * to match the HMD orientation at the ACTUAL scan-out time, rather than the
 * orientation at render start.
 *
 * Algorithm:
 *   1.  At render start:  capture predicted pose P_render
 *   2.  At scan-out time: capture actual pose  P_scanout
 *   3.  Compute delta rotation: R_delta = P_scanout * inverse(P_render)
 *   4.  Per fragment: unproject using P_render projection, rotate by R_delta,
 *       re-project, sample eye texture.
 *
 * This shader handles both the vertex warp (full-screen quad with warped UVs)
 * and an optional compute path for higher throughput.
 */

#include <metal_stdlib>
using namespace metal;

// ── Shared data structures ────────────────────────────────────────────────────

struct TimewarpUniforms {
    float4x4 warpMatrix;       ///< Combined delta rotation + reprojection matrix
    float2   eyeTextureSize;   ///< (width, height) of the eye render target
    float    nearPlane;
    float    farPlane;
    float2   tanHalfFov;       ///< (tan(fovX/2), tan(fovY/2)) — used for unproject
};

// ── Vertex path (full-screen quad warped per-vertex) ─────────────────────────

struct TimewarpVaryings {
    float4 position [[position]];
    float2 uv;
    float2 uvWarped;
};

vertex TimewarpVaryings timewarp_vertex(
    uint                    vid  [[vertex_id]],
    constant TimewarpUniforms& u [[buffer(0)]])
{
    // Full-screen triangle trick: 3 vertices cover the whole screen
    float2 pos = float2((vid == 2) ? 3.0f : -1.0f,
                        (vid == 1) ? 3.0f : -1.0f);
    float2 uv  = float2((vid == 2) ? 2.0f :  0.0f,
                        (vid == 1) ? 2.0f :  0.0f);

    // Unproject UV to view-space ray (at depth = 1)
    float3 ray = float3((uv.x * 2.0f - 1.0f) * u.tanHalfFov.x,
                        (uv.y * 2.0f - 1.0f) * u.tanHalfFov.y,
                        -1.0f);

    // Apply warp rotation (3x3 upper-left of the 4x4 matrix)
    float3 warpedRay = float3(dot(u.warpMatrix[0].xyz, ray),
                              dot(u.warpMatrix[1].xyz, ray),
                              dot(u.warpMatrix[2].xyz, ray));

    // Re-project to UV space
    float2 warpedUV = float2(
        (warpedRay.x / (-warpedRay.z) / u.tanHalfFov.x + 1.0f) * 0.5f,
        (warpedRay.y / (-warpedRay.z) / u.tanHalfFov.y + 1.0f) * 0.5f
    );

    TimewarpVaryings out;
    out.position = float4(pos, 0.0f, 1.0f);
    out.uv       = uv;
    out.uvWarped = warpedUV;
    return out;
}

fragment float4 timewarp_fragment(
    TimewarpVaryings      in   [[stage_in]],
    texture2d<float>      eye  [[texture(0)]],
    sampler               smp  [[sampler(0)]])
{
    // Black border outside the reprojected area
    if (any(in.uvWarped < float2(0.0f)) || any(in.uvWarped > float2(1.0f)))
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    return eye.sample(smp, in.uvWarped);
}

// ── Compute path (higher throughput for Apple Silicon) ────────────────────────

kernel void timewarp_compute(
    texture2d<float, access::read>  srcEye    [[texture(0)]],
    texture2d<float, access::write> dstFrame  [[texture(1)]],
    constant TimewarpUniforms&      u         [[buffer(0)]],
    uint2                           gid       [[thread_position_in_grid]])
{
    uint2 dims = uint2(dstFrame.get_width(), dstFrame.get_height());
    if (any(gid >= dims)) return;

    // Normalised UV of this output pixel
    float2 uv = (float2(gid) + 0.5f) / float2(dims);

    // Unproject to view-space ray
    float3 ray = float3((uv.x * 2.0f - 1.0f) * u.tanHalfFov.x,
                        (uv.y * 2.0f - 1.0f) * u.tanHalfFov.y,
                        -1.0f);

    float3 warpedRay = float3(dot(u.warpMatrix[0].xyz, ray),
                              dot(u.warpMatrix[1].xyz, ray),
                              dot(u.warpMatrix[2].xyz, ray));

    float2 srcUV = float2(
        (warpedRay.x / (-warpedRay.z) / u.tanHalfFov.x + 1.0f) * 0.5f,
        (warpedRay.y / (-warpedRay.z) / u.tanHalfFov.y + 1.0f) * 0.5f
    );

    float4 colour = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (all(srcUV >= float2(0.0f)) && all(srcUV <= float2(1.0f))) {
        uint2 srcCoord = uint2(srcUV * float2(srcEye.get_width(), srcEye.get_height()));
        colour = srcEye.read(srcCoord);
    }

    dstFrame.write(colour, gid);
}
