#!/usr/bin/env bash
# Runs the staged payload on the PC under qemu-aarch64 (binfmt), mounted at the exact
# on-device paths. Catches build mistakes (paths, interpreters, missing libs, FEX
# startup) before touching the phone. The container image is empty (no /lib, /bin,
# /etc), like Android, so nothing can silently resolve outside the rootfs. It does NOT emulate Android's seccomp/SELinux.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/config.sh
scripts/ensure-binfmt.sh >/dev/null
FILES="$(dirname "$FXD_ROOT")"   # x86_64 guest rootfs sits next to $FXD_ROOT
STAGE="$PWD/build/payload-stage"
CMD="${1:-uname -a; ls /; echo; ls \$FXD_ROOT}"
docker run --rm --platform linux/arm64 --user "$(id -u):$(id -g)" \
  -v "$STAGE/arm64:$FXD_ROOT" -v "$STAGE/x86_64:$FILES/x86_64:ro" \
  -e PATH="$FXD_ROOT/usr/bin:$FXD_ROOT/bin" -e HOME="$FXD_ROOT/home" -e LANG=C.UTF-8 \
  -e TMPDIR="$FXD_ROOT/tmp" -e FXD_ROOT="$FXD_ROOT" -e FEX_ROOTFS="$FILES/x86_64" \
  -e FEX_APP_CONFIG_LOCATION="$FXD_ROOT/home/.fex-emu/" -e FEX_APP_DATA_LOCATION="$FXD_ROOT/home/.fex-emu/" \
  --entrypoint "$FXD_ROOT/bin/sh" fexdroid-empty:arm64 -c "$CMD"
