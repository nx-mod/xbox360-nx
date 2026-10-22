// halo3 - the core of the run-time translator, shared with its PC test (halo3/tools/halo3_glsl_crudo.cpp): raw
// microcode to XenosRecomp GLSL. Include after XenosRecomp/shader_recompiler.h, built with NFSMW_RECOMP, HALO3_RECOMP
// and XENOS_GLSL. Throws what the translator throws.
#pragma once

#include "halo3_contenedor.h"

#include <span>
#include <string>
#include <string_view>

namespace halo3::nucleo {

// Where main starts: what follows is the shader's own code, past the prelude and the declarations.
inline std::string_view Cuerpo(const std::string& texto) {
  const size_t main = texto.find("\nvoid main()");
  return main == std::string::npos ? std::string_view() : std::string_view(texto).substr(main);
}

// The outputs a pixel shader's main writes (PixelShaderOutputs): oC0..oC3 and oDepth.
inline uint32_t SalidasEscritas(std::string_view cuerpo) {
  uint32_t salidas = 0;
  for (uint32_t c = 0; c < 4; ++c) {
    const char nombre[] = {'o', 'C', char('0' + c), '\0'};
    if (cuerpo.find(nombre) != std::string_view::npos) salidas |= PIXEL_SHADER_OUTPUT_COLOR0 << c;
  }
  if (cuerpo.find("oDepth") != std::string_view::npos) salidas |= PIXEL_SHADER_OUTPUT_DEPTH;
  return salidas;
}

// A pixel shader is translated first with every output declared, then again with only the ones it writes: an output
// it never writes would leave its render target undefined instead of untouched. `salidas` gets those (0 for a vertex
// shader). `interpoladores`: the pair's count (SQ_PROGRAM_CNTL), the same for both stages.
inline std::string Traducir(bool vertices, std::span<const uint32_t> microcodigo, std::string_view preludio,
                            uint32_t& salidas, uint32_t interpoladores = contenedor::kInterpoladores,
                            bool bucles_estructurados = true) {
  salidas = vertices ? 0u : uint32_t(PIXEL_SHADER_OUTPUT_COLOR0 | PIXEL_SHADER_OUTPUT_COLOR1 |
                                     PIXEL_SHADER_OUTPUT_COLOR2 | PIXEL_SHADER_OUTPUT_COLOR3 |
                                     PIXEL_SHADER_OUTPUT_DEPTH);
  std::string texto, primera;
  uint32_t todas = salidas;
  for (int pasada = 0; pasada < 2; ++pasada) {
    const std::vector<uint8_t> c = contenedor::ContenedorDeMicrocodigo(vertices, microcodigo, salidas, interpoladores);
    ShaderRecompiler traductor;
    traductor.soloInterpoladoresDelContenedor = true;  // only the pair's interpolators, not all 25
    traductor.buclesConSaltosEstructurados = bucles_estructurados;
    traductor.recompile(c.data(), preludio);
    texto = std::move(traductor.out);
    if (vertices) break;
    const uint32_t escritas = SalidasEscritas(Cuerpo(texto));
    if (pasada == 1) {
      // The second text must not write an output it no longer declares (it happened once, in the general form):
      // then the first, which declares them all.
      if (escritas & ~salidas) {
        texto = std::move(primera);
        salidas = todas;
      }
      break;
    }
    if (escritas == salidas) break;
    primera = texto;
    salidas = escritas;
  }
  return texto;
}

}  // namespace halo3::nucleo
