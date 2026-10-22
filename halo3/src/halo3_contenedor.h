// halo3 - shader containers for XenosRecomp (the 2008 layout of shaders/XenosRecomp/shader.h), shared by the PC tool
// (halo3/tools/halo3_hlsl.cpp) and the run-time translator (halo3_traductor.cpp).
//
// Halo 3 ships its containers without a constant table (its offset is 0; Bungie keeps the names in its own tags).
// XenosRecomp needs the table to name the constants, the booleans and the samplers, so ConTablaSintetica appends one:
//
//  - g_c: every float register (256, both stages), so each constant is read by register number, relative addressing
//    included;
//  - g_b0..g_b15: the boolean registers of the shader's stage;
//  - no samplers: XenosRecomp (HALO3_RECOMP) declares the slots the texture fetches use, with each fetch's own
//    dimension, at binding = slot.
//
// The renderer fills the constants by register, as the Xbox 360 GPU sees them, so no names are needed at run time.
//
// ContenedorDeMicrocodigo wraps raw microcode, as the ring carries it, the way Xenia reads a shader: no vertex
// elements (every vertex fetch is a load from its buffer by r0.x, the vertex index), and the pair's interpolators by
// index (vertex shader export k is TEXCOORDk, which is pixel shader register k): as many as SQ_PROGRAM_CNTL says the
// vertex shader exports (vs_export_count + 1), at most 16.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace halo3::contenedor {

inline void PonerBE32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
  v[o] = uint8_t(x >> 24);
  v[o + 1] = uint8_t(x >> 16);
  v[o + 2] = uint8_t(x >> 8);
  v[o + 3] = uint8_t(x);
}

inline void PonerBE16(std::vector<uint8_t>& v, size_t o, uint16_t x) {
  v[o] = uint8_t(x >> 8);
  v[o + 1] = uint8_t(x);
}

inline uint32_t LeerBE32(const std::vector<uint8_t>& v, size_t o) {
  return uint32_t(v[o]) << 24 | uint32_t(v[o + 1]) << 16 | uint32_t(v[o + 2]) << 8 | v[o + 3];
}

// Appends a D3DXSHADER_CONSTANTTABLE (inside XenosRecomp's ConstantTableContainer) and points the header at it.
inline std::vector<uint8_t> ConTablaSintetica(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out = in;
  out.resize((out.size() + 15) & ~size_t(15), 0);
  const bool pixel = (LeerBE32(in, 0) & 1) == 0;

  struct Entrada {
    std::string nombre;
    uint16_t conjunto;  // RegisterSet: 0 bool, 1 int4, 2 float4, 3 sampler
    uint16_t indice;
    uint16_t cantidad;
    uint16_t clase;  // ParameterClass
    uint16_t tipo;   // ParameterType
    uint16_t filas, columnas, elementos;
  };
  std::vector<Entrada> entradas;
  entradas.push_back({"g_c", 2, 0, 256, 1, 3, 1, 4, 256});  // Halo 3's pixel shaders read up to c255 too
  for (uint16_t i = 0; i < 16; ++i) {
    entradas.push_back({"g_b" + std::to_string(i), 0, i, 1, 0, 1, 1, 1, 1});
  }

  // Layout, relative to the ConstantTable (constantTableData): the table (28 bytes), the ConstantInfo array
  // (20 bytes each), the TypeInfo array (16 bytes each), then the names.
  const size_t contenedor = out.size();  // ConstantTableContainer: size, then the ConstantTable
  const size_t tabla = contenedor + 4;
  const size_t infos = 28;
  const size_t tipos = infos + entradas.size() * 20;
  size_t nombres = tipos + entradas.size() * 16;
  size_t fin = nombres;
  for (const Entrada& e : entradas) {
    fin += e.nombre.size() + 1;
  }
  fin = (fin + 3) & ~size_t(3);
  out.resize(tabla + fin, 0);

  PonerBE32(out, contenedor, uint32_t(fin));
  PonerBE32(out, tabla + 0, 28);                                    // size
  PonerBE32(out, tabla + 8, pixel ? 0xFFFF0300u : 0xFFFE0300u);     // version (ps_3_0 / vs_3_0)
  PonerBE32(out, tabla + 12, uint32_t(entradas.size()));
  PonerBE32(out, tabla + 16, uint32_t(infos));
  PonerBE32(out, tabla + 24, 0);                                    // target
  for (size_t i = 0; i < entradas.size(); ++i) {
    const Entrada& e = entradas[i];
    const size_t info = tabla + infos + i * 20;
    const size_t tipo = tabla + tipos + i * 16;
    PonerBE32(out, info + 0, uint32_t(nombres));
    PonerBE16(out, info + 4, e.conjunto);
    PonerBE16(out, info + 6, e.indice);
    PonerBE16(out, info + 8, e.cantidad);
    PonerBE32(out, info + 12, uint32_t(tipos + i * 16));
    PonerBE16(out, tipo + 0, e.clase);
    PonerBE16(out, tipo + 2, e.tipo);
    PonerBE16(out, tipo + 4, e.filas);
    PonerBE16(out, tipo + 6, e.columnas);
    PonerBE16(out, tipo + 8, e.elementos);
    std::memcpy(out.data() + tabla + nombres, e.nombre.c_str(), e.nombre.size() + 1);
    nombres += e.nombre.size() + 1;
  }
  PonerBE32(out, 16, uint32_t(contenedor));  // ShaderContainer::constantTableOffset
  return out;
}

