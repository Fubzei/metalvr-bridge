/**
 * @file vr_compositor.mm
 * @brief IVRCompositor implementation.
 *
 * Frame timing uses a CVDisplayLink (or CADisplayLink on newer macOS) to
 * synchronise submissions with the display vsync at 90 Hz.
 *
 * Texture interop:
 *   - Vulkan path: the game renders into a VkImage backed by a IOSurface
 *     (allocated in vk_memory.mm).  We extract the IOSurface and wrap it
 *     as a MTLTexture for zero-copy compositing.
 *   - Metal path: the game submits an id<MTLTexture> directly.
 */

#include "vr_compositor.h"
#include "../../common/logging.h"
#include "../../common/threading.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CVDisplayLink.h>

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace mvrvb {

// ── Frame synchronisation ─────────────────────────────────────────────────────

static std::mutex              gFrameMutex;
static std::condition_variable gFrameCV;
static bool                    gFrameReady{false};

// CVDisplayLink callback — fires at the display refresh rate
static CVReturn displayLinkCallback(CVDisplayLinkRef,
                                     const CVTimeStamp*,
                                     const CVTimeStamp*,
                                     CVOptionFlags,
                                     CVOptionFlags*,
                                     void* /*context*/) {
    std::lock_guard<std::mutex> lock(gFrameMutex);
    gFrameReady = true;
    gFrameCV.notify_all();
    return kCVReturnSuccess;
}

// ── MvVRCompositor ────────────────────────────────────────────────────────────

MvVRCompositor::MvVRCompositor(MvVRSystem* system)
    : system_(system)
{
    MVRVB_LOG_INFO("VRCompositor created");
}

MvVRCompositor::~MvVRCompositor() {
    shutdown();
}

void MvVRCompositor::shutdown() {
    MVRVB_LOG_INFO("VRCompositor shutdown");
}

uint32_t MvVRCompositor::waitGetPoses(
    TrackedDevicePose_t* pRenderPoses, uint32_t nRenderCount,
    TrackedDevicePose_t* pGamePoses,   uint32_t nGameCount)
{
    // Block until the next vsync window
    {
        std::unique_lock<std::mutex> lock(gFrameMutex);
        gFrameCV.wait_for(lock, std::chrono::milliseconds(20),
                          [](){ return gFrameReady; });
        gFrameReady = false;
    }

    // Fill predicted poses for this frame
    if (pRenderPoses && nRenderCount > 0)
        system_->getDeviceToAbsoluteTrackingPose(trackingOrigin_,
            1.0f / system_->getDisplayFrequency(), pRenderPoses, nRenderCount);

    if (pGamePoses && nGameCount > 0)
        system_->getDeviceToAbsoluteTrackingPose(trackingOrigin_,
            0.0f, pGamePoses, nGameCount);

    return 0; // VRCompositorError_None
}

uint32_t MvVRCompositor::submit(EVREye eEye, const Texture_t* pTexture,
                                 const VRTextureBounds_t* /*pBounds*/,
                                 EVRSubmitFlags /*flags*/)
{
    if (!pTexture) return 1; // VRCompositorError_InvalidTexture

    int eye = (int)eEye;
    @autoreleasepool {
        if (pTexture->eType == 4) {
            // Metal texture submitted directly
            eyeTexture_[eye] = pTexture->handle;
        } else if (pTexture->eType == 2) {
            // Vulkan: pTexture->handle is a VkImage*.
            // The VkImage's underlying MTLTexture is stored in MvImage::mtlTexture.
            // We cast through here — the compositor just stores the pointer.
            eyeTexture_[eye] = pTexture->handle;
        }
        eyeSubmitted_[eye] = true;
    }

    MVRVB_LOG_TRACE("Submit eye=%d type=%u", eye, pTexture->eType);
    return 0;
}

void MvVRCompositor::postPresentHandoff() {
    if (eyeSubmitted_[0] && eyeSubmitted_[1]) {
        presentFrame();
    }
    eyeSubmitted_[0] = eyeSubmitted_[1] = false;
    ++frameIndex_;
}

void MvVRCompositor::presentFrame() {
    // TODO: Route through async timewarp + distortion compositor.
    // For now: the metal layer of the main window picks up eye textures
    // directly in the next CAMetalLayer.nextDrawable cycle.
    MVRVB_LOG_TRACE("VRCompositor: present frame %llu", (unsigned long long)frameIndex_);
}

float MvVRCompositor::getFrameTimeRemaining() const {
    return 1.0f / system_->getDisplayFrequency() * 0.5f; // approx 5.5ms
}

void MvVRCompositor::setTrackingSpace(uint32_t origin) {
    trackingOrigin_ = origin;
}

void MvVRCompositor::fadeToColor(float, float, float, float, float, bool) {}
void MvVRCompositor::fadeGrid(float, bool) {}

} // namespace mvrvb
