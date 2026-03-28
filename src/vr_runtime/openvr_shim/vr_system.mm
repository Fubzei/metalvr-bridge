/**
 * @file vr_system.mm
 * @brief IVRSystem implementation using CoreMotion + ARKit for tracking.
 */

#include "vr_system.h"
#include "../../common/logging.h"

#import <Foundation/Foundation.h>
#import <CoreMotion/CoreMotion.h>

#include <cstring>
#include <cmath>
#include <chrono>

// ── TrackedDeviceProp constants (subset of openvr.h) ─────────────────────────
static constexpr uint32_t Prop_TrackingSystemName_String       = 1000;
static constexpr uint32_t Prop_ModelNumber_String              = 1001;
static constexpr uint32_t Prop_SerialNumber_String             = 1002;
static constexpr uint32_t Prop_ManufacturerName_String         = 1004;
static constexpr uint32_t Prop_DisplayFrequency_Float          = 1011;
static constexpr uint32_t Prop_UserIpdMeters_Float             = 2000;
static constexpr uint32_t Prop_CurrentUniverseId_Uint64        = 2004;
static constexpr uint32_t Prop_IsOnDesktop_Bool                = 2007;
static constexpr uint32_t Prop_DeviceClass_Int32               = 1029;
static constexpr uint32_t Prop_HasCamera_Bool                  = 1024;

// CoreMotion motion manager for head tracking on devices without ARKit
static CMMotionManager* gMotionManager = nil;

namespace mvrvb {

static HmdMatrix34_t identityMatrix34() {
    HmdMatrix34_t m;
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
    return m;
}

// Quaternion → rotation matrix (row-major, right-handed)
static HmdMatrix34_t quatToMatrix34(double qw, double qx, double qy, double qz) {
    HmdMatrix34_t m;
    float w = (float)qw, x = (float)qx, y = (float)qy, z = (float)qz;
    m.m[0][0] = 1-2*(y*y+z*z); m.m[0][1] = 2*(x*y-w*z);   m.m[0][2] = 2*(x*z+w*y);   m.m[0][3] = 0;
    m.m[1][0] = 2*(x*y+w*z);   m.m[1][1] = 1-2*(x*x+z*z); m.m[1][2] = 2*(y*z-w*x);   m.m[1][3] = 0;
    m.m[2][0] = 2*(x*z-w*y);   m.m[2][1] = 2*(y*z+w*x);   m.m[2][2] = 1-2*(x*x+y*y); m.m[2][3] = 0;
    return m;
}

bool MvVRSystem::init(uint32_t applicationType) {
    applicationType_ = applicationType;

    // Initialise CoreMotion for rotation-only tracking on macOS
    @autoreleasepool {
        if (!gMotionManager) {
            gMotionManager = [[CMMotionManager alloc] init];
        }
        if (gMotionManager.deviceMotionAvailable) {
            gMotionManager.deviceMotionUpdateInterval = 1.0 / displayHz_;
            [gMotionManager startDeviceMotionUpdatesUsingReferenceFrame:
                CMAttitudeReferenceFrameXArbitraryZVertical];
            MVRVB_LOG_INFO("CoreMotion device motion started");
        } else {
            MVRVB_LOG_WARN("CoreMotion not available; using identity pose");
        }
    }

    MVRVB_LOG_INFO("VRSystem init OK (renderSize=%ux%u @ %.0fHz IPD=%.1fmm)",
                   renderWidth_, renderHeight_, displayHz_, ipd_ * 1000.f);
    return true;
}

void MvVRSystem::getRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) const {
    if (w) *w = renderWidth_;
    if (h) *h = renderHeight_;
}

HmdMatrix44_t MvVRSystem::getProjectionMatrix(EVREye eye, float nearZ, float farZ) const {
    int e = (int)eye;
    float l = tanLeft_[e],  r = tanRight_[e];
    float u = tanUp_[e],    d = tanDown_[e];

    HmdMatrix44_t m;
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] =  2.0f / (r - l);
    m.m[1][1] =  2.0f / (u - d);
    m.m[2][0] =  (l + r) / (r - l);
    m.m[2][1] =  (u + d) / (u - d);
    m.m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m.m[2][3] = -1.0f;
    m.m[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return m;
}

void MvVRSystem::getProjectionRaw(EVREye eye, float* l, float* r, float* t, float* b) const {
    int e = (int)eye;
    if (l) *l = tanLeft_[e];
    if (r) *r = tanRight_[e];
    if (t) *t = tanUp_[e];
    if (b) *b = tanDown_[e];
}

bool MvVRSystem::computeDistortion(EVREye, float fU, float fV,
                                    float* rfR, float* rfG, float* rfB) const {
    // Passthrough — distortion is handled by the compositor mesh
    if (rfR) { rfR[0] = fU; rfR[1] = fV; }
    if (rfG) { rfG[0] = fU; rfG[1] = fV; }
    if (rfB) { rfB[0] = fU; rfB[1] = fV; }
    return true;
}

