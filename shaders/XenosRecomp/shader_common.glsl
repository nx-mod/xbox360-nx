// shader_common.glsl - the GLSL version of shader_common.h for XenosRecomp's GLSL mode (XENOS_GLSL): microcode
// translated at run time on the console, compiled by uam. The translator's output keeps the HLSL type and function
// names; this prelude gives them their GLSL meaning, and defines __spirv__ and NFSMW_DEKO3D so the text takes its
// deko3d branches (the constant blocks as UBOs, one combined sampler per register). The renderer puts #version 460
// and G_ESPECIALIZACION (the pipeline's specialization bits) before it. Same meaning as the HLSL helpers.
#ifndef SHADER_COMMON_GLSL
#define SHADER_COMMON_GLSL

#define __spirv__ 1
#define NFSMW_DEKO3D 1

// ---- HLSL names
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4
#define bool4 bvec4
#define frac fract
#define rsqrt inversesqrt
#define lerp mix
#ifdef HALO3_VS
#define clip(x)  // a vertex cannot be discarded: kills in vertex shaders do nothing
#else
#define clip(x) if ((x) < 0.0) discard
#endif

float asfloat(uint x) { return uintBitsToFloat(x); }
vec2 asfloat(uvec2 x) { return uintBitsToFloat(x); }
vec3 asfloat(uvec3 x) { return uintBitsToFloat(x); }
vec4 asfloat(uvec4 x) { return uintBitsToFloat(x); }
float asfloat(int x) { return intBitsToFloat(x); }
vec4 asfloat(ivec4 x) { return intBitsToFloat(x); }
uint asuint(float x) { return floatBitsToUint(x); }
uvec2 asuint(vec2 x) { return floatBitsToUint(x); }
uvec3 asuint(vec3 x) { return floatBitsToUint(x); }
uvec4 asuint(vec4 x) { return floatBitsToUint(x); }
float rcp(float x) { return 1.0 / x; }
vec2 rcp(vec2 x) { return 1.0 / x; }
vec3 rcp(vec3 x) { return 1.0 / x; }
vec4 rcp(vec4 x) { return 1.0 / x; }
float saturate(float x) { return clamp(x, 0.0, 1.0); }
float saturate(bool x) { return x ? 1.0 : 0.0; }  // a saturated scalar comparison
vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }
float select(bool c, float a, float b) { return c ? a : b; }
vec4 select(bool c, vec4 a, float b) { return c ? a : vec4(b); }
vec4 select(bool c, vec4 a, vec4 b) { return c ? a : b; }
float f16tof32(uint x) { return unpackHalf2x16(x).x; }
vec2 f16tof32(uvec2 x) { return vec2(unpackHalf2x16(x.x).x, unpackHalf2x16(x.y).x); }

// ---- Per-component comparisons (HLSL's vector ==, >, >=, != give a mask; GLSL's give one bool)
#define XENO_COMPARAR(N, F)                                                                    \
  float N(float a, float b) { return F(vec4(a), vec4(b)).x ? 1.0 : 0.0; }                      \
  vec2 N(vec2 a, vec2 b) { return vec2(F(a, b)); }                                             \
  vec3 N(vec3 a, vec3 b) { return vec3(F(a, b)); }                                             \
  vec4 N(vec4 a, vec4 b) { return vec4(F(a, b)); }
XENO_COMPARAR(xeno_eq, equal)
XENO_COMPARAR(xeno_gt, greaterThan)
XENO_COMPARAR(xeno_ge, greaterThanEqual)
XENO_COMPARAR(xeno_ne, notEqual)
#define XENO_ALGUNO(N, F)                                                                      \
  bool N(float a, float b) { return F(vec4(a), vec4(b)).x; }                                   \
  bool N(vec2 a, vec2 b) { return any(F(a, b)); }                                              \
  bool N(vec3 a, vec3 b) { return any(F(a, b)); }                                              \
  bool N(vec4 a, vec4 b) { return any(F(a, b)); }
