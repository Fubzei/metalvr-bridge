/**
 * @file vr_overlay.mm
 * @brief IVROverlay implementation.
 */
#include "vr_overlay.h"
#include "../../common/logging.h"
#include <cstring>

namespace mvrvb {

uint32_t MvVROverlay::createOverlay(const char* key, const char* name, VROverlayHandle_t* out) {
    if (!key || !out) return 1;
    uint64_t h = nextHandle_++;
    overlays_[h] = { key, name ? name : "", false };
    *out = h;
    MVRVB_LOG_DEBUG("Overlay created: %s handle=%llu", key, (unsigned long long)h);
    return 0;
}

uint32_t MvVROverlay::destroyOverlay(VROverlayHandle_t h) {
    overlays_.erase(h); return 0;
}

uint32_t MvVROverlay::showOverlay(VROverlayHandle_t h) {
    auto it = overlays_.find(h);
    if (it != overlays_.end()) it->second.visible = true;
    return 0;
}

uint32_t MvVROverlay::hideOverlay(VROverlayHandle_t h) {
    auto it = overlays_.find(h);
    if (it != overlays_.end()) it->second.visible = false;
    return 0;
}

bool MvVROverlay::isOverlayVisible(VROverlayHandle_t h) {
    auto it = overlays_.find(h);
    return it != overlays_.end() && it->second.visible;
}

uint32_t MvVROverlay::setOverlayTexture(VROverlayHandle_t, const void*) { return 0; }
uint32_t MvVROverlay::setOverlayWidthInMeters(VROverlayHandle_t, float) { return 0; }
uint32_t MvVROverlay::setOverlayTransformAbsolute(VROverlayHandle_t, uint32_t, const void*) { return 0; }

uint32_t MvVROverlay::findOverlay(const char* key, VROverlayHandle_t* out) {
    for (auto& [h, e] : overlays_) {
        if (e.key == key) { if (out) *out = h; return 0; }
    }
    return 1; // VROverlayError_UnknownOverlay
}

} // namespace mvrvb
