// Packs Halo 3's shader library: each original container (2008 layout, no constant table) with its validated
// SPIR-V, in the format of nfsmw_pal/src/nfsmw_shader_library.h. The same as shaders/nfsmw_empaquetar.cpp without
// its 2005 conversion. Containers whose SPIR-V is missing (DXC refused them) are left out and counted.
//
//   halo3_empaquetar <contenedores> <spirv validados> <salida nueva>
#include "../../nfsmw_pal/src/nfsmw_shader_library.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> Leer(const fs::path& ruta) {
  std::ifstream f(ruta, std::ios::binary | std::ios::ate);
  if (!f || f.tellg() < 0 || f.tellg() > 64 * 1024 * 1024)
    throw std::runtime_error("archivo ilegible o demasiado grande: " + ruta.string());
  std::vector<uint8_t> d(static_cast<size_t>(f.tellg()));
  f.seekg(0);
  if (!f.read(reinterpret_cast<char*>(d.data()), std::streamsize(d.size()))) throw std::runtime_error("lectura incompleta");
  return d;
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc != 4) throw std::runtime_error("uso: halo3_empaquetar <contenedores> <spirv validados> <salida nueva>");
  const fs::path originales = argv[1], compilados = argv[2], salida = argv[3];
  if (fs::exists(salida)) throw std::runtime_error("la salida ya existe");
  std::vector<fs::path> rutas;
  for (const auto& e : fs::directory_iterator(originales))
    if (e.is_regular_file() && e.path().extension() == ".bin") rutas.push_back(e.path());
  std::sort(rutas.begin(), rutas.end());
  std::vector<nfsmw::native::Shader> shaders;
  std::vector<fs::path> incluidos;
  size_t sin_spirv = 0;
  for (const auto& ruta : rutas) {
    const fs::path spv_ruta = compilados / (ruta.stem().string() + ".spv");
    if (!fs::exists(spv_ruta)) {
      ++sin_spirv;
      continue;
    }
    nfsmw::native::Shader s;
    s.original = Leer(ruta);
    s.vertices = (s.original.size() > 3) && (s.original[3] & 1);
    const auto spv = Leer(spv_ruta);
    if (spv.size() % 4) throw std::runtime_error("SPIR-V desalineado: " + spv_ruta.string());
    for (size_t i = 0; i < spv.size(); i += 4)
      s.spirv.push_back(uint32_t(spv[i]) | uint32_t(spv[i + 1]) << 8 | uint32_t(spv[i + 2]) << 16 |
                        uint32_t(spv[i + 3]) << 24);
    shaders.push_back(std::move(s));
    incluidos.push_back(ruta);
  }
  auto paquete = nfsmw::native::EmpaquetarShaders(std::move(shaders));
  nfsmw::native::BibliotecaShaders biblioteca;
  biblioteca.Cargar(paquete);
  for (const auto& ruta : incluidos)
    if (!biblioteca.Buscar(Leer(ruta))) throw std::runtime_error("un shader no se encuentra tras empaquetar");
  std::ofstream f(salida, std::ios::binary);
  if (!f.write(reinterpret_cast<const char*>(paquete.data()), std::streamsize(paquete.size())) || !f.flush())
    throw std::runtime_error("no se pudo escribir el paquete completo");
  std::printf("%zu contenedores -> %zu shaders, %zu bytes; %zu sin SPIR-V (fuera)\n", incluidos.size(),
              biblioteca.shaders().size(), paquete.size(), sin_spirv);
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "%s\n", e.what());
  return 1;
}
