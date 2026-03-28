#pragma once
/**
 * @file vr_input.h
 * @brief IVRInput_010 — action-manifest based input system.
 *
 * Maps hand-tracking from ARKit / CoreMotion to OpenVR controller actions.
 * For SteamVR titles that use the legacy GetControllerState API we also
 * provide a compatibility shim via MvVRSystem.
 */
#include "vr_system.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace mvrvb {

using VRActionSetHandle_t = uint64_t;
using VRActionHandle_t    = uint64_t;
using VRInputValueHandle_t = uint64_t;

struct InputDigitalActionData_t {
    bool     bActive{false};
    VRInputValueHandle_t activeOrigin{0};
    bool     bState{false};
    bool     bChanged{false};
    float    fUpdateTime{0.0f};
};

struct InputAnalogActionData_t {
    bool     bActive{false};
    VRInputValueHandle_t activeOrigin{0};
    float    x{0.f}, y{0.f}, z{0.f};
    float    deltaX{0.f}, deltaY{0.f}, deltaZ{0.f};
    float    fUpdateTime{0.0f};
};

struct InputPoseActionData_t {
    bool              bActive{false};
    VRInputValueHandle_t activeOrigin{0};
    TrackedDevicePose_t pose;
};

class MvVRInput {
public:
    explicit MvVRInput(MvVRSystem* system);

    uint32_t setActionManifestPath(const char* pchActionManifestPath);
    uint32_t getActionSetHandle(const char* pchActionSetName, VRActionSetHandle_t* pHandle);
    uint32_t getActionHandle(const char* pchActionName, VRActionHandle_t* pHandle);
    uint32_t getInputSourceHandle(const char* pchInputSourcePath, VRInputValueHandle_t* pHandle);
    uint32_t updateActionState(const void* pSets, uint32_t unSizeOfVRSelectedActionSet_t, uint32_t unSetCount);
    uint32_t getDigitalActionData(VRActionHandle_t action, InputDigitalActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice);
    uint32_t getAnalogActionData(VRActionHandle_t action, InputAnalogActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice);
    uint32_t getPoseActionDataForNextFrame(VRActionHandle_t action, uint32_t origin, InputPoseActionData_t* pActionData, uint32_t unActionDataSize, VRInputValueHandle_t ulRestrictToDevice);

private:
    MvVRSystem* system_;
    uint64_t    nextHandle_{1};
    std::unordered_map<std::string, uint64_t> handles_;
};

} // namespace mvrvb
