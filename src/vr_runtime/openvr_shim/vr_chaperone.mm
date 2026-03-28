/**
 * @file vr_chaperone.mm
 */
#include "vr_chaperone.h"
#include <cstring>

namespace mvrvb {

ChaperoneCalibrationState MvVRChaperone::getCalibrationState() const {
    return ChaperoneCalibrationState::OK;
}

bool MvVRChaperone::getPlayAreaSize(float* x, float* z) const {
    if (x) *x = 2.0f;
    if (z) *z = 2.0f;
    return true;
}

bool MvVRChaperone::getPlayAreaRect(HmdQuad_t* rect) const {
    if (!rect) return false;
    const float half = 1.0f;
    float corners[4][3] = {
        {-half, 0.f,  half}, { half, 0.f,  half},
        { half, 0.f, -half}, {-half, 0.f, -half}
    };
    std::memcpy(rect->vCorners, corners, sizeof(corners));
    return true;
}

void MvVRChaperone::reloadInfo() {}
void MvVRChaperone::setSceneColor(float, float, float, float) {}
void MvVRChaperone::getBoundsColor(float* r, float* g, float* b, float* a, int n, float, float*) const {
    if (r) *r = 0.1f; if (g) *g = 0.8f; if (b) *b = 0.8f; if (a) *a = 0.8f;
}
bool MvVRChaperone::areBoundsVisible() const { return false; }
void MvVRChaperone::forceBoundsVisible(bool) {}

} // namespace mvrvb
