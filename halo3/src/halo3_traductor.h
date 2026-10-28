// halo3 - shaders translated on the console: microcode the ring loads that the library (halo3_shaders.h) does not
// have, through XenosRecomp's GLSL mode (shaders/XenosRecomp, XENOS_GLSL, prelude shader_common.glsl) into an
// EntradaShader whose `glsl` the renderer compiles with uam (rex/ui/deko3d/glsl.h, GlslDeTraductor).
//
// The microcode is read the way Xenia reads it (halo3_contenedor.h): every vertex fetch loads from its buffer by the
// vertex index, and the interpolators go by index. This covers D3D's own shaders (raw microcode in the xex) and any
// shader no scanned file holds, and is what lets a player run with no shader library at all.
#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <rex/x360/shader_entry.h>

namespace halo3 {

// `microcodigo` in host byte order, as the ring carries it. false with the reason in `error`.
// `interpoladores`: the pair's interpolator count (SQ_PROGRAM_CNTL's vs_export_count + 1). `posicion_pixel`: the pixel
// shader register with the screen position (SQ_CONTEXT_MISC.param_gen_pos), 0xFF for none.
bool TraducirMicrocodigo(bool vertices, std::span<const uint32_t> microcodigo, uint32_t interpoladores,
                         uint32_t posicion_pixel, rex::x360::EntradaShader& entrada, std::string& error);

}  // namespace halo3
