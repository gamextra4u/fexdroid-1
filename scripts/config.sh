# Shared build configuration, sourced by the build-*.sh scripts.
# The on-device rootfs path is baked into glibc and every ELF interpreter, so it must
# be exactly where the tarball is extracted on the device. Override it with FEXROOT_PATH
# (e.g. FEXROOT_PATH=/data/data/com.termux/files/fexroot for Termux); the default is the
# app's private dir (legacy flavor, see app/build.gradle.kts).
FXD_PACKAGE="${FXD_PACKAGE:-ro.cobrabm.fexdroid}"
FXD_ROOT="${FEXROOT_PATH:-/data/data/${FXD_PACKAGE}/files/rootfs}"
FXD_ROOT="${FXD_ROOT%/}"
case "$FXD_ROOT" in /*) ;; *) echo "FEXROOT_PATH must be an absolute path: $FXD_ROOT" >&2; exit 1;; esac
FXD_MULTIARCH="aarch64-linux-gnu"
FXD_LIBDIR="${FXD_ROOT}/usr/lib/${FXD_MULTIARCH}"
FXD_LDSO="${FXD_LIBDIR}/ld-linux-aarch64.so.1"
GLIBC_DEB_VERSION="${GLIBC_DEB_VERSION:-2.41-12+deb13u4}"   # must match libc6 in build/rootfs/rootfs-arm64.packages.txt