XENO_ALGUNO(xeno_any_eq, equal)
XENO_ALGUNO(xeno_any_gt, greaterThan)
XENO_ALGUNO(xeno_any_ge, greaterThanEqual)
XENO_ALGUNO(xeno_any_ne, notEqual)
// CNDE/CNDGE/CNDGT: per component, src0 against 0 picks src1 or src2.
#define XENO_CND(N, F)                                                                         \
  float N(float a, float b, float c) { return F(vec4(a), vec4(0.0)).x ? b : c; }               \
  vec2 N(vec2 a, vec2 b, vec2 c) { return mix(c, b, F(a, vec2(0.0))); }                        \
  vec3 N(vec3 a, vec3 b, vec3 c) { return mix(c, b, F(a, vec3(0.0))); }                        \
  vec4 N(vec4 a, vec4 b, vec4 c) { return mix(c, b, F(a, vec4(0.0))); }
XENO_CND(xeno_cnd_eq, equal)
XENO_CND(xeno_cnd_ge, greaterThanEqual)
XENO_CND(xeno_cnd_gt, greaterThan)

// ---- Specialization (the same bits as shader_common.h)
#define SPEC_CONSTANT_R11G11B10_NORMAL  (1u << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1u << 1)
#define SPEC_CONSTANT_INV_TAMANO_TEX    (1u << 9)
#define SPEC_CONSTANT_PCF_BARATO        (1u << 15)
#define SPEC_CONSTANT_SIN_DESENFOQUE    (1u << 19)
#define SPEC_CONSTANT_SOMBRA_MINIMO     (1u << 23)
#define SPEC_CONSTANT_ALPHA_FUNC_SHIFT  16u
#define SPEC_CONSTANT_ALPHA_FUNC_MASK   (7u << 16)
#ifndef G_ESPECIALIZACION
#define G_ESPECIALIZACION 0u
#endif
#define g_SpecConstants() uint(G_ESPECIALIZACION)
#define FLT_MIN uintBitsToFloat(0xff7fffffu)
#define FLT_MAX uintBitsToFloat(0x7f7fffffu)

// ---- Constant blocks: the renderer's UBOs 0-3 (vertex, pixel, shared, Halo 3's booleans and loop constants)
layout(std140, binding = 0) uniform NfsmwBloqueVs { vec4 v[256]; } g_UboVertex;
layout(std140, binding = 1) uniform NfsmwBloquePs { vec4 v[256]; } g_UboPixel;
layout(std140, binding = 2) uniform NfsmwBloqueCompartidas { vec4 v[48]; } g_UboCompartidas;
layout(std140, binding = 3) uniform Halo3BooleanBlock { uvec4 b[2]; uvec4 bucles[8]; } g_Halo3Booleans;
#define NFSMW_COMPARTIDA_UINT(B)  floatBitsToUint(g_UboCompartidas.v[(B) / 16][((B) % 16) / 4])
#define NFSMW_COMPARTIDA_FLOAT(B) g_UboCompartidas.v[(B) / 16][((B) % 16) / 4]
#define g_Booleans                 NFSMW_COMPARTIDA_UINT(256)
#define g_SwappedTexcoords         NFSMW_COMPARTIDA_UINT(260)
#define g_HalfPixelOffset          vec2(NFSMW_COMPARTIDA_FLOAT(264), NFSMW_COMPARTIDA_FLOAT(268))
#define g_AlphaThreshold           NFSMW_COMPARTIDA_FLOAT(272)
#define g_NdcScale                 vec2(NFSMW_COMPARTIDA_FLOAT(280), NFSMW_COMPARTIDA_FLOAT(284))
#define g_NdcOffset                vec2(NFSMW_COMPARTIDA_FLOAT(288), NFSMW_COMPARTIDA_FLOAT(292))
#define g_InputRemap(LOC)          NFSMW_COMPARTIDA_UINT(296 + (LOC) * 4)
// The guest vertex index (r0.x): the draw rebases its indices by the first vertex, which the renderer puts at 488.
#define XENO_INDICE_VERTICE        (gl_VertexID + int(NFSMW_COMPARTIDA_UINT(488)))
#define g_EscalaPixel              NFSMW_COMPARTIDA_FLOAT(640)  // host pixels per guest pixel (2 on a 4x MSAA target)
#define halo_bool(A) ((g_Halo3Booleans.b[((A) >> 7) & 1][((A) >> 5) & 3] >> ((A) & 31)) & 1u)
ivec4 halo_loop(uint n) {
  uint v = g_Halo3Booleans.bucles[n >> 2][n & 3u];
  return ivec4(int(v & 0xFFu), int((v >> 8) & 0xFFu), int(v << 8) >> 24, 0);
}

