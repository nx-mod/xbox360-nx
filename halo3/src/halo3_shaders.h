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
};

class ShadersHalo {
 public:
  ShadersHalo();
  ~ShadersHalo();
  // false (with a log line) if the library is missing or damaged.
  bool Cargar(const std::filesystem::path& archivo);
  bool cargada() const;
  // Microcode in host byte order, as the ring carries it. nullptr if not in the library. Ring thread only.
  const rex::x360::EntradaShader* Identificar(bool vertices, std::span<const uint32_t> microcodigo);
  EstadisticasShaders Estadisticas() const;
  size_t Cantidad() const;

 private:
  struct Datos;
  std::unique_ptr<Datos> datos_;
};

}  // namespace halo3
