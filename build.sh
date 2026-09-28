#!/usr/bin/env bash
# Compila ValorNX (la app ValorNX.nro y el overlay overlay/ValorNX.ovl) dentro de WSL.
#
# devkitPro está instalado en un entorno Debian aparte (/opt/dkp-root) porque sus
# programas necesitan una glibc más nueva que la de Ubuntu 20.04. Este script entra
# en ese entorno (chroot), compila y sale. Necesita permisos de administrador.
#
# Uso desde PowerShell:   wsl -d Ubuntu -u root -- bash ./build.sh
# Uso dentro de Ubuntu:   sudo ./build.sh          (o "sudo ./build.sh clean")
set -euo pipefail

ROOT=/opt/dkp-root
SRC="$(cd "$(dirname "$0")" && pwd)"

if [ "$(id -u)" != "0" ]; then
    exec sudo bash "$0" "$@"
fi
if [ ! -x "$ROOT/opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcc" ]; then
    echo "No se encontró devkitPro en $ROOT/opt/devkitpro" >&2
    exit 1
fi

mountpoint -q "$ROOT/dev" || mount --bind /dev "$ROOT/dev"
mountpoint -q "$ROOT/proc" || mount -t proc proc "$ROOT/proc"
mkdir -p "$ROOT/src"
if mountpoint -q "$ROOT/src"; then umount "$ROOT/src"; fi
mount --bind "$SRC" "$ROOT/src"
trap 'umount "$ROOT/src" 2>/dev/null || true' EXIT

chroot "$ROOT" /bin/bash -c "
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitA64/bin:/usr/local/bin:/usr/bin:/bin
cd /src && make -j\$(nproc) $* && make -C overlay -j\$(nproc) $*
"
