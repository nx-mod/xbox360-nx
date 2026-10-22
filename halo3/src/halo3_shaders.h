// halo3 - the shader library on the console: Halo 3's translated shaders (halo3/tools, halo3_shaders_dk.nfsp) as
// the SDK renderer's EntradaShader, and the identification of the microcode the game's D3D puts in the ring.
//
// Halo 3's containers are the 2008 layout with no constant table, so what NFS reads from the table comes from
// elsewhere here: the samplers from the SPIR-V (the translator declares s<slot>_Textura with each fetch's
// dimension), all 256 float constants, vertex inputs at locations in element order (HALO3_RECOMP).
//
// Pixel shaders arrive as compiled and are matched exactly (fetch fields masked). Vertex shaders arrive patched by
// D3D (fetches reordered, outputs the pixel shader does not read nulled), so they are matched by the candidate of
// the same length with the most identical words, if that is clear enough.
//
// What the library does not have (or all of it, with no library) is translated on the console (halo3_traductor.h)
// once the translator is enabled: the library is the fast path, the translator the complete one.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include <rex/x360/shader_entry.h>

namespace halo3 {

struct EstadisticasShaders {
  uint64_t cargas = 0;
  uint64_t distintos = 0;
  uint64_t identificados = 0;
  uint64_t aproximados = 0;  // vertex shaders matched by score, not exactly
  uint64_t sin_identificar = 0;
  uint64_t traducidos = 0;  // of those not identified, translated on the console
  uint64_t fallidos = 0;    // and the ones the translator refused
};

class ShadersHalo {
 public:
  ShadersHalo();
  ~ShadersHalo();
  // false (with a log line) if the library is missing or damaged.
  bool Cargar(const std::filesystem::path& archivo);
  bool cargada() const;
  // Translates on the console what the library does not have (deko3d builds only; the renderer compiles the GLSL).
  void HabilitarTraductor();
  // Whether Identificar can answer at all: a library loaded or the translator enabled.
  bool activo() const;
  // Microcode in host byte order, as the ring carries it. The library's entry, or nullptr (then Traducida). Ring
  // thread only.
  const rex::x360::EntradaShader* Identificar(bool vertices, std::span<const uint32_t> microcodigo);
  // The translator's version, library or not (nullptr if it cannot), for a pair with `interpoladores` interpolators
  // (SQ_PROGRAM_CNTL): translated once per microcode and count. A library shader and a translated one read their
  // interpolators differently (by usage, by index), so a pair is drawn from the library only when both are in it.
  const rex::x360::EntradaShader* Traducida(bool vertices, std::span<const uint32_t> microcodigo,
                                            uint32_t interpoladores);
  static bool EsTraducida(const rex::x360::EntradaShader& e) { return !e.glsl.empty(); }
  EstadisticasShaders Estadisticas() const;
  size_t Cantidad() const;

 private:
  struct Datos;
  std::unique_ptr<Datos> datos_;
};

}  // namespace halo3
