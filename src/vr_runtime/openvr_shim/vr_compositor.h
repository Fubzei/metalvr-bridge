#pragma once
/**
 * @file vr_compositor.h
 * @brief IVRCompositor_028 implementation.
 *
 * Manages the frame submission pipeline:
 *   1.  WaitGetPoses     → block until the next frame window; return predicted poses
 *   2.  Submit           → accept game's eye textures (VkImage / Metal texture)
 *   3.  PostPresentHandoff → signal the compositor to display the frame
 *
 * Internally uses a triple-buffered eye texture ring and schedules the
 * Metal compositor pipeline (distortion + timewarp + display).
 */

#include "vr_system.h"
#include <cstdint>
#include <memory>

namespace mvrvb {

enum class EVRSubmitFlags : uint32_t {
    Default           = 0,
    LensDistortionAlreadyApplied = 1 << 0,
    GlRenderBuffer    = 1 << 1,
    Reserved          = 1 << 2,
    TextureWithPose   = 1 << 3,
    TextureWithDepth  = 1 << 4,
};

struct Texture_t {
    void*    handle;         ///< VkImage* or id<MTLTexture> cast to void*
    uint32_t eType;          ///< 0=OpenGL, 2=Vulkan, 4=Metal
    uint32_t eColorSpace;    ///< 0=Auto, 1=Gamma, 2=Linear
};

struct VRTextureBounds_t {
    float uMin, vMin, uMax, vMax;
};

class MvVRCompositor {
public:
    explicit MvVRCompositor(MvVRSystem* system);
    ~MvVRCompositor();

    void shutdown();

    // ── Frame lifecycle ───────────────────────────────────────────────────────
    /** Block until the compositor is ready for the next frame. Returns predicted poses. */
    uint32_t waitGetPoses(TrackedDevicePose_t* pRenderPoseArray, uint32_t unRenderPoseArrayCount,
                          TrackedDevicePose_t* pGamePoseArray,   uint32_t unGamePoseArrayCount);

    /** Submit a rendered eye texture. */
    uint32_t submit(EVREye eEye, const Texture_t* pTexture,
                    const VRTextureBounds_t* pBounds, EVRSubmitFlags nSubmitFlags);

    /** Signal that all eye textures have been submitted. */
    void postPresentHandoff();

    /** @return true if the compositor is currently rendering the scene (not dashboard). */
    bool isFullscreen() const { return true; }

    /** Allow the game to render to the compositor window. */
    bool canRenderScene() const { return true; }

    void setTrackingSpace(uint32_t origin);
    uint32_t getTrackingSpace() const { return trackingOrigin_; }

    float getFrameTimeRemaining() const;

    void fadeToColor(float fSeconds, float fRed, float fGreen, float fBlue, float fAlpha, bool bBackground);
    void fadeGrid(float fSeconds, bool bFadeIn);

private:
    MvVRSystem* system_{nullptr};
    uint32_t    trackingOrigin_{0};
    uint64_t    frameIndex_{0};

    // Left/right submitted textures for the current frame (void* = id<MTLTexture>)
    void* eyeTexture_[2]{nullptr, nullptr};
    bool  eyeSubmitted_[2]{false, false};

    void presentFrame();
};

} // namespace mvrvb
