// Halo 3 shader common header: NFS's shared helpers, plus what Halo's shaders need and NFS's never did.
// Passed to halo3_hlsl as the <shader_common.h> argument, and inlined at the top of every translated shader.
//
// Compile Halo's HLSL with the shaders/XenosRecomp directory on the include path (for shader_common.h), and with
// -DHALO3_RECOMP so XenosRecomp emits the Halo forms below.
#include "shader_common.h"

// Halo 3 uses the full Xbox 360 boolean register space (addresses 0-255), far past the 16 bits per stage NFS packs
// into g_Booleans. They live in their own 256-bit block, read by constant index A (the boolean address is always a
// constant in the control flow, so uam sees no dynamic component index). The renderer fills this block per draw
// from the Xbox 360 boolean constants (SQ_... in the register file).
#ifdef NFSMW_DEKO3D
// The 32 loop constants (count, start, step in bytes 0-2) follow the booleans in the same block.
struct Halo3BooleanBlock { uint4 b[2]; uint4 bucles[8]; };
[[vk::binding(3, 4)]] ConstantBuffer<Halo3BooleanBlock> g_Halo3Booleans;
#define halo_bool(A) ((g_Halo3Booleans.b[((A) >> 7) & 1][((A) >> 5) & 3] >> ((A) & 31)) & 1u)
// The guest vertex index (r0.x): the draw rebases its indices by the first vertex, which the renderer puts at 488.
#define XENO_INDICE_VERTICE (int(iVertexId) + int(NFSMW_COMPARTIDA_UINT(488)))
int4 halo_loop(uint n) {
  uint v = g_Halo3Booleans.bucles[n >> 2][n & 3];
  return int4(int(v & 0xFFu), int((v >> 8) & 0xFFu), int(v << 8) >> 24, 0);
}
// getWeights2D with the 1/size argument the translator adds to every 2D fetch, as a float4 (the swizzles reach z, w).
float4 getWeights2D(Texture2D<float4> texture, SamplerState samplerState, float2 texCoord, float2 offset, float2 invSize) {
  return float4(getWeights2D(texture, samplerState, texCoord, offset), 0.0, 0.0);
}
// 1D fetches read a 2D texture of height 1.
float4 tfetch1D(Texture2D<float4> texture, SamplerState samplerState, float texCoord) {
  return NFSMW_SAMPLE(texture, samplerState, float2(texCoord, 0.5));
}
float4 getWeights1D(Texture2D<float4> texture, SamplerState samplerState, float texCoord) {
  uint w, h;
  texture.GetDimensions(w, h);
  return float4(isnan(texCoord) ? 0.0 : frac(texCoord * float(w) - 0.5), 0.0, 0.0, 0.0);
}
#else
// Non-deko (PC/DXC reference) path: the booleans come from the shared push-constant buffer at byte 512.
#define halo_bool(A) ((vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 512 + (((A) >> 5) & 7) * 4) >> ((A) & 31)) & 1u)
#define halo_loop(N) int4(0, 0, 0, 0)
#define XENO_INDICE_VERTICE int(iVertexId)
#endif

// Vertex fetches that no vertex element declares (pixel shaders, extra streams): a load from the fetch constant's
// buffer, which the renderer copies byte-swapped as for vertex data (so each dword is in host order, first component
// in the low bits). dword = index * stride + offset; format = the Xenos vertex format number; sgn: signed;
// entero: integer (0 = normalized fraction); exp: power of two applied after (exp_adjust).
float halo_snorm(int v, int bits) { return max(float(v) / float((1 << (bits - 1)) - 1), -1.0); }
int halo_ext(uint w, uint pos, uint bits, bool sgn) {
  uint v = (w >> pos) & ((1u << bits) - 1u);
  return sgn ? (int(v << (32u - bits)) >> (32u - bits)) : int(v);
}
float halo_comp(uint w, uint pos, uint bits, uint sgn, uint entero) {
  int v = halo_ext(w, pos, bits, sgn != 0);
  if (entero != 0) return float(v);
  return sgn != 0 ? halo_snorm(v, bits) : float(v) / float((1u << bits) - 1u);
}
float4 halo_vfetch(ByteAddressBuffer b, int dword, uint format, uint sgn, uint entero, int expo) {
  uint a = uint(max(dword, 0)) * 4u;
  float4 r = float4(0.0, 0.0, 0.0, 1.0);
  switch (format) {
    case 6: {  // k_8_8_8_8
      uint w = b.Load(a);
      r = float4(halo_comp(w, 0, 8, sgn, entero), halo_comp(w, 8, 8, sgn, entero), halo_comp(w, 16, 8, sgn, entero),
                 halo_comp(w, 24, 8, sgn, entero));
      break;
    }
    case 7: {  // k_2_10_10_10
      uint w = b.Load(a);
      r = float4(halo_comp(w, 0, 10, sgn, entero), halo_comp(w, 10, 10, sgn, entero), halo_comp(w, 20, 10, sgn, entero),
                 halo_comp(w, 30, 2, sgn, entero));
      break;
    }
    case 16: {  // k_10_11_11
      uint w = b.Load(a);
      r.xyz = float3(halo_comp(w, 0, 11, sgn, entero), halo_comp(w, 11, 11, sgn, entero), halo_comp(w, 22, 10, sgn, entero));
      break;
    }
    case 17: {  // k_11_11_10
      uint w = b.Load(a);
      r.xyz = float3(halo_comp(w, 0, 10, sgn, entero), halo_comp(w, 10, 11, sgn, entero), halo_comp(w, 21, 11, sgn, entero));
      break;
    }
    case 25: {  // k_16_16
      uint w = b.Load(a);
      r.xy = float2(halo_comp(w, 0, 16, sgn, entero), halo_comp(w, 16, 16, sgn, entero));
      break;
    }
    case 26: {  // k_16_16_16_16
      uint2 w = b.Load2(a);
      r = float4(halo_comp(w.x, 0, 16, sgn, entero), halo_comp(w.x, 16, 16, sgn, entero), halo_comp(w.y, 0, 16, sgn, entero),
                 halo_comp(w.y, 16, 16, sgn, entero));
      break;
    }
    case 31: {  // k_16_16_FLOAT
      uint w = b.Load(a);
      r.xy = f16tof32(uint2(w & 0xFFFFu, w >> 16));
      break;
    }
    case 32: {  // k_16_16_16_16_FLOAT
      uint2 w = b.Load2(a);
      r = float4(f16tof32(uint2(w.x & 0xFFFFu, w.x >> 16)), f16tof32(uint2(w.y & 0xFFFFu, w.y >> 16)));
      break;
    }
    case 33: r.x = sgn != 0 ? float(int(b.Load(a))) : float(b.Load(a)); break;  // k_32
    case 34: { uint2 w = b.Load2(a); r.xy = sgn != 0 ? float2(int2(w)) : float2(w); break; }  // k_32_32
    case 35: { uint4 w = b.Load4(a); r = sgn != 0 ? float4(int4(w)) : float4(w); break; }  // k_32_32_32_32
    case 36: r.x = asfloat(b.Load(a)); break;  // k_32_FLOAT
    case 37: r.xy = asfloat(b.Load2(a)); break;  // k_32_32_FLOAT
    case 38: r = asfloat(b.Load4(a)); break;  // k_32_32_32_32_FLOAT
    case 57: r.xyz = asfloat(b.Load3(a)); break;  // k_32_32_32_FLOAT
    default: break;
  }
  return expo != 0 ? r * exp2(float(expo)) : r;
}