#ifdef HALO3_VS
// ---- Memory export (Xenia's ExportToMemory): eA is the stream constant with the element index in y's mantissa
// (x: base dword address and 01 on top; z: endian, format, number type, red/blue swap; w: element count), eM0-4 are
// consecutive elements. Each alloc and each exit store what was written since: every dword goes to the draw's region of
// the record buffer as (guest byte address, value in guest byte order), and the renderer writes them to guest memory
// once the GPU is done, inside the streams it found in the draw's constants. Halo 3's particles, light visibility tests
// and water. Formats of 32 bits and more per element (8/16-bit ones would need read-modify-write).
layout(std430, binding = 15) buffer XenoExportacion { uint d[]; } g_exportacion;
#define XENO_EXPORT_BASE NFSMW_COMPARTIDA_UINT(644)
#define XENO_EXPORT_CAPACIDAD NFSMW_COMPARTIDA_UINT(648)
// A float to a field of `bits` bits as the number type says (bit 0 signed, bit 1 integer).
uint xeno_export_fijo(float v, uint bits, uint numero) {
  float m = float((1u << ((numero & 1u) != 0u ? bits - 1u : bits)) - 1u);
  if (isnan(v)) v = 0.0;
  if ((numero & 2u) == 0u) v *= m;
  v = (numero & 1u) != 0u ? clamp(v, -m, m) : clamp(v, 0.0, m);
  return uint(int(v < 0.0 ? v - 0.5 : v + 0.5)) & ((1u << bits) - 1u);
}
uint xeno_export_cambio(uint v, uint modo) {
  if (modo == 1u) return ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
  if (modo == 2u) return (v << 24) | ((v & 0xFF00u) << 8) | ((v >> 8) & 0xFF00u) | (v >> 24);
  if (modo == 3u) return (v << 16) | (v >> 16);
  return v;
}
void xeno_export_dword(uint direccion, uint valor) {
  uint i = atomicAdd(g_exportacion.d[XENO_EXPORT_BASE], 1u);
  if (i < XENO_EXPORT_CAPACIDAD) {
    g_exportacion.d[XENO_EXPORT_BASE + 2u + i * 2u] = direccion;
    g_exportacion.d[XENO_EXPORT_BASE + 3u + i * 2u] = valor;
  }
}
void xeno_export_elemento(vec4 v, uint direccion, uint formato, uint numero, uint orden) {
  uvec4 p = uvec4(0u);
  uint n = 1u;
  if (formato == 6u) {
    p.x = xeno_export_fijo(v.x, 8u, numero) | (xeno_export_fijo(v.y, 8u, numero) << 8) |
          (xeno_export_fijo(v.z, 8u, numero) << 16) | (xeno_export_fijo(v.w, 8u, numero) << 24);
  } else if (formato == 7u) {
    p.x = xeno_export_fijo(v.x, 10u, numero) | (xeno_export_fijo(v.y, 10u, numero) << 10) |
          (xeno_export_fijo(v.z, 10u, numero) << 20) | (xeno_export_fijo(v.w, 2u, numero) << 30);
  } else if (formato == 25u) {
    p.x = xeno_export_fijo(v.x, 16u, numero) | (xeno_export_fijo(v.y, 16u, numero) << 16);
  } else if (formato == 26u) {
    p.x = xeno_export_fijo(v.x, 16u, numero) | (xeno_export_fijo(v.y, 16u, numero) << 16);
    p.y = xeno_export_fijo(v.z, 16u, numero) | (xeno_export_fijo(v.w, 16u, numero) << 16);
    n = 2u;
  } else if (formato == 31u) {
    p.x = packHalf2x16(v.xy);
  } else if (formato == 32u) {
    p.xy = uvec2(packHalf2x16(v.xy), packHalf2x16(v.zw));
    n = 2u;
  } else if (formato == 36u) {
    p.x = floatBitsToUint(v.x);
  } else if (formato == 37u) {
    p.xy = floatBitsToUint(v.xy);
    n = 2u;
  } else if (formato == 38u) {
    p = floatBitsToUint(v);
    n = 4u;
  } else {
    return;
  }
  if (orden <= 3u) {
    p = uvec4(xeno_export_cambio(p.x, orden), xeno_export_cambio(p.y, orden), xeno_export_cambio(p.z, orden),
              xeno_export_cambio(p.w, orden));
  } else {
    p = uvec4(xeno_export_cambio(p.x, 2u), xeno_export_cambio(p.y, 2u), xeno_export_cambio(p.z, 2u),
              xeno_export_cambio(p.w, 2u));
    p = orden == 4u ? p.yxwz : p.wzyx;
  }
  xeno_export_dword(direccion, p.x);
  if (n > 1u) xeno_export_dword(direccion + 4u, p.y);
  if (n > 2u) {
    xeno_export_dword(direccion + 8u, p.z);
    xeno_export_dword(direccion + 12u, p.w);
  }
}
uint xeno_export_bytes(uint formato) {
  if (formato == 38u) return 16u;
  if (formato == 26u || formato == 32u || formato == 37u) return 8u;
  return 4u;
}
void xeno_memexport(vec4 eA, vec4 m0, vec4 m1, vec4 m2, vec4 m3, vec4 m4, uint escritos) {
  if (escritos == 0u || XENO_EXPORT_CAPACIDAD == 0u) return;  // no region bound: the buffer is not touched
  uvec4 a = floatBitsToUint(eA);
  if ((a.x >> 30) != 1u || (a.y >> 23) != 0x96u || (a.z >> 23) != 0x96u || (a.w >> 23) != 0x96u) return;
  uint base = a.x << 2, indice = a.y & 0x7FFFFFu, cuenta = a.w & 0x7FFFFFu;
  uint orden = a.z & 7u, formato = (a.z >> 8) & 63u, numero = (a.z >> 16) & 7u;
  bool rojo_azul = (a.z & (1u << 19)) != 0u;
  uint bytes = xeno_export_bytes(formato);
  // One call per element, no array (a local array indexed at run time goes to scratch memory).
  if ((escritos & 1u) != 0u && indice < cuenta)
    xeno_export_elemento(rojo_azul ? m0.zyxw : m0, base + indice * bytes, formato, numero, orden);
  if ((escritos & 2u) != 0u && indice + 1u < cuenta)
    xeno_export_elemento(rojo_azul ? m1.zyxw : m1, base + (indice + 1u) * bytes, formato, numero, orden);
  if ((escritos & 4u) != 0u && indice + 2u < cuenta)
    xeno_export_elemento(rojo_azul ? m2.zyxw : m2, base + (indice + 2u) * bytes, formato, numero, orden);
  if ((escritos & 8u) != 0u && indice + 3u < cuenta)
    xeno_export_elemento(rojo_azul ? m3.zyxw : m3, base + (indice + 3u) * bytes, formato, numero, orden);
  if ((escritos & 16u) != 0u && indice + 4u < cuenta)
    xeno_export_elemento(rojo_azul ? m4.zyxw : m4, base + (indice + 4u) * bytes, formato, numero, orden);
}
#endif

