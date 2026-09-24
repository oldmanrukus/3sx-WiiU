# Building for Wii U

`CMakeLists.txt` in the repository root **is** the Wii U build — it replaced
upstream's desktop one, so the desktop instructions in [building.md](building.md)
do not apply to this fork as it stands. (`CMakeLists_desktop.txt` and
`CMakeLists_game.txt.bak` are older copies of this same Wii U file, despite
their names; neither builds for desktop.)

It targets devkitPPC + wut and renders through SDL2's Wii U backend, with
`include/SDL3/SDL.h` acting as an SDL3-to-SDL2 shim for the shared game code.

## Requirements

Install [devkitPro's pacman](https://devkitpro.org/wiki/Getting_Started), then:

```bash
sudo dkp-pacman -S wiiu-dev ppc-zlib
```

`wiiu-dev` brings in devkitPPC, wut, `elf2rpl`, `wuhbtool` and the Wii U
portlibs (including SDL2). Set `DEVKITPRO` in your environment:

```bash
export DEVKITPRO=/opt/devkitpro
```

## Building

```bash
cmake -B build-wiiu -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=wiiu_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-wiiu
```

On Windows under MSYS2, use `wiiu_toolchain_windows.cmake` instead.

This produces `build-wiiu/3sx-wiiu.rpx`, and `build-wiiu/3sx-wiiu.wuhb` as well
if `wuhbtool` is installed.

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `WIIU_TRACE` | `OFF` | Per-frame `OSReport()` tracing of the task loop, game state, texture cache and audio decode, plus an OSScreen task overlay. `OSReport()` is synchronous, so this costs real frame time — turn it on only while debugging. |

```bash
cmake -B build-wiiu -G Ninja -DCMAKE_TOOLCHAIN_FILE=wiiu_toolchain.cmake -DWIIU_TRACE=ON
```

## Installing

Copy onto the SD card:

```
sd:/wiiu/apps/3sx/3sx-wiiu.rpx     (or 3sx.wuhb for Aroma)
sd:/wiiu/apps/3sx/SF33RD.AFS
```

`SF33RD.AFS` comes from your own copy of *Street Fighter III: 3rd Strike* or
*Street Fighter Anniversary Collection* for PlayStation 2 — it is not part of
this repository and is not bundled into the `.wuhb`. The game also accepts it at
`sd:/3sx/SF33RD.AFS`. Settings are written to `sd:/wiiu/apps/3sx/save/`.

## Checking a change without devkitPro

The Wii U is big-endian 32-bit PowerPC, and most of what goes wrong in this port
is data that the PlayStation 2 stored little-endian being read back the other way
round. Two scripts catch that class of bug without the real toolchain:

```bash
sudo apt-get install -y gcc-powerpc-linux-gnu libsdl2-dev qemu-user-static

tools/check-bigendian.sh          # type-check every Wii U source as big-endian PPC
tools/be-tests/run.sh             # run the sound-data endian fixup under emulation
```

`check-bigendian.sh` compiles with a PowerPC cross gcc against stub wut headers
in `tools/be-stubs/` — it will not produce an RPX, but it does catch compile
errors and endianness-sensitive warnings. `be-tests/run.sh` runs the PHD and
SpuMap conversions over the real sound bank data on an emulated big-endian CPU
and diffs the result against a little-endian reference parse of the same bytes.

Neither replaces running the game: use them as a first pass, then build the RPX.

## Notes on the port

- Save data is disabled (`MEMCARD_DISABLED`) and netplay is stubbed out
  (`NETPLAY_STUB`).
- Rendering goes through `src/port/sdl/sdl_game_renderer.c` on SDL2, not through
  `src/port/wiiu/wiiu_game_renderer.c`, which is excluded from the build.
- The PS2 sound banks (`PHD_*`, `SpuMap`) are byte-swapped in place at load by
  `emlSndEndian.c`; sample RAM is swapped as it is uploaded in `spu_wiiu.c`.
