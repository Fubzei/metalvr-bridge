/**
 * @file vision_pro_backend.mm
 * @brief Vision Pro backend — CompositorServices + ARKit integration.
 *
 * CompositorServices API is available on visionOS 1.0+ and macOS 14+ when
 * the MVRVB_VISION_PRO compile flag is set.
 *
 * On macOS without CompositorServices (Intel / early Apple Silicon) we fall
 * back to a CAMetalLayer presentation path with CoreMotion rotation-only tracking.
 */

#include "vision_pro_backend.h"
#include "vision_pro_tracking.h"
#include "../../../common/logging.h"
#include "../../../vr_runtime/openvr_shim/vr_system.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#if __has_include(<CompositorServices/CompositorServices.h>)
  #define HAVE_COMPOSITOR_SERVICES 1
  #import <CompositorServices/CompositorServices.h>
#else
  #define HAVE_COMPOSITOR_SERVICES 0
#endif

#if __has_include(<ARKit/ARKit.h>)
  #define HAVE_ARKIT 1
  #import <ARKit/ARKit.h>
#else
  #define HAVE_ARKIT 0
#endif

#include <mutex>
#include <atomic>
#include <cstring>
#include <cmath>

namespace mvrvb {

struct VisionProBackend::Impl {
    id<MTLDevice>      device{nil};
    id<MTLCommandQueue> queue{nil};

    // Fallback CAMetalLayer when CompositorServices is not available
    CAMetalLayer*      fallbackLayer{nil};

    VisionProTracking* tracking{nullptr};

    bool useCompositorServices{false};

    uint32_t renderWidth{2064};
    uint32_t renderHeight{2096};
    float    displayHz{90.f};
    float    ipd{0.063f};

    // Projection tangents per eye (symmetric 110° FOV as default)
    float tanLeft[2]  = {-1.192f, -1.192f};
    float tanRight[2] = { 1.192f,  1.192f};
    float tanUp[2]    = { 1.192f,  1.192f};
    float tanDown[2]  = {-1.192f, -1.192f};
};

VisionProBackend::VisionProBackend() : impl_(new Impl{}) {}

VisionProBackend::~VisionProBackend() {
    shutdown();
    delete impl_;
}

bool VisionProBackend::isAvailable() {
#if HAVE_COMPOSITOR_SERVICES
    return true;
#else
    // On macOS Intel / early AS, return true for rotation-only mode
    return true;
#endif
}

bool VisionProBackend::init() {
    @autoreleasepool {
        impl_->device = MTLCreateSystemDefaultDevice();
        if (!impl_->device) {
            MVRVB_LOG_ERROR("Vision Pro backend: no Metal device");
            return false;
        }
        impl_->queue = [impl_->device newCommandQueue];

        // Initialise tracking
        impl_->tracking = new VisionProTracking();
        impl_->tracking->start();

#if HAVE_COMPOSITOR_SERVICES
        impl_->useCompositorServices = true;
        MVRVB_LOG_INFO("Vision Pro: CompositorServices available");
#else
        // Fallback: create an offscreen CAMetalLayer for desktop macOS
        impl_->fallbackLayer = [CAMetalLayer layer];
        impl_->fallbackLayer.device = impl_->device;
        impl_->fallbackLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        impl_->fallbackLayer.drawableSize = CGSizeMake(impl_->renderWidth * 2,
                                                        impl_->renderHeight);
        MVRVB_LOG_INFO("Vision Pro: CompositorServices not available; using CAMetalLayer fallback");
#endif
    }

    MVRVB_LOG_INFO("VisionPro backend init: %ux%u @ %.0fHz", impl_->renderWidth, impl_->renderHeight, impl_->displayHz);
    return true;
}

void VisionProBackend::shutdown() {
    if (impl_->tracking) {
        impl_->tracking->stop();
        delete impl_->tracking;
        impl_->tracking = nullptr;
    }
}

bool VisionProBackend::waitNextFrame() {
#if HAVE_COMPOSITOR_SERVICES
    // cp_layer_renderer_t::query_next_frame — blocks until the display is ready
    // TODO: wire up when CompositorServices C++ bridging is available
    return true;
#else
    // Simulate 90Hz frame pacing with a sleep
    struct timespec ts{ 0, 11111111 }; // ~11.1ms
    nanosleep(&ts, nullptr);
    return true;
#endif
}

bool VisionProBackend::submitFrame(void* leftTex, void* rightTex) {
    if (!leftTex || !rightTex) return false;

    @autoreleasepool {
#if HAVE_COMPOSITOR_SERVICES
        // TODO: submit via cp_layer_renderer once bridging is available
        (void)leftTex; (void)rightTex;
        return true;
#else
        id<CAMetalDrawable> drawable = [impl_->fallbackLayer nextDrawable];
        if (!drawable) return false;

        id<MTLCommandBuffer> cb = [impl_->queue commandBuffer];
        cb.label = @"VisionPro.Present";

        // Blit left eye to left half, right eye to right half
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        id<MTLTexture> left  = (__bridge id<MTLTexture>)leftTex;
        id<MTLTexture> right = (__bridge id<MTLTexture>)rightTex;

        // Left eye
        [blit copyFromTexture:left
                  sourceSlice:0 sourceLevel:0
                 sourceOrigin:{0,0,0}
                   sourceSize:{left.width, left.height, 1}
                    toTexture:drawable.texture
             destinationSlice:0 destinationLevel:0
            destinationOrigin:{0, 0, 0}];

        // Right eye (offset by renderWidth in X)
        [blit copyFromTexture:right
                  sourceSlice:0 sourceLevel:0
                 sourceOrigin:{0,0,0}
                   sourceSize:{right.width, right.height, 1}
                    toTexture:drawable.texture
             destinationSlice:0 destinationLevel:0
            destinationOrigin:{(NSUInteger)impl_->renderWidth, 0, 0}];

        [blit endEncoding];
        [cb presentDrawable:drawable];
        [cb commit];
        return true;
#endif
    }
}

void VisionProBackend::getHeadPose(TrackedDevicePose_t* pose) const {
    if (!pose) return;
    if (impl_->tracking) {
        *pose = impl_->tracking->getHeadPose();
    } else {
        std::memset(pose, 0, sizeof(*pose));
        pose->bPoseIsValid = false;
    }
}

void VisionProBackend::getHandPose(int hand, TrackedDevicePose_t* pose) const {
    if (!pose) return;
    if (impl_->tracking) {
        *pose = impl_->tracking->getHandPose(hand);
    } else {
        std::memset(pose, 0, sizeof(*pose));
    }
}

void VisionProBackend::getRecommendedRenderSize(uint32_t* w, uint32_t* h) const {
    if (w) *w = impl_->renderWidth;
    if (h) *h = impl_->renderHeight;
}

float VisionProBackend::getDisplayHz() const { return impl_->displayHz; }
float VisionProBackend::getIPD()       const { return impl_->ipd;       }

void VisionProBackend::getProjectionTangents(int eye, float* l, float* r, float* u, float* d) const {
    if (l) *l = impl_->tanLeft[eye];
    if (r) *r = impl_->tanRight[eye];
    if (u) *u = impl_->tanUp[eye];
    if (d) *d = impl_->tanDown[eye];
}

} // namespace mvrvb
