#pragma once
/**
 * @file async_timewarp.h
 * @brief Asynchronous Timewarp (ATW) thread and reprojection pipeline.
 *
 * ATW runs on a dedicated high-priority Metal command queue (separate from the
 * application's render queue) and fires at display scan-out time, after the
 * application has submitted its eye textures.
 *
 * Timeline:
 *   T=0ms    WaitGetPoses → application starts rendering frame N
 *   T=8ms    Application calls Submit() with both eye textures
 *   T=8ms    ATW thread picks up textures + render pose
 *   T=9ms    ATW computes warp matrix (predicted_pose * inv(render_pose))
 *   T=9.5ms  ATW submits timewarp + distortion Metal command buffer
 *   T=11.1ms Display scan-out
 *
 * The warp matrix is computed from:
 *   render_pose   = pose at WaitGetPoses() time
 *   predict_pose  = pose at scan-out time (extrapolated from IMU)
 *   warp          = predict_rotation * inverse(render_rotation)
 */

#include <cstdint>
#include <atomic>
#include <memory>

namespace mvrvb {

// 4x4 row-major float matrix (enough for the warp + projection)
struct TimewarpMatrix {
    float m[4][4];
    static TimewarpMatrix identity();
};

/**
 * Eye texture handle passed from the compositor to the ATW thread.
 * Carries both the Metal texture pointer (id<MTLTexture>) and the
 * predicted head pose at render time.
 */
struct ATWEyeFrame {
    void*           mtlTexture{nullptr};   ///< id<MTLTexture>
    TimewarpMatrix  renderPose;            ///< Head rotation at render start
    uint64_t        frameIndex{0};
};

class AsyncTimewarp {
public:
    AsyncTimewarp();
    ~AsyncTimewarp();

    /** Start the ATW thread and Metal pipeline. */
    bool start(void* mtlDevice);

    /** Stop the ATW thread cleanly. */
    void stop();

    /**
     * Submit a completed eye frame to the ATW queue.
     * Called from the compositor thread after Submit().
     */
    void submitFrame(int eye, const ATWEyeFrame& frame);

    /**
     * Set the current predicted pose from the tracking thread.
     * Called at high frequency from the IMU update thread.
     */
    void updateCurrentPose(const TimewarpMatrix& pose);

    /** @return Average ATW processing time in milliseconds over last 64 frames. */
    float averageLatencyMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mvrvb
