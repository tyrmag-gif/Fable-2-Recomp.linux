#!/usr/bin/env bash
# Build x360extract for Linux (port of build.cmd).
#   ./build.sh          self-contained single-file binary (no .NET install needed to run)
#   ./build.sh dev      framework-dependent binary (needs the .NET 8 runtime)
#   ./build.sh pack     pack the reusable library as a NuGet package
# Output: <repo>/out/tooling/x360extract/x360extract
# Needs the .NET 8 SDK (Arch: sudo pacman -S dotnet-sdk-8.0).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="$root/out/tooling/x360extract"

command -v dotnet >/dev/null || { echo "error: dotnet not found (Arch: sudo pacman -S dotnet-sdk-8.0)" >&2; exit 1; }

if [[ "${1:-}" == "pack" ]]; then
  rm -rf "$root/out/tooling/nuget"
  dotnet pack "$here/lib/X360Extract.Core.csproj" -c Release -o "$root/out/tooling/nuget"
  exit 0
fi

rm -rf "$out" "$here/bin" "$here/obj" "$here/lib/bin" "$here/lib/obj"
self_contained=true
[[ "${1:-}" == "dev" || "${1:-}" == "framework-dependent" ]] && self_contained=false

dotnet publish "$here/X360Extract.csproj" -c Release -r linux-x64 \
  --self-contained "$self_contained" -p:PublishSingleFile=true \
  -p:IncludeNativeLibrariesForSelfExtract=true -o "$out"
echo "x360extract built: $out/x360extract"