// ---- Texture fetches: combined samplers; the HLSL "sampler" argument is a dummy int. Vertex shaders sample level 0.
#ifdef HALO3_VS
#define XENO_MUESTRA(t, c) textureLod(t, c, 0.0)
#else
#define XENO_MUESTRA(t, c) texture(t, c)
#endif
// The fetch constant's sign modes (signed, biased) on what the host texture returned unsigned: the renderer's word per
// slot at bytes 512 + 4 * slot (kPalabraSignos). Signed reads the stored 8- or 16-bit value as two's complement.
float xeno_signo(float v, uint modo, uint ancho) {
  if (modo == 2u) return v * 2.0 - 1.0;
  if (modo == 1u && ancho != 0u) {
    float m = ancho == 2u ? 65535.0 : 255.0, h = ancho == 2u ? 32767.0 : 127.0;
    float x = round(clamp(v, 0.0, 1.0) * m);
    return max((x > h ? x - m - 1.0 : x) / h, -1.0);
  }
  return v;
}
vec4 xeno_signos(vec4 v, int s) {
  uint w = NFSMW_COMPARTIDA_UINT(512 + (s & 31) * 4);
  if ((w & 0xFFu) == 0u) return v;
  uint a = (w >> 8) & 3u;
  return vec4(xeno_signo(v.x, w & 3u, a), xeno_signo(v.y, (w >> 2) & 3u, a), xeno_signo(v.z, (w >> 4) & 3u, a),
              xeno_signo(v.w, (w >> 6) & 3u, a));
}
// Unnormalized (texel) coordinates to the 0..1 range the samplers take.
float xeno_denorm1D(sampler2D t, float u) { return u / float(textureSize(t, 0).x); }
vec2 xeno_denorm2D(sampler2D t, vec2 uv) { return uv / vec2(textureSize(t, 0)); }
vec3 xeno_denorm3D(sampler3D t, vec3 uvw) { return uvw / vec3(textureSize(t, 0)); }
vec4 tfetch2D(sampler2D t, int s, vec2 uv, vec2 offset, vec2 invSize) {
  vec2 d = (g_SpecConstants() & SPEC_CONSTANT_INV_TAMANO_TEX) != 0u ? offset * invSize : offset / vec2(textureSize(t, 0));
  return xeno_signos(XENO_MUESTRA(t, uv + d), s);
}
vec4 getWeights2D(sampler2D t, int s, vec2 uv, vec2 offset, vec2 invSize) {
  vec2 f = fract(uv * vec2(textureSize(t, 0)) + offset - 0.5);
  return vec4(isnan(uv.x) ? 0.0 : f.x, isnan(uv.y) ? 0.0 : f.y, 0.0, 0.0);
}
vec4 tfetch1D(sampler2D t, int s, float u) { return xeno_signos(XENO_MUESTRA(t, vec2(u, 0.5)), s); }
vec4 getWeights1D(sampler2D t, int s, float u) {
  return vec4(isnan(u) ? 0.0 : fract(u * float(textureSize(t, 0).x) - 0.5), 0.0, 0.0, 0.0);
}
vec4 tfetch3D(sampler3D t, int s, vec3 uvw) { return xeno_signos(XENO_MUESTRA(t, uvw), s); }

