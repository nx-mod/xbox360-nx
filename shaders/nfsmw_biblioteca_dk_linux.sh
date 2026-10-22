#!/bin/bash
# nfsmw_biblioteca_dk_linux.sh SALIDA ISO
# Builds the deko3d shader library (nfsmw_shaders_dk.nfsp) of one edition straight from its ISO, on Linux with no
# native DXC (it runs on the Switch dev phone, ARM64). Same result as the DEKO3D=1 part of
# nfsmw_regenerar_biblioteca_pcf.sh, with two tools borrowed from the installer page's repository:
#   - its container scanner (test/dump_containers.mjs) takes the containers out of the ISO;
#   - its WebAssembly DXC (wasm/dxc_web.mjs, run by wasm/dxc_node.mjs) compiles the HLSL. Its fixed arguments are the
#     Vulkan set: vertex shaders get -fvk-invert-y, which deko3d must not have (upside-down image, and every 3D
#     triangle culled because the winding flips). dxc_node.mjs --sin-invertir-y removes it (patched temporary copy);
#     -DNFSMW_DEKO3D goes in as a #define.
# The translator and the packer are this repository's (built here with g++), so the HLSL comes from the current
# XenosRecomp/shader_common.h, which has the NFSMW_DEKO3D branches; the installer's copy does not.
#
#   INSTALADOR=/path/to/nfsmw-nx-installer shaders/nfsmw_biblioteca_dk_linux.sh out/biblioteca_es "NFS (Spain).iso"
#
# SALIDA must be a new folder. Heavy job on the phone: run it after heavy-preflight, nice'd and detached.
# The library goes next to the deko3d NRO (nfsmw_nativo_sistema.cpp loads nfsmw_shaders_dk.nfsp from there).
set -euo pipefail
RAIZ=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$RAIZ/.." && pwd)
SDK=${SDK:-$REPO/sdk}
INSTALADOR=${INSTALADOR:?set INSTALADOR to the nfsmw-nx-installer checkout}
SALIDA=${1:?falta la carpeta de salida}
ISO=${2:?falta el ISO}
[ -e "$SALIDA" ] && { echo "La carpeta de salida ya existe"; exit 1; }
mkdir -p "$SALIDA"/{contenedores,hlsl,spirv_dk,bin}
SALIDA=$(cd "$SALIDA" && pwd)

echo "== contenedores"
node "$INSTALADOR/test/dump_containers.mjs" "$ISO" "$SALIDA/contenedores"

echo "== traductor y empaquetador (g++)"
# xxhash.h from the SDK's thirdparty instead of Mesa's src/util (same XXH3).
g++ -std=c++23 -O1 "-I$RAIZ" "-I$RAIZ/XenosRecomp" "-I$SDK/thirdparty/fmt/include" "-I$SDK/thirdparty/xxHash" \
  -include "$RAIZ/pch_min.h" -DFMT_HEADER_ONLY -DXXH_INLINE_ALL -DNFSMW_RECOMP \
  "$RAIZ/nfsmw_hlsl.cpp" "$RAIZ/XenosRecomp/shader_recompiler.cpp" -o "$SALIDA/bin/nfsmw_hlsl" \
  > "$SALIDA/compilar.log" 2>&1 || { echo "Fallo al compilar el traductor; ver compilar.log"; exit 1; }
g++ -std=c++23 -O2 "-I$SDK/thirdparty/xxHash" "$RAIZ/nfsmw_empaquetar.cpp" "$REPO/nfsmw_pal/src/nfsmw_shader_library.cpp" \
  -o "$SALIDA/bin/nfsmw_empaquetar" >> "$SALIDA/compilar.log" 2>&1 \
  || { echo "Fallo al compilar el empaquetador; ver compilar.log"; exit 1; }

echo "== traduccion"
"$SALIDA/bin/nfsmw_hlsl" "$SALIDA/contenedores" "$SALIDA/hlsl" "$RAIZ/XenosRecomp/shader_common.h" \
  > "$SALIDA/traduccion.log" || { echo "Hay shaders sin traducir; ver traduccion.log"; exit 1; }

echo "== reescrituras (las de nfsmw_regenerar_biblioteca_pcf.sh, tal cual)"
# The shadow-map and composition rewrites are taken verbatim from the reference script, so they cannot drift.
py() { python3 "$@"; }
REESCRITURAS=$(sed -n '/^cambios=/,/^: > "\$SALIDA\/dxc.log"/p' "$RAIZ/nfsmw_regenerar_biblioteca_pcf.sh" | sed '$d')
[ -n "$REESCRITURAS" ] || { echo "No se encontraron las reescrituras en nfsmw_regenerar_biblioteca_pcf.sh"; exit 1; }
# The reference script names the final composition shader p_000139, its name in PAL Spanish. Other editions number
# it differently (USA: p_000140), so find it by its container's SHA-256, as the installer does
# (blur_container_sha256 in its manifest; the same container in every edition checked so far).
BLUR_SHA=${BLUR_SHA:-e80cca037bd4becd8a192daa4583e3a3a9281e06436015746583f30701a296f9}
COMPOSICION=$(cd "$SALIDA/contenedores" && sha256sum *.bin | awk -v s="$BLUR_SHA" '$1 == s {sub(/\.bin$/, "", $2); print $2; exit}')
if [ -n "$COMPOSICION" ]; then
  echo "composicion: $COMPOSICION"
  REESCRITURAS=${REESCRITURAS//p_000139/$COMPOSICION}
else
  echo "aviso: ningun contenedor con BLUR_SHA $BLUR_SHA; la mezcla del desenfoque no se reescribe"
fi
eval "$REESCRITURAS"

echo "== DXC (wasm) para deko3d"
node "$RAIZ/wasm/dxc_node.mjs" "$INSTALADOR/wasm/dxc_web.mjs" "$SALIDA/hlsl" "$SALIDA/spirv_dk" --sin-invertir-y -D NFSMW_DEKO3D \
  2> "$SALIDA/dxc_dk.log" || { echo "DXC rechazo un shader; ver dxc_dk.log"; exit 1; }

echo "== spirv-val"
: > "$SALIDA/spirv-val.log"
for f in "$SALIDA"/spirv_dk/*.spv; do
  spirv-val --target-env vulkan1.2 --scalar-block-layout "$f" 2>> "$SALIDA/spirv-val.log" \
    || { echo "SPIR-V invalido: $(basename "$f")"; exit 1; }
done

echo "== empaquetado"
"$SALIDA/bin/nfsmw_empaquetar" "$SALIDA/contenedores" "$SALIDA/spirv_dk" "$SALIDA/nfsmw_shaders_dk.nfsp" \
  > "$SALIDA/paquete_dk.log" || { echo "Fallo al empaquetar; ver paquete_dk.log"; exit 1; }
echo "spirv=$(ls "$SALIDA"/spirv_dk/*.spv | wc -l) contenedores=$(ls "$SALIDA"/contenedores/*.bin | wc -l)"
ls -la "$SALIDA/nfsmw_shaders_dk.nfsp"
