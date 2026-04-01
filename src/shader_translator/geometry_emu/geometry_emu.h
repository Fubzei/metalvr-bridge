#pragma once
/**
 * @file geometry_emu.h
 * @brief Geometry shader emulator: SPIR-V geometry stage → Metal compute + indirect draw.
 *
 * Metal has no geometry shader stage.  This module emulates it with two passes:
 *
 *   Pass 1 (MTLComputeCommandEncoder):
 *     - Each threadgroup handles one input primitive.
 *     - The "expansion function" (translated from SPIR-V) emits 0..maxVertices
 *       output vertices into a typed MTLBuffer.
 *     - An atomic counter in a separate buffer accumulates the output vertex count.
 *
 *   Pass 2 (MTLRenderCommandEncoder with indirect draw):
 *     - vkCmdDrawIndirect reads the output vertex count from Pass 1.
 *     - A passthrough vertex shader reads GeoEmitVertex from the output buffer.
 *
 * Limitations / deferred work:
 *   - Only triangle list input topology is currently handled.
 *   - Output topology must be triangle list or triangle strip (converted to list).
 *   - Stream output / transform feedback not implemented.
 *   - Multi-stream output not implemented.
 *   - Maximum 60 output vertices per input primitive (adjustable via maxVertices).
 */

#include "../spirv/spirv_parser.h"
#include <cstdint>
#include <string>

namespace mvrvb {

/** Configuration for one geometry stage emulation. */
struct GeometryEmuOptions {
    uint32_t    maxOutputVertices{60};   ///< From SPIR-V OutputVertices decoration
    uint32_t    inputPrimitiveVertices{3}; ///< 1=points, 2=lines, 3=triangles (also linestrip=2, tristrip=3)
    bool        inputAdjacency{false};   ///< Lines/triangles with adjacency (6/6 verts)
    std::string outputTopology{"triangles"}; ///< "points", "line_strip", "triangle_strip"
    std::string symbolPrefix{"geo_emu_"};
};

/**
 * Emitted by `buildGeometryEmulator()`.
 * Contains the Metal Shading Language source for:
 *   - The compute expansion kernel
 *   - The passthrough vertex + fragment shaders for the draw pass
 */
struct GeometryEmuResult {
    std::string computeKernelMSL;   ///< Complete .metal source for the compute pass
    std::string drawShaderMSL;      ///< Complete .metal source for the draw pass
    uint32_t    outputVertexStrideBytes{0};
    bool        success{false};
    std::string errorMessage;
};

/**
 * Build the geometry emulator Metal shaders for the given SPIR-V geometry function.
 *
 * @param module       Parsed SPIR-V module containing the geometry entry point.
 * @param entryIndex   Index of the geometry entry point in module.entryPoints.
 * @param opts         Emulation options (topology, vertex counts, etc.)
 * @return             Emitted MSL sources + metadata.
 */
GeometryEmuResult buildGeometryEmulator(const spirv::SPIRVModule& module,
                                        uint32_t                  entryIndex,
                                        const GeometryEmuOptions& opts);

} // namespace mvrvb