inline constexpr uint32_t kInterpoladores = 16;
inline constexpr uint32_t kUsoTexcoord = 5;  // DeclUsage::TexCoord

/*
 * A 2008 container around `microcodigo` (host byte order): the ShaderContainer header (36 bytes), then the shader
 * header at 36 (Shader, then VertexShader's field18 / element count / field20 or PixelShader's field18 / outputs),
 * then the interpolators, then the microcode as the physical part. `salidas` (pixel shaders): PixelShaderOutputs.
 * Each interpolator word is usageIndex | usage << 4 | reg << 8 (shader.h's Interpolator).
 */
inline std::vector<uint8_t> ContenedorDeMicrocodigo(bool vertices, std::span<const uint32_t> microcodigo,
                                                    uint32_t salidas, uint32_t interpoladores = kInterpoladores) {
  interpoladores = std::min(interpoladores, kInterpoladores);
  constexpr size_t kShader = 36;  // right after the ShaderContainer header
  // Shader (24 bytes), VertexShader's 3 words (PixelShader's 2), then the interpolators.
  const size_t virtuales = (kShader + 36 + kInterpoladores * 4 + 15) & ~size_t(15);
  const size_t fisicos = microcodigo.size() * 4;
  std::vector<uint8_t> v(virtuales + fisicos, 0);
  PonerBE32(v, 0, 0x102A1100u | (vertices ? 1u : 0u));  // flags: 2008 layout, bit 0 = vertex shader
  PonerBE32(v, 4, uint32_t(virtuales));
  PonerBE32(v, 8, uint32_t(fisicos));
  PonerBE32(v, 24, uint32_t(kShader));  // shaderOffset
  PonerBE32(v, kShader + 0, 0);                         // physicalOffset
  PonerBE32(v, kShader + 4, uint32_t(fisicos));         // size
  PonerBE32(v, kShader + 12, 0xFFu << 8);               // fieldC: no register gets the pixel position
  PonerBE32(v, kShader + 20, interpoladores << 5);      // interpolatorInfo: the count
  if (vertices) {
    PonerBE32(v, kShader + 24, 0);  // field18: no words before the elements
    PonerBE32(v, kShader + 28, 0);  // vertexElementCount
  } else {
    PonerBE32(v, kShader + 24, 0);  // field18
    PonerBE32(v, kShader + 28, salidas);
  }
  // VertexShader's interpolators follow field18 + elements (both 0) after field20; PixelShader's follow outputs.
  const size_t primero = vertices ? kShader + 36 : kShader + 32;
  for (uint32_t k = 0; k < interpoladores; ++k) {
    PonerBE32(v, primero + k * 4, k | (kUsoTexcoord << 4) | (k << 8));
  }
  for (size_t i = 0; i < microcodigo.size(); ++i) {
    PonerBE32(v, virtuales + i * 4, microcodigo[i]);
  }
  return ConTablaSintetica(v);
}

}  // namespace halo3::contenedor
