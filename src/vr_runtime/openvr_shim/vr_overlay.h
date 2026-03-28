#pragma once
/**
 * @file vr_overlay.h
 * @brief IVROverlay_025 stub — world-space overlay management.
 *
 * Overlay textures are composited by the Metal compositor on top of the scene.
 */
#include <cstdint>
#include <string>
#include <unordered_map>

namespace mvrvb {

using VROverlayHandle_t = uint64_t;
static constexpr VROverlayHandle_t kInvalidOverlayHandle = 0;

struct HmdMatrix34_t; // forward

class MvVROverlay {
public:
    uint32_t createOverlay(const char* pchOverlayKey, const char* pchOverlayName, VROverlayHandle_t* pOverlayHandle);
    uint32_t destroyOverlay(VROverlayHandle_t ulOverlayHandle);
    uint32_t showOverlay(VROverlayHandle_t);
    uint32_t hideOverlay(VROverlayHandle_t);
    bool     isOverlayVisible(VROverlayHandle_t);
    uint32_t setOverlayTexture(VROverlayHandle_t, const void* pTexture);
    uint32_t setOverlayWidthInMeters(VROverlayHandle_t, float fWidthInMeters);
    uint32_t setOverlayTransformAbsolute(VROverlayHandle_t, uint32_t trackingOrigin, const void* pmatTrackingOriginToOverlayTransform);
    uint32_t findOverlay(const char* pchOverlayKey, VROverlayHandle_t* pOverlayHandle);

private:
    uint64_t nextHandle_{1};
    struct OverlayEntry { std::string key, name; bool visible{false}; };
    std::unordered_map<uint64_t, OverlayEntry> overlays_;
};

} // namespace mvrvb
