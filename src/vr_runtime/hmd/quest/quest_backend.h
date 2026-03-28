#pragma once
/**
 * @file quest_backend.h
 * @brief Meta Quest streaming backend (ALVR-compatible).
 *
 * Renders eye images on the Mac, H.265 encodes via VideoToolbox, and streams
 * over UDP to a Quest client (ALVR or compatible).
 *
 * Protocol:
 *   - UDP control port 9943  (session negotiation, pose updates)
 *   - UDP video  port 9944   (H.265 NAL units, fragmented if >MTU)
 *   - IMU pose received from Quest at ~250Hz
 *   - Frame acknowledged by client; missing frames trigger keyframe request
 */

#include <cstdint>
#include <functional>
#include <string>

namespace mvrvb {

struct TrackedDevicePose_t;

struct QuestConfig {
    std::string clientIP{"255.255.255.255"};  ///< Broadcast for auto-discovery
    uint16_t    controlPort{9943};
    uint16_t    videoPort{9944};
    uint32_t    renderWidth{1832};    ///< Per-eye render width
    uint32_t    renderHeight{1920};   ///< Per-eye render height
    uint32_t    bitrateMbps{150};
    uint32_t    fps{72};
    bool        foveatedEncoding{true};
};

class QuestBackend {
public:
    QuestBackend();
    ~QuestBackend();

    bool init(const QuestConfig& config);
    void shutdown();

    /** @return true if a Quest client is connected. */
    bool isConnected() const;

    /** Block until next frame timing slot. */
    bool waitNextFrame();

    /**
     * Encode and stream the rendered eye textures.
     * @param leftTex   id<MTLTexture> left eye
     * @param rightTex  id<MTLTexture> right eye
     */
    bool submitFrame(void* leftTex, void* rightTex);

    /** Latest head pose received from the Quest. */
    void getHeadPose(TrackedDevicePose_t* pose) const;

    void getRecommendedRenderSize(uint32_t* w, uint32_t* h) const;
    float getDisplayHz() const;

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace mvrvb
