#pragma once
/**
 * @file vision_pro_backend.h
 * @brief Apple Vision Pro HMD backend.
 *
 * Uses CompositorServices for immersive rendering and ARKit for head tracking.
 * Requires macOS 14.0+ / visionOS 1.0+.
 *
 * Architecture:
 *   - CompositorServices provides cp_layer_renderer_t for direct frame submission
 *     bypassing CAMetalLayer (lower latency)
 *   - ARKit WorldTrackingProvider gives 6DOF head pose at IMU rate (~200Hz)
 *   - Hand tracking → controller emulation via ARKit HandAnchor
 */

#include <cstdint>
#include <functional>

namespace mvrvb {

struct TrackedDevicePose_t;
struct ATWEyeFrame;

/**
 * Vision Pro rendering context.
 * One instance per app session.
 */
class VisionProBackend {
public:
    VisionProBackend();
    ~VisionProBackend();

    bool init();
    void shutdown();

    /** @return true if running on a visionOS / Vision Pro device or simulator. */
    static bool isAvailable();

    // ── Per-frame API ─────────────────────────────────────────────────────────
    /** Block until CompositorServices is ready for a new frame. */
    bool waitNextFrame();

    /**
     * Submit eye textures to CompositorServices.
     * @param leftTex   id<MTLTexture> for the left eye
     * @param rightTex  id<MTLTexture> for the right eye
     */
    bool submitFrame(void* leftTex, void* rightTex);

    // ── Tracking ──────────────────────────────────────────────────────────────
    void getHeadPose(TrackedDevicePose_t* pose) const;
    void getHandPose(int hand, TrackedDevicePose_t* pose) const;  ///< 0=left, 1=right

    // ── Display properties ────────────────────────────────────────────────────
    void    getRecommendedRenderSize(uint32_t* w, uint32_t* h) const;
    float   getDisplayHz() const;
    float   getIPD() const;

    void    getProjectionTangents(int eye, float* left, float* right, float* up, float* down) const;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace mvrvb
