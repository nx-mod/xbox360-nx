// Runs the installer page's WebAssembly DXC (wasm/dxc_web.mjs, built from dxc_web.cpp here) from Node over a folder
// of HLSL, so the shader library can be built where there is no native DXC (Linux on ARM, the Switch dev phone).
//
//   node shaders/wasm/dxc_node.mjs <dxc_web.mjs> <hlsl folder> <spirv folder> [--sin-invertir-y] [-D NAME[=VALUE]]...
//
// dxc_web.cpp's compile() has the arguments fixed: -spirv -T vs_6_6|ps_6_6 -E main -HV 2021
// -fspv-target-env=vulkan1.2 -fvk-use-dx-layout -Werror=parameter-usage, plus -fvk-invert-y for vertex shaders.
// That is the Vulkan set of nfsmw_regenerar_biblioteca_pcf.sh. The deko3d library needs it without -fvk-invert-y
// (deko3d's clip-space y points up): --sin-invertir-y ("without inverting y") patches the module, replacing
// that flag with the same-length, harmless -DNOINVERTY=1 in a temporary copy, so the installer's files are never
// changed. Without it, the
// vertex shaders come out upside down on deko3d and their triangles are culled (the winding flips with y).
// -DNFSMW_DEKO3D cannot be passed to compile(), so each -D here goes at the top of the source as a #define instead.
// p_* files are pixel shaders, the rest vertex shaders, as in that script.
// Exits 1 on the first shader DXC rejects (its diagnostics go to stderr).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const args = process.argv.slice(2);
const [modulo, entrada, salida] = args;
if (!modulo || !entrada || !salida) {
  console.error('uso: node dxc_node.mjs <dxc_web.mjs> <carpeta hlsl> <carpeta spirv> [-D NOMBRE[=VALOR]]...');
  process.exit(2);
}
const defines = [];
let sinInvertirY = false;
for (let i = 3; i < args.length; i++) {
  if (args[i] === '--sin-invertir-y') sinInvertirY = true;
  else if (args[i] === '-D' && i + 1 < args.length) defines.push(args[++i]);
  else if (args[i].startsWith('-D')) defines.push(args[i].slice(2));
}

// The wide string L"-fvk-invert-y" is UTF-32 in the module (Emscripten's wchar_t); the data segment drops the
// trailing zero bytes, so only the low half of the last character, 'y', is stored. The replacement keeps every
// stored byte position and ends in '1' the same way.
function sinInvertir(wasm) {
  const u32 = (s) => Buffer.from([...s].flatMap((c) => [c.charCodeAt(0), 0, 0, 0]));
  const viejo = Buffer.concat([u32('-fvk-invert-'), Buffer.from([0x79, 0x00])]);
  const nuevo = Buffer.concat([u32('-DNOINVERTY='), Buffer.from([0x31, 0x00])]);
  const i = wasm.indexOf(viejo);
  if (i < 0 || wasm.indexOf(viejo, i + 1) >= 0) {
    console.error('dxc_node: -fvk-invert-y not found exactly once in dxc_web.wasm; refusing to patch');
    process.exit(1);
  }
  const copia = Buffer.from(wasm);
  nuevo.copy(copia, i);
  return copia;
}
const cabecera = defines.map((d) => {
  const [n, v = '1'] = d.split('=');
  return `#define ${n} ${v}\n`;
}).join('');

// The module ignores Module.wasmBinary and always loads dxc_web.wasm from beside its own .mjs, so the patched copy
// goes in a temporary folder with a copy of the .mjs (removed at exit).
let moduloUsado = path.resolve(modulo);
if (sinInvertirY) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'dxc_dk_'));
  fs.copyFileSync(moduloUsado, path.join(tmp, 'dxc_web.mjs'));
  fs.writeFileSync(path.join(tmp, 'dxc_web.wasm'),
                   sinInvertir(fs.readFileSync(path.join(path.dirname(moduloUsado), 'dxc_web.wasm'))));
  process.on('exit', () => fs.rmSync(tmp, { recursive: true, force: true }));
  moduloUsado = path.join(tmp, 'dxc_web.mjs');
}
const createDxcModule = (await import(pathToFileURL(moduloUsado).href)).default;
const errores = [];
const dxc = await createDxcModule({ print: () => {}, printErr: (s) => errores.push(s) });
dxc.FS.mkdirTree('/work');
fs.mkdirSync(salida, { recursive: true });

const ficheros = fs.readdirSync(entrada).filter((n) => n.endsWith('.hlsl')).sort();
let n = 0;
for (const nombre of ficheros) {
  const base = nombre.slice(0, -5);
  const fuente = cabecera + fs.readFileSync(path.join(entrada, nombre), 'utf8');
  const e = `/work/${base}.hlsl`;
  const s = `/work/${base}.spv`;
  dxc.FS.writeFile(e, fuente);
  errores.length = 0;
  const rc = dxc.ccall('compile', 'number', ['string', 'string', 'number'], [e, s, base.startsWith('p_') ? 0 : 1]);
  if (rc !== 0) {
    console.error(`DXC rechazo ${base} (${rc})\n${errores.join('\n')}`);
    process.exit(1);
  }
  fs.writeFileSync(path.join(salida, `${base}.spv`), dxc.FS.readFile(s));
  dxc.FS.unlink(e);
  dxc.FS.unlink(s);
  n++;
}
console.log(`dxc_node: ${n} shaders -> ${salida}${sinInvertirY ? ' (sin -fvk-invert-y)' : ''}` +
            `${defines.length ? ` (${defines.join(' ')})` : ''}`);
