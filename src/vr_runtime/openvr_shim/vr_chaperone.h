#pragma once
/**
 * @file vr_chaperone.h
 * @brief IVRChaperone_004 — play area boundary management.
 *
 * Returns a minimal 2m x 2m standing-only play area on all platforms
 * until real room-scale tracking is available.
 */
#include <cstdint>

namespace mvrvb {

struct HmdVector2_t { float v[2]; };
struct HmdQuad_t    { float vCorners[4][3]; };

enum class ChaperoneCalibrationState : uint32_t {
    OK        = 1,
    Warning_BaseStationMayHaveMoved = 101,
    Error_Not_Calibrated = 200,
};

class MvVRChaperone {
public:
    ChaperoneCalibrationState getCalibrationState() const;
    bool getPlayAreaSize(float* pSizeX, float* pSizeZ) const;
    bool getPlayAreaRect(HmdQuad_t* rect) const;
    void reloadInfo();
    void setSceneColor(float r, float g, float b, float a);
    void getBoundsColor(float* r, float* g, float* b, float* a, int nNumOutputColors, float flCollisionBoundsFadeDistance, float* pOutputColorArray) const;
    bool areBoundsVisible() const;
    void forceBoundsVisible(bool bForce);
};

} // namespace mvrvb
