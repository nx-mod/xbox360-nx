// Finds Xbox 360 shader containers in a game's files, for any game (nfsmw_buscar_contenedores.cpp is NFS's, 2005
// layout only). Files are mapped, not read into memory, so a 500 MB Halo 3 map costs no RAM. Identical containers
// (Halo repeats most of them across maps) are written once.
//
//   x360_buscar_contenedores <version> <salida nueva> <archivo o carpeta>...
//
// <version> is the third byte of the signature 10 2A vv: 11 for the layout XenosRecomp reads (Halo 3, 2007-2008
// titles), 0E for 2005 titles (NFS Most Wanted). A match of signature and sizes is only a candidate; the translator
// validates the microcode. procedencia.tsv keeps where each one came from.
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace {

uint32_t LeerBE(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

struct Mapa {
  const uint8_t* datos = nullptr;
  size_t n = 0;
  explicit Mapa(const std::filesystem::path& ruta) {
    const int fd = open(ruta.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("no se puede abrir " + ruta.string());
    struct stat st {};
    fstat(fd, &st);
    n = size_t(st.st_size);
    if (n) {
      void* p = mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
      if (p == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("no se puede mapear " + ruta.string());
      }
      madvise(p, n, MADV_SEQUENTIAL);
      datos = static_cast<const uint8_t*>(p);
    }
    close(fd);
  }
  ~Mapa() {
    if (datos) munmap(const_cast<uint8_t*>(datos), n);
  }
};

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 4) {
    std::fprintf(stderr, "uso: x360_buscar_contenedores <version: 11 o 0E> <salida nueva> <archivo o carpeta>...\n");
    return 1;
  }
  const uint8_t version = uint8_t(std::strtoul(argv[1], nullptr, 16));
  const std::filesystem::path salida(argv[2]);
  if (std::filesystem::exists(salida)) throw std::runtime_error("la salida ya existe");
  std::filesystem::create_directories(salida);
  std::ofstream indice(salida / "procedencia.tsv");
  indice << "archivo\torigen\tdesplazamiento\tvirtual\tfisico\n";

  std::vector<std::filesystem::path> archivos;
  for (int i = 3; i < argc; ++i) {
    const std::filesystem::path p(argv[i]);
    if (std::filesystem::is_directory(p)) {
      for (const auto& e : std::filesystem::recursive_directory_iterator(p))
        if (e.is_regular_file()) archivos.push_back(e.path());
    } else {
      archivos.push_back(p);
    }
  }

  std::unordered_set<uint64_t> vistos;
  size_t total = 0, repetidos = 0;
  for (const auto& ruta : archivos) {
    const Mapa m(ruta);
    size_t encontrados = 0, nuevos = 0;
    for (size_t i = 0; i + 24 <= m.n; ++i) {
      const uint8_t* p = m.datos + i;
      if (p[0] != 0x10 || p[1] != 0x2A || p[2] != version || p[3] > 1) continue;
      // flags, virtual size, physical size, ?, constant table, definition table, shader: offsets into the virtual
      // part. Halo 3 strips the constant table (offset 0: its engine binds by register), so 0 is accepted for it.
      const uint32_t v = LeerBE(p + 4), f = LeerBE(p + 8), c = LeerBE(p + 16), s = LeerBE(p + 24);
      if (v < 28 || !f || uint64_t(v) + f > 262144 || uint64_t(v) + f > m.n - i || (c && (c < 28 || c >= v)) ||
          s < 28 || uint64_t(s) + 24 > v)
        continue;
      ++encontrados;
      const size_t largo = size_t(v) + f;
      if (!vistos.insert(XXH3_64bits(p, largo) ^ largo).second) {
        ++repetidos;
        continue;
      }
      char nombre[48];
      std::snprintf(nombre, sizeof(nombre), "%c_%06zu.bin", p[3] ? 'v' : 'p', total);
      std::ofstream copia(salida / nombre, std::ios::binary);
      copia.write(reinterpret_cast<const char*>(p), std::streamsize(largo));
      if (!copia) throw std::runtime_error("fallo de escritura");
      indice << nombre << '\t' << ruta.string() << '\t' << i << '\t' << v << '\t' << f << '\n';
      ++total;
      ++nuevos;
      i += largo - 1;  // the next container cannot start inside this one
    }
    std::printf("%s: %zu candidatos, %zu nuevos\n", ruta.filename().string().c_str(), encontrados, nuevos);
    std::fflush(stdout);
  }
  std::printf("Total: %zu contenedores distintos (%zu repetidos); comprobar antes de traducir.\n", total, repetidos);
  return 0;
} catch (const std::exception& error) {
  std::fprintf(stderr, "error: %s\n", error.what());
  return 2;
}
