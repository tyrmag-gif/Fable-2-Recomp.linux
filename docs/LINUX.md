# Fable 2 Recompiled on Linux

This is a native Linux build of the recompilation: no Wine or Proton. The
game runs on the ReXGlue SDK's Linux runtime and renders with **Vulkan**.
Windows support is unchanged.

> **Status: experimental.** The Linux port compiles, but it has not been
> played through yet. Please report crashes along with the log from
> `logs/` next to the binary.

## What you need

- An x86-64 Linux PC with a GPU and Vulkan driver (Mesa RADV/ANV or NVIDIA)
- About 25 GB of free space (7 GB game files plus the SDK and build)
- **Your own** Fable 2 GOTY disc image (`.iso`). The supported versions are
  listed in [German GOTY support](GERMAN_GOTY_SUPPORT.md) and
  `constants/game_versions.json`.

### Arch Linux packages

```bash
sudo pacman -S --needed base-devel clang lld cmake ninja python git curl unzip autoconf \
  gtk3 libx11 libxss libxkbcommon wayland wayland-protocols libdecor \
  vulkan-icd-loader vulkan-headers alsa-lib libpulse pipewire dotnet-sdk-8.0
```

You also need the Vulkan driver for your GPU: `vulkan-radeon` (AMD),
`vulkan-intel` (Intel) or `nvidia-utils` (NVIDIA). `./build.sh deps` prints
the same list. The game needs clang 20 or newer, which Arch already ships.
`dotnet-sdk-8.0` is only needed for the ISO extractor.

## 1. Get the code

```bash
git clone --recurse-submodules https://github.com/tyrmag-gif/Fable-2-Recomp.linux.git
cd Fable-2-Recomp.linux
```

## 2. Extract the game files from your ISO

```bash
tools/linux/extract_iso.sh ~/Games/Fable2.iso
```

This works like the Windows launcher's **Extract from ISO** button. It first
extracts only `default.xex` and checks its SHA-256 against the supported
versions. Only on a match does it extract `default.xex` and `data/` into the
repository root. The first run builds the `x360extract` tool, which needs the
.NET 8 SDK.

## 3. Build

```bash
./build.sh -release fable_2
```

The first build does all of the following, and takes a while:

1. Downloads the official ReXGlue SDK 0.10.0 for Linux (the codegen tool,
   checked against a pinned SHA-256).
2. Builds the project's ReXGlue runtime fork (`thirdparty/rexglue-sdk`) with
   the Vulkan backend.
3. Statically recompiles `default.xex` to C++ and compiles it.

The result is `out/build/linux-amd64-release/fable_2`. Use `./build.sh fable_2`
(without `-release`) for a Debug build.

## 4. Run

```bash
tools/linux/stage_content.sh out/build/linux-amd64-release
out/build/linux-amd64-release/fable2.sh
```

`stage_content.sh` symlinks the game files next to the binary instead of
copying 7 GB. You can also skip it and point the game at the files:

```bash
out/build/linux-amd64-release/fable2.sh --game_data_root="$PWD"
```

Saves, cache, logs and `fable_2.toml` live next to the binary, as on
Windows. Back up `saves/` to keep your progress.

All the command-line options and `fable2_config.toml` settings from the main
[README](../README.md) work the same way.

## Differences from Windows

| Area | Windows | Linux |
|---|---|---|
| Renderer | D3D12 (Vulkan optional) | Vulkan |
| Keyboard and mouse | polls `GetAsyncKeyState`, recenters the cursor with `SetCursorPos` | reads SDL window events and uses pointer lock (relative mouse mode), so it works on both X11 and Wayland |
| File names | case-insensitive | case-insensitive lookup for game files (SDK VFS plus `src/core/host_path.h`) |
| Launcher | WPF GUI (`Fable2Launcher.exe`) | not ported yet: use `extract_iso.sh` and `fable2.sh` |
| Debug helpers | `.cmd` scripts | `tools/linux/*.sh` (`fable2-functrace.sh`, `StartWithoutAudio.sh`) |
| AV logger and stall dumper | `fable2_av_probe.h`, `fable2_stall_dump.h` | Windows-only, not compiled on Linux |

## Troubleshooting

- **`clang ... too old`**: install clang 20 or newer, or point the build at
  it with `CC=clang-20 CXX=clang++-20 ./build.sh ...`.
- **No window, or a Vulkan error at start**: check your driver with
  `vulkaninfo --summary` (package `vulkan-tools`).
- **Mouse look does not lock the pointer**: some compositors refuse pointer
  lock for unfocused windows. Click into the game window once. Press `F4`
  to toggle the debug menu and the free cursor.
- **Rebuilding the SDK** after changing `thirdparty/rexglue-sdk`:
  `FABLE2_REBUILD_SDK=1 ./build.sh -release fable_2`.
