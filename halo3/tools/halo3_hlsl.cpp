/**
 * @file    halo3_hlsl.cpp
 * @brief   Halo 3's shader containers to HLSL with XenosRecomp (shaders/XenosRecomp, NFS's version).
 *
 * The same driver as shaders/nfsmw_hlsl.cpp, for the containers halo3_shader_containers.py finds in the maps.
 * Those are in the 2008 format XenosRecomp reads, without a constant table: each gets the synthetic one of
 * halo3/src/halo3_contenedor.h.
 *
 * Usage
 *   halo3_hlsl <input folder> <output folder> <shader_common.h>
 */
#include "XenosRecomp/shader_recompiler.h"
#include "../src/halo3_contenedor.h"  // the synthetic constant table, shared with the run-time translator

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
  std::vector<uint8_t> data;
  FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) {
    return data;
  }
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  data.resize(n > 0 ? size_t(n) : 0);
  if (!data.empty() && std::fread(data.data(), 1, data.size(), f) != data.size()) {
    data.clear();
  }
  std::fclose(f);
  return data;
}

uint32_t GetBE32(const std::vector<uint8_t>& v, size_t o) { return halo3::contenedor::LeerBE32(v, o); }

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 4) {
    std::printf("usage: halo3_hlsl <input> <output> <shader_common.h>\n");
    return 1;
  }
  const std::filesystem::path input = argv[1];
  const std::filesystem::path output = argv[2];
  const std::vector<uint8_t> common = ReadAll(argv[3]);
  if (common.empty()) {
    std::printf("cannot read %s\n", argv[3]);
    return 1;
  }
  const std::string_view include(reinterpret_cast<const char*>(common.data()), common.size());
  if (std::filesystem::exists(output) && !std::filesystem::is_empty(output)) {
    std::fprintf(stderr, "the output folder must be empty\n");
    return 1;
  }
  std::filesystem::create_directories(output);

  std::vector<std::filesystem::path> files;
  for (const auto& e : std::filesystem::directory_iterator(input)) {
    if (e.is_regular_file() && e.path().extension() == ".bin") {
      files.push_back(e.path());
    }
  }
  std::sort(files.begin(), files.end());

  size_t ok = 0, skipped = 0;
  for (const auto& path : files) {
    const std::vector<uint8_t> data = ReadAll(path);
    const std::string name = path.filename().string();
    if (data.size() < 36 || (GetBE32(data, 0) & 0xFFFFFF00u) != 0x102A1100u) {
      std::printf("  %s: not a 2008 container\n", name.c_str());
      ++skipped;
      continue;
    }
    const std::vector<uint8_t> container = GetBE32(data, 16) ? data : halo3::contenedor::ConTablaSintetica(data);
    ShaderRecompiler recompiler;
    try {
      recompiler.recompile(container.data(), include);
    } catch (const std::exception& error) {
      std::printf("  %s: rejected: %s\n", name.c_str(), error.what());
      if (std::getenv("HALO3_HLSL_COLA")) {  // the tail of what was written, to see where a runaway loops
        const size_t n = std::min<size_t>(recompiler.out.size(), 3000);
        std::printf("--- last %zu of %zu bytes:\n%s\n---\n", n, recompiler.out.size(),
                    recompiler.out.c_str() + recompiler.out.size() - n);
      }
      ++skipped;
      continue;
    }
    if (recompiler.out.empty()) {
      std::printf("  %s: produced nothing\n", name.c_str());
      ++skipped;
      continue;
    }
    auto target = output / path.filename();
    target.replace_extension(".hlsl");
    FILE* f = std::fopen(target.string().c_str(), "wb");
    const bool written = f && std::fwrite(recompiler.out.data(), 1, recompiler.out.size(), f) == recompiler.out.size();
    if (f && std::fclose(f) != 0) {
      ++skipped;
      continue;
    }
    written ? ++ok : ++skipped;
  }
  std::printf("\n%zu shaders: %zu translated, %zu skipped\n", files.size(), ok, skipped);
  return !files.empty() && ok == files.size() ? 0 : 2;
} catch (const std::exception& error) {
  std::fprintf(stderr, "error: %s\n", error.what());
  return 1;
}