HmdMatrix34_t MvVRSystem::getEyeToHeadTransform(EVREye eye) const {
    HmdMatrix34_t m = identityMatrix34();
    float offset = (eye == EVREye::Eye_Left) ? -ipd_ * 0.5f : ipd_ * 0.5f;
    m.m[0][3] = offset;  // X translation in metres
    return m;
}

bool MvVRSystem::getTimeSinceLastVsync(float* pSec, uint64_t* pFrame) const {
    static auto last = std::chrono::high_resolution_clock::now();
    static uint64_t frame = 0;
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    float period = 1.0f / displayHz_;
    if (dt >= period) { last = now; ++frame; }
    if (pSec)   *pSec   = std::fmod(dt, period);
    if (pFrame) *pFrame = frame;
    return true;
}

float MvVRSystem::getIPD() const { return ipd_; }

void MvVRSystem::getDeviceToAbsoluteTrackingPose(
    uint32_t /*origin*/, float /*predictedSecs*/,
    TrackedDevicePose_t* poses, uint32_t count) const
{
    if (!poses || count == 0) return;
    TrackedDevicePose_t hmdPose = getCurrentPose(0);
    poses[0] = hmdPose;
    // Mark all other devices as disconnected
    for (uint32_t i = 1; i < count; ++i) {
        poses[i] = TrackedDevicePose_t{};
        poses[i].eTrackingResult = ETrackingResult::Uninitialized;
    }
}

TrackedDevicePose_t MvVRSystem::getCurrentPose(uint32_t deviceIndex) const {
    TrackedDevicePose_t pose{};
    pose.bDeviceIsConnected = (deviceIndex == 0);

    if (deviceIndex != 0) {
        pose.eTrackingResult = ETrackingResult::Uninitialized;
        return pose;
    }

    @autoreleasepool {
        CMDeviceMotion* motion = gMotionManager ? gMotionManager.deviceMotion : nil;
        if (motion) {
            CMQuaternion q = motion.attitude.quaternion;
            pose.mDeviceToAbsoluteTracking = quatToMatrix34(q.w, q.x, q.y, q.z);
            pose.vVelocity       = { (float)motion.rotationRate.x,
                                     (float)motion.rotationRate.y,
                                     (float)motion.rotationRate.z };
            pose.vAngularVelocity = pose.vVelocity;
            pose.eTrackingResult = ETrackingResult::Fallback_RotationOnly;
        } else {
            pose.mDeviceToAbsoluteTracking = identityMatrix34();
            pose.eTrackingResult = ETrackingResult::Running_OK;
        }
        pose.bPoseIsValid = true;
    }
    return pose;
}

bool MvVRSystem::isTrackedDeviceConnected(uint32_t deviceIndex) const {
    return deviceIndex == 0;
}

const char* MvVRSystem::getStringProperty(uint32_t deviceIndex, uint32_t prop,
                                           char* buf, uint32_t bufLen, uint32_t* err) const {
    if (err) *err = 0;
    if (deviceIndex != 0) { if (err)*err = 1; return ""; }

    const char* val = "";
    switch (prop) {
    case Prop_TrackingSystemName_String: val = "MetalVRBridge"; break;
    case Prop_ModelNumber_String:        val = "AppleVisionPro"; break;
    case Prop_SerialNumber_String:       val = "MVRVB-001"; break;
    case Prop_ManufacturerName_String:   val = "Apple Inc."; break;
    default: if (err) *err = 2; break;
    }
    if (buf && bufLen > 0) {
        std::strncpy(buf, val, bufLen - 1);
        buf[bufLen - 1] = '\0';
    }
    return val;
}

float MvVRSystem::getFloatProperty(uint32_t, uint32_t prop, uint32_t* err) const {
    if (err) *err = 0;
    switch (prop) {
    case Prop_DisplayFrequency_Float: return displayHz_;
    case Prop_UserIpdMeters_Float:    return ipd_;
    default: if (err) *err = 2; return 0.0f;
    }
}

int32_t MvVRSystem::getInt32Property(uint32_t, uint32_t prop, uint32_t* err) const {
    if (err) *err = 0;
    switch (prop) {
    case Prop_DeviceClass_Int32: return 1; // HMD
    default: if (err) *err = 2; return 0;
    }
}

uint64_t MvVRSystem::getUint64Property(uint32_t, uint32_t prop, uint32_t* err) const {
    if (err) *err = 0;
    switch (prop) {
    case Prop_CurrentUniverseId_Uint64: return 1;
    default: if (err) *err = 2; return 0;
    }
}

bool MvVRSystem::getBoolProperty(uint32_t, uint32_t prop, uint32_t* err) const {
    if (err) *err = 0;
    switch (prop) {
    case Prop_HasCamera_Bool:   return false;
    case Prop_IsOnDesktop_Bool: return false;
    default: if (err) *err = 2; return false;
    }
}

uint32_t MvVRSystem::getTrackedDeviceIndexForControllerRole(uint32_t role) const {
    return UINT32_MAX; // No controllers yet
}

} // namespace mvrvb
