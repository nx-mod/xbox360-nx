# TODO

## Need for Speed: Most Wanted

1. **Bloom downsample in two passes.** The first 320x180 pass (PS `p_000091`, n65 USA / n77 PAL, `huella`
   0xABE81C36A1A89DEC) reads 16 spread taps per pixel and takes 2.4 ms of a ~25 ms GPU frame, 95 % of it texture
   reads. Its taps sit on texel corners (offsets +-2 and +-6 source texels), so each is a 2x2 average: a 640x360
   pre-pass where texel k = average of source 2k-1 and 2k (both axes), then the game's shader on that texture with
   offsets -2.5, -0.5, 1.5, 3.5 half-res texels, gives the same sums with cache-friendly reads (only the last column
   differs). Only when the constants match that pattern; a setting to turn it off. Model the pass on `Copiar3D`.
2. **Cracked glass**: near the end of the career intro the screen goes dark instead of showing cracked glass. Ruled
   out so far are in the [devlog](docs/deko3d-devlog.md). Next: a diagnostic that writes only around that moment.
3. **`nfsmw_intervalo_presentacion = "inmediato"`**: test on the console (menus above 30 FPS; check that movies keep
   their speed), then decide the default.
4. **Port from the other NFS MW recompilations** (all GPL-3.0):
   - [madelrandel-blip/NFSMW-Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled): the game's `memset`
     (0x826BE610) as native NEON; occlusion culling and a shadow map cache; main thread priority.
   - [frankyfife/NFSMW-Recompiled_ff](https://github.com/frankyfife/NFSMW-Recompiled_ff): frame pacing in `VdSwap`
     and a low-latency mode, ZPD occlusion queries as a running counter (sun glare through buildings), cars at full
     detail at any distance.
   - [codepdbh/nfsmw-android](https://github.com/codepdbh/nfsmw-android): the shader library generated on the device
     at first start (then the release needs no PC step at all); cutscene audio fixes on skip.
5. **LAN play (online)** through a LAN server: [victorgbd/nfsmw-server-lan](https://github.com/victorgbd/nfsmw-server-lan) stands in
   for EA's 2005 servers (Quick Match works between phones). Port the game-side changes of
   [NFSMW-Recompiled-Mobile](https://github.com/victorgbd/NFSMW-Recompiled-Mobile) (`docs/diario/red-y-privilegios.md`)
   and add a server IP setting. Needs the SDK networking below.
6. Spain runs some menus at 30 FPS where USA reaches 60; a crash when closing the game.
7. Remove the temporary stderr diagnostics once the hunts are over.

## Halo 3

- A picture: the plan and the state are in [halo3/RENDERER.md](halo3/RENDERER.md). Open: the screen never clears
  (Halo clears with D3D's own full-screen draws), translated vertex shaders are slow (structured loops, off), the
  exposure chain's 16-bit targets. 2026-10-26 on the console: the intro movie shows, the menu is black but for a very
  faint Halo logo.
- Static in the audio.

## Next game

Pick the next game for this repo. Call of Duty 4 has no ReXGlue port anywhere (2026-10-26); a new recomp needs the
Xbox 360 disc. ReXGlue games already on the Switch, to learn from or to avoid duplicating: Sonic Adventure 2 (sa2-nx),
Mass Effect, The Simpsons Game, Torchlight, Dead Space 2 and 3 (VasilkovND), Metal Gear Rising (tuancapoo/mgr-nx-deko),
Naruto: Rise of a Ninja (malinkin-s). Several draw with the shared reNut renderer on deko3d: read it before more
renderer work here.

## SDK

- Shader library generation on the console as an SDK feature, for every game.
- **Networking for LAN play**: the Xbox 360 network calls (XNet, sockets, Xbox Live sign-in stand-ins) over the
  Switch's sockets, shared by every game. NFS's LAN server is the first user; Halo 3 system link could follow.
