// halo3 - the shader library on the console (see halo3_shaders.h).

#include "halo3_shaders.h"

#include <rex/logging.h>

#include "../../nfsmw_pal/src/nfsmw_shader_library.h"  // the library file format (NFS's; to move into the SDK)
#if HALO3_DEKO3D
#include "halo3_traductor.h"
#endif

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#if REX_PLATFORM_SWITCH
extern "C" const char* RexSwitchLogDir(void);  // the rexglue log folder, ending in '/'
#endif

namespace halo3 {
namespace {

using rex::x360::ElementoVertice;
using rex::x360::EntradaShader;
using rex::x360::SamplerShader;

// Bits of a vertex fetch instruction that D3D does not patch: opcode, registers, destination swizzle and predicate
// (the same as NFS's, nfsmw_nativo_shaders.cpp).
constexpr uint32_t kFetchConserva[3] = {0x0007FFFF, 0x80000FFF, 0x80000000};
// A vertex shader matched by score needs this share of identical words, and to beat the next candidate.
constexpr double kParecidoMinimo = 0.90;
constexpr uint32_t kMaxAvisos = 24;

uint32_t BE(const std::vector<uint8_t>& o, size_t p) {
  return uint32_t(o[p]) << 24 | uint32_t(o[p + 1]) << 16 | uint32_t(o[p + 2]) << 8 | o[p + 3];
}

// The samplers the translator declared: variables named s<slot>_Textura, with the dimension of their image type.
// D3DXPARAMETER_TYPE as NFS's table gives it: 12 Sampler2D, 13 Sampler3D, 14 SamplerCube (1D reads a 2D texture).
std::vector<SamplerShader> SamplersDelSpirv(std::span<const uint32_t> spv) {
  std::unordered_map<uint32_t, std::string> nombres;
  std::unordered_map<uint32_t, uint32_t> dimension_imagen;  // OpTypeImage id -> Dim
  std::unordered_map<uint32_t, uint32_t> puntero;           // OpTypePointer id -> pointee
  std::unordered_map<uint32_t, uint32_t> variable;          // OpVariable id -> pointer type
  for (size_t i = 5; i < spv.size();) {
    const uint32_t palabras = spv[i] >> 16, op = spv[i] & 0xFFFF;
    if (!palabras || i + palabras > spv.size()) break;
    if (op == 5 && palabras >= 3) {  // OpName
      nombres[spv[i + 1]] = reinterpret_cast<const char*>(&spv[i + 2]);
    } else if (op == 25 && palabras >= 4) {  // OpTypeImage: result, sampled type, Dim
      dimension_imagen[spv[i + 1]] = spv[i + 3];
    } else if (op == 32 && palabras >= 4) {  // OpTypePointer: result, storage class, type
      puntero[spv[i + 1]] = spv[i + 3];
    } else if (op == 59 && palabras >= 4) {  // OpVariable: result type, result
      variable[spv[i + 2]] = spv[i + 1];
    }
    i += palabras;
  }
  std::map<uint32_t, uint16_t> por_slot;
  for (const auto& [id, nombre] : nombres) {
    unsigned slot = 0;
    char resto[16] = {};
    if (std::sscanf(nombre.c_str(), "s%u_%15s", &slot, resto) != 2 || std::string(resto) != "Textura" || slot > 31) {
      continue;
    }
    const auto v = variable.find(id);
    if (v == variable.end()) continue;
    const auto p = puntero.find(v->second);
    const auto d = dimension_imagen.find(p == puntero.end() ? v->second : p->second);
    const uint32_t dim = d == dimension_imagen.end() ? 1 : d->second;  // Dim: 0 1D, 1 2D, 2 3D, 3 Cube
    por_slot[slot] = dim == 2 ? 13 : dim == 3 ? 14 : 12;
  }
  std::vector<SamplerShader> r;
  for (const auto& [slot, tipo] : por_slot) {
    SamplerShader s;
    s.registro = uint16_t(slot);
    s.tipo = tipo;
    r.push_back(s);
  }
  return r;
}

// The 2008 container: header (signature, virtual size, physical size, ?, constant table, definitions, shader),
// then the shader header (XenosRecomp shader.h): physicalOffset, size, ..., and for vertex shaders field18, element
// count, field20 and the elements; for pixel shaders field18 and the outputs. Returns the reason if unreadable.
const char* Leer(const nfsmw::native::Shader& s, EntradaShader& e) {
  const auto& o = s.original;
  if (o.size() < 28) return "contenedor corto";
  const uint32_t virtuales = BE(o, 4), fisicos = BE(o, 8), cabecera = BE(o, 24);
  if (size_t(virtuales) + fisicos > o.size() || size_t(cabecera) + 36 > virtuales) return "cabecera fuera";
  const uint32_t desplazamiento = BE(o, cabecera), bytes = BE(o, cabecera + 4);
  if (!bytes || (bytes % 4) || size_t(desplazamiento) + bytes > fisicos) return "microcodigo fuera";
  e.vertices = s.vertices;
  e.microcodigo.resize(bytes / 4);
  for (size_t i = 0; i < e.microcodigo.size(); ++i) {
    e.microcodigo[i] = BE(o, size_t(virtuales) + desplazamiento + i * 4);
  }
  if (e.vertices) {
    const uint32_t previos = BE(o, cabecera + 24), cantidad = BE(o, cabecera + 28);
    const size_t comienzo = size_t(cabecera) + 36 + size_t(previos) * 4;
    if (cantidad > 64 || previos > 1024 || comienzo + size_t(cantidad) * 4 > virtuales) return "elementos fuera";
    for (uint32_t i = 0; i < cantidad; ++i) {
      const uint32_t valor = BE(o, comienzo + size_t(i) * 4);
      ElementoVertice el;
      el.instruccion = uint16_t(valor & 0xFFF);
      el.uso = uint8_t((valor >> 12) & 0xF);
      el.indice_uso = uint8_t((valor >> 16) & 0xF);
      if ((size_t(el.instruccion) + 1) * 3 > e.microcodigo.size()) return "elemento fuera del microcodigo";
      e.elementos.push_back(el);
      for (size_t j = 0; j < 3; ++j) {
        e.microcodigo[size_t(el.instruccion) * 3 + j] &= kFetchConserva[j];
      }
    }
    e.ubicaciones_por_orden = true;
  } else {
    e.salidas = BE(o, cabecera + 28);
    e.samplers = SamplersDelSpirv(s.spirv);
  }
  e.constantes_bytes = 256 * 16;
  e.huella = XXH3_64bits(e.microcodigo.data(), e.microcodigo.size() * sizeof(uint32_t));
  e.huella_contenedor = XXH3_64bits(o.data(), o.size());
  return nullptr;
}

uint64_t Clave(bool vertices, std::span<const uint32_t> microcodigo) {
  return XXH3_64bits(microcodigo.data(), microcodigo.size_bytes()) ^ (uint64_t(vertices) << 63) ^ microcodigo.size();
}

}  // namespace

struct ShadersHalo::Datos {
  nfsmw::native::BibliotecaShaders biblioteca;
  std::vector<EntradaShader> entradas;
  std::deque<EntradaShader> traducidas;  // translated on the console (a deque: the renderer keeps the pointers)
  std::map<std::pair<bool, uint32_t>, std::vector<uint32_t>> candidatos;  // (vertex, words) -> entries
  std::unordered_map<uint64_t, const EntradaShader*> cache;              // raw microcode -> answer
  std::unordered_map<uint64_t, const EntradaShader*> cache_traducidas;   // raw microcode -> translation (or nullptr)
  std::vector<uint32_t> temporal;
  EstadisticasShaders estadisticas;
  uint32_t avisos = 0;
  bool cargada = false;
  bool traductor = false;
};

ShadersHalo::ShadersHalo() : datos_(std::make_unique<Datos>()) {}
ShadersHalo::~ShadersHalo() = default;
bool ShadersHalo::cargada() const { return datos_->cargada; }
bool ShadersHalo::activo() const { return datos_->cargada || datos_->traductor; }
void ShadersHalo::HabilitarTraductor() {
#if HALO3_DEKO3D
  datos_->traductor = true;
#endif
}
size_t ShadersHalo::Cantidad() const { return datos_->entradas.size(); }
EstadisticasShaders ShadersHalo::Estadisticas() const { return datos_->estadisticas; }

bool ShadersHalo::Cargar(const std::filesystem::path& archivo) {
  Datos& d = *datos_;
  try {
    d.biblioteca.Cargar(archivo);
  } catch (const std::exception& error) {
    REXLOG_WARN("[halo3] biblioteca de shaders {}: {}", archivo.string(), error.what());
    return false;
  }
  const auto& shaders = d.biblioteca.shaders();
  d.entradas.resize(shaders.size());
  size_t malos = 0;
  for (size_t i = 0; i < shaders.size(); ++i) {
    EntradaShader& e = d.entradas[i];
    e.spirv = shaders[i].spirv;
    e.juego = &shaders[i];
    e.numero = uint32_t(i);
    if (const char* motivo = Leer(shaders[i], e)) {
      if (++malos <= 8) REXLOG_WARN("[halo3] shader {} de la biblioteca: {}", i, motivo);
      continue;
    }
    d.candidatos[{e.vertices, uint32_t(e.microcodigo.size())}].push_back(uint32_t(i));
  }
  d.cargada = true;
  REXLOG_INFO("[halo3] biblioteca de shaders: {} ({} ilegibles)", shaders.size(), malos);
  return true;
}

const EntradaShader* ShadersHalo::Identificar(bool vertices, std::span<const uint32_t> microcodigo) {
  Datos& d = *datos_;
  ++d.estadisticas.cargas;
  const uint64_t clave = Clave(vertices, microcodigo);
  if (const auto it = d.cache.find(clave); it != d.cache.end()) return it->second;
  ++d.estadisticas.distintos;

  const EntradaShader* elegido = nullptr;
  size_t mejor = 0, segundo = 0;
  if (const auto c = d.candidatos.find({vertices, uint32_t(microcodigo.size())}); c != d.candidatos.end()) {
    for (uint32_t indice : c->second) {
      const EntradaShader& e = d.entradas[indice];
      d.temporal.assign(microcodigo.begin(), microcodigo.end());
      for (const ElementoVertice& el : e.elementos) {
        for (size_t j = 0; j < 3; ++j) d.temporal[size_t(el.instruccion) * 3 + j] &= kFetchConserva[j];
      }
      if (d.temporal == e.microcodigo) {  // exact (every pixel shader, unpatched vertex shaders)
        elegido = &e;
        mejor = microcodigo.size();
        segundo = 0;
        break;
      }
      if (!vertices) continue;
      size_t iguales = 0;
      for (size_t k = 0; k < d.temporal.size(); ++k) iguales += d.temporal[k] == e.microcodigo[k];
      if (iguales > mejor) {
        segundo = mejor;
        mejor = iguales;
        elegido = &e;
      } else if (iguales > segundo) {
        segundo = iguales;
      }
    }
  }
  const bool exacto = elegido && mejor == microcodigo.size();
  if (elegido && !exacto && (double(mejor) < kParecidoMinimo * double(microcodigo.size()) || mejor == segundo)) {
    elegido = nullptr;
  }
  if (elegido) {
    ++d.estadisticas.identificados;
    if (!exacto) ++d.estadisticas.aproximados;
  } else {
    ++d.estadisticas.sin_identificar;
    if (d.traductor) {
      if (const EntradaShader* t = Traducida(vertices, microcodigo)) {
        d.cache.emplace(clave, t);
        return t;
      }
    }
    if (d.avisos++ < kMaxAvisos) {
      // To stderr (it survives closing the game) and the microcode to logs/rex/, to compare with the library.
      std::fprintf(stderr, "[halo3] %s shader sin identificar: %zu palabras, huella %016llX, mejor parecido %zu y segundo %zu\n",
                   vertices ? "vertex" : "pixel", microcodigo.size(), static_cast<unsigned long long>(clave), mejor,
                   segundo);
#if REX_PLATFORM_SWITCH
      char ruta[512];
      std::snprintf(ruta, sizeof(ruta), "%sshader_sin_identificar_%c_%016llX.bin", RexSwitchLogDir(),
                    vertices ? 'v' : 'p', static_cast<unsigned long long>(clave));
      if (FILE* f = std::fopen(ruta, "wb")) {
        std::fwrite(microcodigo.data(), sizeof(uint32_t), microcodigo.size(), f);  // host order
        std::fclose(f);
      }
#endif
    }
  }
  d.cache.emplace(clave, elegido);
  return elegido;
}

const EntradaShader* ShadersHalo::Traducida(bool vertices, std::span<const uint32_t> microcodigo) {
  Datos& d = *datos_;
  if (!d.traductor) return nullptr;
  const uint64_t clave = Clave(vertices, microcodigo);
  if (const auto it = d.cache_traducidas.find(clave); it != d.cache_traducidas.end()) return it->second;
  const EntradaShader* r = nullptr;
#if HALO3_DEKO3D
  EntradaShader& e = d.traducidas.emplace_back();
  std::string error;
  if (TraducirMicrocodigo(vertices, microcodigo, e, error)) {
    e.numero = uint32_t(0x80000000u | (d.traducidas.size() - 1));  // reports: translated, not in the library
    ++d.estadisticas.traducidos;
    r = &e;
  } else {
    d.traducidas.pop_back();
    ++d.estadisticas.fallidos;
    std::fprintf(stderr, "[halo3] %s shader %016llX: el traductor no puede: %s\n", vertices ? "vertex" : "pixel",
                 static_cast<unsigned long long>(clave), error.c_str());
  }
#endif
  d.cache_traducidas.emplace(clave, r);
  return r;
}

}  // namespace halo3
