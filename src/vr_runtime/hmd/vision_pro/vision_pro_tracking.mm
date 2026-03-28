/**
 * @file vision_pro_tracking.mm
 * @brief Head and hand tracking using ARKit WorldTracking + HandTracking providers.
 *
 * On macOS without ARKit hand tracking we fall back to CoreMotion DeviceMotion
 * for rotation-only 3DOF tracking (covers iMac, MacBook, external HMD use cases).
 */

#include "vision_pro_tracking.h"
#include "../../../common/logging.h"

#import <Foundation/Foundation.h>
#import <CoreMotion/CoreMotion.h>

#if __has_include(<ARKit/ARKit.h>)
  #import <ARKit/ARKit.h>
  #define HAVE_ARKIT 1
#else
  #define HAVE_ARKIT 0
#endif

#include <mutex>
#include <cstring>
#include <cmath>

namespace mvrvb {

static HmdMatrix34_t identityMat34() {
    HmdMatrix34_t m; std::memset(&m,0,sizeof(m));
    m.m[0][0]=m.m[1][1]=m.m[2][2]=1.f; return m;
}

static HmdMatrix34_t quatToMat34(double qw, double qx, double qy, double qz) {
    float w=(float)qw,x=(float)qx,y=(float)qy,z=(float)qz;
    HmdMatrix34_t m;
    m.m[0][0]=1-2*(y*y+z*z); m.m[0][1]=2*(x*y-w*z);   m.m[0][2]=2*(x*z+w*y);   m.m[0][3]=0;
    m.m[1][0]=2*(x*y+w*z);   m.m[1][1]=1-2*(x*x+z*z); m.m[1][2]=2*(y*z-w*x);   m.m[1][3]=0;
    m.m[2][0]=2*(x*z-w*y);   m.m[2][1]=2*(y*z+w*x);   m.m[2][2]=1-2*(x*x+y*y); m.m[2][3]=0;
    return m;
}

struct VisionProTracking::Impl {
    CMMotionManager*    motionManager{nil};
    mutable std::mutex  headMutex;
    TrackedDevicePose_t headPose{};
    TrackedDevicePose_t handPose[2]{};

    void start() {
        headPose.mDeviceToAbsoluteTracking = identityMat34();
        headPose.eTrackingResult = ETrackingResult::Running_OK;
        headPose.bPoseIsValid = true;
        headPose.bDeviceIsConnected = true;

        motionManager = [[CMMotionManager alloc] init];
        if (motionManager.deviceMotionAvailable) {
            motionManager.deviceMotionUpdateInterval = 1.0 / 200.0; // 200Hz
            [motionManager startDeviceMotionUpdatesUsingReferenceFrame:
                CMAttitudeReferenceFrameXArbitraryZVertical
                toQueue:[NSOperationQueue new]
                withHandler:^(CMDeviceMotion* motion, NSError* /*err*/) {
                    if (!motion) return;
                    CMQuaternion q = motion.attitude.quaternion;
                    std::lock_guard<std::mutex> lock(headMutex);
                    headPose.mDeviceToAbsoluteTracking = quatToMat34(q.w, q.x, q.y, q.z);
                    headPose.vVelocity = {
                        (float)motion.rotationRate.x,
                        (float)motion.rotationRate.y,
                        (float)motion.rotationRate.z
                    };
                    headPose.eTrackingResult = ETrackingResult::Fallback_RotationOnly;
                    headPose.bPoseIsValid = true;
                }];
            MVRVB_LOG_INFO("VisionProTracking: CoreMotion started at 200Hz");
        } else {
            MVRVB_LOG_WARN("VisionProTracking: no DeviceMotion; using identity pose");
        }
    }

    void stop() {
        if (motionManager) {
            [motionManager stopDeviceMotionUpdates];
            motionManager = nil;
        }
    }
};

VisionProTracking::VisionProTracking() : impl_(new Impl{}) {}
VisionProTracking::~VisionProTracking() { stop(); delete impl_; }

void VisionProTracking::start() { impl_->start(); }
void VisionProTracking::stop()  { impl_->stop();  }

TrackedDevicePose_t VisionProTracking::getHeadPose() const {
    std::lock_guard<std::mutex> lock(impl_->headMutex);
    return impl_->headPose;
}

TrackedDevicePose_t VisionProTracking::getHandPose(int hand) const {
    // Hand tracking via ARKit HandAnchor is a TODO
    // Return an invalid pose for now
    TrackedDevicePose_t p{};
    p.eTrackingResult     = ETrackingResult::Uninitialized;
    p.bPoseIsValid        = false;
    p.bDeviceIsConnected  = false;
    return p;
}

} // namespace mvrvb
