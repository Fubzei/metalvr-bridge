#pragma once
/**
 * @file vision_pro_tracking.h
 * @brief ARKit + CoreMotion head/hand tracking for Vision Pro.
 */
#include "../../openvr_shim/vr_system.h"

namespace mvrvb {

class VisionProTracking {
public:
    VisionProTracking();
    ~VisionProTracking();

    void start();
    void stop();

    TrackedDevicePose_t getHeadPose() const;
    TrackedDevicePose_t getHandPose(int hand) const;  ///< 0=left, 1=right

private:
    struct Impl;
    Impl* impl_{nullptr};
};

} // namespace mvrvb
