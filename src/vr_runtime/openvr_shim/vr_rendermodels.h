#pragma once
/**
 * @file vr_rendermodels.h
 * @brief IVRRenderModels_006 — controller / hand mesh models.
 *
 * Returns a simple flat quad for any requested render model (stub).
 */
#include <cstdint>

namespace mvrvb {

struct RenderModel_Vertex_t {
    float vPosition[3];
    float vNormal[3];
    float rfTextureCoord[2];
};
struct RenderModel_t {
    const RenderModel_Vertex_t* rVertexData;
    uint32_t unVertexCount;
    const uint16_t* rIndexData;
    uint32_t unTriangleCount;
    uint32_t diffuseTextureId;
};
struct RenderModel_TextureMap_t {
    uint16_t unWidth, unHeight;
    const uint8_t* rubTextureMapData;
};

class MvVRRenderModels {
public:
    uint32_t loadRenderModel_Async(const char* pchRenderModelName, RenderModel_t** ppRenderModel);
    void     freeRenderModel(RenderModel_t* pRenderModel);
    uint32_t loadTexture_Async(int32_t textureId, RenderModel_TextureMap_t** ppTexture);
    void     freeTexture(RenderModel_TextureMap_t* pTexture);
    const char* getRenderModelName(uint32_t unRenderModelIndex) const;
    uint32_t getRenderModelCount() const;
    const char* getRenderModelThumbnailURL(const char* pchRenderModelName, char* pchThumbnailURL, uint32_t unThumbnailURLLen, uint32_t* peError) const;
};

} // namespace mvrvb