// The directions of the last two cube instructions, as two variables: an array indexed at run time went to local
// memory (scratch) on Maxwell, and the shaders with reflections (Halo 3's characters) spilled.
struct CubeMapData {
  vec3 cubeMapDirection0;
  vec3 cubeMapDirection1;
  uint cubeMapIndex;
};
CubeMapData xeno_cubo_vacio() { return CubeMapData(vec3(0.0), vec3(0.0), 0u); }
vec3 xeno_cubo_direccion(float indice, CubeMapData c) {
  return (int(indice) & 1) == 0 ? c.cubeMapDirection0 : c.cubeMapDirection1;
}
vec4 tfetchCube(samplerCube t, int s, vec3 uvw, inout CubeMapData c) {
  return xeno_signos(XENO_MUESTRA(t, xeno_cubo_direccion(uvw.z, c)), s);
}
// The bilinear weights on the face the direction lands on (getWeights on a cube map).
vec4 getWeightsCube(samplerCube t, int s, vec3 uvw, inout CubeMapData c) {
  vec3 d = xeno_cubo_direccion(uvw.z, c), a = abs(d);
  vec2 uv = a.x >= a.y && a.x >= a.z ? d.zy / a.x : a.y >= a.z ? d.xz / a.y : d.xy / max(a.z, 1e-20);
  vec2 f = fract((uv * 0.5 + 0.5) * float(textureSize(t, 0).x) - 0.5);
  return vec4(f, 0.0, 0.0);
}
vec4 cube(vec4 value, inout CubeMapData c) {
  uint index = c.cubeMapIndex;
  if ((index & 1u) == 0u) {
    c.cubeMapDirection0 = value.xyz;
  } else {
    c.cubeMapDirection1 = value.xyz;
  }
  ++c.cubeMapIndex;
  return vec4(0.0, 0.0, 0.0, float(index));
}

