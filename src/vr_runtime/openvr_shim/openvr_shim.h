#pragma once
/**
 * @file openvr_shim.h
 * @brief macOS OpenVR shim — public interface.
 *
 * Wine loads openvr_api.dll from the game's directory.  We replace it with
 * this macOS dylib (renamed to openvr_api.dll for Wine compatibility via
 * dll-override).
 *
 * The shim implements the OpenVR IVR* interfaces using Objective-C++ Metal
 * for rendering and CoreMotion / ARKit for tracking data.
 *
 * Entry points:
 *   VR_Init              → Creates and returns the IVRSystem interface
 *   VR_Shutdown          → Tears down the VR runtime
 *   VR_IsHmdPresent      → Returns true if a supported HMD is connected
 *   VR_GetGenericInterface → Returns other IVR* interface pointers
 *   VR_InitInternal      → Low-level init used by the OpenVR loader
 *   VR_GetVRInitErrorAsSymbol / VR_GetVRInitErrorAsEnglishDescription
 */

#include <cstdint>
#include <string>

// ── Error codes ───────────────────────────────────────────────────────────────
enum class VRInitError : uint32_t {
    None                 = 0,
    Init_InstallationNotFound = 100,
    Init_HmdNotFound     = 108,
    Init_NotInitialized  = 110,
    Init_WebServerFailed = 112,
    Init_FileNotFound    = 113,
    Init_FactoryNotFound = 121,
    Init_InterfaceNotFound = 124,
    Init_InvalidInterface  = 125,
};

// ── Interface version strings (must match the OpenVR SDK versions the game uses)
static constexpr const char* kIVRSystem_Version         = "IVRSystem_022";
static constexpr const char* kIVRCompositor_Version     = "IVRCompositor_028";
static constexpr const char* kIVRInput_Version          = "IVRInput_010";
static constexpr const char* kIVROverlay_Version        = "IVROverlay_025";
static constexpr const char* kIVRChaperone_Version      = "IVRChaperone_004";
static constexpr const char* kIVRSettings_Version       = "IVRSettings_003";
static constexpr const char* kIVRRenderModels_Version   = "IVRRenderModels_006";

namespace mvrvb {

/**
 * Initialise the VR runtime.
 * @param applicationType  0=Scene, 1=Overlay, 2=Background
 * @param startupInfo      Optional JSON startup info
 * @param error            Filled on failure
 * @return Pointer to IVRSystem, or nullptr on failure
 */
void* VRInit(uint32_t applicationType, const char* startupInfo, VRInitError* error);

/** Shut down and release all VR runtime resources. */
void  VRShutdown();

/** @return true if any supported HMD is detected. */
bool  VRIsHmdPresent();

/**
 * Retrieve a specific IVR* interface pointer.
 * @param interfaceVersion  e.g. "IVRSystem_022"
 * @param error             Filled on failure
 * @return Interface pointer cast to void*
 */
void* VRGetGenericInterface(const char* interfaceVersion, VRInitError* error);

const char* VRGetVRInitErrorAsSymbol(VRInitError error);
const char* VRGetVRInitErrorAsEnglishDescription(VRInitError error);

} // namespace mvrvb
