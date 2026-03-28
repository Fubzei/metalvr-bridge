/**
 * @file async_timewarp.mm
 * @brief ATW thread implementation using Metal compute (timewarp.metal kernel).
 *
 * Thread priority: THREAD_TIME_CONSTRAINT_POLICY at ~90Hz with a 2ms budget.
 * The ATW thread wakes, fetches the latest head pose, computes the warp matrix,
 * dispatches the timewarp compute kernel, and signals the display.
 */

#include "async_timewarp.h"
#include "../../common/logging.h"
#include "../../common/threading.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <array>
#include <cstring>
#include <cmath>
#include <chrono>

// ── Matrix helpers ────────────────────────────────────────────────────────────

namespace mvrvb {

TimewarpMatrix TimewarpMatrix::identity() {
    TimewarpMatrix m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

// Multiply two 4x4 row-major matrices
static TimewarpMatrix matMul(const TimewarpMatrix& a, const TimewarpMatrix& b) {
    TimewarpMatrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

// Transpose (inverse of rotation-only matrix)
static TimewarpMatrix matTranspose3x3(const TimewarpMatrix& a) {
    TimewarpMatrix r = TimewarpMatrix::identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[j][i];
    return r;
}

// ── Timewarp uniforms (matches timewarp.metal) ────────────────────────────────

struct TimewarpUniforms {
    float    warpMatrix[16];
    float    eyeTextureSize[2];
    float    nearPlane;
    float    farPlane;
    float    tanHalfFov[2];
};

// ── ATW impl ──────────────────────────────────────────────────────────────────

struct AsyncTimewarp::Impl {
    // Metal
    id<MTLDevice>             device{nil};
    id<MTLCommandQueue>       atwQueue{nil};
    id<MTLComputePipelineState> timewarpPipeline{nil};
    id<MTLRenderPipelineState>  distortionPipeline{nil};

    // Eye frame ring buffer (double-buffered per eye)
    static constexpr int kRingSize = 3;
    std::mutex           frameMutex;
    ATWEyeFrame          frames[2][kRingSize]{};  // [eye][slot]
    int                  writeSlot[2]{0, 0};
    int                  readSlot[2]{0, 0};
    std::atomic<bool>    frameAvailable[2]{false, false};

    // Current tracking pose (written from IMU thread, read from ATW thread)
    std::mutex     poseMutex;
    TimewarpMatrix currentPose{TimewarpMatrix::identity()};

    // ATW thread
    std::thread        atwThread;
    std::atomic<bool>  running{false};
    std::mutex         wakeupMutex;
    std::condition_variable wakeupCV;

    // Stats
    static constexpr int kStatWindow = 64;
    float latencyHistory[kStatWindow]{};
    int   latencyIdx{0};
    float avgLatencyMs{0.0f};

    void threadFunc() {
        // Set real-time priority
        setRealtimePriority(90.0, 0.002);  // 90Hz, 2ms budget

        MVRVB_LOG_INFO("ATW thread started");

        while (running.load()) {
            // Wait for a new frame from either eye
            {
                std::unique_lock<std::mutex> lock(wakeupMutex);
                wakeupCV.wait_for(lock, std::chrono::milliseconds(12));
            }
            if (!running.load()) break;

            if (!frameAvailable[0].load() || !frameAvailable[1].load())
                continue;

            auto t0 = std::chrono::high_resolution_clock::now();

            @autoreleasepool {
                processFrame();
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
            latencyHistory[latencyIdx % kStatWindow] = ms;
            ++latencyIdx;

            // Update rolling average
            float sum = 0.f;
            int   n   = std::min(latencyIdx, kStatWindow);
            for (int i = 0; i < n; ++i) sum += latencyHistory[i];
            avgLatencyMs = sum / n;

            frameAvailable[0].store(false);
            frameAvailable[1].store(false);
        }

        MVRVB_LOG_INFO("ATW thread stopped (avg latency %.2f ms)", avgLatencyMs);
    }

    void processFrame() {
        if (!timewarpPipeline) {
            // Pipeline not ready — skip this frame
            return;
        }

        // Read the current predicted pose
        TimewarpMatrix predict;
        {
            std::lock_guard<std::mutex> lock(poseMutex);
            predict = currentPose;
        }

        for (int eye = 0; eye < 2; ++eye) {
            ATWEyeFrame frame;
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                frame = frames[eye][readSlot[eye]];
            }

            if (!frame.mtlTexture) continue;

            // Compute warp matrix: predict * inverse(render)
            TimewarpMatrix renderInv = matTranspose3x3(frame.renderPose);
            TimewarpMatrix warp      = matMul(predict, renderInv);

            // Build uniforms
            TimewarpUniforms u{};
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    u.warpMatrix[i*4+j] = warp.m[i][j];

            id<MTLTexture> srcTex = (__bridge id<MTLTexture>)frame.mtlTexture;
            u.eyeTextureSize[0] = (float)srcTex.width;
            u.eyeTextureSize[1] = (float)srcTex.height;
            u.nearPlane  = 0.1f;
            u.farPlane   = 1000.f;
            u.tanHalfFov[0] = 1.0f;
            u.tanHalfFov[1] = 1.0f;

            // Dispatch compute timewarp kernel
            id<MTLCommandBuffer> cb = [atwQueue commandBuffer];
            cb.label = [NSString stringWithFormat:@"ATW_eye%d", eye];

            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:timewarpPipeline];
            [enc setTexture:srcTex atIndex:0];
            [enc setTexture:srcTex atIndex:1]; // TODO: separate output texture
            [enc setBytes:&u length:sizeof(u) atIndex:0];

            MTLSize threads    = {8, 8, 1};
            MTLSize grid       = {
                (srcTex.width  + 7) / 8,
                (srcTex.height + 7) / 8,
                1
            };
            [enc dispatchThreadgroups:grid threadsPerThreadgroup:threads];
            [enc endEncoding];
            [cb commit];
        }
    }

    bool buildPipelines() {
        @autoreleasepool {
            // Load the timewarp Metal shader from the app bundle
            NSError* err = nil;
            NSBundle* bundle = [NSBundle mainBundle];
            NSString* metalLibPath = [bundle pathForResource:@"default" ofType:@"metallib"];
            if (!metalLibPath) {
                MVRVB_LOG_WARN("ATW: default.metallib not found; skipping pipeline build");
                return false;
            }
            NSURL* url = [NSURL fileURLWithPath:metalLibPath];
            id<MTLLibrary> lib = [device newLibraryWithURL:url error:&err];
            if (!lib) {
                MVRVB_LOG_ERROR("ATW: failed to load metallib: %s",
                                [[err localizedDescription] UTF8String]);
                return false;
            }

            id<MTLFunction> fn = [lib newFunctionWithName:@"timewarp_compute"];
            if (!fn) {
                MVRVB_LOG_WARN("ATW: timewarp_compute function not found in library");
                return false;
            }

            timewarpPipeline = [device newComputePipelineStateWithFunction:fn error:&err];
            if (!timewarpPipeline) {
                MVRVB_LOG_ERROR("ATW: failed to create compute pipeline: %s",
                                [[err localizedDescription] UTF8String]);
                return false;
            }
        }
        return true;
    }
};

// ── Public API ────────────────────────────────────────────────────────────────

AsyncTimewarp::AsyncTimewarp() : impl_(std::make_unique<Impl>()) {}
AsyncTimewarp::~AsyncTimewarp() { stop(); }

bool AsyncTimewarp::start(void* mtlDevicePtr) {
    if (impl_->running.load()) return true;

    @autoreleasepool {
        impl_->device   = (__bridge id<MTLDevice>)mtlDevicePtr;
        impl_->atwQueue = [impl_->device newCommandQueueWithMaxCommandBufferCount:8];
        impl_->atwQueue.label = @"MetalVRBridge.ATW";
    }

    impl_->buildPipelines();

    impl_->running.store(true);
    impl_->atwThread = std::thread([this]{ impl_->threadFunc(); });
    return true;
}

void AsyncTimewarp::stop() {
    if (!impl_->running.load()) return;
    impl_->running.store(false);
    impl_->wakeupCV.notify_all();
    if (impl_->atwThread.joinable())
        impl_->atwThread.join();
}

void AsyncTimewarp::submitFrame(int eye, const ATWEyeFrame& frame) {
    if (eye < 0 || eye > 1) return;
    {
        std::lock_guard<std::mutex> lock(impl_->frameMutex);
        int slot = (impl_->writeSlot[eye] + 1) % Impl::kRingSize;
        impl_->frames[eye][slot] = frame;
        impl_->readSlot[eye]  = slot;
        impl_->writeSlot[eye] = slot;
    }
    impl_->frameAvailable[eye].store(true);
    impl_->wakeupCV.notify_one();
}

void AsyncTimewarp::updateCurrentPose(const TimewarpMatrix& pose) {
    std::lock_guard<std::mutex> lock(impl_->poseMutex);
    impl_->currentPose = pose;
}

float AsyncTimewarp::averageLatencyMs() const {
    return impl_->avgLatencyMs;
}

} // namespace mvrvb