vec4 tfetchTexcoord(uint swappedTexcoords, vec4 value, uint semanticIndex) {
  return (swappedTexcoords & (1u << semanticIndex)) != 0u ? value.yxwz : value;
}
// 3 bits per component: 0-3 = data component, 4 = 0, 5 = 1, 7 = unchanged.
vec4 remapInput(vec4 value, uint code) {
  if (code == 0xFFFu) return value;
  vec4 r = value;
  for (uint i = 0u; i < 4u; i++) {
    uint source = (code >> (i * 3u)) & 7u;
    if (source < 4u) r[i] = value[source];
    else if (source == 4u) r[i] = 0.0;
    else if (source == 5u) r[i] = 1.0;
  }
  return r;
}
float alphaTestValue(float alpha) {
  bool pass = true;
  switch ((g_SpecConstants() & SPEC_CONSTANT_ALPHA_FUNC_MASK) >> SPEC_CONSTANT_ALPHA_FUNC_SHIFT) {
    case 0u: pass = false; break;
    case 1u: pass = alpha < g_AlphaThreshold; break;
    case 2u: pass = alpha == g_AlphaThreshold; break;
    case 3u: pass = alpha <= g_AlphaThreshold; break;
    case 4u: pass = alpha > g_AlphaThreshold; break;
    case 5u: pass = alpha != g_AlphaThreshold; break;
    case 6u: pass = alpha >= g_AlphaThreshold; break;
    default: break;
  }
  return pass ? 1.0 : -1.0;
}
vec4 dst(vec4 a, vec4 b) { return vec4(1.0, a.y * b.y, a.z, b.w); }
vec4 max4(vec4 a) { return vec4(max(max(a.x, a.y), max(a.z, a.w))); }

