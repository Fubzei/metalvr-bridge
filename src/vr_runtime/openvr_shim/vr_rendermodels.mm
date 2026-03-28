/**
 * @file vr_rendermodels.mm
 * @brief IVRRenderModels stub — returns a degenerate placeholder model.
 */
#include "vr_rendermodels.h"
#include "../../common/logging.h"
#include <cstring>
#include <vector>

namespace mvrvb {

// A minimal 1-triangle render model to satisfy apps that always load models
static const RenderModel_Vertex_t kStubVerts[3] = {
    {{0,0,0},{0,1,0},{0,0}},
    {{1,0,0},{0,1,0},{1,0}},
    {{0,1,0},{0,1,0},{0,1}},
};
static const uint16_t kStubIdx[3] = {0,1,2};
static const uint8_t  kStubPixel[4] = {128,128,128,255};

uint32_t MvVRRenderModels::loadRenderModel_Async(const char* name, RenderModel_t** ppOut) {
    MVRVB_LOG_DEBUG("LoadRenderModel: %s (stub)", name ? name : "");
    if (!ppOut) return 1;
    auto* m = new RenderModel_t{};
    m->rVertexData     = kStubVerts;
    m->unVertexCount   = 3;
    m->rIndexData      = kStubIdx;
    m->unTriangleCount = 1;
    m->diffuseTextureId = 0;
    *ppOut = m;
    return 0; // VRRenderModelError_None
}

void MvVRRenderModels::freeRenderModel(RenderModel_t* m) { delete m; }

uint32_t MvVRRenderModels::loadTexture_Async(int32_t, RenderModel_TextureMap_t** ppOut) {
    if (!ppOut) return 1;
    auto* t = new RenderModel_TextureMap_t{};
    t->unWidth  = 1;
    t->unHeight = 1;
    t->rubTextureMapData = kStubPixel;
    *ppOut = t;
    return 0;
}

void MvVRRenderModels::freeTexture(RenderModel_TextureMap_t* t) { delete t; }

const char* MvVRRenderModels::getRenderModelName(uint32_t idx) const {
    return (idx == 0) ? "generic_hmd" : nullptr;
}

uint32_t MvVRRenderModels::getRenderModelCount() const { return 1; }

const char* MvVRRenderModels::getRenderModelThumbnailURL(const char*, char* buf, uint32_t len, uint32_t* err) const {
    if (err) *err = 0;
    if (buf && len > 0) buf[0] = '\0';
    return "";
}

} // namespace mvrvb
