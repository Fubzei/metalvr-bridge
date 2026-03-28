/**
 * @file openvr_shim.mm
 * @brief Wine-compatible OpenVR API entry points.
 *
 * Wine loads openvr_api.dll; this dylib exports the same C entry points so
 * Wine forwards them transparently.  The "dll override" in the Wine prefix
 * is set to "native,builtin" for openvr_api.
 *
 * All interface objects are singletons allocated on first VR_Init and freed
 * on VR_Shutdown.  Re-initialisation is supported.
 */

#include "openvr_shim.h"
#include "vr_system.h"
#include "vr_compositor.h"
#include "vr_input.h"
#include "vr_overlay.h"
#include "vr_chaperone.h"
#include "vr_settings.h"
#include "vr_rendermodels.h"
#include "../../common/logging.h"

#import <Foundation/Foundation.h>

#include <mutex>
#include <string>
#include <cstring>

namespace mvrvb {

// ── Global singleton state ────────────────────────────────────────────────────

struct VRState {
    std::unique_ptr<MvVRSystem>       system;
    std::unique_ptr<MvVRCompositor>   compositor;
    std::unique_ptr<MvVRInput>        input;
    std::unique_ptr<MvVROverlay>      overlay;
    std::unique_ptr<MvVRChaperone>    chaperone;
    std::unique_ptr<MvVRSettings>     settings;
    std::unique_ptr<MvVRRenderModels> renderModels;
    bool                              initialised{false};
};

static VRState    gState;
static std::mutex gMutex;

// ── VRInit ────────────────────────────────────────────────────────────────────

void* VRInit(uint32_t applicationType, const char* /*startupInfo*/, VRInitError* error) {
    std::lock_guard<std::mutex> lock(gMutex);

    if (gState.initialised) {
        MVRVB_LOG_WARN("VR_Init called while already initialised; returning existing system");
        if (error) *error = VRInitError::None;
        return gState.system.get();
    }

    MVRVB_LOG_INFO("VR_Init: applicationType=%u", applicationType);

    @autoreleasepool {
        gState.system       = std::make_unique<MvVRSystem>();
        gState.compositor   = std::make_unique<MvVRCompositor>(gState.system.get());
        gState.input        = std::make_unique<MvVRInput>(gState.system.get());
        gState.overlay      = std::make_unique<MvVROverlay>();
        gState.chaperone    = std::make_unique<MvVRChaperone>();
        gState.settings     = std::make_unique<MvVRSettings>();
        gState.renderModels = std::make_unique<MvVRRenderModels>();

        if (!gState.system->init(applicationType)) {
            MVRVB_LOG_ERROR("VR_Init: system init failed");
            if (error) *error = VRInitError::Init_HmdNotFound;
            gState = VRState{};
            return nullptr;
        }

        gState.initialised = true;
    }

    MVRVB_LOG_INFO("VR_Init: success");
    if (error) *error = VRInitError::None;
    return gState.system.get();
}

// ── VRShutdown ────────────────────────────────────────────────────────────────

void VRShutdown() {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gState.initialised) return;
    MVRVB_LOG_INFO("VR_Shutdown");
    gState.compositor->shutdown();
    gState = VRState{};
}

// ── VRIsHmdPresent ────────────────────────────────────────────────────────────

bool VRIsHmdPresent() {
    // TODO: probe CoreBluetooth / IOHIDManager for HMD devices; for now
    // return true when compiled with Vision Pro or streaming support.
#if defined(MVRVB_VISION_PRO) || defined(MVRVB_STREAMING)
    return true;
#else
    return false;
#endif
}

// ── VRGetGenericInterface ─────────────────────────────────────────────────────

void* VRGetGenericInterface(const char* ver, VRInitError* error) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gState.initialised) {
        if (error) *error = VRInitError::Init_NotInitialized;
        return nullptr;
    }
    if (error) *error = VRInitError::None;

    if      (std::strstr(ver, "IVRSystem"))       return gState.system.get();
    else if (std::strstr(ver, "IVRCompositor"))   return gState.compositor.get();
    else if (std::strstr(ver, "IVRInput"))        return gState.input.get();
    else if (std::strstr(ver, "IVROverlay"))      return gState.overlay.get();
    else if (std::strstr(ver, "IVRChaperone"))    return gState.chaperone.get();
    else if (std::strstr(ver, "IVRSettings"))     return gState.settings.get();
    else if (std::strstr(ver, "IVRRenderModels")) return gState.renderModels.get();

    MVRVB_LOG_WARN("VRGetGenericInterface: unknown interface '%s'", ver);
    if (error) *error = VRInitError::Init_InterfaceNotFound;
    return nullptr;
}

// ── Error string helpers ──────────────────────────────────────────────────────

const char* VRGetVRInitErrorAsSymbol(VRInitError err) {
    switch (err) {
    case VRInitError::None:                   return "VRInitError_None";
    case VRInitError::Init_InstallationNotFound: return "VRInitError_Init_InstallationNotFound";
    case VRInitError::Init_HmdNotFound:       return "VRInitError_Init_HmdNotFound";
    case VRInitError::Init_NotInitialized:    return "VRInitError_Init_NotInitialized";
    case VRInitError::Init_InterfaceNotFound: return "VRInitError_Init_InterfaceNotFound";
    default:                                  return "VRInitError_Unknown";
    }
}

const char* VRGetVRInitErrorAsEnglishDescription(VRInitError err) {
    switch (err) {
    case VRInitError::None:                   return "No error";
    case VRInitError::Init_HmdNotFound:       return "No HMD was found";
    case VRInitError::Init_NotInitialized:    return "The VR system was not initialized";
    case VRInitError::Init_InterfaceNotFound: return "The requested interface was not found";
    default:                                  return "Unknown error";
    }
}

} // namespace mvrvb

// ── C / Wine-compatible export wrappers ──────────────────────────────────────

extern "C" {

__attribute__((visibility("default")))
void* VR_Init(mvrvb::VRInitError* peError, uint32_t eType) {
    return mvrvb::VRInit(eType, nullptr, peError);
}

__attribute__((visibility("default")))
void VR_Shutdown() {
    mvrvb::VRShutdown();
}

__attribute__((visibility("default")))
bool VR_IsHmdPresent() {
    return mvrvb::VRIsHmdPresent();
}

__attribute__((visibility("default")))
void* VR_GetGenericInterface(const char* pchInterfaceVersion, mvrvb::VRInitError* peError) {
    return mvrvb::VRGetGenericInterface(pchInterfaceVersion, peError);
}

__attribute__((visibility("default")))
bool VR_IsRuntimeInstalled() {
    return true;
}

__attribute__((visibility("default")))
const char* VR_RuntimePath() {
    return "/Library/Application Support/MetalVRBridge";
}

__attribute__((visibility("default")))
const char* VR_GetVRInitErrorAsSymbol(mvrvb::VRInitError eError) {
    return mvrvb::VRGetVRInitErrorAsSymbol(eError);
}

__attribute__((visibility("default")))
const char* VR_GetVRInitErrorAsEnglishDescription(mvrvb::VRInitError eError) {
    return mvrvb::VRGetVRInitErrorAsEnglishDescription(eError);
}

} // extern "C"