// ---- Vertex fetches with no element (halo_vfetch_<slot>, declared per shader): decode one element from its words,
// first component in the low bits (the renderer byte-swaps as for vertex data).
int xeno_ext(uint w, uint pos, uint bits, bool sgn) {
  uint v = (w >> pos) & ((1u << bits) - 1u);
  return sgn ? (int(v << (32u - bits)) >> (32u - bits)) : int(v);
}
// sgn: bit 0 signed, bit 1 the "no zero" signed fraction mode ((v + 0.5) / (2^(n-1) - 0.5), as Xenia).
float xeno_comp(uint w, uint pos, uint bits, uint sgn, uint entero) {
  int v = xeno_ext(w, pos, bits, (sgn & 1u) != 0u);
  if (entero != 0u) return float(v);
  if ((sgn & 1u) == 0u) return float(v) / float((1u << bits) - 1u);
  float m = float((1u << (bits - 1u)) - 1u);
  return (sgn & 2u) != 0u ? (float(v) + 0.5) / (m + 0.5) : max(float(v) / m, -1.0);
}
// The 32-bit integer formats: as above, normalized unless integer.
vec4 xeno_32(uvec4 w, uint sgn, uint entero) {
  vec4 v = (sgn & 1u) != 0u ? vec4(ivec4(w)) : vec4(w);
  if (entero != 0u) return v;
  if ((sgn & 1u) == 0u) return v / 4294967295.0;
  return (sgn & 2u) != 0u ? (v + 0.5) / 2147483647.5 : max(v / 2147483647.0, vec4(-1.0));
}
vec4 halo_vf_decodificar(uint w0, uint w1, uint w2, uint w3, uint f, uint sg, uint en, int ex) {
  vec4 r = vec4(0.0, 0.0, 0.0, 1.0);
  switch (f) {
    case 6u: r = vec4(xeno_comp(w0, 0u, 8u, sg, en), xeno_comp(w0, 8u, 8u, sg, en), xeno_comp(w0, 16u, 8u, sg, en),
                      xeno_comp(w0, 24u, 8u, sg, en)); break;  // k_8_8_8_8
    case 7u: r = vec4(xeno_comp(w0, 0u, 10u, sg, en), xeno_comp(w0, 10u, 10u, sg, en), xeno_comp(w0, 20u, 10u, sg, en),
                      xeno_comp(w0, 30u, 2u, sg, en)); break;  // k_2_10_10_10
    case 16u: r.xyz = vec3(xeno_comp(w0, 0u, 11u, sg, en), xeno_comp(w0, 11u, 11u, sg, en),
                           xeno_comp(w0, 22u, 10u, sg, en)); break;  // k_10_11_11
    case 17u: r.xyz = vec3(xeno_comp(w0, 0u, 10u, sg, en), xeno_comp(w0, 10u, 11u, sg, en),
                           xeno_comp(w0, 21u, 11u, sg, en)); break;  // k_11_11_10
    case 25u: r.xy = vec2(xeno_comp(w0, 0u, 16u, sg, en), xeno_comp(w0, 16u, 16u, sg, en)); break;  // k_16_16
    case 26u: r = vec4(xeno_comp(w0, 0u, 16u, sg, en), xeno_comp(w0, 16u, 16u, sg, en), xeno_comp(w1, 0u, 16u, sg, en),
                       xeno_comp(w1, 16u, 16u, sg, en)); break;  // k_16_16_16_16
    case 31u: r.xy = unpackHalf2x16(w0); break;  // k_16_16_FLOAT
    case 32u: r = vec4(unpackHalf2x16(w0), unpackHalf2x16(w1)); break;  // k_16_16_16_16_FLOAT
    case 33u: r.x = xeno_32(uvec4(w0, 0u, 0u, 0u), sg, en).x; break;  // k_32
    case 34u: r.xy = xeno_32(uvec4(w0, w1, 0u, 0u), sg, en).xy; break;  // k_32_32
    case 35u: r = xeno_32(uvec4(w0, w1, w2, w3), sg, en); break;  // k_32_32_32_32
    case 36u: r.x = uintBitsToFloat(w0); break;  // k_32_FLOAT
    case 37u: r.xy = uintBitsToFloat(uvec2(w0, w1)); break;  // k_32_32_FLOAT
    case 38u: r = uintBitsToFloat(uvec4(w0, w1, w2, w3)); break;  // k_32_32_32_32_FLOAT
    case 57u: r.xyz = uintBitsToFloat(uvec3(w0, w1, w2)); break;  // k_32_32_32_FLOAT
    default: break;
  }
  return ex != 0 ? r * exp2(float(ex)) : r;
}

#endif
