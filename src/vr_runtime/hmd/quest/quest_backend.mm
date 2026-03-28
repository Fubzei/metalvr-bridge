/**
 * @file quest_backend.mm
 * @brief Quest streaming backend — VideoToolbox H.265 + UDP.
 */

#include "quest_backend.h"
#include "../../../common/logging.h"
#include "../../../vr_runtime/openvr_shim/vr_system.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <Network/Network.h>

#include <mutex>
#include <atomic>
#include <thread>
#include <cstring>
#include <chrono>

namespace mvrvb {

struct QuestBackend::Impl {
    QuestConfig       config;
    id<MTLDevice>     device{nil};
    id<MTLCommandQueue> queue{nil};

    // VideoToolbox compression session
    VTCompressionSessionRef vt_session{nullptr};

    // Network
    nw_connection_t    udpConn{nullptr};
    dispatch_queue_t   netQueue{nullptr};

    std::atomic<bool>  connected{false};
    std::atomic<uint64_t> frameIndex{0};

    // Latest head pose from Quest
    mutable std::mutex     poseMutex;
    TrackedDevicePose_t    headPose{};

    // Frame timing
    std::chrono::steady_clock::time_point lastFrame;

    static void vtCallback(void* /*ctx*/, void* /*src*/,
                           OSStatus status, VTEncodeInfoFlags /*flags*/,
                           CMSampleBufferRef sampleBuffer) {
        if (status != noErr || !sampleBuffer) return;
        // Extract NAL units and send via UDP
        // TODO: fragment if > MTU (1400 bytes) and add sequence numbers
        MVRVB_LOG_TRACE("VT encoded frame (status=%d)", (int)status);
    }

    bool createVTSession(uint32_t width, uint32_t height, uint32_t fps, uint32_t bps) {
        NSDictionary* encoderSpec = @{
            (NSString*)kVTVideoEncoderSpecification_EnableLowLatencyRateControl: @YES,
        };
        NSDictionary* imageSpec = @{
            (NSString*)kCVPixelBufferWidthKey:       @(width * 2),
            (NSString*)kCVPixelBufferHeightKey:      @(height),
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
        };

        OSStatus err = VTCompressionSessionCreate(
            nullptr, (int32_t)(width * 2), (int32_t)height,
            kCMVideoCodecType_HEVC,
            (__bridge CFDictionaryRef)encoderSpec,
            (__bridge CFDictionaryRef)imageSpec,
            nullptr, vtCallback, nullptr,
            &vt_session
        );

        if (err != noErr) {
            MVRVB_LOG_ERROR("VTCompressionSessionCreate failed: %d", (int)err);
            return false;
        }

        VTSessionSetProperty(vt_session, kVTCompressionPropertyKey_RealTime,         kCFBooleanTrue);
        VTSessionSetProperty(vt_session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
        VTSessionSetProperty(vt_session, kVTCompressionPropertyKey_AverageBitRate,
                             (__bridge CFTypeRef)@(bps));
        VTSessionSetProperty(vt_session, kVTCompressionPropertyKey_ExpectedFrameRate,
                             (__bridge CFTypeRef)@(fps));

        VTCompressionSessionPrepareToEncodeFrames(vt_session);
        MVRVB_LOG_INFO("Quest: VideoToolbox H.265 session ready (%ux%u @ %u fps %u Mbps)",
                       width*2, height, fps, bps/1000000);
        return true;
    }
};

QuestBackend::QuestBackend() : impl_(new Impl{}) {}
QuestBackend::~QuestBackend() { shutdown(); delete impl_; }

bool QuestBackend::init(const QuestConfig& config) {
    impl_->config = config;
    impl_->headPose.mDeviceToAbsoluteTracking.m[0][0] = 1;
    impl_->headPose.mDeviceToAbsoluteTracking.m[1][1] = 1;
    impl_->headPose.mDeviceToAbsoluteTracking.m[2][2] = 1;
    impl_->headPose.eTrackingResult  = ETrackingResult::Running_OK;
    impl_->headPose.bPoseIsValid     = true;
    impl_->headPose.bDeviceIsConnected = true;

    @autoreleasepool {
        impl_->device = MTLCreateSystemDefaultDevice();
        impl_->queue  = [impl_->device newCommandQueue];
        impl_->netQueue = dispatch_queue_create("MetalVRBridge.quest.net", DISPATCH_QUEUE_SERIAL);

        if (!impl_->createVTSession(config.renderWidth, config.renderHeight,
                                    config.fps, config.bitrateMbps * 1000000)) {
            return false;
        }

        // Set up UDP connection to broadcast / configured IP
        nw_endpoint_t ep = nw_endpoint_create_host(
            config.clientIP.c_str(),
            std::to_string(config.videoPort).c_str()
        );
        nw_parameters_t params = nw_parameters_create_secure_udp(
            NW_PARAMETERS_DISABLE_PROTOCOL,
            NW_PARAMETERS_DEFAULT_CONFIGURATION
        );
        impl_->udpConn = nw_connection_create(ep, params);
        nw_connection_set_queue(impl_->udpConn, impl_->netQueue);
        nw_connection_set_state_changed_handler(impl_->udpConn,
            ^(nw_connection_state_t state, nw_error_t /*err*/) {
                if (state == nw_connection_state_ready) {
                    MVRVB_LOG_INFO("Quest: UDP connection ready");
                    impl_->connected.store(true);
                } else if (state == nw_connection_state_failed ||
                           state == nw_connection_state_cancelled) {
                    impl_->connected.store(false);
                }
            });
        nw_connection_start(impl_->udpConn);
    }

    impl_->lastFrame = std::chrono::steady_clock::now();
    MVRVB_LOG_INFO("Quest backend initialised");
    return true;
}

void QuestBackend::shutdown() {
    if (impl_->vt_session) {
        VTCompressionSessionInvalidate(impl_->vt_session);
        CFRelease(impl_->vt_session);
        impl_->vt_session = nullptr;
    }
    if (impl_->udpConn) {
        nw_connection_cancel(impl_->udpConn);
        impl_->udpConn = nullptr;
    }
}

bool QuestBackend::isConnected() const { return impl_->connected.load(); }

bool QuestBackend::waitNextFrame() {
    // Simple frame pacing based on target FPS
    float period = 1.0f / impl_->config.fps;
    auto target = impl_->lastFrame + std::chrono::duration<float>(period);
    std::this_thread::sleep_until(target);
    impl_->lastFrame = std::chrono::steady_clock::now();
    return true;
}

bool QuestBackend::submitFrame(void* leftTex, void* rightTex) {
    if (!leftTex || !rightTex || !impl_->vt_session) return false;
    // TODO: blit left+right textures to a side-by-side CVPixelBuffer and
    //       feed to VTCompressionSessionEncodeFrame.
    ++impl_->frameIndex;
    MVRVB_LOG_TRACE("Quest: frame %llu submitted", (unsigned long long)impl_->frameIndex.load());
    return true;
}

void QuestBackend::getHeadPose(TrackedDevicePose_t* pose) const {
    if (!pose) return;
    std::lock_guard<std::mutex> lock(impl_->poseMutex);
    *pose = impl_->headPose;
}

void QuestBackend::getRecommendedRenderSize(uint32_t* w, uint32_t* h) const {
    if (w) *w = impl_->config.renderWidth;
    if (h) *h = impl_->config.renderHeight;
}

float QuestBackend::getDisplayHz() const { return (float)impl_->config.fps; }

} // namespace mvrvb
