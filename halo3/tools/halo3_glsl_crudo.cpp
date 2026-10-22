/**
 * @file    halo3_glsl_crudo.cpp
 * @brief   The PC test of the console's run-time shader translator (halo3/src/halo3_traductor_nucleo.h): microcode
 *          as the ring carries it, to the GLSL the renderer hands uam.
 *
 * Takes the microcode out of each input and translates it the console's way (no vertex elements, interpolators by
 * index), so what fails here fails there. Inputs, by name:
 *  - a 2008 container (halo3_shaders/contenedores, xex_cont): its microcode, the stage from its flags;
 *  - a console dump, shader_sin_identificar_<v|p>_<hash>.bin (halo3_shaders.cpp): raw microcode in host order.
 * Each output is <name>.vert.glsl or <name>.frag.glsl, ready for uam once "#version 460" is put in front.
 *
 * Build with -DNFSMW_RECOMP -DHALO3_RECOMP -DXENOS_GLSL, as halo3_traductor.cpp is.
 *
 * Usage
 *   halo3_glsl_crudo <input folder> <output folder> <shader_common.glsl>
 */
#include "XenosRecomp/shader_recompiler.h"
#include "../src/halo3_traductor_nucleo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> LeerTodo(const std::filesystem::path& ruta) {
  std::ifstream f(ruta, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 4) {
    std::printf("uso: halo3_glsl_crudo <entrada> <salida> <shader_common.glsl>\n");
    return 1;
  }
  const std::filesystem::path entrada = argv[1], salida = argv[2];
  const std::vector<uint8_t> p = LeerTodo(argv[3]);
  const std::string preludio(p.begin(), p.end());
  if (preludio.empty()) {
    std::printf("no se puede leer %s\n", argv[3]);
    return 1;
  }
  std::filesystem::create_directories(salida);
  std::vector<std::filesystem::path> archivos;
  for (const auto& e : std::filesystem::directory_iterator(entrada)) {
    if (e.is_regular_file() && e.path().extension() == ".bin") archivos.push_back(e.path());
  }
  std::sort(archivos.begin(), archivos.end());

  size_t ok = 0, mal = 0;
  for (const auto& ruta : archivos) {
    const std::vector<uint8_t> d = LeerTodo(ruta);
    const std::string nombre = ruta.stem().string();
    bool vertices = false;
    std::vector<uint32_t> microcodigo;
    if (d.size() >= 36 && (halo3::contenedor::LeerBE32(d, 0) & 0xFFFFFF00u) == 0x102A1100u) {
      vertices = halo3::contenedor::LeerBE32(d, 0) & 1;
      const uint32_t virtuales = halo3::contenedor::LeerBE32(d, 4), cabecera = halo3::contenedor::LeerBE32(d, 24);
      const uint32_t desplazamiento = halo3::contenedor::LeerBE32(d, cabecera);
      const uint32_t bytes = halo3::contenedor::LeerBE32(d, cabecera + 4);
      if (size_t(virtuales) + desplazamiento + bytes > d.size() || (bytes % 4)) {
        std::printf("  %s: microcodigo fuera\n", nombre.c_str());
        ++mal;
        continue;
      }
      for (uint32_t i = 0; i < bytes; i += 4) {
        microcodigo.push_back(halo3::contenedor::LeerBE32(d, size_t(virtuales) + desplazamiento + i));
      }
    } else if (nombre.starts_with("shader_sin_identificar_") && nombre.size() > 24 && !(d.size() % 4)) {
      vertices = nombre[23] == 'v';
      microcodigo.resize(d.size() / 4);
      std::memcpy(microcodigo.data(), d.data(), d.size());  // host order already
    } else {
      std::printf("  %s: ni contenedor ni volcado\n", nombre.c_str());
      ++mal;
      continue;
    }
    try {
      uint32_t salidas = 0;
      const std::string texto = halo3::nucleo::Traducir(vertices, microcodigo, preludio, salidas);
      std::ofstream f(salida / (nombre + (vertices ? ".vert.glsl" : ".frag.glsl")), std::ios::binary);
      f.write(texto.data(), std::streamsize(texto.size()));
      ++ok;
    } catch (const std::exception& error) {
      std::printf("  %s: rechazado: %s\n", nombre.c_str(), error.what());
      ++mal;
    }
  }
  std::printf("\n%zu: %zu traducidos, %zu mal\n", archivos.size(), ok, mal);
  return mal ? 2 : 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "error: %s\n", error.what());
  return 1;
}
