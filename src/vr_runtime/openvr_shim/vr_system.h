#pragma once
/**
 * @file vr_system.h
 * @brief IVRSystem_022 implementation.
 *
 * Provides HMD properties, display timing, tracking poses, and
 * controller/hand device enumeration to SteamVR games.
 */

#include <cstdint>
#include <string>
#include <array>

// ── Tracking pose ─────────────────────────────────────────────────────────────
struct HmdMatrix34_t { float m[3][4]; };
struct HmdMatrix44_t { float m[4][4]; };
struct HmdVector3_t  { float v[3]; };
struct HmdVector2_t  { float v[2]; };
struct HmdQuaternion_t { double w, x, y, z; };

enum class ETrackingResult : uint32_t {
    Uninitialized         = 1,
    Calibrating_InProgress = 100,
    Calibrating_OutOfRange = 101,
    Running_OK            = 200,
    Running_OutOfRange    = 201,
    Fallback_RotationOnly = 300,
};

struct TrackedDevicePose_t {
    HmdMatrix34_t  mDeviceToAbsoluteTracking;
    HmdVector3_t   vVelocity;
    HmdVector3_t   vAngularVelocity;
    ETrackingResult eTrackingResult{ETrackingResult::Uninitialized};
    bool           bPoseIsValid{false};
    bool           bDeviceIsConnected{false};
};

static constexpr uint32_t kMaxTrackedDeviceCount = 64;

// ── Eye / projection ──────────────────────────────────────────────────────────
enum class EVREye : uint32_t { Eye_Left = 0, Eye_Right = 1 };

namespace mvrvb {

class MvVRSystem {
public:
    MvVRSystem()  = default;
    ~MvVRSystem() = default;

    bool init(uint32_t applicationType);

    // ── Display ───────────────────────────────────────────────────────────────
    void     getRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) const;
    HmdMatrix44_t getProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ) const;
    void     getProjectionRaw(EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) const;
    bool     computeDistortion(EVREye eEye, float fU, float fV, float* rfRed, float* rfGreen, float* rfBlue) const;
    HmdMatrix34_t getEyeToHeadTransform(EVREye eEye) const;

    // ── Timing ────────────────────────────────────────────────────────────────
    bool     getTimeSinceLastVsync(float* pfSecondsSinceLastVsync, uint64_t* pulFrameCounter) const;
    float    getIPD() const;
    float    getDisplayFrequency() const { return displayHz_; }

    // ── Tracking ──────────────────────────────────────────────────────────────
    void     getDeviceToAbsoluteTrackingPose(
                 uint32_t eOrigin,
                 float    fPredictedSecondsToPhotonsFromNow,
                 TrackedDevicePose_t* pTrackedDevicePoseArray,
                 uint32_t unTrackedDevicePoseArrayCount) const;

    // Returns an identity pose with ETrackingResult::Running_OK when tracking
    // is not available (e.g. desktop VR without a positional tracker).
    TrackedDevicePose_t getCurrentPose(uint32_t deviceIndex) const;

    // ── Device properties ─────────────────────────────────────────────────────
    bool         isTrackedDeviceConnected(uint32_t deviceIndex) const;
    const char*  getStringProperty(uint32_t deviceIndex, uint32_t prop, char* buf, uint32_t bufLen, uint32_t* peError) const;
    float        getFloatProperty(uint32_t deviceIndex, uint32_t prop, uint32_t* peError) const;
    int32_t      getInt32Property(uint32_t deviceIndex, uint32_t prop, uint32_t* peError) const;
    uint64_t     getUint64Property(uint32_t deviceIndex, uint32_t prop, uint32_t* peError) const;
    bool         getBoolProperty(uint32_t deviceIndex, uint32_t prop, uint32_t* peError) const;

    // ── Input ─────────────────────────────────────────────────────────────────
    uint32_t getTrackedDeviceIndexForControllerRole(uint32_t role) const;

private:
    uint32_t  renderWidth_{2064};
    uint32_t  renderHeight_{2096};  // Per-eye for Apple Vision Pro
    float     displayHz_{90.0f};
    float     ipd_{0.063f};         // Default 63mm IPD
    uint32_t  applicationType_{0};

    // Pre-baked left/right projection tangents for 90° FOV
    float tanLeft_[2]  = {-1.0f, -1.0f};  // {left-eye, right-eye}
    float tanRight_[2] = { 1.0f,  1.0f};
    float tanUp_[2]    = { 1.0f,  1.0f};
    float tanDown_[2]  = {-1.0f, -1.0f};
};

} // namespace mvrvb
