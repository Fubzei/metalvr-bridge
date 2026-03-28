/**
 * @file vr_input.mm
 * @brief IVRInput implementation — action manifest + controller state.
 *
 * Currently provides stub digital/analog actions (all inactive) and
 * routes head pose via MvVRSystem::getCurrentPose.
 * Hand-tracking integration with ARKit hand anchors is a TODO.
 */

#include "vr_input.h"
#include "../../common/logging.h"
#include <cstring>

namespace mvrvb {

MvVRInput::MvVRInput(MvVRSystem* system) : system_(system) {}

uint32_t MvVRInput::setActionManifestPath(const char* path) {
    MVRVB_LOG_INFO("IVRInput: action manifest = %s", path ? path : "(null)");
    return 0; // VRInputError_None
}

static uint64_t hashStr(const char* s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
    return h;
}

uint32_t MvVRInput::getActionSetHandle(const char* name, VRActionSetHandle_t* out) {
    if (!name || !out) return 1;
    auto h = hashStr(name);
    handles_[name] = h;
    *out = h;
    return 0;
}

uint32_t MvVRInput::getActionHandle(const char* name, VRActionHandle_t* out) {
    if (!name || !out) return 1;
    auto h = hashStr(name);
    handles_[name] = h;
    *out = h;
    return 0;
}

uint32_t MvVRInput::getInputSourceHandle(const char* path, VRInputValueHandle_t* out) {
    if (!path || !out) return 1;
    *out = hashStr(path);
    return 0;
}

uint32_t MvVRInput::updateActionState(const void*, uint32_t, uint32_t) {
    return 0;
}

uint32_t MvVRInput::getDigitalActionData(VRActionHandle_t, InputDigitalActionData_t* d,
                                          uint32_t sz, VRInputValueHandle_t) {
    if (d && sz >= sizeof(InputDigitalActionData_t)) *d = InputDigitalActionData_t{};
    return 0;
}

uint32_t MvVRInput::getAnalogActionData(VRActionHandle_t, InputAnalogActionData_t* d,
                                         uint32_t sz, VRInputValueHandle_t) {
    if (d && sz >= sizeof(InputAnalogActionData_t)) *d = InputAnalogActionData_t{};
    return 0;
}

uint32_t MvVRInput::getPoseActionDataForNextFrame(VRActionHandle_t, uint32_t,
                                                   InputPoseActionData_t* d, uint32_t sz,
                                                   VRInputValueHandle_t) {
    if (d && sz >= sizeof(InputPoseActionData_t)) {
        d->bActive = true;
        d->pose    = system_->getCurrentPose(0);
    }
    return 0;
}

} // namespace mvrvb
